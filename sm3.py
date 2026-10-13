"""D3D9 shader model 3 bytecode (the 1.0 .vp / .fp programs): parse into instructions.

Token stream (D3D9 'Shader Model 3' tokens, little-endian u32):
  version  0xFFFE0300 (vs_3_0) / 0xFFFF0300 (ps_3_0)
  comment  opcode 0xFFFE, length in bits 16..30 (the CTAB constant table lives in one)
  instr    opcode bits 0..15, operand token count bits 24..27, predicated bit 28, controls bits 16..23
  end      0x0000FFFF
  dst      reg number bits 0..10, type bits 28..30 | 11..12 << 3, write mask 16..19, modifier 20..23, relative 13
  src      reg number, type as dst, swizzle 16..23 (2 bits per component), modifier 24..27, relative 13
           (a relative source is followed by one more token: the address register and its component)
  dcl      one extra token before the dst: usage 0..4 + index 16..19 (VS/PS inputs, outputs), or sampler
           texture type 27..30 (2 = 2D, 3 = cube, 4 = volume)
  def      dst + 4 literal floats; defi 4 ints; defb 1 bool

python sm3.py <prog> [...]          disassemble (a bare 8-hex id is looked up in the 1.0 extract)
python sm3.py --survey              opcode coverage over every 1.0 program
"""
import os, struct, sys
from collections import Counter

OPS = {0: 'nop', 1: 'mov', 2: 'add', 3: 'sub', 4: 'mad', 5: 'mul', 6: 'rcp', 7: 'rsq', 8: 'dp3', 9: 'dp4', 10: 'min',
       11: 'max', 12: 'slt', 13: 'sge', 14: 'exp', 15: 'log', 16: 'lit', 17: 'dst', 18: 'lrp', 19: 'frc', 20: 'm4x4',
       21: 'm4x3', 22: 'm3x4', 23: 'm3x3', 24: 'm3x2', 25: 'call', 26: 'callnz', 27: 'loop', 28: 'ret', 29: 'endloop',
       30: 'label', 31: 'dcl', 32: 'pow', 33: 'crs', 34: 'sgn', 35: 'abs', 36: 'nrm', 37: 'sincos', 38: 'rep',
       39: 'endrep', 40: 'if', 41: 'ifc', 42: 'else', 43: 'endif', 44: 'break', 45: 'breakc', 46: 'mova', 47: 'defb',
       48: 'defi', 64: 'texcoord', 65: 'texkill', 66: 'texld', 78: 'expp', 79: 'logp', 80: 'cnd', 81: 'def',
       88: 'cmp', 89: 'bem', 90: 'dp2add', 91: 'dsx', 92: 'dsy', 93: 'texldd', 94: 'setp', 95: 'texldl', 96: 'breakp'}
# register types
TEMP, INPUT, CONST, ADDR_TEX, RASTOUT, ATTROUT, OUTPUT, CONSTINT, COLOROUT, DEPTHOUT, SAMPLER = range(11)
CONSTBOOL, LOOP, TEMPF16, MISCTYPE, LABEL, PREDICATE = 14, 15, 16, 17, 18, 19
RNAME = {TEMP: 'r', INPUT: 'v', CONST: 'c', ADDR_TEX: 'a', RASTOUT: 'oPos', ATTROUT: 'oD', OUTPUT: 'o',
         CONSTINT: 'i', COLOROUT: 'oC', DEPTHOUT: 'oDepth', SAMPLER: 's', CONSTBOOL: 'b', LOOP: 'aL',
         MISCTYPE: 'vMisc', LABEL: 'l', PREDICATE: 'p'}
USAGE = ['POSITION', 'BLENDWEIGHT', 'BLENDINDICES', 'NORMAL', 'PSIZE', 'TEXCOORD', 'TANGENT', 'BINORMAL',
         'TESSFACTOR', 'POSITIONT', 'COLOR', 'FOG', 'DEPTH', 'SAMPLE']


def rtype(t):
    return ((t >> 28) & 7) | ((t >> 8) & 0x18)


class Op:
    __slots__ = ('op', 'name', 'ctl', 'pred', 'dst', 'src', 'decl', 'lit')

    def __repr__(self):
        return '%s %s %s' % (self.name, self.dst, self.src)


def parse(b):
    """-> (kind 'vs'/'ps', [Op], comments [bytes])"""
    toks = struct.unpack('<%dI' % (len(b) // 4), b[:len(b) // 4 * 4])
    ver = toks[0]
    kind = 'ps' if ver >> 16 == 0xFFFF else 'vs'
    if ver & 0xffff != 0x0300:
        raise ValueError('not shader model 3: %08x' % ver)
    out, comments, i = [], [], 1
    while i < len(toks):
        t = toks[i]
        op = t & 0xffff
        if op == 0xffff:
            break
        if op == 0xfffe:
            n = (t >> 16) & 0x7fff
            comments.append(struct.pack('<%dI' % n, *toks[i + 1:i + 1 + n]))
            i += 1 + n
            continue
        n = (t >> 24) & 0xf
        args = toks[i + 1:i + 1 + n]
        o = Op()
        o.op, o.name, o.ctl, o.pred = op, OPS.get(op, 'op%d' % op), (t >> 16) & 0xff, bool(t & (1 << 28))
        o.dst, o.src, o.decl, o.lit = None, [], None, None
        k = 0
        if op == 31:                                     # dcl: decl token, dst
            o.decl = args[0]
            o.dst = dst(args[1])
        elif op in (81, 48, 47):                         # def / defi / defb: dst, literals
            o.dst = dst(args[0])
            o.lit = (struct.unpack('<4f', struct.pack('<4I', *args[1:5])) if op == 81 else
                     struct.unpack('<4i', struct.pack('<4I', *args[1:5])) if op == 48 else (args[1],))
        else:
            has_dst = op not in (25, 26, 27, 28, 29, 30, 38, 39, 40, 41, 42, 43, 44, 45, 96, 65) or op == 65
            if op == 65:                                 # texkill: its operand is a destination token
                has_dst = True
            if has_dst and n:
                o.dst = dst(args[0]); k = 1
            while k < n:
                s = src(args[k]); k += 1
                if s['rel']:
                    s['reladdr'] = src(args[k]); k += 1
                o.src.append(s)
            if o.pred and o.src:                         # predicated: the predicate is the first source
                pass
        out.append(o)
        i += 1 + n
    return kind, out, comments


def dst(t):
    return dict(type=rtype(t), num=t & 0x7ff, mask=(t >> 16) & 0xf, mod=(t >> 20) & 0xf, shift=(t >> 24) & 0xf,
                rel=bool(t & (1 << 13)))


def src(t):
    return dict(type=rtype(t), num=t & 0x7ff, swz=(t >> 16) & 0xff, mod=(t >> 24) & 0xf, rel=bool(t & (1 << 13)))


def fmt_reg(r):
    s = '%s%d' % (RNAME.get(r['type'], 'R%d_' % r['type']), r['num'])
    if 'swz' in r and r['swz'] != 0xe4:
        s += '.' + ''.join('xyzw'[(r['swz'] >> (2 * c)) & 3] for c in range(4))
    if 'mask' in r and r['mask'] != 0xf:
        s += '.' + ''.join('xyzw'[c] for c in range(4) if r['mask'] >> c & 1)
    if 'swz' in r and r['mod']:
        s = {1: '-%s', 11: '|%s|', 12: '-|%s|'}.get(r['mod'], 'mod%d(%%s)' % r['mod']) % s
    if 'mask' in r and r['mod'] & 1:
        s += '_sat'
    return s


# ---- HLSL (shader model 4+) for the D3D11 compiler ----
# Varyings use one fixed interface (SV_Position, TEXCOORD0..9, COLOR0..1, all float4) on both sides, so any 1.0 VS /
# PS pair links under D3D11 whatever subset each program uses. vPos = SV_Position.xy - 0.5 (D3D9 pixel centres are
# integers); vFace = +-1. Instructions follow their D3D9 definitions: rsq / log / pow on |x|, scalar ops read the
# replicate component, cmp = per component src0 >= 0, nrm scales all four components, mova rounds.
VARYINGS = ['TEXCOORD%d' % i for i in range(10)] + ['COLOR0', 'COLOR1']
# 3.0 renders reversed-Z (its sky VSs write z = 0); 1.0's far-plane idiom z = w is 3.0's near plane (sm3.hpp REVERSED_Z)
REVERSED_Z = '    if (vout.pos.z == vout.pos.w) vout.pos.z = 0;'
SWZ = 'xyzw'


class Binding:
    """Where a program's resources live. This default is a flat register file, for compile tests only:
    c# -> C[#] and b# -> B[#] in b0, s# -> t# / s#."""
    def cbuffers(self):
        return 'cbuffer CB0 : register(b0) { float4 C[256]; uint4 B[16]; };'

    def const(self, num, rel):             # float4 expression for c[num] (rel: an int expression or None)
        return 'C[%s%d]' % ((rel + ' + ') if rel else '', num)

    def bool_(self, num):
        return '(B[%d].x != 0)' % num

    def sampler(self, num, kind):          # -> (texture name, sampler name, declarations)
        tex = {2: 'Texture2D', 3: 'TextureCube', 4: 'Texture3D'}[kind]
        return 't%d' % num, 's%d' % num, '%s t%d : register(t%d); SamplerState s%d : register(s%d);' % (tex, num, num, num, num)

    def sample(self, num, kind, method, uv, extra):
        """float4 expression for a texld of sampler num: method 'Sample' / 'SampleBias' / 'SampleLevel'."""
        t, smp, _ = self.sampler(num, kind)
        return '%s.%s(%s, %s%s)' % (t, method, smp, uv, (', ' + extra) if extra else '')

    def input_type(self, semantic):        # 'float' / 'uint' / 'int': how the input layout feeds a VS input
        return 'float'

    gbuffer = False                        # PS output re-encoded for 3.0's G-buffer (gen30.GBUFFER_EPILOGUE)
    epilogue = ''


def to_hlsl(kind, prog, binding=None):
    B = binding or Binding()
    defs, samplers, inputs, outputs = {}, {}, {}, {}
    temps, body, depth = set(), [], [1]
    for o in prog:
        if o.name == 'def':
            defs[o.dst['num']] = o.lit
        elif o.name == 'dcl':
            t, n = o.dst['type'], o.dst['num']
            if t == SAMPLER:
                samplers[n] = (o.decl >> 27) & 0xf
            elif t == INPUT:
                inputs[n] = '%s%d' % (USAGE[o.decl & 0x1f], (o.decl >> 16) & 0xf)
            elif t == OUTPUT:
                outputs[n] = '%s%d' % (USAGE[o.decl & 0x1f], (o.decl >> 16) & 0xf)

    def reg(r):
        t, n = r['type'], r['num']
        if t == TEMP:
            temps.add(n)
            return 'r%d' % n
        if t == INPUT:
            return 'v%d' % n
        if t == CONST:
            rel = None
            if r.get('rel'):
                a = r['reladdr']
                rel = '%s%d.%s' % ('a' if a['type'] == ADDR_TEX else 'aL', a['num'], SWZ[a['swz'] & 3])
            if not rel and n in defs:
                return 'float4(%r, %r, %r, %r)' % tuple(float(x) for x in defs[n])
            return B.const(n, rel)
        if t == ADDR_TEX and kind == 'vs':
            return 'a%d' % n
        if t == OUTPUT:
            return 'o%d' % n
        if t == COLOROUT:
            return 'o.oC%d' % n
        if t == DEPTHOUT:
            return 'o.oDepth'
        if t == MISCTYPE:
            return 'vPos' if n == 0 else 'vFace.xxxx'
        if t == CONSTBOOL:
            return B.bool_(n)
        raise ValueError('register type %d' % t)

    def mod(e, m):
        if m == 1: return '(-%s)' % e
        if m == 11: return 'abs(%s)' % e
        if m == 12: return '(-abs(%s))' % e
        if m: raise ValueError('source modifier %d' % m)
        return e

    def s(r, n=4):
        e = reg(r)
        if r['type'] == CONSTBOOL:
            return e
        if r['type'] == ADDR_TEX:
            e = '((float4)%s)' % e
        sw = ''.join(SWZ[(r['swz'] >> (2 * c)) & 3] for c in range(4))[:n]
        return mod(e if sw == 'xyzw' else '%s.%s' % (e, sw), r['mod'])

    def scal(r):                            # a scalar source (replicate swizzle)
        e = reg(r)
        if r['type'] == ADDR_TEX:
            e = '((float4)%s)' % e
        return mod('%s.%s' % (e, SWZ[r['swz'] & 3]), r['mod'])

    def emit(line):
        body.append('    ' * depth[0] + line)

    def store(o, expr, scalar=False):
        d = o.dst
        if d['shift'] or d['mod'] & ~1:
            raise ValueError('dst shift/modifier %d/%d' % (d['shift'], d['mod']))
        mask = ''.join(SWZ[c] for c in range(4) if d['mask'] >> c & 1)
        val = ('((float4)(%s)).%s' if scalar else '(%s).%s') % (expr, mask)
        if d['mod'] & 1:
            val = 'saturate(%s)' % val
        if d['type'] == ADDR_TEX and kind == 'vs':
            emit('%s.%s = (int%s)round(%s);' % (reg(d), mask, '' if len(mask) == 1 else len(mask), val))
        else:
            emit('%s.%s = %s;' % (reg(d), mask, val))

    for o in prog:
        n, S = o.name, o.src
        if n in ('dcl', 'def', 'defi', 'defb', 'nop'):
            continue
        if o.pred:
            raise ValueError('predication')
        if n in ('mov', 'mova'): store(o, s(S[0]))
        elif n == 'add': store(o, '%s + %s' % (s(S[0]), s(S[1])))
        elif n == 'sub': store(o, '%s - %s' % (s(S[0]), s(S[1])))
        elif n == 'mul': store(o, '%s * %s' % (s(S[0]), s(S[1])))
        elif n == 'mad': store(o, '%s * %s + %s' % (s(S[0]), s(S[1]), s(S[2])))
        elif n == 'min': store(o, 'min(%s, %s)' % (s(S[0]), s(S[1])))
        elif n == 'max': store(o, 'max(%s, %s)' % (s(S[0]), s(S[1])))
        elif n == 'slt': store(o, '(float4)(%s < %s)' % (s(S[0]), s(S[1])))
        elif n == 'sge': store(o, '(float4)(%s >= %s)' % (s(S[0]), s(S[1])))
        elif n == 'frc': store(o, 'frac(%s)' % s(S[0]))
        elif n == 'abs': store(o, 'abs(%s)' % s(S[0]))
        elif n == 'cmp': store(o, '(%s >= 0 ? %s : %s)' % (s(S[0]), s(S[1]), s(S[2])))
        elif n == 'dp3': store(o, 'dot(%s, %s)' % (s(S[0], 3), s(S[1], 3)), True)
        elif n == 'dp4': store(o, 'dot(%s, %s)' % (s(S[0]), s(S[1])), True)
        elif n == 'dp2add': store(o, 'dot(%s, %s) + %s' % (s(S[0], 2), s(S[1], 2), scal(S[2])), True)
        elif n == 'rcp': store(o, '1.0 / %s' % scal(S[0]), True)
        elif n == 'rsq': store(o, 'rsqrt(abs(%s))' % scal(S[0]), True)
        elif n == 'exp': store(o, 'exp2(%s)' % scal(S[0]), True)
        elif n == 'log': store(o, 'log2(abs(%s))' % scal(S[0]), True)
        elif n == 'pow': store(o, 'pow(abs(%s), %s)' % (scal(S[0]), scal(S[1])), True)
        elif n == 'nrm':
            v = s(S[0])
            store(o, '%s * rsqrt(dot((%s).xyz, (%s).xyz))' % (v, v, v))
        elif n == 'sincos':
            a = scal(S[0])
            store(o, 'float4(cos(%s), sin(%s), 0, 0)' % (a, a))
        elif n in ('texld', 'texldl'):
            sm = S[1]
            k = samplers.get(sm['num'], 2)
            uv = s(S[0], 2 if k == 2 else 3)
            if n == 'texldl':
                e = B.sample(sm['num'], k, 'SampleLevel', uv, '%s.w' % s(S[0]))
            elif o.ctl == 2:
                e = B.sample(sm['num'], k, 'SampleBias', uv, '%s.w' % s(S[0]))
            elif o.ctl == 0:
                e = B.sample(sm['num'], k, 'Sample', uv, None)
            else:
                raise ValueError('texld control %d' % o.ctl)
            sw = ''.join(SWZ[(sm['swz'] >> (2 * c)) & 3] for c in range(4))
            store(o, e if sw == 'xyzw' else '(%s).%s' % (e, sw))
        elif n == 'texkill':
            d = o.dst
            emit('if (any(%s.%s < 0)) discard;' % (reg(d), ''.join(SWZ[c] for c in range(4) if d['mask'] >> c & 1)))
        elif n == 'if':
            emit('if (%s) {' % s(S[0])); depth[0] += 1
        elif n == 'ifc':
            emit('if (%s %s %s) {' % (scal(S[0]), {1: '>', 2: '==', 3: '>=', 4: '<', 5: '!=', 6: '<='}[o.ctl], scal(S[1])))
            depth[0] += 1
        elif n == 'else':
            depth[0] -= 1; emit('} else {'); depth[0] += 1
        elif n == 'endif':
            depth[0] -= 1; emit('}')
        else:
            raise ValueError('opcode ' + n)

    out = [B.cbuffers()] + [B.sampler(num, k)[2] for num, k in sorted(samplers.items())]
    io = ' '.join(['float4 pos : SV_Position;'] + ['float4 %s : %s;' % (v, v) for v in VARYINGS])
    if kind == 'vs':
        out.append('struct VSIn { %s };' % ' '.join('%s4 v%d : %s;' % (B.input_type(v), k, v) for k, v in sorted(inputs.items())))
        out.append('struct VSOut { %s };' % io)
        out.append('VSOut main(VSIn vin) {')
        out.append('    precise float4 ' + ', '.join('o%d = 0' % k for k in range(16)) + ';')   # bit-equal positions across passes (sm3.hpp)
        out.append('    int4 a0 = 0;')
        out += ['    float4 v%d = (float4)vin.v%d;' % (k, k) for k in sorted(inputs)]
    else:
        outs = ' '.join('float4 oC%d : SV_Target%d;' % (k, k) for k in range(4)
                        if any(o.dst and o.dst['type'] == COLOROUT and o.dst['num'] == k for o in prog))
        if any(o.dst and o.dst['type'] == DEPTHOUT for o in prog):
            outs += ' float oDepth : SV_Depth;'
        if B.gbuffer:                       # 3.0 G-buffer: normal+gloss, albedo, reflectance targets
            outs = 'float4 oC0 : SV_Target0; float4 o1 : SV_Target1; float4 o2 : SV_Target2;'
        out.append('struct PSIn { %s bool face : SV_IsFrontFace; };' % io)
        out.append('struct PSOut { %s };' % outs)
        out.append('PSOut main(PSIn pin) {')
        out.append('    PSOut o = (PSOut)0;')
        out.append('    float4 vPos = float4(pin.pos.xy - 0.5, 0, 0); float vFace = pin.face ? 1.0 : -1.0;')
        out += ['    float4 v%d = pin.%s;' % (k, v) for k, v in sorted(inputs.items())]
    if temps:
        out.append(('    precise float4 ' if kind == 'vs' else '    float4 ') + ', '.join('r%d = 0' % t for t in sorted(temps)) + ';')
    out += body
    if kind == 'vs':
        out.append('    VSOut vout = (VSOut)0;')
        out += ['    vout.%s = o%d;' % ('pos' if v == 'POSITION0' else v, k) for k, v in sorted(outputs.items())]
        out.append(REVERSED_Z)
        out.append('    return vout;')
    else:
        if B.epilogue:
            out.append(B.epilogue)
        out.append('    return o;')
    out.append('}')
    return '\n'.join(out)


def d3dcompile(src, target):
    """(DXBC bytes or None, messages) through the system d3dcompiler_47 (64-bit here; the plugin uses the 32-bit)."""
    import ctypes
    from ctypes import c_void_p, c_char_p, c_size_t, c_uint, POINTER, byref
    dll = ctypes.WinDLL('d3dcompiler_47.dll')
    code, err = c_void_p(), c_void_p()
    data = src.encode()
    hr = dll.D3DCompile(c_char_p(data), c_size_t(len(data)), c_char_p(b'sm3'), None, None, c_char_p(b'main'),
                        c_char_p(target.encode()), c_uint(0), c_uint(0), byref(code), byref(err))

    def blob(p):
        if not p.value:
            return b''
        vt = ctypes.cast(ctypes.cast(p, POINTER(c_void_p))[0], POINTER(c_void_p))
        ptr = ctypes.WINFUNCTYPE(c_void_p, c_void_p)(vt[3])(p)
        size = ctypes.WINFUNCTYPE(c_size_t, c_void_p)(vt[4])(p)
        b = ctypes.string_at(ptr, size)
        ctypes.WINFUNCTYPE(c_uint, c_void_p)(vt[2])(p)
        return b
    c, e = blob(code), blob(err)
    return (c if hr == 0 else None), e.decode(errors='replace')


def compile_all(target_vs='vs_4_0', target_ps='ps_4_0'):
    import paths
    d = str(paths.ext10('shaders', 'shaders'))
    ok, bad = 0, []
    for f in sorted(os.listdir(d)):
        if not f.endswith(('.vp', '.fp')):
            continue
        try:
            kind, prog, _ = parse(open(os.path.join(d, f), 'rb').read())
            code, msg = d3dcompile(to_hlsl(kind, prog), target_vs if kind == 'vs' else target_ps)
        except Exception as e:
            code, msg = None, 'translate: %s' % e
        if code:
            ok += 1
        else:
            bad.append((f, msg.strip().splitlines()[0] if msg.strip() else '?'))
    print('%d compiled, %d failed' % (ok, len(bad)))
    for b in bad[:15]:
        print('  ', b)


def program_path(arg):
    if os.path.exists(arg):
        return arg
    import paths
    for kind in ('vp', 'fp'):
        p = paths.ext10('shaders', 'shaders', '%s.%s' % (arg, kind))
        if os.path.exists(p):
            return str(p)
    raise SystemExit('no program ' + arg)


def survey():
    import paths
    d = str(paths.ext10('shaders', 'shaders'))
    ops, rtypes, bad, n = Counter(), Counter(), [], 0
    for f in sorted(os.listdir(d)):
        if not f.endswith(('.vp', '.fp')):
            continue
        n += 1
        try:
            kind, prog, _ = parse(open(os.path.join(d, f), 'rb').read())
        except Exception as e:
            bad.append((f, str(e)))
            continue
        for o in prog:
            ops[o.name] += 1
            for r in ([o.dst] if o.dst else []) + o.src:
                rtypes[RNAME.get(r['type'], r['type'])] += 1
                if r.get('mod'):
                    rtypes['srcmod%d' % r['mod'] if 'swz' in r else 'dstmod%d' % r['mod']] += 1
    print('%d programs, %d failed to parse %s' % (n, len(bad), bad[:3]))
    print('opcodes:', ops.most_common())
    print('registers/modifiers:', rtypes.most_common())


if __name__ == '__main__':
    if sys.argv[1:] == ['--survey']:
        survey()
    elif sys.argv[1:] == ['--compile']:
        compile_all()
    elif sys.argv[1:2] == ['--hlsl']:
        for a in sys.argv[2:]:
            kind, prog, _ = parse(open(program_path(a), 'rb').read())
            print(to_hlsl(kind, prog))
    else:
        for a in sys.argv[1:]:
            kind, prog, _ = parse(open(program_path(a), 'rb').read())
            print('==', a, kind)
            for o in prog:
                d = fmt_reg(o.dst) if o.dst else ''
                print('  %-8s %-10s %s%s' % (o.name, d, ', '.join(fmt_reg(s) for s in o.src),
                                             (' ' + str(o.lit)) if o.lit else ''))


def d3ddisasm(dxbc):
    """Text disassembly of a DXBC program (system d3dcompiler_47)."""
    import ctypes
    from ctypes import c_void_p, c_size_t, c_uint, POINTER, byref
    dll = ctypes.WinDLL('d3dcompiler_47.dll')
    out = c_void_p()
    if dll.D3DDisassemble(ctypes.c_char_p(dxbc), c_size_t(len(dxbc)), c_uint(0), None, byref(out)):
        return None
    vt = ctypes.cast(ctypes.cast(out, POINTER(c_void_p))[0], POINTER(c_void_p))
    ptr = ctypes.WINFUNCTYPE(c_void_p, c_void_p)(vt[3])(out)
    size = ctypes.WINFUNCTYPE(c_size_t, c_void_p)(vt[4])(out)
    s = ctypes.string_at(ptr, size).decode(errors='replace')
    ctypes.WINFUNCTYPE(c_uint, c_void_p)(vt[2])(out)
    return s

"""Real 1.0 shaders in 3.0: a 1.0 shader description (.shd) and its D3D9 programs -> a 3.0 .shd and D3D11 programs.

Nothing is approximated by a donor: every pass runs the 1.0 program, translated instruction for instruction (sm3.py),
with its resources bound where the 3.0 engine provides them. Decoded from both exes and checked on the 208 shaders
that ship in both games (shd.py has the formats):
  constants  by name: 3.0 globals perFrame b0 / perView b1 / perDraw b2 keep one layout across all 3.0 programs
             (harvested from their RDEF); material parameters by the engine name hash into block A -> perInstance b4
             (same offsets) and block B -> perMaterial b3 (offset - the first B parameter register). 1.0 registers
             alias 3.0 buffer registers one for one (objectToWorld c21+k = perDraw[k], worldToObject c25+k =
             perDraw[4+k], worldToProjected c11+k = perView[8+k], timeVars c2 = perFrame[1]: twin programs read the
             same registers). One 1.0 global was folded away: fog sun 'primary' = g_fog_sunLight_params + g_fog_color
             (both games' fog code, term for term).
  textures   material slot i: 1.0 s(3+i) -> t(5+i)/s(5+i) (3.0 slots start at t5); engine: depth s0 -> t0,
             normal+gloss s1 -> t1, light buffer s2 -> t3 + t4 (3.0 splits specular out; the 1.0 program gets
             float4(light.rgb, 6 * luminance(specular)), its own packing).
  G-buffer   1.0 writes world normal + gloss to one target; 3.0's lighting reads an octahedral normal in .xy and the
             same gloss code (exp(g * 10 + 1)) in .z, plus albedo and reflectance targets. G-buffer passes get that
             encoding, black albedo (the 1.0 final pass shades) and white reflectance (it applies its own).
  passes     word fields move: depth 0..2 -> 3..5, blend 3..6 -> 6..9, cull 7..8 -> 10..11, X 9..12 -> 12..15,
             Y 13..15 -> 16..19 (both exes' stage classifiers, FUN_0049fb20 / FUN_0045cf40, match field for field);
             the bits 3.0 adds per technique are learned from the 208 pairs (RULES).
  vertices   the 1.0 vertex formats stay (registered beside 3.0's), so VS inputs are the D3D9 declarations, typed
             from the format (integer elements arrive as uint).

python gen30.py <1.0 shader> [...]     generate, compile, print the 3.0 description
python gen30.py --all                   every 1.0 shader: generate + compile counts
"""
import os, struct, sys, zlib
from collections import Counter, defaultdict
from functools import lru_cache
import shd, sm3, vtxf
from shd import ehash

ENGINE_TEX = {0: 0, 1: 1}                 # 1.0 sampler register -> 3.0 texture register (depth, normal+gloss)
LIGHT10 = 2                               # 1.0 light buffer sampler; 3.0 t3 (diffuse light) + t4 (specular light)
SLOT10, SLOT30 = 3, 5                     # first material slot register in each game
INT_TYPES = {'U8_4': 'uint', 'U16_2': 'uint', 'U16_4': 'uint', 'U8_1': 'uint', 'U16_1': 'uint',
             'S16_2': 'int', 'S16_4': 'int', 'S8_4': 'int'}

GBUFFER_EPILOGUE = '''    {   // 3.0 G-buffer encoding of the 1.0 world normal (.xyz) and gloss (.w)
        float3 n = o.oC0.xyz / (abs(o.oC0.x) + abs(o.oC0.y) + abs(o.oC0.z));
        float2 f = (1.0 - abs(n.yx)) * (n.xy >= 0 ? 1.0 : -1.0);
        o.oC0 = float4((n.z >= 0 ? n.xy : f) * 0.5 + 0.5, o.oC0.w, 1.0);
        o.o1 = float4(0, 0, 0, 0);
        o.o2 = float4(1, 1, 1, 0.5);
    }'''


@lru_cache(None)
def globals30():
    """name -> (buffer register, byte offset, size) for 3.0's perFrame / perView / perDraw variables."""
    slot = {'perFrame': 0, 'perView': 1, 'perDraw': 2}
    out = {}
    d = shd.shader_dir('3.0')
    for f in os.listdir(d):
        if f.endswith(('.vp', '.fp')):
            for cb, size, vs in shd.rdef(open(os.path.join(d, f), 'rb').read())['cb']:
                if cb in slot:
                    for v, off, sz in vs:
                        out[v] = (slot[cb], off, sz)
    return out


def conv_word(w):
    return ((w & 7) << 3) | (((w >> 3) & 0xf) << 6) | (((w >> 7) & 3) << 10) | (((w >> 9) & 0xf) << 12) | \
           (((w >> 13) & 7) << 16)


@lru_cache(None)
def rules():
    """(technique hash, depth kind, X) -> bits 3.0 adds to a converted 1.0 word, from the shaders shipped in both games;
    and (technique hash, word) -> the pass's +0x1c byte as 3.0 ships it."""
    d1, d3 = shd.shader_dir('1.0'), shd.shader_dir('3.0')
    both = set(f[:-4] for f in os.listdir(d1) if f.endswith('.shd')) & set(f[:-4] for f in os.listdir(d3) if f.endswith('.shd'))
    add = defaultdict(Counter)
    b1c = defaultdict(Counter)
    for f in os.listdir(d3):
        if f.endswith('.shd'):
            s = shd.load(f[:-4], '3.0')
            for t in s['techs']:
                for p in s['passes'][t['first']:t['first'] + t['count']]:
                    b1c[(t['hash'], p['word'])][p['raw'][0x1c]] += 1
    for sh in both:
        a, c = shd.load(sh, '1.0'), shd.load(sh, '3.0')
        tc = {t['hash']: c['passes'][t['first']:t['first'] + t['count']] for t in c['techs']}
        for t in a['techs']:
            pc = tc.get(t['hash'], [])
            for p in a['passes'][t['first']:t['first'] + t['count']]:
                w = conv_word(p['word'])
                key = (t['hash'], (w >> 3) & 7, (w >> 12) & 0xf)
                for q in pc:
                    if (q['word'] >> 3) & 7 == key[1] and ((q['word'] & ~0x108000) >> 12) & 0xf == key[2]:
                        add[key][q['word'] & ~w & 0x1fffff] += 1
                        break
    learned = {k: v.most_common(1)[0][0] for k, v in add.items() if v.most_common(1)[0][1] * 2 > sum(v.values())}
    return learned, {k: v.most_common(1)[0][0] for k, v in b1c.items()}


def word30(tech, w10):
    w = conv_word(w10)
    return w | rules()[0].get((tech, (w >> 3) & 7, (w >> 12) & 0xf), 0)


class Binding30(sm3.Binding):
    def __init__(self, s10, layout, ctab, inputs_fmt=None, gbuffer=False):
        self.s10, self.L, self.fmt = s10, layout, inputs_fmt or {}
        self.creg = {}
        self.samp = {}
        for name, rs, reg, cnt in ctab:
            for k in range(cnt):
                (self.creg if rs == 'f' else self.samp if rs == 's' else {})[reg + k] = (name, reg)
        self.used = defaultdict(int)       # buffer register -> registers declared
        self.unresolved = []
        self.gbuffer = gbuffer
        self.epilogue = GBUFFER_EPILOGUE if gbuffer else ''
        self.light = False

    def locate(self, name):
        """-> (buffer register, first register) or ('expr', expression) or None."""
        h = ehash(name)
        if h in self.L['A']:
            return 4, self.L['A'][h] // 16
        if h in self.L['B']:
            return 3, self.L['B'][h] // 16 - self.L['firstB']
        g = globals30().get(name)
        if g:
            return g[0], g[1] // 16
        if name == 'g_fog_sunLight_color_primary':
            p, c = globals30()['g_fog_sunLight_params'], globals30()['g_fog_color']
            self.used[0] = max(self.used[0], p[1] // 16 + 1, c[1] // 16 + 1)
            return 'expr', 'float4(CB0[%d].xyz + CB0[%d].xyz, CB0[%d].w)' % (p[1] // 16, c[1] // 16, p[1] // 16)
        return None

    def const(self, num, rel):
        hit = self.creg.get(num)
        loc = self.locate(hit[0]) if hit else None
        if loc is None:
            self.unresolved.append(hit[0] if hit else 'c%d' % num)
            return 'float4(0, 0, 0, 0)'
        if loc[0] == 'expr':
            return loc[1]
        cb, base = loc
        name, reg0 = hit
        if rel:
            span = max(r - reg0 for r, h in self.creg.items() if h == hit) + 1
            self.used[cb] = max(self.used[cb], base + span)
            return 'CB%d[%s + %d]' % (cb, rel, base + num - reg0)
        self.used[cb] = max(self.used[cb], base + num - reg0 + 1)
        return 'CB%d[%d]' % (cb, base + num - reg0)

    def bool_(self, num):
        return 'false'                     # renderReducedRes, the only 1.0 bool: full-resolution passes

    def cbuffers(self):
        names = {0: 'perFrame', 1: 'perView', 2: 'perDraw', 3: 'perMaterial', 4: 'perInstance'}
        return '\n'.join('cbuffer %s : register(b%d) { float4 CB%d[%d]; };' % (names[k], k, k, n)
                         for k, n in sorted(self.used.items()) if n)

    def treg(self, num):
        return ENGINE_TEX[num] if num in ENGINE_TEX else (SLOT30 + num - SLOT10 if num >= SLOT10 else None)

    def sampler(self, num, kind):
        tex = {2: 'Texture2D', 3: 'TextureCube', 4: 'Texture3D'}[kind]
        if num == LIGHT10:
            self.light = True
            return 'tLight', 'sLight', ('Texture2D tLight : register(t3); SamplerState sLight : register(s3); '
                                        'Texture2D tSpec : register(t4); SamplerState sSpec : register(s4);')
        r = self.treg(num)
        return 't%d' % r, 's%d' % r, '%s t%d : register(t%d); SamplerState s%d : register(s%d);' % (tex, r, r, r, r)

    def sample(self, num, kind, method, uv, extra):
        if num == LIGHT10:
            x = (', ' + extra) if extra else ''
            return ('float4(tLight.%s(sLight, %s%s).rgb, 6.0 * dot(tSpec.%s(sSpec, %s%s).rgb, '
                    'float3(0.2125, 0.7154, 0.0721)))' % (method, uv, x, method, uv, x))
        return super().sample(num, kind, method, uv, extra)

    def input_type(self, semantic):
        return INT_TYPES.get(self.fmt.get(semantic), 'float')


# ---- block layouts ----
def layout(s10):
    """1.0 parameter offsets in 3.0 terms: block A unchanged; block B re-laid for 8-byte slots."""
    slots = len(s10['slotsB'])
    bparams = [p for p in s10['params'] if p['block'] == 'B']
    first10 = min((p['offset'] for p in bparams), default=(slots * 5 + 15) & ~15)
    first30 = (slots * 9 + 15) & ~15
    A = {p['hash']: p['offset'] for p in s10['params'] if p['block'] == 'A'}
    B = {p['hash']: p['offset'] - first10 + first30 for p in bparams}
    return dict(A=A, B=B, first10=first10, first30=first30, firstB=first30 // 16, slots=slots,
                sizeA=s10['sizeA'], sizeB=first30 + max(s10['sizeB'] - first10, 0))


def block_b30(b10, L):
    """A 1.0 block B (4-byte slots) in the 3.0 layout (8-byte slots, an empty one written whole)."""
    n = L['slots']
    out = bytearray()
    for i in range(n):
        idx = struct.unpack_from('<I', b10, 4 * i)[0]
        out += struct.pack('<II', idx, 0xffffffff if idx == 0xffffffff else 0)
    out += b10[4 * n:5 * n]
    out += bytes(L['first30'] - len(out))
    out += b10[L['first10']:]
    out += bytes(L['sizeB'] - len(out))
    return bytes(out[:L['sizeB']])


def defaults10(raw10, s10):
    """(block A, block B) templates of a 1.0 .shd: 16-aligned after its name string (FUN_004af160)."""
    o = len(raw10) - len(s10['defaults'])
    a = (o + 15) & ~15
    b = (a + s10['sizeA'] + 15) & ~15
    return raw10[a:a + s10['sizeA']], raw10[b:b + s10['sizeB']]


# ---- the generator ----
def prog_id(shader, i, kind):
    return zlib.crc32(('di1compat:%s:%d:%s' % (shader, i, kind)).encode()) | 0x80000000


def generate(shader, compile_=True):
    """-> dict(shd=3.0 .shd bytes, programs={id: dxbc}, name=3.0 shader name, problems=[...])"""
    raw10 = open(os.path.join(shd.shader_dir('1.0'), shader + '.shd'), 'rb').read()
    s10 = shd.parse(raw10, '1.0')
    L = layout(s10)
    progs, problems, passes = {}, [], []
    tech_of = {}
    for t in s10['techs']:
        for i in range(t['first'], t['first'] + t['count']):
            tech_of[i] = t['hash']
    for i, p in enumerate(s10['passes']):
        w = word30(tech_of.get(i), p['word'])
        gbuffer = (w >> 3) & 7 == 2
        fmt = {}
        try:
            fmt = {e[1] if e[1][-1].isdigit() else e[1] + '0': e[2] for e in vtxf.elements(vtxf.fmt_of(p['vf'], '1.0'))}
        except Exception:
            problems.append('pass %d: vformat %08x unknown' % (i, p['vf']))
        ids = {}
        for kind, pid, target in (('vp', p['vs'], 'vs_4_0'), ('fp', p['ps'], 'ps_4_0')):
            if not pid:
                ids[kind] = (0, 0)
                continue
            b = shd.program('1.0', pid, kind)
            if b is None:
                problems.append('pass %d: no %08x.%s' % (i, pid, kind))
                ids[kind] = (0, 0)
                continue
            k, prog, _ = sm3.parse(b)
            B = Binding30(s10, L, shd.ctab(b), fmt if kind == 'vp' else None, gbuffer and kind == 'fp')
            src = sm3.to_hlsl(k, prog, B)
            if B.unresolved:
                problems.append('pass %d %s: unresolved %s' % (i, kind, sorted(set(B.unresolved))))
            nid = prog_id(shader, i, kind)
            code = None
            if compile_:
                code, msg = sm3.d3dcompile(src, target)
                if not code:
                    problems.append('pass %d %s: compile: %s' % (i, kind, msg.strip().splitlines()[0] if msg.strip() else '?'))
            progs[nid] = code if code else src.encode()
            ids[kind] = (nid, len(code) if code else 0)
        passes.append((w, p['vf'], ids, tech_of.get(i)))
    name = 'x' + shader
    return dict(shd=build_shd(raw10, s10, L, passes), programs=progs, name=name, problems=problems)


def build_shd(raw10, s10, L, passes):
    b1c = rules()[1]
    params = s10['params']
    nA = sum(1 for p in params if p['block'] == 'A')
    nB = len(params) - nA
    slots = L['slots']
    h = bytearray(0xE0)
    h[0x18], h[0x19], h[0x1a], h[0x1b] = len(params), len(passes), len(s10['techs']), slots
    flags = 0
    for w, *_ in passes:
        if ((w >> 6) & 0xf) not in (0, 1):
            flags |= 0x4                   # a blended pass (translucent)
        if (w >> 3) & 7 == 0 and (w >> 12) & 0xf == 3:
            flags |= 0x40                  # a light-buffer final pass
    h[0x48] = flags
    words = [(p['type'] | (p['offset'] << 16)) for p in params]
    if nA:
        struct.pack_into('<II', h, 0x4c, words[0], words[nA - 1])
    h[0x54:0x64] = raw10[0x30:0x40]        # technique slot table (same slot order in both games)
    h[0x68], h[0x69] = nA, 0
    struct.pack_into('<H', h, 0x6a, L['sizeA'])
    h[0x90] = max(L['sizeA'] // 16 - 1, 0)
    h[0x94:0x9a] = b'\x0f' * 6
    h[0xa0], h[0xa1] = nB, slots
    struct.pack_into('<H', h, 0xa2, L['sizeB'])
    h[0xc4] = L['firstB']
    h[0xc8] = max(L['sizeB'] // 16 - L['firstB'], 0)
    h[0xca] = slots
    h[0xcc] = SLOT30 | (slots << 4) if slots else 0x0f
    h[0xcd:0xd2] = b'\x0f' * 5
    h[0xd9], h[0xda] = 1, 0xff
    out = bytearray(h)
    for w, vf, ids, tech in passes:
        r = bytearray(0x50)
        struct.pack_into('<I', r, 0, w)
        struct.pack_into('<I', r, 0x18, vf)
        r[0x1c] = b1c.get((tech, w), 0)
        struct.pack_into('<I', r, 0x28, ids['vp'][0])
        struct.pack_into('<I', r, 0x30, ids['fp'][0])
        struct.pack_into('<II', r, 0x40, ids['vp'][1], ids['fp'][1])
        out += r
    for t in s10['techs']:
        out += struct.pack('<H', t['first'] | (t['count'] << 8))
    out += bytes(-len(out) % 4)
    out += struct.pack('<%dI' % len(params), *[p['hash'] for p in params])
    out += struct.pack('<%dI' % len(s10['techs']), *[t['hash'] for t in s10['techs']])
    words30 = [p['type'] | ((p['offset'] if p['block'] == 'A' else L['B'][p['hash']]) << 16) for p in params]
    out += struct.pack('<%dI' % len(params), *words30)
    out += struct.pack('<%dI' % slots, *s10['slotsB'])
    dA, dB = defaults10(raw10, s10)
    out += bytes(-len(out) % 16) + dA
    # the template's slots hold texture object pointers the engine fills (FUN_0045ce20 reads any non-zero one as a
    # texture): zero in all 836 3.0 shaders. 1.0's empty 0xffffffff there crashed finalize (in-game 2026-10-13).
    out += bytes(-len(out) % 16) + bytes(8 * L['slots']) + block_b30(dB, L)[8 * L['slots']:]
    return bytes(out)


def show(shader):
    g = generate(shader)
    s = shd.parse(g['shd'], '3.0')
    print('== %s -> %s: %d bytes, %d programs, A %d B %d' % (shader, g['name'], len(g['shd']), len(g['programs']),
                                                           s['sizeA'], s['sizeB']))
    print('  params:', ', '.join('%s %s@%#x t%#x' % (shd.nm(p['hash']), p['block'], p['offset'], p['type']) for p in s['params']))
    print('  slots:', [shd.nm(x) for x in s['slotsB']])
    for t in s['techs']:
        ps = s['passes'][t['first']:t['first'] + t['count']]
        print('  tech %s: %s' % (shd.nm(t['hash']), ' | '.join('w%#x vs %08x ps %08x' % (p['word'], p['vs'], p['ps']) for p in ps)))
    for p in g['problems']:
        print('  PROBLEM', p)


def all_():
    d = shd.shader_dir('1.0')
    ok, bad, problems = 0, 0, Counter()
    for f in sorted(os.listdir(d)):
        if not f.endswith('.shd'):
            continue
        try:
            g = generate(f[:-4])
            shd.parse(g['shd'], '3.0')
        except Exception as e:
            bad += 1
            problems['exception %s' % type(e).__name__] += 1
            continue
        if g['problems']:
            bad += 1
            for p in g['problems']:
                problems[p.split(':')[1].strip()[:60] if ':' in p else p] += 1
        else:
            ok += 1
    print('%d shaders clean, %d with problems' % (ok, bad))
    for k, v in problems.most_common(20):
        print('  %5d %s' % (v, k))


if __name__ == '__main__':
    if sys.argv[1:] == ['--all']:
        all_()
    else:
        for a in sys.argv[1:]:
            show(a)

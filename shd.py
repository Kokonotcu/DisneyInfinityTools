"""Shader descriptions (.shd) of both games, decoded from their loaders, and their programs' named resources.

3.0 (DisneyInfinity3.exe: list loader FUN_00476180, finalize FUN_0045cf40): header to 0xE0, then in order
  passes      u8 @0x19 x 0x50: +0 render-state word, +0x18 vformatCRC, +0x28 VS id, +0x30 PS id (<8hex>.vp/.fp)
  techniques  u8 @0x1a x u16 (lo = first pass, hi = pass count), align 4
  param names u8 @0x18 x u32 name hash
  tech names  u8 @0x1a x u32 name hash
  param words u8 @0x18 x u32 (hi16 = offset in its block, lo16 = type)
  slot names  block A slots, then block B slots, u32 name hash each (g_<role>_sampler)
  defaults    (the material template blocks)
  block A descriptor @0x68: u8 params in A, u8 slots, u16 size; block B @0xa0: +1 slots, +2 size; PS stage @0xcc.
  The first <params in A> params are block A's (-> cbuffer perInstance, b4, same offsets); the rest live in block B
  after its slots (-> perMaterial, b3, offset = block-B offset - the first B param's offset).
1.0 (DisneyInfinity.exe: FUN_004af380 'Shaders/%s.shd', finalize FUN_004af200): header to 0xA4, same order with
  passes u8 @0x0d x 0x30 (+0 flags, +0x10 vformatCRC, +0x18 VS id, +0x20 PS id), counts params @0x0c,
  techniques @0x0e, block A descriptor @0x44, block B @0x68, then a NUL-terminated name after the slot names.
Names hash with ehash (engine FUN_0044a9f0, case-sensitive). 1.0 programs are D3D9 (constant table CTAB), 3.0 DXBC (RDEF).

python shd.py <shader> [...]          both games' descriptions of a shader, names resolved
python shd.py --pairs                 every shader shipped in both games: params / techniques / passes compared
"""
import os, struct, sys
from functools import lru_cache
import paths


def ehash(name):
    """The engine's name hash for shader parameter / slot / technique names (FUN_0044a9f0): case-SENSITIVE.
    (vtxf.name_hash lowercases first -- right for shader and vformat GUIDs, wrong for 'g_normalMap_scale'.)"""
    b = name.encode('latin-1') + b'\0'
    u, acc, i = (b[0] if b[0] < 128 else b[0] - 256) & 0xffffffff, 0, 0
    while b[i]:
        i += 1
        c = b[i] if b[i] < 128 else b[i] - 256
        u = ((u << 8) | (c & 0xffffffff)) & 0xffffffff
        if i & 3 == 3:
            acc = ((acc + u) * 0x25) & 0xffffffff
    return ((u << ((24 - 8 * (i & 3)) & 31)) + acc) & 0xffffffff

LAYOUT = {  # counts (params, passes, techniques), data start, pass size, pass fields, descriptors A/B
    '3.0': dict(counts=0x18, data=0xE0, psize=0x50, vf=0x18, vs=0x28, ps=0x30, a=0x68, b=0xA0),
    '1.0': dict(counts=0x0C, data=0xA4, psize=0x30, vf=0x10, vs=0x18, ps=0x20, a=0x44, b=0x68),
}


def shader_dir(game):
    return paths.ext30('shaders', 'shaders') if game == '3.0' else paths.ext10('shaders', 'shaders')


def parse(b, game):
    L = LAYOUT[game]
    npar, npass, ntech = b[L['counts']], b[L['counts'] + 1], b[L['counts'] + 2]
    o = L['data']
    passes = []
    for i in range(npass):
        r = b[o:o + L['psize']]
        passes.append(dict(word=struct.unpack_from('<I', r, 0)[0], vf=struct.unpack_from('<I', r, L['vf'])[0],
                           vs=struct.unpack_from('<I', r, L['vs'])[0], ps=struct.unpack_from('<I', r, L['ps'])[0],
                           raw=r))
        o += L['psize']
    techs = [struct.unpack_from('<H', b, o + 2 * i)[0] for i in range(ntech)]
    o = (o + 2 * ntech + 3) & ~3
    pnames = list(struct.unpack_from('<%dI' % npar, b, o)); o += 4 * npar
    tnames = list(struct.unpack_from('<%dI' % ntech, b, o)); o += 4 * ntech
    pwords = list(struct.unpack_from('<%dI' % npar, b, o)); o += 4 * npar
    a, bb = L['a'], L['b']
    nA, slotsA, sizeA = b[a], b[a + 1], struct.unpack_from('<H', b, a + 2)[0]
    slotsB, sizeB = b[bb + 1], struct.unpack_from('<H', b, bb + 2)[0]
    snA = list(struct.unpack_from('<%dI' % slotsA, b, o)); o += 4 * slotsA
    snB = list(struct.unpack_from('<%dI' % slotsB, b, o)); o += 4 * slotsB
    name = None
    if game == '1.0':
        e = b.index(b'\0', o)
        name = b[o:e].decode('latin-1'); o = e + 1
    params = [dict(hash=h, block='A' if i < nA else 'B', offset=w >> 16, type=w & 0xffff)
              for i, (h, w) in enumerate(zip(pnames, pwords))]
    return dict(game=game, passes=passes, techs=[dict(hash=h, first=t & 0xff, count=t >> 8) for h, t in zip(tnames, techs)],
                params=params, slotsA=snA, slotsB=snB, sizeA=sizeA, sizeB=sizeB, name=name, defaults=b[o:])


def load(shader, game):
    p = os.path.join(shader_dir(game), shader + '.shd')
    return parse(open(p, 'rb').read(), game) if os.path.exists(p) else None


# ---- program resources: names for the hashes ----
def ctab(b):
    """[(name, register set 'b'/'i'/'f'/'s', register, count)] from a D3D9 program's constant table."""
    i = b.find(b'CTAB')
    if i < 0:
        return []
    base = i + 4
    n, info = struct.unpack_from('<II', b, base + 12)
    out = []
    for k in range(n):
        nm, rset, ridx, rcnt = struct.unpack_from('<IHHH', b, base + info + 20 * k)
        out.append((b[base + nm:b.index(b'\0', base + nm)].decode('latin-1'), 'bifs'[rset], ridx, rcnt))
    return out


def rdef(b):
    """{'res': [(name, type, bind)], 'cb': [(cbuffer, size, [(var, offset, size)])]} from a DXBC program."""
    d = b.find(b'DXBC')
    if d < 0:
        return {'res': [], 'cb': []}
    for i in range(struct.unpack_from('<I', b, d + 28)[0]):
        co = d + struct.unpack_from('<I', b, d + 32 + 4 * i)[0]
        if b[co:co + 4] != b'RDEF':
            continue
        base = co + 8
        ncb, cboff, nres, resoff, ver = struct.unpack_from('<5I', b, base)
        vsz = 40 if (ver & 0xff00) >= 0x500 else 24
        s = lambda o: b[base + o:b.index(b'\0', base + o)].decode('latin-1')
        res = [(s(struct.unpack_from('<I', b, base + resoff + 32 * r)[0]),) + struct.unpack_from('<I12xI', b, base + resoff + 32 * r + 4)
               for r in range(nres)]
        cbs = []
        for c in range(ncb):
            no, nv, voff, size = struct.unpack_from('<4I', b, base + cboff + 24 * c)
            cbs.append((s(no), size, [(s(struct.unpack_from('<I', b, base + voff + vsz * v)[0]),) +
                                      struct.unpack_from('<II', b, base + voff + vsz * v + 4) for v in range(nv)]))
        return {'res': res, 'cb': cbs}
    return {'res': [], 'cb': []}


def program(game, pid, kind):
    """A program's bytes by id. File names are '%x' (1.0's loader: 'Shaders/%x.vp', no zero padding)."""
    if not pid:
        return None
    for name in ('%x.%s' % (pid, kind), '%08x.%s' % (pid, kind)):
        p = os.path.join(shader_dir(game), name)
        if os.path.exists(p):
            return open(p, 'rb').read()
    return None


@lru_cache(None)
def names():
    """name hash -> name, over every constant / resource / sampler / cbuffer name in both games' programs."""
    out = {}
    for game in ('1.0', '3.0'):
        d = shader_dir(game)
        for f in os.listdir(d):
            if not f.endswith(('.vp', '.fp')):
                continue
            b = open(os.path.join(d, f), 'rb').read()
            if game == '1.0':
                ns = [c[0] for c in ctab(b)]
            else:
                r = rdef(b)
                ns = [x[0] for x in r['res']] + [c[0] for c in r['cb']] + [v[0] for c in r['cb'] for v in c[2]]
            for n in ns:
                out.setdefault(ehash(n), n)
    return out


def nm(h):
    return names().get(h, '#%08x' % h)


def show(shader):
    for game in ('1.0', '3.0'):
        s = load(shader, game)
        if not s:
            print('%s: none' % game)
            continue
        print('== %s %s  A %d B %d  name %s' % (game, shader, s['sizeA'], s['sizeB'], s['name']))
        print('  params:', ', '.join('%s %s@%#x t%#x' % (nm(p['hash']), p['block'], p['offset'], p['type']) for p in s['params']))
        print('  slots A:', [nm(h) for h in s['slotsA']], 'B:', [nm(h) for h in s['slotsB']])
        for t in s['techs']:
            ps = s['passes'][t['first']:t['first'] + t['count']]
            print('  tech %s: %s' % (nm(t['hash']), ' | '.join('w%#x vf %08x vs %08x ps %08x' % (p['word'], p['vf'], p['vs'], p['ps']) for p in ps)))


def pairs():
    both = sorted(set(f[:-4] for f in os.listdir(shader_dir('1.0')) if f.endswith('.shd')) &
                  set(f[:-4] for f in os.listdir(shader_dir('3.0')) if f.endswith('.shd')))
    tech10, tech30 = {}, {}
    same_params = 0
    for sh in both:
        a, c = load(sh, '1.0'), load(sh, '3.0')
        for t in a['techs']: tech10[nm(t['hash'])] = tech10.get(nm(t['hash']), 0) + 1
        for t in c['techs']: tech30[nm(t['hash'])] = tech30.get(nm(t['hash']), 0) + 1
        pa = [(p['hash'], p['block']) for p in a['params']]
        pc = [(p['hash'], p['block']) for p in c['params']]
        same_params += pa == pc[:len(pa)]
    print('%d shaders in both; 1.0 params a prefix of 3.0 params in %d' % (len(both), same_params))
    print('1.0 techniques:', sorted(tech10.items(), key=lambda x: -x[1]))
    print('3.0 techniques:', sorted(tech30.items(), key=lambda x: -x[1]))


if __name__ == '__main__':
    if sys.argv[1:] == ['--pairs']:
        pairs()
    else:
        for a in sys.argv[1:]:
            show(a)

"""vformat.vtxf reader and 1.0 -> 3.0 vertex conversion.
vformatCRC (.oct) == name_hash(format GUID key)  [FUN_0044aa40, loader FUN_004642f0]; shader names hash the
same way (FUN_0045c820 -> Shader_LookupCrc).

Element types (3.0 uses all nine): FLOAT32_2/3/4, FLOAT16_2/4, U8_4_NORMALIZED, S16_4_NORMALIZED (real values),
U8_4, U16_2 (integers: BLENDINDICES, packed texcoords). A 3.0 element is filled from the 1.0 element with
the same semantic (COLOR0, TEXCOORD1, ...): copied when the type matches, converted between any two real
types (normalized <-> float, float16 <-> float32, 3 <-> 4 components) or between integer types; components
the source lacks come from the 3.0 element's declared default, else as shipped data has them: w = +1 for the
*_signed tangents (+-1 in 477,578 of 477,578 sampled vertices, +1 in 94.4%), else 0 (NORMAL w: 0 in 491,568 of
491,568). Only a semantic the source does not have at all is padded.

python vtxf.py <vformat.vtxf> [crc_hex ...]   list formats, or dump the ones matching the CRCs
python vtxf.py --selftest [--ext30 DIR] [--ext10 DIR]   hash vs every shipped vformatCRC, conversions, twins
"""
import re, struct


def name_hash(s):
    """FUN_0044aa40: case-insensitive, 4-byte chunks folded with *0x25."""
    b = [c + 0x20 if 0x41 <= c <= 0x5a else c for c in s.encode()] + [0]
    acc, tot, i = b[0], 0, 0
    while b[i]:
        i += 1
        acc = ((acc << 8) | b[i]) & 0xffffffff
        if (i & 3) == 3:
            tot = ((tot + acc) * 0x25) & 0xffffffff
    return ((acc << ((0x18 - (i & 3) * 8) & 0x1f)) + tot) & 0xffffffff


TOK = re.compile(r'\s*(?:(--[^\n]*)|("(?:[^"\\]|\\.)*")|(\[)|(\])|([{}=,])|([^\s{}=,\[\]]+))')


def lua_table(txt):
    """Minimal Lua table-literal parser for data files ('return {...}', 'ZoneList = {...}'): the first table in
    txt -> dict (keyed) or list (positional); strings unquoted, true/false -> bool, numbers left as text."""
    toks = [t for t in TOK.findall(txt[txt.index('{'):]) if not t[0]]
    toks = [next(x for x in t if x) for t in toks]
    pos = 0

    def val():
        nonlocal pos
        t = toks[pos]; pos += 1
        if t == '{':
            d, lst = {}, []
            while toks[pos] != '}':
                if toks[pos] == '[':
                    pos += 1
                    k = val(); pos += 1  # ']'
                    pos += 1  # '='
                    d[k] = val()
                elif toks[pos + 1] == '=' and toks[pos] not in '{':
                    k = toks[pos]; pos += 2
                    d[k] = val()
                else:
                    lst.append(val())
                if toks[pos] == ',':
                    pos += 1
            pos += 1
            return d if d else lst
        if t.startswith('"'):
            return t[1:-1]
        return {'true': True, 'false': False}.get(t, t)
    return val()


def parse(path):
    return lua_table(open(path, encoding='latin-1').read())['formats']


def elements(fmt, variant='main'):
    """-> [(stream, semantic, type, name)]"""
    return [(si, e['semantic'], e['type'], e['name'])
            for si, s in enumerate(fmt[variant]['streams']) for e in s]


_cache, _by_crc = {}, {}


def formats(game):
    """{GUID: format} of the 1.0 or 3.0 extract's vformat.vtxf."""
    if game not in _cache:
        import paths
        _cache[game] = parse((paths.ext30 if game == 3 else paths.ext10)('shaders', 'shaders', 'vformat.vtxf'))
    return _cache[game]


def fmt_of(crc, game):
    if game not in _by_crc:
        _by_crc[game] = {name_hash(g): f for g, f in formats(game).items()}
    return _by_crc[game][crc]


def candidates_1to3(crc):
    """1.0 vformatCRC -> every 3.0 vformatCRC whose 'main' declaration is identical.
    3.0 has no entry for a 1.0 CRC; the primitive loader (FUN_004553c0) then stores 0xbadddec1 as
    byte 0xc1 -> an empty registry slot -> the input layout is built from nothing."""
    f1, f3 = formats(1), formats(3)
    if crc in {name_hash(g) for g in f3}:
        return [crc]
    src = [f for g, f in f1.items() if name_hash(g) == crc]
    assert src, f'{crc:08x} is in neither vformat.vtxf'
    sig = lambda f: [e[:3] for e in elements(f)]
    return [name_hash(g) for g, f in f3.items() if sig(f) == sig(src[0])]


# type -> (struct code, components); NORM = scale of the normalized integer types
TYPES = {'FLOAT32_2': ('f', 2), 'FLOAT32_3': ('f', 3), 'FLOAT32_4': ('f', 4), 'FLOAT16_2': ('e', 2),
         'FLOAT16_4': ('e', 4), 'U8_4_NORMALIZED': ('B', 4), 'S16_4_NORMALIZED': ('h', 4), 'U8_4': ('B', 4),
         'U16_2': ('H', 2)}
SIZE = {t: struct.calcsize('<%d%s' % (n, c)) for t, (c, n) in TYPES.items()}
NORM = {'U8_4_NORMALIZED': 255.0, 'S16_4_NORMALIZED': 32767.0}


def _real(ty):
    """Holds real values (float or normalized) -- convertible into any other real type."""
    return TYPES[ty][0] in 'fe' or ty in NORM


def decode(ty, b):
    c, n = TYPES[ty]
    v = struct.unpack_from('<%d%s' % (n, c), b)
    return [max(x / NORM[ty], -1.0) for x in v] if ty in NORM else list(v)


def encode(ty, vals):
    c, n = TYPES[ty]
    vals = (list(vals) + [0.0] * n)[:n]
    if ty in NORM:
        lo = 0 if c == 'B' else -1
        vals = [round(max(lo, min(1.0, v)) * NORM[ty]) for v in vals]
    elif c not in 'fe':
        hi = (1 << (8 * struct.calcsize(c))) - 1
        vals = [max(0, min(hi, int(v))) for v in vals]
    return struct.pack('<%d%s' % (n, c), *vals)


def layout(fmt, variant='main'):
    """-> ([stride per stream], {(semantic, type): (stream, offset)}, {(semantic, type): default})
    variant 'skin': the layout 3.0 stores a skinned node's primitives in (2,585 of 2,585 shipped skinned primitives;
    4,094 of 4,094 rigid ones are 'main'): the GPU skinning pass reads it."""
    strides, where, dflt = [], {}, {}
    for si, s in enumerate(fmt[variant]['streams']):
        off = 0
        for e in s:
            where[e['semantic'], e['type']] = (si, off)
            if 'default' in e:
                dflt[e['semantic'], e['type']] = [float(x) for x in e['default']]
            elif e['name'].endswith('_signed'):     # tangent + handedness: w is +1 or -1 in every shipped vertex
                dflt[e['semantic'], e['type']] = [0.0, 0.0, 0.0, 1.0]     # (+1 in 94.4%, 0 never); NORMAL w is 0
            off += SIZE[e['type']]
        strides.append(off)
    return strides, where, dflt


def plan(src_crc, dst_crc, variant='main'):
    """How to build a dst (3.0) vertex from a src (1.0) one (1.0 formats have one layout; variant picks the 3.0 one).
    -> (src strides, dst strides, [(dst stream, dst off, src stream | None, src off | bytes, size, conv)],
        dropped src elements, padded dst elements); conv = None (copy) or (src type, dst type, default)"""
    ss, sw, _ = layout(fmt_of(src_crc, 1))
    ds, dw, dd = layout(fmt_of(dst_crc, 3), variant)
    ops, padded = [], []
    for (sem, ty), (dsi, doff) in dw.items():
        if (sem, ty) in sw:
            ops.append((dsi, doff, *sw[sem, ty], SIZE[ty], None))
            continue
        alt = next(((s2, t2) for (s2, t2) in sw if s2 == sem and _real(t2) == _real(ty)), None)
        if alt:
            ops.append((dsi, doff, *sw[alt], SIZE[ty], (alt[1], ty, dd.get((sem, ty), []))))
        else:
            ops.append((dsi, doff, None, encode(ty, dd.get((sem, ty), [])), SIZE[ty], None))
            padded.append(sem)
    used = {(o[2], o[3]) for o in ops if o[2] is not None}
    dropped = [sem for (sem, ty), loc in sw.items() if loc not in used]
    return ss, ds, ops, dropped, padded


def convert_vertex(src_streams, ops, ds):
    """src_streams: [bytes of this vertex per src stream] -> [bytes per dst stream]"""
    out = [bytearray(n) for n in ds]
    for dsi, doff, ssi, soff, size, conv in ops:
        if ssi is None:
            out[dsi][doff:doff + size] = soff
        elif conv is None:
            out[dsi][doff:doff + size] = src_streams[ssi][soff:soff + size]
        else:
            st, dt, dflt = conv
            vals = decode(st, src_streams[ssi][soff:soff + SIZE[st]])
            n = TYPES[dt][1]
            vals += (dflt or [0.0] * n)[len(vals):n]
            out[dsi][doff:doff + size] = encode(dt, vals)
    return out


def targets_1to3(crc):
    """3.0 formats a 1.0 format can become, best first: identical twins, then conversions ranked
    by (no element besides colour dropped, fewest padded, fewest dropped). -> [(dst, dropped, padded)]"""
    twins = candidates_1to3(crc)
    out = []
    for g in formats(3):
        h = name_hash(g)
        try:
            _, _, ops, dropped, padded = plan(crc, h)
        except KeyError:
            continue
        if 'POSITION' in padded or h in twins:
            continue
        lossy = [d for d in dropped if not d.startswith('COLOR')]
        out.append(((len(lossy), len(padded), len(dropped)), h, dropped, padded))
    return [(t, [], []) for t in twins] + [(h, d, p) for _, h, d, p in sorted(out)]


def remap_1to3(crc, prefer=()):
    """Unique twin, or among several identical ones the one the materials' shaders declare."""
    hits = candidates_1to3(crc)
    if len(hits) > 1:
        hits = [h for h in hits if h in prefer] or hits
    assert len(hits) == 1, f'{crc:08x}: {len(hits)} 3.0 twins {[hex(h) for h in hits]} (no twin = needs vertex conversion)'
    return hits[0]


def _crcs(p):
    from oct import Oct
    try:
        return {r.ints()[0] & 0xffffffff for r in Oct.load(p).find(name='vformatCRC')}
    except Exception:
        return set()


def _selftest():
    import paths
    from par import pmap
    assert name_hash('8bc48a5716cd76811da52f7aee4c28b2') == 0x63ae710f  # verified vs 3.0 corpus id
    known = {name_hash(g) for g in formats(3)}
    used = set().union(*pmap(_crcs, sorted(paths.ext30().rglob('*.oct')), chunksize=16))
    assert used <= known, 'vformatCRCs no 3.0 format hashes to: %s' % sorted(map(hex, used - known))
    print('name_hash: all %d vformatCRCs used by shipped 3.0 .oct files are 3.0 format GUIDs' % len(used))
    # conversions: real types round-trip within their precision, integers clamp, defaults fill
    assert decode('U8_4_NORMALIZED', encode('U8_4_NORMALIZED', [1, 0.5, 0, 2])) == [1.0, 128 / 255, 0.0, 1.0]
    assert decode('S16_4_NORMALIZED', encode('S16_4_NORMALIZED', [-1, 1, 0])) == [-1.0, 1.0, 0.0, 0.0]
    assert decode('FLOAT16_4', encode('FLOAT16_4', [0.5, -2, 3])) == [0.5, -2.0, 3.0, 0.0]
    assert decode('U8_4', encode('U8_4', [3, 300, -1, 7])) == [3, 255, 0, 7]
    st = encode('FLOAT32_3', [0.0, 1.0, 0.0])
    op = (0, 0, 0, 0, SIZE['S16_4_NORMALIZED'], ('FLOAT32_3', 'S16_4_NORMALIZED', [0, 0, 0, 1]))
    assert decode('S16_4_NORMALIZED', convert_vertex([st], [op], [8])[0]) == [0.0, 1.0, 0.0, 1.0]
    print('element conversions OK')
    if paths.where('ext10')[0]:
        assert remap_1to3(0xea1acc4c) == 0x63ae710f  # tested in-game 2026-10-02: renders, no CreateInputLayout throw
        conv = sum(1 for g in formats(1) if not candidates_1to3(name_hash(g)))
        print('1.0: %d formats, %d without a 3.0 twin (converted)' % (len(formats(1)), conv))


if __name__ == '__main__':
    import argparse, paths
    ap = argparse.ArgumentParser(description='vformat.vtxf reader')
    ap.add_argument('file', nargs='?')
    ap.add_argument('crcs', nargs='*')
    ap.add_argument('--selftest', action='store_true')
    paths.add_args(ap, 'ext30', 'ext10')
    a = paths.use(ap.parse_args())
    if a.selftest:
        _selftest()
    elif not a.file:
        ap.print_help()
    else:
        fmts = parse(a.file)
        want = {int(x, 16) for x in a.crcs}
        for g, f in fmts.items():
            h = name_hash(g)
            if want and h not in want:
                continue
            print(f'{h:08x}  {g}  variants={list(f)}')
            if want:
                for si, sem, ty, nm in elements(f):
                    print(f'    s{si}  {sem:<14} {ty:<20} {nm}')
        if not want:
            print(len(fmts), 'formats')

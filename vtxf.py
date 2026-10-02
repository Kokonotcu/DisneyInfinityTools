"""vformat.vtxf reader. vformatCRC (.oct) == name_hash(format GUID key)  [FUN_0044aa40, loader FUN_004642f0]

python vtxf.py <vformat.vtxf> [crc_hex ...]   -> list formats, or dump the ones matching the CRCs
"""
import re, struct, sys


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
    """Minimal Lua table-literal parser: returns dict (keyed) or list (positional)."""
    toks = [t for t in TOK.findall(txt.split('return', 1)[1]) if not t[0]]
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


V10 = r'D:\DI-Extract\1.0\assets\shaders\shaders\vformat.vtxf'
V30 = r'D:\DI-Extract\3.0\assets\shaders\shaders\vformat.vtxf'


_cache = {}


def candidates_1to3(crc):
    """1.0 vformatCRC -> every 3.0 vformatCRC whose 'main' declaration is identical.
    3.0 has no entry for a 1.0 CRC; the primitive loader (FUN_004553c0) then stores 0xbadddec1 as
    byte 0xc1 -> an empty registry slot -> the input layout is built from nothing."""
    if not _cache:
        _cache[1], _cache[3] = parse(V10), parse(V30)
    f1, f3 = _cache[1], _cache[3]
    if crc in {name_hash(g) for g in f3}:
        return [crc]
    src = [f for g, f in f1.items() if name_hash(g) == crc]
    assert src, f'{crc:08x} is in neither vformat.vtxf'
    sig = lambda f: [e[:3] for e in elements(f)]
    return [name_hash(g) for g, f in f3.items() if sig(f) == sig(src[0])]


SIZE = {'FLOAT32_2': 8, 'FLOAT32_3': 12, 'FLOAT32_4': 16, 'FLOAT16_2': 4, 'FLOAT16_4': 8,
        'U8_4': 4, 'U8_4_NORMALIZED': 4, 'S16_4_NORMALIZED': 8, 'U16_2': 4}


_by_crc = {}


def fmt_of(crc, game):
    if not _cache:
        _cache[1], _cache[3] = parse(V10), parse(V30)
    if game not in _by_crc:
        _by_crc[game] = {name_hash(g): f for g, f in _cache[game].items()}
    return _by_crc[game][crc]


def layout(fmt):
    """-> ([stride per stream], {(semantic, type): (stream, offset)}, {(semantic, type): default})"""
    strides, where, dflt = [], {}, {}
    for si, s in enumerate(fmt['main']['streams']):
        off = 0
        for e in s:
            where[e['semantic'], e['type']] = (si, off)
            if 'default' in e:
                dflt[e['semantic'], e['type']] = [float(x) for x in e['default']]
            off += SIZE[e['type']]
        strides.append(off)
    return strides, where, dflt


def _encode(ty, vals):
    n = int(ty.split('_')[1])
    vals = (list(vals) + [0.0] * n)[:n]
    if ty.startswith('FLOAT32'):
        return struct.pack('<%df' % n, *vals)
    if ty.startswith('FLOAT16'):
        return struct.pack('<%de' % n, *vals)
    if ty == 'U8_4_NORMALIZED':
        return bytes(max(0, min(255, round(v * 255))) for v in vals)
    return bytes(SIZE[ty])


def plan(src_crc, dst_crc):
    """How to build a dst (3.0) vertex from a src (1.0) one.
    -> (src strides, dst strides, [(dst stream, dst off, src stream | None, src off | bytes, size, conv)],
        dropped src elements, padded dst elements)"""
    ss, sw, _ = layout(fmt_of(src_crc, 1))
    ds, dw, dd = layout(fmt_of(dst_crc, 3))
    ops, padded = [], []
    for (sem, ty), (dsi, doff) in dw.items():
        if (sem, ty) in sw:
            ops.append((dsi, doff, *sw[sem, ty], SIZE[ty], None))
            continue
        alt = next(((s2, t2) for (s2, t2) in sw if s2 == sem and t2[:5] == 'FLOAT' and
                    t2[5:] [2:] == ty[7:] and t2 != ty), None)   # same semantic, float16 <-> float32
        if alt:
            ops.append((dsi, doff, *sw[alt], SIZE[ty], (alt[1], ty)))
        else:
            ops.append((dsi, doff, None, _encode(ty, dd.get((sem, ty), [])), SIZE[ty], None))
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
            st, dt = conv
            n = int(dt.split('_')[1])
            vals = struct.unpack_from('<%d%s' % (n, 'f' if st.startswith('FLOAT32') else 'e'), src_streams[ssi], soff)
            out[dsi][doff:doff + size] = _encode(dt, vals)
    return out


def targets_1to3(crc):
    """3.0 formats a 1.0 format can become, best first: identical twins, then conversions ranked
    by (no element besides colour dropped, fewest padded, fewest dropped). -> [(dst, dropped, padded)]"""
    twins = candidates_1to3(crc)
    out = []
    for g in _cache[3]:
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


if __name__ == '__main__':
    assert remap_1to3(0xea1acc4c) == 0x63ae710f  # tested in-game 2026-10-02: renders, no CreateInputLayout throw
    assert name_hash('8bc48a5716cd76811da52f7aee4c28b2') == 0x63ae710f  # verified vs 3.0 corpus id
    fmts = parse(sys.argv[1])
    want = {int(x, 16) for x in sys.argv[2:]}
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

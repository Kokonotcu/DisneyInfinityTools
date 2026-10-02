"""vformat.vtxf reader. vformatCRC (.oct) == name_hash(format GUID key)  [FUN_0044aa40, loader FUN_004642f0]

python vtxf.py <vformat.vtxf> [crc_hex ...]   -> list formats, or dump the ones matching the CRCs
"""
import re, sys


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


def remap_1to3(crc):
    """1.0 vformatCRC -> the 3.0 vformatCRC whose 'main' declaration is identical.
    3.0 has no entry for a 1.0 CRC; the primitive loader (FUN_004553c0) then stores 0xbadddec1 as
    byte 0xc1 -> an empty registry slot -> the input layout is built from nothing."""
    f1, f3 = parse(V10), parse(V30)
    if crc in {name_hash(g) for g in f3}:
        return crc
    src = [f for g, f in f1.items() if name_hash(g) == crc]
    assert src, f'{crc:08x} is in neither vformat.vtxf'
    sig = lambda f: [e[:3] for e in elements(f)]
    hits = [name_hash(g) for g, f in f3.items() if sig(f) == sig(src[0])]
    assert len(hits) == 1, f'{crc:08x}: {len(hits)} identical 3.0 declarations {[hex(h) for h in hits]}'
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

"""Compiled gamedb tables (.chd, assets/gamedb/<pkg>/<pkg>): read, edit, write.

A .chd is a Lua table literal compiled to binary; 30 of the 33 shipped 3.0 files have their source .lua
beside them. Layout, decoded from those 33 files (all little-endian):
  0x00 u32 0x0123abcd   0x04 u32 0x3000   0x08 u32 0x20 -> key table   0x0C u32 key table size
  0x10 -> string table (= end of the key table)   0x14 string table size
  0x18 -> data (end of the strings, zero-padded to 4)   0x1C data size (data runs to EOF)
  key table     each distinct key name once, sorted bytewise: u16 length, bytes, NUL
  string table  each distinct string value once, same form and order ('' only when some value is '')
  data          root entry (00 00 00 d0, 8), then every table in depth-first pre-order:
                u16 0xfeed, u16 n, n entries x 8 B: u24 key, u8 type, 4 B value
  key    offset of the name in the key table; 0xffffff for an array item
  type   0x00 i32   0x20 f32   0x30 string (offset in the string table)   0x40 bool (0 / 1)
         0x50 table (offset in data);   + 0x80 for an array item
A table holds named entries sorted by key, or array items in Lua order, never both. The key table also
holds 'true' / 'false' when some bool is true / false (the compiler interns the Lua identifiers): all
11 inventory__lua files carry them unused; every other key is used. Types seen: 0x00 0x20 0x30 0x40
0x50 0x80 0xb0 0xd0. Array items 0xa0 / 0xc0 (f32 / bool) never ship; dump() writes them by the rule.
So the key table, string table and layout all follow from the tree, and load() keeps only the tree:
dicts (named), lists (array; an empty table loads as []), int, float, bool, str. Strings are bytes in
the file (stateData is a 16-byte blob); they load as UTF-8 with surrogateescape, so any bytes survive.
Floats load as the shortest decimal that packs to the same f32 (0.3, as in the .lua).
Measured on 33 files (345,031 entries, 36,037 tables): load/dump round trip exact for 33,
export->import exact for 33. lua() + dump() compiles 29 of the 30 .lua sources to their .chd byte-exact;
empire/rumpusdata.lua has ID = 5 where its .chd has 4.
dump() takes any tree -- changed values or types, new / removed keys, records, strings -- and rebuilds
both name tables and every offset. An int / float / bool edit changes only its 4 value bytes. Whether
the game reads a key it does not know is another matter.

  d = load(raw); d['Data']['Records'][0]['maxOwned'] = 2; raw = dump(d)
  raw = dump(lua(Path('locks__lua.lua').read_text('latin-1')))

python chd.py dump <file.chd>                 the tree: key = value  (type)
python chd.py export <file.chd> <out.json>    the tree as JSON (non-UTF-8 bytes as \\udc80-\\udcff)
python chd.py import <in.json> <out.chd>
python chd.py --selftest [--ext30 DIR]
"""
import argparse, json, re, struct, sys
from pathlib import Path

MAGIC, VER, FEED, ARRAY = 0x0123abcd, 0x3000, 0xfeed, 0xffffff
INT, FLOAT, STR, BOOL, TABLE, ITEM = 0x00, 0x20, 0x30, 0x40, 0x50, 0x80
ROOT = struct.pack('<Ii', (TABLE | ITEM) << 24, 8)


def _dec(b):
    """UTF-8 text as is; anything else one char per byte, bytes >= 0x80 as U+DC80-U+DCFF."""
    try:
        return b.decode('utf-8')
    except UnicodeDecodeError:
        return b.decode('ascii', 'surrogateescape')


def _enc(s):
    return s.encode('utf-8', 'surrogateescape')


def _f32(b):
    """f32 bytes -> the shortest float that packs back to them."""
    x = struct.unpack('<f', b)[0]
    return next((y for y in (float('%.*g' % (p, x)) for p in range(1, 10)) if struct.pack('<f', y) == b), x)


def _names(raw, at, size):
    """offset -> str for a key / string table."""
    out, q = {}, at
    while q < at + size:
        n, = struct.unpack_from('<H', raw, q)
        out[q - at] = _dec(raw[q + 2:q + 2 + n])
        q += 3 + n
    return out


def load(raw):
    """-> the root table (dict / list tree)."""
    magic, ver, ko, ks, so, ss, do, ds = hdr = struct.unpack_from('<8I', raw)
    if magic != MAGIC or ver != VER or ko != 0x20 or so != ko + ks or raw[do:do + 8] != ROOT:
        raise ValueError('not a 3.0 .chd, or a variant none of the shipped files has: %r' % (hdr,))
    keys, strs = _names(raw, ko, ks), _names(raw, so, ss)

    def table(off):
        tag, n = struct.unpack_from('<2H', raw, do + off)
        assert tag == FEED, 'no table at data + %#x' % off
        named, items = {}, []
        for q in range(do + off + 4, do + off + 4 + 8 * n, 8):
            w, v = struct.unpack_from('<Ii', raw, q)
            t = w >> 24 & 0x7f
            x = (v if t == INT else _f32(raw[q + 4:q + 8]) if t == FLOAT else strs[v] if t == STR else
                 bool(v) if t == BOOL and v in (0, 1) else table(v) if t == TABLE else None)
            if x is None:
                raise ValueError('entry at %#x: type %#x value %d' % (q, w >> 24, v))
            if w >> 24 & ITEM:
                items.append(x)
            else:
                named[keys[w & ARRAY]] = x
        if named and items:
            raise ValueError('table at data + %#x mixes named and array entries' % off)
        return named or items
    return table(8)


def _pool(names):
    """-> (bytes, name -> offset): each name once, sorted bytewise, u16 length + bytes + NUL."""
    out, at = bytearray(), {}
    for b, s in sorted((_enc(s), s) for s in names):
        at[s] = len(out)
        out += struct.pack('<H', len(b)) + b + b'\0'
    return bytes(out), at


def dump(tree):
    keys, strs = set(), set()

    def scan(v):
        if isinstance(v, dict):
            keys.update(v)
        if isinstance(v, (dict, list)):
            for x in v.values() if isinstance(v, dict) else v:
                scan(x)
        elif isinstance(v, bool):
            keys.add('true' if v else 'false')
        elif isinstance(v, str):
            strs.add(v)
    scan(tree)
    kt, ko = _pool(keys)
    st, so = _pool(strs)
    data = bytearray(ROOT)

    def table(v):
        es = sorted(v.items(), key=lambda e: _enc(e[0])) if isinstance(v, dict) else [(None, x) for x in v]
        at = len(data)
        data.extend(struct.pack('<2H', FEED, len(es)) + bytes(8 * len(es)))
        for i, (k, x) in enumerate(es):
            t, val = ((BOOL, struct.pack('<i', x)) if isinstance(x, bool) else
                      (INT, struct.pack('<i', x)) if isinstance(x, int) else
                      (FLOAT, struct.pack('<f', x)) if isinstance(x, float) else
                      (STR, struct.pack('<I', so[x])) if isinstance(x, str) else
                      (TABLE, None) if isinstance(x, (dict, list)) else (None, None))
            if t is None:
                raise TypeError('%r: %r is not int, float, bool, str, dict or list' % (k, x))
            w = (ARRAY | (t | ITEM) << 24) if k is None else ko[k] | t << 24
            data[at + 4 + 8 * i:at + 8 + 8 * i] = struct.pack('<I', w)
            data[at + 8 + 8 * i:at + 12 + 8 * i] = val or struct.pack('<I', table(x))
        return at
    table(tree)
    pad = -(0x20 + len(kt) + len(st)) % 4
    do = 0x20 + len(kt) + len(st) + pad
    hdr = struct.pack('<8I', MAGIC, VER, 0x20, len(kt), 0x20 + len(kt), len(st), do, len(data))
    return hdr + kt + st + bytes(pad) + bytes(data)


def show(v, name=None, ind=''):
    """-> lines: key = value  (type); a table is its key and {n} (named) or [n] (array), entries below."""
    if not isinstance(v, (dict, list)):
        t = {bool: 'bool', int: 'int', float: 'float', str: 'string'}[type(v)]
        return ['%s%s = %s  (%s)' % (ind, name, repr(_enc(v))[1:] if t == 'string' else json.dumps(v), t)]
    es = v.items() if isinstance(v, dict) else (('[%d]' % i, x) for i, x in enumerate(v, 1))
    out = [] if name is None else ['%s%s  %s' % (ind, name, ('{%d}' if isinstance(v, dict) else '[%d]') % len(v))]
    return out + [s for k, x in es for s in show(x, k, ind if name is None else ind + '  ')]


_LUA = re.compile(r'--[^\n]*|"((?:\\.|[^"\\])*)"|\'((?:\\.|[^\'\\])*)\'|(-?\d+\.\d*)|(-?\d+)|(\w+)|(\S)')


def lua(text):
    """The tables of a generated gamedb .lua (text read as Latin-1) as load() gives them, so
    dump(lua(text)) compiles it. Covers what the generators write: name / ["name"] keys, C escapes, ints,
    decimals, true / false. Tokens: (group, value), group 1-2 string, 3 float, 4 int, 5 name, 6 symbol."""
    tok = [(g, _dec(s.encode('latin-1').decode('unicode_escape').encode('latin-1')) if g < 3 else
            float(s) if g == 3 else int(s) if g == 4 else s)
           for m in _LUA.finditer(text) for g in [m.lastindex] if g for s in [m[g]]] + [(6, '}')]
    i = 0

    def table():
        nonlocal i
        named, items = {}, []
        while tok[i] != (6, '}'):
            k, i = ((tok[i + 1][1], i + 4) if tok[i] == (6, '[') else
                    (tok[i][1], i + 2) if tok[i + 1] == (6, '=') else (None, i))
            g, v = tok[i]
            i += 1
            v = table() if (g, v) == (6, '{') else {'true': True, 'false': False}[v] if g == 5 else v
            if k is None:
                items.append(v)
            else:
                named[k] = v
            i += tok[i] == (6, ',')
        i += 1
        return named or items
    return table()


def _count(v):
    """-> (entries, tables) below v."""
    if not isinstance(v, (dict, list)):
        return 0, 0
    sub = [_count(x) for x in (v.values() if isinstance(v, dict) else v)]
    return len(v) + sum(e for e, t in sub), 1 + sum(t for e, t in sub)


def _diff(a, b, at=''):
    """-> one line per leaf where trees a and b differ in value, or per place they differ in shape or type."""
    if isinstance(a, dict) and isinstance(b, dict) and a.keys() == b.keys():
        return [s for k in a for s in _diff(a[k], b[k], '%s.%s' % (at, k) if at else k)]
    if isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        return [s for i, (x, y) in enumerate(zip(a, b), 1) for s in _diff(x, y, '%s[%d]' % (at, i))]
    if type(a) is not type(b) or isinstance(a, (dict, list)):
        return ['%s: SHAPE' % at]
    return [] if a == b else ['%s = %r in the .lua, %r in the .chd' % (at, a, b)]


def _check(p):
    """(round trip exact, export->import exact, entries, tables, .lua beside, it compiles exact, its diffs)"""
    raw = p.read_bytes()
    d = load(raw)
    src = p.with_suffix('.lua')
    t = lua(src.read_text('latin-1')) if src.exists() else d
    return ((dump(d) == raw, dump(json.loads(json.dumps(d))) == raw) + _count(d) +
            (src.exists(), src.exists() and dump(t) == raw, _diff(t, d)))


def _selftest():
    import paths
    from par import pmap
    files = sorted(paths.ext30('gamedb').rglob('*.chd'), key=lambda p: -p.stat().st_size)
    assert files, 'no .chd under %s' % paths.ext30('gamedb')
    res = pmap(_check, files, chunksize=2)
    rt, ji, ents, tabs, srcs, same = (sum(c) for c in list(zip(*res))[:6])
    print('round-trip exact: %d of %d' % (rt, len(files)))
    print('export->import exact: %d of %d' % (ji, len(files)))
    print('entries: %s, tables: %s' % (format(ents, ','), format(tabs, ',')))
    print('.lua beside: %d, compiles to its .chd byte-exact: %d' % (srcs, same))
    for p, r in zip(files, res):
        if not (r[0] and r[1]):
            print('  FAIL', p, r[:6])
        for s in r[6]:
            print('  %s: %s' % (p.with_suffix('.lua').relative_to(paths.ext30('gamedb')), s))
    shape = sum(s.endswith(': SHAPE') for r in res for s in r[6])
    # edits: a same-type value change touches only its 4 bytes; a new key + string rebuilds both tables
    p = next(p for p in files[::-1] if 'Records' in load(p.read_bytes()).get('Data', {}))
    raw = p.read_bytes()
    d = load(raw)
    rec = d['Data']['Records'][0]
    k = next(k for k, v in rec.items() if type(v) is int)
    rec[k] += 1
    diff = [i for i, (a, b) in enumerate(zip(raw, dump(d))) if a != b]
    assert len(dump(d)) == len(raw) and diff and diff[-1] - diff[0] < 4, diff
    rec['chd_selftest_key'] = 'chd selftest string'
    rec['Categories'] = [1.5, False]
    new = dump(d)
    assert json.dumps(load(new), sort_keys=True) == json.dumps(d, sort_keys=True), 'edit did not reload the same'
    print('%s: %s + 1 changed %d byte(s); a new key, string and f32 / bool array items reload the same'
          % (p.relative_to(paths.ext30('gamedb')), k, len(diff)))
    assert rt == ji == len(files) and not shape, 'failures above'


if __name__ == '__main__':
    import paths
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('cmd', choices=('dump', 'export', 'import'), nargs='?')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--selftest', action='store_true')
    paths.add_args(ap, 'ext30')
    a = paths.use(ap.parse_args())
    sys.stdout.reconfigure(encoding='utf-8')
    if a.selftest:
        _selftest()
    elif a.cmd == 'dump':
        print('\n'.join(show(load(Path(a.args[0]).read_bytes()))))
    elif a.cmd == 'export':
        Path(a.args[1]).write_text(json.dumps(load(Path(a.args[0]).read_bytes()), indent=1), encoding='utf-8')
        print('exported', a.args[0], '->', a.args[1])
    elif a.cmd == 'import':
        Path(a.args[1]).write_bytes(dump(json.loads(Path(a.args[0]).read_text(encoding='utf-8'))))
        print('imported', a.args[0], '->', a.args[1])
    else:
        ap.print_help()

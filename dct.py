"""Localisation dictionaries (.dct, assets/lang/<pkg>): read, edit, write.

Layout, decoded from the 471 shipped files (all little-endian, the _xbox360 ones too):
  0x00 'DICT'   0x04 u32 0x2000   0x08 u32 initval (hash seed)   0x0C rel -> slot table (0x20)
  0x10 u32 slots   0x14 u32 1   0x18 rel -> containers (= end of the slot table)   0x1C u32 0 containers
  0x20 slots x 12 B: u32 hash, rel -> value, u32 0. An empty slot is 12 zero bytes
  string pool to EOF: '' first (even when no key uses it), then each distinct value once, UTF-8 + NUL,
      in order of first use by slot
  rel: target = field offset + 1 + stored value
Keys are stored only as hashes: hash = Bob Jenkins' lookup2 of the name's Latin-1 bytes, seeded with
initval. A stringids*.dct ships beside each package's language files, and its values ARE the key names.
Placement: slot = hash % slots, then linear probe forward with wrap. slots = (keys + 1) * 6 // 5 for all
27 key counts that ship (int(keys * 1.2) + 1 misses 67 files). The insertion order cannot be recovered,
so load() lists keys in table order from the first empty slot, and re-inserting in that order rebuilds
the same table.
Measured on 471 files (4,372,484 keys): load/dump round trip exact for 471, export->import exact for 471.
The hash reproduces 337,023 of 337,023 keys in the 37 stringids files from their names. Hashing UTF-8
instead misses 'mba_side_yondu_quest_01' + NBSP (3 files). 8 of those keys (2 names x 4 files) hold
'...' as U+2026 where the hash has '&'. The stringids beside each file name 4,319,415 of its 4,372,484
keys. The rest export as '#hash'.
The _xbox360 files have the same layout. 141 of 147 differ from _win32 in initval, key set or text.
Containers (24 B records in a third-party converter) never ship, so load() rejects them.

  d = load(raw); d['keys'][key('my_new_key', d['initval'])] = 'Text'; raw = dump(d)

python dct.py list <file.dct>                 name = "value" (name: through the stringids beside it)
python dct.py export <file.dct> <out.json>    {"initval": "99fc0d82", "keys": {name or "#hash": value}}
python dct.py import <in.json> <out.dct>      new names are hashed with the file's initval
python dct.py --selftest [--ext30 DIR]
"""
import argparse, functools, json, re, struct, sys
from pathlib import Path

M = 0xffffffff


def _mix(a, b, c):
    for s1, s2, s3 in ((13, 8, 13), (12, 16, 5), (3, 10, 15)):
        a = (a - b - c) & M ^ c >> s1
        b = (b - c - a) & M ^ a << s2 & M
        c = (c - a - b) & M ^ b >> s3
    return a, b, c


def lookup2(k, seed):
    """Bob Jenkins' hash() from lookup2.c (1996) over bytes k."""
    n, a, b, c = len(k), 0x9e3779b9, 0x9e3779b9, seed
    full = n - n % 12
    for i in range(0, full, 12):
        x, y, z = struct.unpack_from('<3I', k, i)
        a, b, c = _mix(a + x & M, b + y & M, c + z & M)
    x, y, z = struct.unpack('<3I', k[full:].ljust(12, b'\0'))
    return _mix(a + x & M, b + y & M, c + n + (z << 8) & M)[2]   # last block: c's low byte is the length


def key(name, initval):
    """The stored hash of a key name."""
    return lookup2(name.encode('latin-1'), initval)


def slots(n):
    return (n + 1) * 6 // 5


def load(raw):
    """-> {'initval': int, 'keys': {hash: value}}, keys in re-insertion order."""
    magic, ver, init, kp, ns, one, cp, nc = hdr = struct.unpack_from('<4s7I', raw)
    if magic != b'DICT' or ver != 0x2000 or kp != 0x13 or one != 1 or nc:
        raise ValueError('not a 3.0 .dct, or a variant none of the shipped files has: %r' % (hdr,))
    tab = list(struct.iter_unpack('<IiI', raw[0x20:0x20 + 12 * ns]))
    e = tab.index((0, 0, 0))   # slots > keys, so there is always one
    keys = {}
    for i in range(e + 1, e + ns):
        h, rel, _ = tab[i % ns]
        if rel:
            p = 0x25 + 12 * (i % ns) + rel
            keys[h] = raw[p:raw.index(b'\0', p)].decode('utf-8')
    return {'initval': init, 'keys': keys}


def dump(d):
    keys = d['keys']
    ns = slots(len(keys))
    tab = [None] * ns
    for h in keys:
        i = h % ns
        while tab[i] is not None:
            i = (i + 1) % ns
        tab[i] = h
    pool, at = bytearray(1), {'': 0}
    for h in tab:
        if h is not None and keys[h] not in at:
            at[keys[h]] = len(pool)
            v = keys[h].encode('utf-8')
            assert b'\0' not in v, 'NUL in the value of %08x' % h
            pool += v + b'\0'
    end = 0x20 + 12 * ns
    out = bytearray(struct.pack('<4s7I', b'DICT', 0x2000, d['initval'], 0x13, ns, 1, end - 0x19, 0))
    for i, h in enumerate(tab):
        out += bytes(12) if h is None else struct.pack('<IiI', h, end + at[keys[h]] - 0x25 - 12 * i, 0)
    return bytes(out + pool)


@functools.lru_cache(None)
def names(folder, initval):
    """hash -> name for every name in the stringids*.dct in folder."""
    out = {}
    for p in sorted(Path(folder).glob('stringids*.dct')):
        for v in load(p.read_bytes())['keys'].values():
            v = v.replace('…', '&')   # 2 shipped names hold an ellipsis where their hash has '&'
            try:
                out.setdefault(key(v, initval), v)
            except UnicodeEncodeError:
                pass
    return out


def to_json(path):
    """The editable form of a .dct file. Keys are named through the stringids beside it, else '#hash'."""
    d = load(Path(path).read_bytes())
    nm = names(Path(path).parent, d['initval'])
    return {'initval': '%08x' % d['initval'],
            'keys': {nm.get(h, '#%08x' % h): v for h, v in d['keys'].items()}}


def from_json(j):
    init = int(j['initval'], 16)
    keys = {int(k[1:], 16) if re.fullmatch('#[0-9a-f]{8}', k) else key(k, init): v for k, v in j['keys'].items()}
    if len(keys) != len(j['keys']):
        raise ValueError('two keys hash the same with initval %08x' % init)
    return {'initval': init, 'keys': keys}


def _check(p):
    """(round trip exact, export->import exact, stringids keys, of them hashed right, keys, of them named)"""
    raw = p.read_bytes()
    d = load(raw)
    j = json.loads(json.dumps(to_json(p), ensure_ascii=False))
    ids = d['keys'] if p.name.startswith('stringids') else {}
    good = sum(key(v.replace('…', '&'), d['initval']) == h for h, v in ids.items())
    return (dump(d) == raw, dump(from_json(j)) == raw, len(ids), good, len(d['keys']),
            sum(not k.startswith('#') for k in j['keys']))


def _selftest():
    import paths
    from par import pmap
    files = sorted(paths.ext30('lang').rglob('*.dct'), key=lambda p: -p.stat().st_size)
    assert files, 'no .dct under %s' % paths.ext30('lang')
    res = pmap(_check, files, chunksize=4)
    rt, ji, tried, good, nkeys, named = (sum(c) for c in zip(*res))
    print('round-trip exact: %d of %d' % (rt, len(files)))
    print('export->import exact: %d of %d' % (ji, len(files)))
    print('hash reproduces %s of %s stringids keys from their names' % (format(good, ','), format(tried, ',')))
    print('keys named by the stringids beside them: %s of %s' % (format(named, ','), format(nkeys, ',')))
    for p, r in zip(files, res):
        if not (r[0] and r[1] and r[2] == r[3]):
            print('  FAIL', p, r)
    # a new key: the table grows to slots(n + 1) and the engine's probe from hash % slots reaches it
    d = load(files[-1].read_bytes())
    h = key('dct_selftest_new_key', d['initval'])
    d['keys'][h] = 'new'
    raw = dump(d)
    ns, i = slots(len(d['keys'])), h % slots(len(d['keys']))
    while struct.unpack_from('<I', raw, 0x20 + 12 * i)[0] != h:
        assert raw[0x20 + 12 * i:0x2c + 12 * i] != bytes(12), 'probe met an empty slot'
        i = (i + 1) % ns
    assert load(raw) == d and struct.unpack_from('<I', raw, 16)[0] == ns
    print('added key found by probing from hash %% slots in %s' % files[-1].name)
    assert rt == ji == len(files) and good == tried, 'failures above'


if __name__ == '__main__':
    import paths
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('cmd', choices=('list', 'export', 'import'), nargs='?')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--selftest', action='store_true')
    paths.add_args(ap, 'ext30')
    a = paths.use(ap.parse_args())
    sys.stdout.reconfigure(encoding='utf-8')
    if a.selftest:
        _selftest()
    elif a.cmd == 'list':
        for k, v in to_json(a.args[0])['keys'].items():
            print(k, '=', json.dumps(v, ensure_ascii=False))
    elif a.cmd == 'export':
        Path(a.args[1]).write_text(json.dumps(to_json(a.args[0]), ensure_ascii=False, indent=1), encoding='utf-8')
        print('exported', a.args[0], '->', a.args[1])
    elif a.cmd == 'import':
        Path(a.args[1]).write_bytes(dump(from_json(json.loads(Path(a.args[0]).read_text(encoding='utf-8')))))
        print('imported', a.args[0], '->', a.args[1])
    else:
        ap.print_help()

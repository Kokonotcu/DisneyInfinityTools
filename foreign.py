"""Records in an .oct that never occur in shipped 3.0 content -- the cheapest complete check for
1.0 leftovers. Key = (pool, record name, parent record name, level, type).
python foreign.py <built .oct>"""
import sys
from collections import Counter
from pathlib import Path
from oct import Oct
from par import pmap


def keys(path):
    try:
        o = Oct.load(str(path))
    except Exception:
        return set()
    out, parent = set(), {}
    for r in o.recs:
        parent[r.level] = r.name
        p = parent.get(r.level - 1, '') if r.level > 1 else ''
        name = r.name if not r.name.isdigit() else '#'
        pn = p if not p.isdigit() else '#'
        out.add((r.pool or '', name, pn, r.level, r.type))
    return out


CACHE = Path(__file__).with_name('shippedkeys.json')


def shipped_keys():
    """Every (pool, name, parent, level, type) over the shipped 3.0 worlds (cached)."""
    import json
    if CACHE.exists():
        return {tuple(k) for k in json.loads(CACHE.read_text())}
    shipped = set()
    for ks in pmap(keys, sorted(Path(r'D:\DI-Extract\3.0\assets\worlds').glob('*/*.oct')), chunksize=8):
        shipped |= ks
    CACHE.write_text(json.dumps(sorted(shipped)))
    return shipped


def foreign_in(path):
    shipped = shipped_keys()
    return sorted(k for k in keys(Path(path)) if k not in shipped)


if __name__ == '__main__':
    shipped = shipped_keys()
    files = list(Path(r'D:\DI-Extract\3.0\assets\worlds').glob('*/*.oct'))
    mine = keys(Path(sys.argv[1]))
    foreign = sorted(k for k in mine if k not in shipped)
    # loosen: same (pool, name, parent, level) with another type is a type mismatch, not a new field
    loose = {k[:4] for k in shipped}
    print(f'{len(files)} shipped worlds; {len(mine)} distinct keys in ours; {len(foreign)} not in 3.0')
    for k in foreign:
        print('  %s %-30s %-28s L%d type %#x' % (('TYPE ONLY' if k[:4] in loose else 'NEW      '), k[0] + '.' + k[1], 'under ' + k[2], k[3], k[4]))

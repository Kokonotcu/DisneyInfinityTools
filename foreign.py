"""Records in an .oct that never occur in shipped 3.0 content of the same kind -- the cheapest complete check for
1.0 leftovers. Key = (pool, record name, parent record name, level, type); the reference set is measured per asset
kind (worlds, realms, exploders: each converter checks against what 3.0 ships for what it writes).
python foreign.py <built .oct> [--kind worlds|realms|exploders] [--ext30 DIR]"""
from pathlib import Path
from oct import Oct
from par import pmap
import paths

def keys(o):
    """Key set of an Oct (or a path to one). Raises if it does not parse: a broken file is not 'clean'."""
    o = o if isinstance(o, Oct) else Oct.load(str(o))
    out, parent = set(), {}
    for r in o.recs:
        parent[r.level] = r.name
        p = parent.get(r.level - 1, '') if r.level > 1 else ''
        name = r.name if not r.name.isdigit() else '#'
        pn = p if not p.isdigit() else '#'
        out.add((r.pool or '', name, pn, r.level, r.type))
    return out


def _shipped_one(p):
    try:
        return keys(p)
    except Exception:
        return None


KINDS = ('worlds', 'realms', 'exploders')
_ref = {}


def shipped_keys(kind='worlds'):
    """Every (pool, name, parent, level, type) over the shipped 3.0 .oct files of one asset kind (cached)."""
    if kind not in _ref:
        def build():
            shipped, bad = set(), 0
            for ks in pmap(_shipped_one, sorted(paths.ext30(kind).rglob('*.oct')), chunksize=8):
                if ks is None:
                    bad += 1
                else:
                    shipped |= ks
            assert shipped, 'no shipped .oct parsed under ' + str(paths.ext30(kind))
            if bad:
                print('foreign: %d shipped %s did not parse and are not in the reference set' % (bad, kind))
            return sorted(shipped)
        _ref[kind] = {tuple(k) for k in paths.cached('shippedkeys-%s.json' % kind, build, 3)}
    return _ref[kind]


def foreign_in(o, kind='worlds'):
    """Keys of o (an Oct or a path) that no shipped 3.0 .oct of that kind has."""
    shipped = shipped_keys(kind)
    return sorted(k for k in keys(o) if k not in shipped)


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='Record kinds in an .oct that 3.0 never ships')
    ap.add_argument('oct')
    ap.add_argument('--kind', choices=KINDS, default='worlds', help='the shipped asset kind to compare with')
    paths.add_args(ap, 'ext30')
    a = paths.use(ap.parse_args())
    shipped = shipped_keys(a.kind)
    mine = keys(Path(a.oct))
    foreign = sorted(k for k in mine if k not in shipped)
    # loosen: same (pool, name, parent, level) with another type is a type mismatch, not a new field
    loose = {k[:4] for k in shipped}
    print(f'{len(mine)} distinct keys in ours; {len(foreign)} not in 3.0')
    for k in foreign:
        print('  %s %-30s %-28s L%d type %#x' % (('TYPE ONLY' if k[:4] in loose else 'NEW      '), k[0] + '.' + k[1], 'under ' + k[2], k[3], k[4]))

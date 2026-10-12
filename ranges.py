"""Values and counts in an .oct that exceed anything shipped 3.0 content uses.
Key = (pool, record name, parent name). Per key: max |int| value and records per file.
python ranges.py <built .oct> [--ext30 DIR]"""
from pathlib import Path
from oct import Oct, spec, FLOAT
from par import pmap
import paths


def stats(path):
    o = Oct.load(str(path))
    out, parent = {}, {}
    for r in o.recs:
        parent[r.level] = r.name
        p = parent.get(r.level - 1, '') if r.level > 1 else ''
        k = '|'.join((r.pool or '', '#' if r.name.isdigit() else r.name, '#' if p.isdigit() else p))
        v = 0 if spec(r.type)[2] == FLOAT else max((abs(x) for x in r.ints()), default=0)
        mx, n = out.get(k, (0, 0))
        out[k] = (max(mx, v), n + 1)
    return out


def _shipped_one(p):
    try:
        return stats(p)
    except Exception:
        return {}


def shipped():
    """Per key over the shipped 3.0 worlds: (max |int|, max records per file). Committed as shippedranges.json."""
    def build():
        agg = {}
        for st in pmap(_shipped_one, sorted(paths.ext30('worlds').glob('*/*.oct')), chunksize=8):
            for k, (v, n) in st.items():
                a = agg.get(k, (0, 0))
                agg[k] = (max(a[0], v), max(a[1], n))
        return agg
    return paths.cached('shippedranges.json', build, 2)


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='Values/counts beyond what shipped 3.0 worlds use')
    ap.add_argument('oct')
    paths.add_args(ap, 'ext30')
    a = paths.use(ap.parse_args())
    ref = shipped()
    for k, (v, n) in sorted(stats(Path(a.oct)).items()):
        if k not in ref:
            continue  # foreign.py covers unknown keys
        sv, sn = ref[k]
        if v > sv or n > sn:
            print('%-60s value %10d (shipped max %10d)   count %6d (shipped max %6d)' % (k, v, sv, n, sn))

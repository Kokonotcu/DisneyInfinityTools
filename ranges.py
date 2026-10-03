"""Values and counts in an .oct that exceed anything shipped 3.0 content uses.
Key = (pool, record name, parent name). Per key: max |int| value and records per file.
python ranges.py <built .oct>"""
import sys, json
from pathlib import Path
from oct import Oct
from par import pmap

CACHE = Path(__file__).with_name('shippedranges.json')


def stats(path):
    try:
        o = Oct.load(str(path))
    except Exception:
        return {}
    out, parent = {}, {}
    for r in o.recs:
        parent[r.level] = r.name
        p = parent.get(r.level - 1, '') if r.level > 1 else ''
        k = '|'.join((r.pool or '', '#' if r.name.isdigit() else r.name, '#' if p.isdigit() else p))
        try:
            v = max((abs(x) for x in r.ints()), default=0)
        except Exception:
            v = 0
        mx, n = out.get(k, (0, 0))
        out[k] = (max(mx, v), n + 1)
    return out


def shipped():
    if CACHE.exists():
        return json.loads(CACHE.read_text())
    agg = {}
    for st in pmap(stats, sorted(Path(r'D:\DI-Extract\3.0\assets\worlds').glob('*/*.oct')), chunksize=8):
        for k, (v, n) in st.items():
            a = agg.get(k, (0, 0))
            agg[k] = (max(a[0], v), max(a[1], n))
    CACHE.write_text(json.dumps(agg))
    return agg


if __name__ == '__main__':
    ref = shipped()
    for k, (v, n) in sorted(stats(Path(sys.argv[1])).items()):
        if k not in ref:
            continue  # foreign.py covers unknown keys
        sv, sn = ref[k]
        if v > sv or n > sn:
            print('%-60s value %10d (shipped max %10d)   count %6d (shipped max %6d)' % (k, v, sv, n, sn))

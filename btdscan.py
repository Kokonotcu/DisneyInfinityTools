"""Survey: which worlds carry several TriangleList collision meshes (SerializeIndex), and how big
their .btd is. python btdscan.py <worlds dir>"""
import sys
from pathlib import Path
from oct import Oct
from par import pmap


def scan(d):
    f, b = d / f'{d.name}.oct', d / f'{d.name}.btd'
    if not f.exists() or not b.exists():
        return None
    try:
        s = Oct.load(str(f))
    except Exception:
        return None
    si = [r.ints()[0] for r in s.recs if r.name == 'SerializeIndex']
    return (len(si), max(si) if si else -1, d.name, b.stat().st_size) if si else None


if __name__ == '__main__':
    res = sorted((r for r in pmap(scan, sorted(Path(sys.argv[1]).iterdir()), chunksize=8) if r), reverse=True)
    print(len(res), 'worlds with TriangleList shapes; (count, max SerializeIndex, world, btd bytes):')
    for r in res[:8]:
        print('  ', r)

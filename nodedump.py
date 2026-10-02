"""Print one geometry node's record block, for field-by-field diffing.

  python nodedump.py <world> [nodeIndex]     3.0 extract
  python nodedump.py --file <path> [i]
"""
import sys
from pathlib import Path
from oct import Oct

W30 = Path(r'D:\DI-Extract\3.0\assets\worlds')


def nodes(f):
    """(TypeRec, blockStart, blockEnd) for every geometry node."""
    out = []
    for r in f.find(name='Type', pool='SceneTreeNodePool'):
        if r.names()[0] != 'Geometry':
            continue
        i = f.idx(r)
        # walk back to the record that opens this node's subtree
        lvl = r.level
        j = i
        while j > 0 and f.recs[j - 1].level >= lvl:
            j -= 1
        k = i + 1
        while k < len(f.recs) and f.recs[k].level >= lvl:
            k += 1
        out.append((j - 1, k))
    return out


a = sys.argv[1:]
if a[0] == '--file':
    p = Path(a[1]); want = int(a[2]) if len(a) > 2 else None
else:
    p = W30 / a[0] / (a[0] + '.oct'); want = int(a[1]) if len(a) > 1 else None

f = Oct.load(p)
ns = nodes(f)
print(f'{p.name}: {len(ns)} geometry nodes')
for n, (i, k) in enumerate(ns):
    npr = [r for r in f.recs[i:k] if r.name == 'NumPrimitives']
    cnt = npr[0].ints()[0] if npr else 1
    if want is None:
        print(f'  node {n:3}  recs {i}..{k}  NumPrimitives {cnt}')
        continue
    if n != want:
        continue
    for r in f.recs[i:k]:
        print(f'{"  " * (r.level - f.recs[i].level)}{r.name} [{r.type:03x}] = {r.value()}')

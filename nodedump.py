"""Print one geometry node's record block, for field-by-field diffing.

  python nodedump.py <world | path to .oct> [nodeIndex] [--ext30 DIR]   (a bare world name is looked up in the 3.0 extract)
"""
import argparse
from pathlib import Path
from oct import Oct
import paths


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


ap = argparse.ArgumentParser(description="Print one geometry node's record block")
ap.add_argument('world', help='3.0 world name, or a path to any .oct')
ap.add_argument('node', nargs='?', type=int)
paths.add_args(ap, 'ext30')
a = paths.use(ap.parse_args())
p = Path(a.world) if a.world.lower().endswith('.oct') else paths.ext30('worlds', a.world, a.world + '.oct')
want = a.node

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

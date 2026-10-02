"""On a multi-primitive node, is the vbuf stream-major or primitive-major?

split-mesh.py lays out each primitive's streams contiguously, then the next
primitive -- primitive-major. That is what shipped multi-NODE worlds do, which
is what it was written against. psx_l3_planet_main's 3-primitive node instead
puts ALL of stream 0 for every primitive first, then all of stream 1.

Test: for every multi-primitive node, do the stream regions stay disjoint --
max(end of stream s) <= min(start of stream s+1) across the node's primitives?
"""
import collections
from pathlib import Path
from oct import Oct

W30 = Path(r'D:\DI-Extract\3.0\assets\worlds')
res = collections.Counter()
bad = []

for d in sorted(p for p in W30.iterdir() if p.is_dir()):
    p = d / (d.name + '.oct')
    if not p.exists():
        continue
    try:
        f = Oct.load(p)
    except Exception:
        continue
    cur = None
    for r in f.recs:
        if r.pool != 'SceneTreeNodePool':
            continue
        if r.name == 'Primitives':
            cur = []
        elif r.name == 'Vdata' and cur is not None:
            v = r.ints()
            ns, nv = v[0], v[1]
            streams = []
            for s in range(ns):
                _, off, stride = v[2 + s * 3: 5 + s * 3]
                streams.append((off, off + nv * stride))
            cur.append(streams)
        elif r.name == 'ParentNodeReferences' and cur is not None:
            if len(cur) > 1 and len({len(s) for s in cur}) == 1:
                ns = len(cur[0])
                if ns > 1:
                    # per stream index, the span across all primitives
                    spans = [(min(pr[s][0] for pr in cur), max(pr[s][1] for pr in cur))
                             for s in range(ns)]
                    ordered = all(spans[s][1] <= spans[s + 1][0] for s in range(ns - 1))
                    # primitive-major would mean each primitive's own streams are
                    # adjacent: pr[s].end == pr[s+1].start
                    primmajor = all(pr[s][1] == pr[s + 1][0]
                                    for pr in cur for s in range(ns - 1))
                    if ordered and not primmajor:
                        res['STREAM-major (regions disjoint per stream)'] += 1
                    elif primmajor and not ordered:
                        res['primitive-major (own streams adjacent)'] += 1
                    elif ordered and primmajor:
                        res['both (single primitive-like)'] += 1
                    else:
                        res['neither'] += 1
                        if len(bad) < 3:
                            bad.append((d.name, cur[:3]))
            cur = None

for k, v in res.most_common():
    print('%-46s %6d nodes' % (k, v))
if bad:
    print('\nneither, samples:')
    for b in bad:
        print('  ', b[0], b[1])

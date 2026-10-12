"""On a multi-primitive node, is the vbuf stream-major or primitive-major?

Multi-NODE worlds lay out each node's streams contiguously, then the next node -- primitive-major
across nodes. psx_l3_planet_main's 3-primitive node instead puts ALL of stream 0 for every primitive
first, then all of stream 1; conv.build_buffers writes that (stream-major within a node).

Test: for every multi-primitive node, do the stream regions stay disjoint --
max(end of stream s) <= min(start of stream s+1) across the node's primitives?
python streamlayout.py [--ext30 DIR]
"""
import argparse, collections
from oct import Oct
import paths

ap = argparse.ArgumentParser(description='Stream layout of multi-primitive nodes over the 3.0 worlds')
paths.add_args(ap, 'ext30')
paths.use(ap.parse_args())
W30 = paths.ext30('worlds')
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
assert res and not (res['neither'] or res['primitive-major (own streams adjacent)']), 'not every node is stream-major'

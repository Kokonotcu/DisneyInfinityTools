"""Vertex layout of skinned vs rigid primitives: a primitive of a SKINNED node (its node block has NumInfluences) is
stored in its 3.0 vformat's 'skin' variant, every other one in 'main' (Vdata strides compared with each variant's).
Shipped 3.0 exploders + choreographies (19,841 files, 2026-10-14): rigid 75,895/75,895 'main', skinned 3,396/3,396
'skin', in six formats (rentable.SKINNED_SHIPPED). di1compat writes skinned converted primitives the same way
(render.hpp, R plans, cfg skinlayout10); run it on octtest --render output to check.

python skinlayout.py <dir> [...] [--assert] [--ext30 DIR]   walks *.oct; --assert: exit 1 if any primitive breaks the rule
"""
import os, sys
from collections import Counter
from oct import Oct
import vtxf


def strides(fmt, variant):
    return [sum(vtxf.SIZE[e['type']] for e in s) for s in fmt[variant]['streams']] if variant in fmt else None


def main(dirs):
    by3 = {vtxf.name_hash(g): f for g, f in vtxf.formats(3).items()}
    tally, example, skinfmts, files = Counter(), {}, set(), 0
    for root in dirs:
        for dp, _, fn in os.walk(root):
            for n in fn:
                if not n.endswith('.oct'):
                    continue
                try:
                    f = Oct.load(os.path.join(dp, n))
                except Exception:
                    continue
                files += 1
                for node in f.find(name='Node', pool='SceneTreeNodePool'):
                    i, j = f.block(node)
                    blk = f.recs[i:j]
                    skinned = any(r.name == 'NumInfluences' for r in blk)
                    crc = None
                    for r in blk:
                        if r.name == 'vformatCRC':
                            crc = r.ints()[0] & 0xffffffff
                        elif r.name == 'Vdata' and crc is not None:
                            v = r.ints()
                            got = [v[4 + 3 * s] for s in range(v[0])]
                            fmt = by3.get(crc)
                            k = '1.0 vformat (translated)' if fmt is None else \
                                'main' if got == strides(fmt, 'main') else 'skin' if got == strides(fmt, 'skin') else 'neither'
                            if skinned and k == 'skin':
                                skinfmts.add(crc)
                            tally[skinned, k] += 1
                            example.setdefault((skinned, k), os.path.join(dp, n))
    print(files, 'files; skinned primitives use', ' '.join(sorted('%08x' % c for c in skinfmts)))
    bad = 0
    for (sk, k), c in sorted(tally.items(), key=str):
        ok = k == ('skin' if sk else 'main') or (not sk and k.startswith('1.0'))
        bad += 0 if ok else c
        print('%s %-26s %6d %s%s' % ('skinned' if sk else 'rigid  ', k, c, example[sk, k], '' if ok else '   <-- breaks the rule'))
    return bad


if __name__ == '__main__':
    import argparse, paths
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dirs', nargs='+', metavar='dir')
    ap.add_argument('--assert', dest='strict', action='store_true', help='exit 1 if any primitive breaks the rule')
    paths.add_args(ap, 'ext30')
    a = paths.use(ap.parse_args())
    bad = main(a.dirs)
    sys.exit(1 if bad and a.strict else 0)

"""The Python render pipeline's output for one 1.0 world -- the oracle di1compat's C++ render transcode must equal
(octtest --render). Exactly the render steps of conv.prepare: autobundle.resolve on the untouched .oct, then
convert_geometry, then the 1.0-only Material RenderCaps removed; nothing else (no pools dropped, no graph).
python renderoracle.py <1.0 world dir> <out dir> [--ext10 DIR] [--ext30 DIR]
"""
import argparse
from pathlib import Path
import autobundle, conv, paths
from oct import Oct


def main(world, out):
    world, out = Path(world), Path(out)
    stem = world.name
    s = Oct.load(world / f'{stem}.oct')
    data, problems, vmap, tiers, _ = autobundle.resolve(world, s)
    assert not problems, problems[:5]
    out.mkdir(parents=True, exist_ok=True)
    conv.convert_geometry(s, conv.geo_nodes(s), world, stem, vmap, out, stem)
    for mt in s.find(name='Material', pool='MaterialPool'):
        i, j = s.block(mt)
        s.recs[i + 1:j] = [r for r in s.recs[i + 1:j] if r.name != 'RenderCaps']
    s.save(out / f'{stem}.oct')
    (out / f'{stem}.mtb').write_bytes(data)
    print('tiers', {t: tiers.count(t) for t in set(tiers)})


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('world', help='1.0 world dir')
    ap.add_argument('out', help='out dir')
    paths.add_args(ap, 'ext10', 'ext30')
    a = paths.use(ap.parse_args())
    main(a.world, a.out)

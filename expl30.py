"""Convert a 1.0 exploder (expendable model) to 3.0: assets/exploders/<xx>/<name>/ (loose, like worlds).

Exploders are one-node assets in the world format: Scene > Transform 'Anchor' > one Geometry node,
links named 'Anchor|<node>'. Measured against shipped 3.0 psx_barrel, the differences are:
  1.0 SurfaceTypesPool, PrimaryAsset link          -> dropped
  3.0 AssetPool, CollisionAggregate per instance   -> from the 3.0 template
  DataNode link 'Anchor|X' -> 'Anchor|X|Transform|In WorldBasis.Transform' (aggregate uses the same)
  graph sized for the node under root 'Anchor'; vertex formats + materials as for worlds (conv.prepare).

python expl30.py <1.0 exploder dir> [...] [--assets DIR]
python expl30.py --package <1.0 package.lua> [--assets DIR]   converts every Model the package uses (from --ext10)
"""
import argparse
from pathlib import Path
from oct import Oct, children
from collections import Counter
import graph, paths
from conv import (pool_rec, geo_nodes, _reindex, _roundtrip, prepare, foreign_pools, rename_files, copy_textures,
                  sort_pools, sort_associations, check_foreign)
from native30 import native_scene

# besides the pools 3.0 never ships (conv.foreign_pools: SurfaceTypes...): gameplay and set-piece pools an exploder
# does not carry in 3.0 (psx_barrel)
DROP_POOLS = ('BoundingVolumePool', 'MeridianLocatorPool', 'NavMeshPool', 'SceneNodeRefsPool')
TEMPLATE = ('exploders', 'ps', 'psx_barrel', 'psx_barrel.oct')     # in the 3.0 extract


def convert(src_dir, assets=None):
    """-> loose 3.0 exploder folder <assets>/exploders/<xx>/<name>/ (default: the game's assets)."""
    src_dir = Path(src_dir)
    assets = Path(assets) if assets else paths.game('assets')
    stem = src_dir.name
    s, ref = Oct.load(src_dir / ('%s.oct' % stem)), Oct.load(paths.ext30(*TEMPLATE))
    out = assets / 'exploders' / stem[:2].lower() / stem.lower()
    data, tiers, tocopy, gn = prepare(s, src_dir, out, stem.lower(), foreign_pools(s) + list(DROP_POOLS))
    native_scene(s)   # 1.0 Light nodes (bb05_socket04's point lights) broke every VFX in-game

    # associations: drop PrimaryAsset; DataNode links get the 3.0 path; one CollisionAggregate each
    tmpl = None
    for a in ref.find(name='Association', pool='AssociationPool'):
        if ref.kids(a)['Type'].names()[0] == 'CollisionAggregate':
            tmpl = ref.block(a)
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    add = []
    for a in reversed(s.find(name='Association', pool='AssociationPool')):
        k = s.kids(a)
        t = k['Type'].names()[0]
        if t == 'PrimaryAsset':
            i, j = s.block(a)
            del s.recs[i:j]
        elif t == 'CollisionInstanceToDataNode':
            path = k['NodeName'].names()[0]
            if not path.endswith('|Transform|In WorldBasis.Transform'):
                path += '|Transform|In WorldBasis.Transform'
                k['NodeName'].set_name(path)
            blk = _reindex(ref, s, ref.recs[tmpl[0]:tmpl[1]])
            bk = children(blk)
            bk['NodeName'].set_name(path)
            bk['CollisionInstanceRef'].set_int(k['CollisionInstanceRef'].ints()[0])
            bk['SceneTreeNodeRef'].set_int(nodes.index(gn[0][0]))
            add.append(blk)
    _, end = s.block(pool_rec(s, 'AssociationPool'))
    s.recs[end:end] = [r for blk in add for r in blk]

    # 3.0-only AssetPool and the graph, from the template
    i, j = ref.block(pool_rec(ref, 'AssetPool'))
    s.recs.extend(_reindex(ref, s, ref.recs[i:j]))
    i, j = s.block(pool_rec(s, 'SubNetworkPool'))
    a2, b2 = ref.block(pool_rec(ref, 'SubNetworkPool'))
    s.recs[i:j] = _reindex(ref, s, ref.recs[a2:b2])
    s = _roundtrip(s)
    names = [k['NodeName'].names()[0] for _, k, _ in geo_nodes(s)]
    graph.expand(s, names, 'Anchor')
    s = _roundtrip(s)
    ag = s.find(name='AggregateCount')
    if ag:
        ag[0].set_int(sum(1 for r in s.recs if r.name == 'Instance' and r.pool == 'CollisionInstancePool'))
    rename_files(s, stem, stem.lower())   # internal file names follow the lower-case install name
    s = sort_associations(sort_pools(s))  # the shipped pool and association orders (AssetPool before AssociationPool)
    check_foreign(s, 'exploders')
    s.save(out / ('%s.oct' % stem.lower()))
    (out / ('%s.mtb' % stem.lower())).write_bytes(data)
    copy_textures(tocopy, assets / 'textures')
    return out, dict(Counter(tiers)), len(tocopy)


def models_of(package):
    from luabc import run_package
    pk = run_package(package)
    return sorted({e['Model'].split('/')[-1].lower() for e in pk['Expendables'].values()
                   if e.get('Model', '').lower().startswith('exploders/')})


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description='Convert 1.0 exploders to 3.0')
    ap.add_argument('dirs', nargs='*', help='1.0 exploder folders')
    ap.add_argument('--package', help='1.0 expendables package (.lua): convert every Model it uses')
    ap.add_argument('--assets', help="write into this assets folder (default: the game's; a mod folder works)")
    paths.add_args(ap)
    a = paths.use(ap.parse_args())
    dirs = [Path(d) for d in a.dirs]
    if a.package:
        dirs += [paths.ext10('exploders', m[:2], m) for m in models_of(a.package)]
    bad = []
    for d in dirs:
        try:
            out, tiers, ntex = convert(d, a.assets)
            print('%-40s -> %s  materials %s, %d textures' % (d.name, out, tiers, ntex))
        except Exception as e:
            bad.append((d.name, str(e)[:120]))
    print('%d converted, %d failed' % (len(dirs) - len(bad), len(bad)))
    for b in bad:
        print('  FAIL', b)

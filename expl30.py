"""Convert a 1.0 exploder (expendable model) to 3.0: assets/exploders/<xx>/<name>/ (loose, like worlds).

Exploders are one-node assets in the world format: Scene > Transform 'Anchor' > one Geometry node,
links named 'Anchor|<node>'. Measured against shipped 3.0 psx_barrel, the differences are:
  1.0 SurfaceTypesPool, PrimaryAsset link          -> dropped
  3.0 AssetPool, CollisionAggregate per instance   -> from the 3.0 template
  DataNode link 'Anchor|X' -> 'Anchor|X|Transform|In WorldBasis.Transform' (aggregate uses the same)
  graph sized for the node under root 'Anchor'; vertex formats + materials as for worlds (world30).

python expl30.py <1.0 exploder dir> [...]     |  --package <1.0 package.lua>  converts every Model it uses
"""
import sys, shutil
from pathlib import Path
from oct import Oct
import graph, autobundle
from conv import kids, pool_rec, geo_nodes, _reindex, _roundtrip, convert_geometry
from native30 import native_scene

DROP_POOLS = ('BoundingVolumePool', 'MeridianLocatorPool', 'NavMeshPool', 'SceneNodeRefsPool', 'SurfaceTypesPool')

SRC_ROOT = Path(r'D:\DI-Extract\1.0\assets\exploders')
TEMPLATE = Path(r'D:\DI-Extract\3.0\assets\exploders\ps\psx_barrel\psx_barrel.oct')
GAME = Path(r'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition\assets')


def convert(src_dir, out_root=GAME / 'exploders'):
    src_dir = Path(src_dir)
    stem = src_dir.name
    s, ref = Oct.load(src_dir / ('%s.oct' % stem)), Oct.load(TEMPLATE)
    data, problems, vmap, tiers, tocopy = autobundle.resolve(src_dir, s)
    assert not problems, '%s: %d materials unresolved: %s' % (stem, len(problems), problems[:3])
    native_scene(s)   # 1.0 Light nodes (bb05_socket04's point lights) broke every VFX in-game
    gn = geo_nodes(s)
    for name in DROP_POOLS:
        p = pool_rec(s, name)
        if p:
            i, j = s.block(p)
            del s.recs[i:j]
    out = out_root / stem[:2].lower() / stem.lower()
    out.mkdir(parents=True, exist_ok=True)
    convert_geometry(s, gn, src_dir, stem, vmap, out, stem.lower())
    for m in s.find(name='Material', pool='MaterialPool'):          # 1.0-only field
        i, j = s.block(m)
        s.recs[i + 1:j] = [r for r in s.recs[i + 1:j] if r.name != 'RenderCaps']

    # associations: drop PrimaryAsset; DataNode links get the 3.0 path; one CollisionAggregate each
    tmpl = None
    for a in ref.find(name='Association', pool='AssociationPool'):
        if kids(ref, a)['Type'].names()[0] == 'CollisionAggregate':
            tmpl = ref.block(a)
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    add = []
    for a in reversed(s.find(name='Association', pool='AssociationPool')):
        k = kids(s, a)
        t = k['Type'].names()[0]
        if t == 'PrimaryAsset':
            i, j = s.block(a)
            del s.recs[i:j]
        elif t == 'CollisionInstanceToDataNode':
            path = k['NodeName'].names()[0]
            if not path.endswith('|Transform|In WorldBasis.Transform'):
                path += '|Transform|In WorldBasis.Transform'
                k['NodeName'].pay = s.add_name(path).to_bytes(2, 'little')
            blk = _reindex(ref, s, ref.recs[tmpl[0]:tmpl[1]])
            bk = {r.name: r for r in blk[1:]}
            bk['NodeName'].pay = s.add_name(path).to_bytes(2, 'little')
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
    for r in s.recs:   # internal file names follow the lower-case install name
        if r.type in (0x05, 0x0B):
            try:
                v = s.name(int.from_bytes(r.pay[:2], 'little'))
            except Exception:
                continue
            if stem in v and v != stem.lower():
                r.pay = s.add_name(v.replace(stem, stem.lower())).to_bytes(2, 'little')
    s.save(out / ('%s.oct' % stem.lower()))
    (out / ('%s.mtb' % stem.lower())).write_bytes(data)
    for f in tocopy:
        shutil.copy2(f, GAME / 'textures' / f.name)
    return out, dict(__import__('collections').Counter(tiers)), len(tocopy)


def models_of(package):
    from luabc import run_package
    pk = run_package(package)
    return sorted({e['Model'].split('/')[-1].lower() for e in pk['Expendables'].values()
                   if e.get('Model', '').lower().startswith('exploders/')})


if __name__ == '__main__':
    args = sys.argv[1:]
    dirs = ([SRC_ROOT / m[:2] / m for m in models_of(args[1])] if args[:1] == ['--package']
            else [Path(a) for a in args])
    bad = []
    for d in dirs:
        try:
            out, tiers, ntex = convert(d)
            print('%-40s -> %s  materials %s, %d textures' % (d.name, out, tiers, ntex))
        except Exception as e:
            bad.append((d.name, str(e)[:120]))
    print('%d converted, %d failed' % (len(dirs) - len(bad), len(bad)))
    for b in bad:
        print('  FAIL', b)

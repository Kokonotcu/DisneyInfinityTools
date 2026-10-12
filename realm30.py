"""Convert a 1.0 realm (sky geometry + lighting rigs) to 3.0: assets/realms/<name>.zip.

Rules measured on rlm_intro_day, which ships in both games (--selftest reproduces 3.0's records):
  RealmDataPool       -> a RealmData scene node at index 1: parent of every former root child, NodeRef
                         = itself, DisplayLayer 0, + AddToZone 1 / RotateToGravity / RotateSkipLighting /
                         BlendWithGravity 0. Every node reference >= 1 shifts by one.
  Light               1.0 named fields -> LightType 6 (Directional) / 7 (Ambient), FeatureCaps 0,
                         LightData directional [r, g, b, intensity, 0 x6], ambient [r, g, b, intensity];
                         the light keeps its Uuid
  LightRig            gains a Uuid after LightRefs
  SurfaceTypesPool, PrimaryAsset dropped; AssetPool 'Root' added; geometry/materials as for worlds
  The realm's 1.0-only textures go into the zip's textures/ folder, as rlm_moa_motunui_day ships them.

python realm30.py rlm_in_day [...] [--assets DIR]   convert + install as <assets>/realms/<name>.zip
python realm30.py --selftest                        rlm_intro_day 1.0 -> compare with the shipped 3.0 records
(sources come from --ext10 <ext>/assets/realms, the template from --ext30; --assets defaults to the game's)
"""
import argparse, hashlib, tempfile
from pathlib import Path
from oct import Oct, children
import di3arc, paths
from conv import (pool_rec, _reindex, _roundtrip, prepare, foreign_pools, check_foreign, copy_textures, sort_pools,
                  sort_associations)

TEMPLATE = ('realms', 'rlm_intro_day', 'rlm_intro_day.oct')    # in the 3.0 extract
REALMDATA_FIELDS = ('Name', 'AmbientRigRef', 'SunRigRef', 'SceneSettingsRef', 'SettingsName', 'ZoneName',
                    'RigBlendType', 'SettingsNonDefault', 'SunNoDirection', 'SunNoColor', 'StartWeight',
                    'FollowsCamera', 'ParallaxFactor', 'GlobalOverride')
LIGHT_TYPE = {'Directional': 6, 'Ambient': 7}


def _uuid(seed):
    return bytes([16]) + hashlib.md5(seed.encode()).digest()


def _take(blk, src):
    """replace blk's record of src's name by a copy of src (record types differ between games)"""
    i = next(i for i, r in enumerate(blk) if r.name == src.name and r.level == src.level)
    blk[i] = type(src)(src.f, src.level, src.type, src.name_idx, bytes(src.pay))


def convert(src_dir, out_dir):
    """-> (out_dir with the loose 3.0 realm files, textures to put in its textures/ folder)"""
    src_dir, out_dir = Path(src_dir), Path(out_dir)
    stem = src_dir.name
    s, t = Oct.load(src_dir / f'{stem}.oct'), Oct.load(paths.ext30(*TEMPLATE))
    # 1.0-only pools (SurfaceTypes...) go; RealmDataPool becomes the RealmData node below
    data, _, tocopy, _ = prepare(s, src_dir, out_dir, stem, foreign_pools(s, keep=('RealmDataPool',)))

    tnodes = t.find(name='Node', pool='SceneTreeNodePool')
    tlight = {t.kids(n)['LightType'].value(): n for n in tnodes
              if t.kids(n)['Type'].names()[0] == 'Light'}
    # lights: rebuilt from a shipped 3.0 light of the same kind
    for n in reversed(s.find(name='Node', pool='SceneTreeNodePool')):
        k = s.kids(n)
        if k['Type'].names()[0] != 'Light':
            continue
        lt = LIGHT_TYPE[k['LightType'].names()[0]]
        a, b = t.block(tlight[lt])
        blk = _reindex(t, s, t.recs[a:b])
        bk = children(blk)
        for f in ('NodeName', 'Uuid', 'DisplayLayer', 'ExclusionType', 'ParentNodeReferences', 'LocalToParentMatrix'):
            _take(blk, k[f])
        bk = children(blk)
        col, inten = k['LightColor'].floats(), k['LightIntensity'].value()
        bk['LightData'].set_floats(col + [inten] + [0.0] * (len(bk['LightData'].floats()) - 4))
        bk['FeatureCaps'].set_int(0)
        blk[0].pay = n.pay
        i, j = s.block(n)
        s.recs[i:j] = blk

    # RealmDataPool -> RealmData node right after the Scene node
    rd = {r.name: r for r in s.recs if r.pool == 'RealmDataPool' and r.level == 3}
    a, b = t.block(tnodes[1])
    blk = _reindex(t, s, t.recs[a:b])
    bk = children(blk)
    for f in REALMDATA_FIELDS:
        _take(blk, rd[f])
    bk = children(blk)
    bk['SunLightRef'].set_int(rd['SunLightRef'].ints()[0] + 1)
    bk['NodeName'].set_name(rd['Name'].names()[0].upper())
    bk['Uuid'].pay = _uuid(stem + '/RealmData')
    i, j = s.block(pool_rec(s, 'RealmDataPool'))
    del s.recs[i:j]
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    _, e = s.block(nodes[0])
    s.recs[e:e] = blk

    # node references shift by one; former root children hang under the RealmData node
    for r in s.recs:
        if r.name == 'ParentNodeReferences' and r not in blk:
            r.set_ints([p + 1 if p >= 1 else 1 for p in r.ints()])
        elif r.name in ('LightRefs', 'LinkedSceneTreeNodeRefs'):
            r.set_ints([x + 1 for x in r.ints()])
        elif r.name == 'LinkedNodeRefs':
            r.set_ints([x + 1 if x >= 0 else x for x in r.ints()])
    # rigs gain a Uuid after LightRefs
    tu = next(r for r in t.recs if r.pool == 'LightRigPool' and r.name == 'Uuid')
    for rig in s.find(name='LightRig', pool='LightRigPool'):
        k = s.kids(rig)
        u = _reindex(t, s, [tu])[0]
        u.pay = _uuid(stem + '/' + k['RigName'].names()[0] + rig.names()[0])
        s.recs.insert(s.idx(k['LightRefs']) + 1, u)

    # PrimaryAsset -> AssetPool (before AssociationPool, as shipped)
    for asc in reversed(s.find(name='Association', pool='AssociationPool')):
        if s.kids(asc)['Type'].names()[0] == 'PrimaryAsset':
            i, j = s.block(asc); del s.recs[i:j]
    a, b = t.block(pool_rec(t, 'AssetPool'))
    i, _ = s.block(pool_rec(s, 'AssociationPool'))
    s.recs[i:i] = _reindex(t, s, t.recs[a:b])
    s = _roundtrip(s)                    # inserted records get their pool tags here
    for i, n in enumerate(s.find(name='Node', pool='SceneTreeNodePool')):
        n.set_name(str(i))
    s = _roundtrip(s)

    s = sort_associations(sort_pools(s))      # the shipped pool and association orders
    check_foreign(s, 'realms')
    s.save(out_dir / f'{stem}.oct')
    (out_dir / f'{stem}.mtb').write_bytes(data)
    (out_dir / f'{stem}.ap').write_bytes(b'')
    return out_dir, sorted(tocopy)


def install(stem, assets=None):
    out, tocopy = convert(paths.ext10('realms', stem), Path(tempfile.mkdtemp()) / stem)
    tex = out / 'textures'
    tex.mkdir(exist_ok=True)
    copy_textures(tocopy, tex)
    zp = Path(assets or paths.game('assets')) / 'realms' / f'{stem}.zip'
    zp.parent.mkdir(parents=True, exist_ok=True)
    zp.write_bytes(di3arc.pack_dir(out, di3arc.key_for(f'realms/{stem}.zip')))
    m = di3arc.unpack(zp)
    print('%s -> %s: %s' % (stem, zp, {k: len(v) for k, v in m.items()}))


def _selftest():
    out, _ = convert(paths.ext10('realms', 'rlm_intro_day'), Path(tempfile.mkdtemp()) / 'rlm_intro_day')
    mine, ship = Oct.load(out / 'rlm_intro_day.oct'), Oct.load(paths.ext30(*TEMPLATE))
    skip = {'Uuid', 'MaterialName', 'MaterialReference', 'vformatCRC', 'RenderCaps', 'Idata', 'Vdata',
            'ExporterDate', 'ExporterTime', 'NodeName'}
    def val(v):   # 3.0 re-exported some matrices: float noise ~1e-5
        return [round(x, 3) if isinstance(x, float) else x for x in v] if isinstance(v, list) else             (round(v, 3) if isinstance(v, float) else v)
    def sig(f, pools):
        return [(r.pool, r.level, r.name, repr(val(r.value()))) for r in f.recs
                if r.pool in pools and r.name not in skip]
    pools = ('LightRigPool', 'SceneTreeNodePool', 'AssetPool', 'AssociationPool', 'CellPool', 'DisplayLayerPool')
    a, b = sig(mine, pools), sig(ship, pools)
    geo = lambda x: x[2] in ('Visible', 'DynamicVisPlacement', 'MeshName') or x[2].startswith('Bounding') \
        or x[2] in ('NumPrimitives', 'Primitives', 'Primitive', 'BillboardType', 'OcclusionType',
                    'OcclusionCheckRadius', 'OcclusionFadeKp')
    diff = [(x, y) for x, y in zip(a, b) if x != y and not geo(x)]
    print('records %d vs %d shipped, %d differ (geometry fields ignored)' % (len(a), len(b), len(diff)))
    for x, y in diff[:15]:
        print('  mine', x[:4]); print('  ship', y[:4])
    assert len(a) == len(b) and not diff


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description='Convert 1.0 realms to 3.0 realm archives')
    ap.add_argument('realms', nargs='*', help='1.0 realm names, e.g. rlm_in_day')
    ap.add_argument('--assets', help="install into this assets folder (default: the game's; a mod folder works)")
    ap.add_argument('--selftest', action='store_true')
    paths.add_args(ap)
    a = paths.use(ap.parse_args())
    if a.selftest:
        _selftest()
    for stem in a.realms:
        install(stem, a.assets)

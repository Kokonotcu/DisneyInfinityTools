"""NATIVE conversion of a hierarchical 1.0 world (Metroville / in_city) to 3.0 -- no flattening, no offset.

The scene tree is kept as authored: every Transform, the LOD structure, visibility cells, bounds and
matrices stay in native 1.0 coordinates, so nothing world-space has to be re-derived (the static mode
moved the world by an offset and every world-space field it missed -- spheres, cells -- hid geometry).

Only what 3.0 cannot read changes (measured against the shipped 3.0 corpus, foreign.py):
  geometry/materials  convert_geometry (vertex formats, Idata byte offsets, RenderCaps) + autobundle
  LodGroup (131)      3.0 never ships the node type -> Transform in place, geometry below any child but
                      the first hidden (child 0 = most detailed)
  Light (135)         1.0 light fields -> plain Transform in place
  pools               pools 3.0 never ships dropped (measured: Realm*, SurfaceTypes; ExpendablePool kept);
                      Curves/NavMesh/BoundingVolume/MeridianLocator dropped (gameplay, needs the 1.0 graph =
                      plan B); LayoutPool/AssetPool/SubNetworkPool from the 3.0 donor; all in shipped order
  graph               the 1.0 graph has NO processor nodes (3,480 Dilations), 3.0 resolves positions
                      through processor chains -> a generated graph (graph.expand) carries only what must
                      live there: the expendable sockets and the player-start locator, native positions
  associations        CellLink/VisHierarchyLink/Expendable kept; collision as the in-game-proven static
                      scheme (instance Transform places the body); the rest are graph bindings (plan B)
  spawn               .mer = emp_0_spaceempty's start chain (bound to Layout|LOC_PlayerStart at --start, or a
                      1.0 graph locator) + a connection-free RealmCache for lighting

python native30.py --source <1.0 world dir> --world <3.0 folder> --zone <zone record name>
                   [--start x,y,z | --start-locator NAME] [--realm rlm_in_day] [--assets DIR] [--translate] [--dry]
  Metroville as installed in-game: --source <ext10>/assets/worlds/in_city --world out_intro2 --zone OUT_Intro2
                                   --start -87.85,155.35,619.19
"""
import argparse, shutil, struct
from collections import Counter
from pathlib import Path
from oct import Oct, children
import graph, paths
from conv import (pool_rec, _roundtrip, _reindex, world_by_path, mat2quat, prepare, foreign_pools, sort_pools,
                  rename_files, check_foreign, copy_textures, TRANSFORM_FIELDS, LAYOUT)
from subnet import subnets

# Donors from the 3.0 extract. Start graph: a 3.0 level spawns the player through Catalyst -> TardisManifestAdv
# -> PlayerStartPosNode <- LocatorNode(Layout|LOC_PlayerStart). emp_endor_details (LocatorNode ->
# PlayerStartPosNode only, no manifest) never built a start node. emp_0_spaceempty is the smallest shipped graph
# with the whole chain linked (10 nodes); its LinksBuffer is compressed, so it is used verbatim.
# ponytail: its transport rule spawns the avatar in emp_xwing_ps and its realm is RLM_EMP_Space; changing
# them needs a ParameterStrings rebuild
DONOR = 'out_intro2'                         # Layout/AssetPool/SubNetworkPool, collision links, .mtc/.ap
START_MER = ('emp_0_spaceempty', 'emp_0_spaceempty.mer')
INTRO = 'psx_intro'                          # the start locator's MeridianLocatorPool + association
# besides the pools 3.0 never ships (conv.foreign_pools): CurvesPool ships in 131 3.0 worlds, but 1.0's is not
# converted; NavMesh/BoundingVolume/MeridianLocator are gameplay (plan B; the 1.0 BoundingVolume world-bounds blob
# caused endless respawns); Layout/Asset/SubNetwork are replaced by the donor's
DROP_POOLS = ('CurvesPool', 'NavMeshPool', 'BoundingVolumePool', 'MeridianLocatorPool', 'LayoutPool', 'AssetPool',
              'SubNetworkPool')
KEEP_POOLS = {'ExpendablePool': 'LayoutPool'}    # 1.0 pool 3.0 still registers (FUN_01604820) -> placed before
KEEP_ASSOC = ('CellLink', 'VisHierarchyLink', 'Expendable', 'CollisionShapeToCollisionInstance')
START = 'LOC_PlayerStart'   # the name emp_0_spaceempty's LocatorNode is bound to
ADD_ASSOC = ('CollisionAggregate', 'Transform', 'Anchor', 'PrimaryAnchor', 'PlacementNode')


def graph_locators(s):
    """Every header name of the 1.0 world's graph."""
    rec, f, body = subnets(s)[0]
    return f['HeaderStrings'].names()


def graph_locator(s, name):
    """World position of a 1.0 graph locator: its '<name>|Transform' Dilation (+ its basis' Dilation)."""
    rec, f, body = subnets(s)[0]
    hs, hi, hl = f['HeaderStrings'].names(), f['HeaderStringIndices'].ints(), f['HeaderLayout'].ints()
    ch, nxt = {}, 1
    for h, c in zip(hl[0::2], hl[1::2]):      # breadth-first: each pair hands out the next c headers
        ch[h] = list(range(nxt, nxt + c)); nxt += c
    pools = {'DataNodePool': [], 'BasisConversionPool': []}
    pool = cur = None
    for r in body:
        if r.level == 3:
            pool = r.name
        elif pool in pools and r.level == 4:
            cur = {}; pools[pool].append(cur)
        elif pool in pools and r.level >= 5 and cur is not None:
            cur[r.name] = r.value()
    dns, convs = pools['DataNodePool'], pools['BasisConversionPool']
    # header NUMBERS are 1-based (HeaderLayout pairs and DataNode.Header); HeaderStringIndices is 0-based.
    # Reading them 0-based picked a newspaper stand's Dilation instead of the start point.
    h = next(i for i, x in enumerate(hi) if hs[x] == name) + 1
    d = next(d for c in ch[h] for d in dns if d['Header'] == c and d.get('Type') == ['Dilation'])
    pos = list(d.get('Translation') or [0, 0, 0])
    b = d.get('BasisRef', 0)
    while b:                                  # parent bases (identity in in_city, kept general)
        cv = next(c for c in convs if c['FromBasisRef'] == b)
        t = dns[cv['DataNodeRef']].get('Translation') or [0, 0, 0]
        pos = [p + q for p, q in zip(pos, t)]
        b = cv['ToBasisRef']
    return pos


def native_scene(s):
    """LodGroup/Light -> Transform in place; geometry below LOD children other than the first hidden."""
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    K = [s.kids(n) for n in nodes]
    typ = [k['Type'].names()[0] if 'Type' in k else '' for k in K]
    par = [k['ParentNodeReferences'].ints()[0] if 'ParentNodeReferences' in k else None for k in K]
    first = {}
    for i, t in enumerate(typ):
        if t == 'LodGroup':
            c = [j for j in range(len(nodes)) if par[j] == i]
            first[i] = c[0] if c else None
    hidden = 0
    for i, t in enumerate(typ):
        if t != 'Geometry':
            continue
        a, p = i, par[i]
        while p is not None:
            if typ[p] == 'LodGroup' and first[p] != a:
                K[i]['Visible'].set_int(0); hidden += 1
                break
            a, p = p, par[p]
    conv = Counter()
    for n, k, t in zip(nodes, K, typ):
        if t in ('Light', 'LodGroup'):
            k['Type'].set_name('Transform')
            i, j = s.block(n)
            s.recs[i + 1:j] = [r for r in s.recs[i + 1:j] if r.level == n.level + 1 and r.name in TRANSFORM_FIELDS]
            conv[t] += 1
    print('  scene: %s -> Transform in place, %d lower-LOD geometry nodes hidden, hierarchy kept'
          % (dict(conv), hidden))


def build_mer(out, realm, zone):
    """emp_0_spaceempty's start chain verbatim; the namespace is renamed to the zone and its RealmCache
    points at our realm (appended to ParameterStrings, so no other string offset moves)."""
    m = Oct.load(paths.ext30('worlds', *START_MER))
    # start points register per ZONE under the graph namespace: every shipped graph names it after its
    # zone (PSX_Intro, KRT_ALDN_SpdWay...).
    for r in m.recs:                      # node pool header + the link pool's copy of it
        if r.pool in ('MeridianNodePool', 'MeridianLinkPool') and r.name in ('Name', 'GraphFileName', 'UserNamespace'):
            r.set_name(zone if r.name != 'UserNamespace' else zone.lower())
    nk = m.kids(m.find(name='MNode')[0])
    blob = lambda b: b[1:] if b[0] + 1 == len(b) else b[2:]
    pack = lambda d: bytes([len(d)]) + d if len(d) <= 255 else struct.pack('<H', len(d)) + d
    vals, strs = bytearray(blob(nk['ParameterValues'].value())), bytearray(blob(nk['ParameterStrings'].value()))
    rc = next(x for x in m.find(name='MNode') if m.kids(x)['Type'].names()[0] == 'RealmCache')
    off = m.kids(rc)['Mer_ParametersOffset'].ints()[0]
    assert strs[struct.unpack_from('<I', vals, off)[0]:].startswith(b'RLM_EMP_Space\0')   # its realm string slot
    struct.pack_into('<I', vals, off, len(strs))
    strs += realm.encode() + b'\0'
    nk['ParameterValues'].pay, nk['ParameterStrings'].pay = pack(bytes(vals)), pack(bytes(strs))
    m.save(out)
    chk = Oct.load(str(out))
    types = [r.names()[0] for r in chk.recs if r.pool == 'MeridianNodePool' and r.name == 'Type' and r.level == 3]
    assert {'TardisManifestAdv', 'PlayerStartPosNode', 'LocatorNode'} <= set(types), types
    bound = [r.names()[0] for r in chk.recs if r.name == 'BoundObjectName']
    assert bound == ['%s|%s' % (LAYOUT, START)], bound
    print('  .mer: %s, start bound to %s, realm %s, zone %s' % (types, bound[0], realm, zone))


def convert(source, world, zone, realm='rlm_in_day', start=None, start_locator=None, translate=False, dry=False,
            fade_scale=4.0, donor=DONOR, assets=None):
    src_dir = Path(source); stem = src_dir.name
    assets = Path(assets) if assets else paths.game('assets')
    ref_dir = paths.ext30('worlds', donor)
    s = Oct.load(src_dir / ('%s.oct' % stem))
    ref = Oct.load(ref_dir / ('%s.oct' % donor))
    intro = Oct.load(paths.ext30('worlds', INTRO, '%s.oct' % INTRO))
    # Metroville: IN_City_StartPoint is outside the playable city (user: "weird place"); its in-game start is
    # given with --start instead: the centre of the densest 60 m block of building sockets, where a downward ray
    # through the geometry first hits street level (open sky, not a roof): native (-87.85, 155.35, 619.19).
    if start is None:
        name = start_locator or next((h for h in graph_locators(s) if h.endswith('StartPoint')), None)
        assert name, 'no *StartPoint locator in the 1.0 graph: give --start x,y,z or --start-locator NAME'
        start = graph_locator(s, name)
        print('  start: graph locator %s' % name)
    wpath = world_by_path(s)
    print('  start (native): %s' % [round(v, 2) for v in start])

    out = assets / 'worlds' / world
    data, _, tocopy, _ = prepare(s, src_dir, out, world, foreign_pools(s, KEEP_POOLS) + list(DROP_POOLS), dry)
    native_scene(s)
    # 1.0 fades many props out at 10-50 m (user: "a lot of LODing"); fade_scale is the knob
    fades = [r for r in s.recs if r.name in ('DistanceAlphaFadeStart', 'DistanceAlphaFadeEnd')]
    for r in fades:
        r.set_floats(r.value() * fade_scale)
    print('  %d fade distances x%g' % (len(fades), fade_scale))

    # the Camera node becomes the graph root 'Layout' (in place: no node renumbering), parented to Scene
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    cam = next(x for x in nodes if s.kids(x)['Type'].names()[0] == 'Camera')
    layout_ref = nodes.index(cam)
    ra, rb = ref.block(ref.find(name='Node', pool='SceneTreeNodePool')[1])
    blk = _reindex(ref, s, ref.recs[ra:rb])
    bk = children(blk)
    bk['NodeName'].set_name(LAYOUT)
    a, b = s.block(cam)
    s.recs[a:b] = blk
    scene = next(i for i, x in enumerate(nodes) if s.kids(x)['Type'].names()[0] == 'Scene')
    s.kids(s.recs[a])['ParentNodeReferences'].set_ints([scene])
    for x in s.find(name='Node', pool='SceneTreeNodePool'):   # 1.0 'Layout' nodes would shadow the root
        k = s.kids(x)
        if x is not s.recs[a] and 'NodeName' in k and k['NodeName'].names()[0] == LAYOUT:
            k['NodeName'].set_name('Layout_src%d' % s.idx(x))

    # associations
    dropped, sock = Counter(), []
    for asc in reversed(s.find(name='Association', pool='AssociationPool')):
        t = s.kids(asc)['Type'].names()[0]
        if t not in KEEP_ASSOC:
            i, j = s.block(asc); del s.recs[i:j]; dropped[t] += 1
    print('  associations dropped (graph bindings, plan B): %s' % dict(dropped))
    for asc in s.find(name='Association', pool='AssociationPool'):
        k = s.kids(asc)
        t = k['Type'].names()[0]
        if t == 'Expendable':      # resolves NodeName + '|Transform' as a graph DataNode (FUN_01604820)
            sock.append(wpath[k['NodeName'].names()[0]])
            k['NodeName'].set_name('%s|SOCK_%d' % (LAYOUT, len(sock) - 1))
        elif t == 'CollisionShapeToCollisionInstance':
            k['NodeName'].set_name('%s|COL_%d' % (LAYOUT, k['CollisionInstanceRef'].ints()[0]))
    add, tmpl = [], {}
    for asc in ref.find(name='Association', pool='AssociationPool'):
        k = ref.kids(asc)
        t = k['Type'].names()[0]
        ai, aj = ref.block(asc)
        if t == 'CollisionInstanceToDataNode':
            tmpl[t] = (ai, aj)
        if t not in ADD_ASSOC:
            continue
        b2 = _reindex(ref, s, ref.recs[ai:aj])
        bk = children(b2)
        if 'SceneTreeNodeRef' in bk:
            bk['SceneTreeNodeRef'].set_int(layout_ref)
        if t == 'CollisionAggregate':
            bk['NodeName'].set_name('COL_0')
            bk['SceneTreeNodeRef'].set_int(-1)
        add.append(b2)
    n_inst = sum(1 for r in s.recs if r.name == 'Instance' and r.pool == 'CollisionInstancePool')
    for i in range(n_inst):   # static scheme, proven in-game: the instance Transform places the body
        ai, aj = tmpl['CollisionInstanceToDataNode']
        b2 = _reindex(ref, s, ref.recs[ai:aj])
        bk = children(b2)
        bk['NodeName'].set_name('COL_%d' % i)
        bk['CollisionInstanceRef'].set_int(i)
        add.append(b2)
    for asc in intro.find(name='Association', pool='AssociationPool'):   # the start locator
        k = intro.kids(asc)
        if k['Type'].names()[0] == 'MeridianLocator':
            ai, aj = intro.block(asc)
            b2 = _reindex(intro, s, intro.recs[ai:aj])
            # the shipped path (emp_0_spaceempty): '...|Transform|In WorldBasis.Transform', the Transform the
            # slot's processors output. Bound to the slot Dilation instead, the locator got the wrong node
            # type (debugger: zone + player checks pass, transform fails); bound here, the output stayed at
            # its identity default (origin). So the outputs are also pre-filled with the start (below).
            children(b2)['NodeName'].set_name('%s|%s|Transform|In WorldBasis.Transform' % (LAYOUT, START))
            add.append(b2)
    _, end = s.block(pool_rec(s, 'AssociationPool'))
    s.recs[end:end] = [r for b2 in add for r in b2]
    s = _roundtrip(s)
    for i, asc in enumerate(s.find(name='Association', pool='AssociationPool')):
        asc.set_name(str(i))

    # 3.0 pools: LayoutPool/AssetPool/SubNetworkPool from the donor, the locator pool from psx_intro
    for src, name in ((ref, 'LayoutPool'), (ref, 'AssetPool'), (ref, 'SubNetworkPool'), (intro, 'MeridianLocatorPool')):
        i, j = src.block(pool_rec(src, name))
        s.recs.extend(_reindex(src, s, src.recs[i:j]))
    s = _roundtrip(s)
    slots = ['SOCK_%d' % i for i in range(len(sock))] + [START]
    gp = graph.expand(s, slots, LAYOUT)
    s = _roundtrip(s)
    ag = s.find(name='AggregateCount')
    ag[0].set_int(len(slots))
    # SPAWN: in this zone the player spawns at the world ORIGIN whatever the start graph says (debugger:
    # no start node is ever constructed, even with the unmodified shipped emp_endor_details graph). So the
    # world is translated to put the start 1 below the origin -- applied ONLY to world-space
    # fields (field audit): root scene nodes (children inherit), BoundingOBBCenter, CellToWorldMatrix,
    # collision instance Transforms, graph slots. Local fields (BoundingSphereCenter, child matrices,
    # vertices, .btd) are untouched -- moving the sphere centres and missing the cells is what hid the
    # buildings in the static build.
    # --translate: the old workaround. Without it the world stays native (1.0 data expecting native
    # coordinates -- the graph-driven SceneNodeRefs set pieces -- stays consistent) and the start graph
    # moves the player instead.
    off = [-start[0], -start[1] - 1.0, -start[2]] if translate else [0.0, 0.0, 0.0]
    pos = [[m[12] + off[0], m[13] + off[1], m[14] + off[2]] for m in sock] + [[a + b for a, b in zip(start, off)]]
    rot = [mat2quat(m) for m in sock] + [None]
    dn, moved, orient = None, 0, []
    for r in s.recs:
        if r.pool != 'DataNodePool':
            continue
        if r.name == 'DataNode':
            dn = {}
        elif dn is not None and r.name == 'Type':
            dn['t'] = r.names()[0]
        elif dn is not None and r.name == 'BasisRef':
            dn['b'] = r.ints()[0]
        elif dn is not None and r.name == 'Translation' and dn.get('t') == 'Dilation' and dn.get('b') == 1 and moved < len(pos):
            r.set_floats(list(pos[moved]))
            if rot[moved]:
                orient.append((r, rot[moved]))
            moved += 1
    assert moved == len(pos), 'placed %d of %d graph slots' % (moved, len(pos))
    # the start slot's processor outputs ('In WorldBasis.Transform', 'In WorldBasis') hold the world
    # position too: the Layout basis is identity, so it equals the slot's own Translation
    dn, outs, seen, nslot = None, [], False, 0
    for r in s.recs:
        if r.pool != 'DataNodePool':
            continue
        if r.name == 'DataNode':
            dn = {}
        elif dn is not None and r.name == 'Type':
            dn['t'] = r.names()[0]
        elif dn is not None and r.name == 'BasisRef':
            dn['b'] = r.ints()[0]
        elif r.name == 'Translation' and dn is not None:
            if seen and len(outs) < 2:
                outs.append((dn.get('t'), r))
            if dn.get('t') == 'Dilation' and dn.get('b') == 1 and nslot < len(pos):
                nslot += 1
                seen = nslot == len(pos)          # the start is the last slot
    assert [t for t, _ in outs] == ['Transform', 'Dilation'], outs
    for _, r in outs:
        r.set_floats(list(pos[-1]))
    for r, q in reversed(orient):     # inserting after r leaves every earlier position (and s.idx's map) valid
        s.recs.insert(s.idx(r) + 1, type(r)(s, r.level, r.type, s.add_name('Orientation'), r.pay).set_floats(q))
    s = _roundtrip(s)
    print('  graph: %d sockets + start, headers %d, datanodes %d'
          % (len(sock), gp['VariableHeaderCount'], gp['VariableDataNodeCount']))
    moved = Counter()
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    scene_i = next(i for i, x in enumerate(nodes) if s.kids(x)['Type'].names()[0] == 'Scene')
    for i, x in enumerate(nodes):
        k = s.kids(x)
        t = k['Type'].names()[0]
        if t == 'Scene' or k.get('NodeName') and k['NodeName'].names()[0] == LAYOUT:
            continue
        if 'BoundingOBBCenter' in k:
            b = k['BoundingOBBCenter'].floats()
            k['BoundingOBBCenter'].set_floats([b[0] + off[0], b[1] + off[1], b[2] + off[2]]); moved['OBB'] += 1
        par = k['ParentNodeReferences'].ints()[0] if 'ParentNodeReferences' in k else None
        if (par is None or par == scene_i) and 'LocalToParentMatrix' in k:
            m = k['LocalToParentMatrix'].floats()
            m[12], m[13], m[14] = m[12] + off[0], m[13] + off[1], m[14] + off[2]
            k['LocalToParentMatrix'].set_floats(m); moved['root nodes'] += 1
    for r in s.recs:
        if (r.name == 'CellToWorldMatrix' and r.pool == 'CellPool') or (r.name == 'Transform' and r.pool == 'CollisionInstancePool'):
            m = r.floats()
            m[12], m[13], m[14] = m[12] + off[0], m[13] + off[1], m[14] + off[2]
            r.set_floats(m); moved[r.pool] += 1
    print('  offset %s (start -> origin): %s' % ([round(v, 2) for v in off], dict(moved)))

    rename_files(s, stem, world)
    # pools in the order every shipped world uses: the graph (SubNetworkPool), locators, layout and assets
    # BEFORE AssociationPool. Appended after it, the graph-bound links (expendable sockets, the start
    # locator) were resolved before the graph existed: 2 exploder archives opened instead of 85.
    s = sort_pools(s, KEEP_POOLS)
    print('  pool order: %s' % [r.name for r in s.recs if r.level == 1])
    check_foreign(s, allow=('ExpendablePool', 'ExpendableNodeRef'))
    import btdconv
    bd = btdconv.convert_file((src_dir / ('%s.btd' % stem)).read_bytes())
    print('  btd: %d blocks, native coordinates' % len(btdconv.blocks(bd, 3)))
    if dry:
        print('(dry run: nothing written)'); return
    s.save(out / ('%s.oct' % world))
    for ext in ('.mtc', '.ap'):
        shutil.copy2(ref_dir / ('%s%s' % (donor, ext)), out / ('%s%s' % (world, ext)))
    build_mer(out / ('%s.mer' % world), realm, zone)
    (out / ('%s.mtb' % world)).write_bytes(data)
    copy_textures(tocopy, assets / 'textures')
    (out / ('%s.btd' % world)).write_bytes(bd)
    (out / ('%s.btm' % world)).write_bytes(btdconv.btm(bd))
    print('wrote %s (native) into %s' % (world, out))


def _xyz(s):
    v = [float(x) for x in s.split(',')]
    if len(v) != 3:
        raise argparse.ArgumentTypeError('expected x,y,z')
    return v


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description='Native 1.0 -> 3.0 world conversion', epilog=__doc__.split('\n\n')[-1],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--source', required=True, help='1.0 world folder (holding <name>.oct)')
    ap.add_argument('--world', required=True, help='3.0 world folder to write (assets/worlds/<world>)')
    ap.add_argument('--zone', required=True, help='zonelist record the world loads under (case as in zonelist.lua)')
    ap.add_argument('--start', type=_xyz, help='player start x,y,z in native 1.0 coordinates')
    ap.add_argument('--start-locator', help='1.0 graph locator to start at (default: the one named *StartPoint)')
    ap.add_argument('--realm', default='rlm_in_day', help="realm the .mer binds (Metroville's own, ported by realm30.py)")
    ap.add_argument('--donor', default=DONOR, help='3.0 world the Layout/asset/collision templates come from')
    ap.add_argument('--assets', help='write into this assets folder (default: the game install\'s; a mod folder works)')
    ap.add_argument('--translate', action='store_true', help='move the start to the origin (old workaround)')
    ap.add_argument('--dry', action='store_true', help='convert and check, write nothing')
    ap.add_argument('--fade-scale', type=float, default=4.0, help='multiplies every 1.0 DistanceAlphaFade distance')
    paths.add_args(ap)
    a = paths.use(ap.parse_args())
    convert(a.source, a.world, a.zone, a.realm, a.start, a.start_locator, a.translate, a.dry, a.fade_scale,
            a.donor, a.assets)

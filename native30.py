"""NATIVE conversion of a hierarchical 1.0 world (Metroville / in_city) to 3.0 -- no flattening, no offset.

The scene tree is kept as authored: every Transform, the LOD structure, visibility cells, bounds and
matrices stay in native 1.0 coordinates, so nothing world-space has to be re-derived (the static mode
moved the world by an offset and every world-space field it missed -- spheres, cells -- hid geometry).

Only what 3.0 cannot read changes (measured against the shipped 3.0 corpus, foreign.py):
  geometry/materials  convert_geometry (vertex formats, Idata byte offsets, RenderCaps) + autobundle
  LodGroup (131)      3.0 never ships the node type -> Transform in place, geometry below any child but
                      the first hidden (child 0 = most detailed)
  Light (135)         1.0 light fields -> plain Transform in place
  pools               1.0-only Realm*/SurfaceTypes/Curves dropped; NavMesh/MeridianLocator dropped
                      (gameplay, needs the 1.0 graph = plan B); LayoutPool/AssetPool/SubNetworkPool from
                      the 3.0 donor
  graph               the 1.0 graph has NO processor nodes (3,480 Dilations), 3.0 resolves positions
                      through processor chains -> a generated graph (graph.expand) carries only what must
                      live there: the expendable sockets and the player-start locator, native positions
  associations        CellLink/VisHierarchyLink/Expendable kept; collision as the in-game-proven static
                      scheme (instance Transform places the body); the rest are graph bindings (plan B)
  spawn               .mer = holodestruction's LocatorNode->PlayerStartPosNode (bound to Layout|LOC_Start
                      at the world's own IN_City_StartPoint) + a connection-free RealmCache for lighting

python native30.py [--source <1.0 world dir>] [--world out_intro2] [--translate] [--dry]
"""
import argparse, shutil, struct, sys
from collections import Counter
from pathlib import Path
from oct import Oct
import graph, autobundle
from conv import (kids, pool_rec, geo_nodes, _roundtrip, _reindex, convert_geometry, world_by_path,
                  mat2quat, copy_texture, TRANSFORM_FIELDS, REF, INSTALL, LAYOUT)
from subnet import subnets

SRC = Path(r'D:\DI-Extract\1.0\assets\worlds\in_city')
# Start graph. A 3.0 level spawns the player through Catalyst -> TardisManifestAdv -> PlayerStartPosNode
# <- LocatorNode(Layout|LOC_PlayerStart). emp_endor_details (LocatorNode -> PlayerStartPosNode only, no
# manifest) never built a start node. emp_0_spaceempty is the smallest shipped graph with the whole chain
# linked (10 nodes); its LinksBuffer is compressed, so it is used verbatim.
# ponytail: its transport rule spawns the avatar in emp_xwing_ps and its realm is RLM_EMP_Space; changing
# them needs a ParameterStrings rebuild
START_MER = Path(r'D:\DI-Extract\3.0\assets\worlds\emp_0_spaceempty\emp_0_spaceempty.mer')
INTRO = Path(r'D:\DI-Extract\3.0\assets\worlds\psx_intro')
DROP_POOLS = ('RealmDataPool', 'RealmVolumePool', 'SurfaceTypesPool', 'CurvesPool', 'NavMeshPool', 'BoundingVolumePool',
              'MeridianLocatorPool', 'LayoutPool', 'AssetPool', 'SubNetworkPool')
KEEP_ASSOC = ('CellLink', 'VisHierarchyLink', 'Expendable', 'CollisionShapeToCollisionInstance')
START = 'LOC_PlayerStart'   # the name emp_0_spaceempty's LocatorNode is bound to
ADD_ASSOC = ('CollisionAggregate', 'Transform', 'Anchor', 'PrimaryAnchor', 'PlacementNode')


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
    K = [kids(s, n) for n in nodes]
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
            k['Type'].pay = s.add_name('Transform').to_bytes(2, 'little')
            i, j = s.block(n)
            s.recs[i + 1:j] = [r for r in s.recs[i + 1:j] if r.level == n.level + 1 and r.name in TRANSFORM_FIELDS]
            conv[t] += 1
    print('  scene: %s -> Transform in place, %d lower-LOD geometry nodes hidden, hierarchy kept'
          % (dict(conv), hidden))


def build_mer(out, zone="OUT_Intro2"):
    """emp_0_spaceempty's start chain verbatim; only the namespace is renamed to the zone."""
    m = Oct.load(str(START_MER))
    # start points register per ZONE under the graph namespace: every shipped graph names it after its
    # zone (PSX_Intro, KRT_ALDN_SpdWay...).
    for r in m.recs:                      # node pool header + the link pool's copy of it
        if r.pool in ('MeridianNodePool', 'MeridianLinkPool') and r.name in ('Name', 'GraphFileName', 'UserNamespace'):
            r.pay = m.add_name(zone if r.name != 'UserNamespace' else zone.lower()).to_bytes(2, 'little')
    m.save(out)
    chk = Oct.load(str(out))
    types = [r.names()[0] for r in chk.recs if r.pool == 'MeridianNodePool' and r.name == 'Type' and r.level == 3]
    assert {'TardisManifestAdv', 'PlayerStartPosNode', 'LocatorNode'} <= set(types), types
    bound = [r.names()[0] for r in chk.recs if r.name == 'BoundObjectName']
    assert bound == ['%s|%s' % (LAYOUT, START)], bound
    print('  .mer: %s, start bound to %s' % (types, bound[0]))


def convert(source=SRC, world='out_intro2', translate=False, dry=False):
    src_dir = Path(source); stem = src_dir.name
    s = Oct.load(src_dir / ('%s.oct' % stem))
    ref = Oct.load(REF / 'out_intro2.oct')
    intro = Oct.load(INTRO / 'psx_intro.oct')
    data, problems, vmap, tiers, tocopy = autobundle.resolve(src_dir, s)
    assert not problems, problems[:5]
    print('%s: materials %s' % (stem, dict(Counter(tiers))))
    gn = geo_nodes(s)
    # IN_City_StartPoint (graph_locator) is outside the playable city (user: "weird place"). Spawn in the
    # densest 60 m block of building sockets, at its centre, where a downward ray through the geometry
    # first hits street level (open sky, not a roof): native (-87.85, 155.35, 619.19).
    start = [-87.85, 155.35, 619.19]
    wpath = world_by_path(s)
    print('  start (native): %s' % [round(v, 2) for v in start])

    gone = []
    for name in DROP_POOLS:
        p = pool_rec(s, name)
        if p:
            i, j = s.block(p); del s.recs[i:j]; gone.append(name)
    print('  dropped pools %s' % gone)

    out = INSTALL / world
    out.mkdir(parents=True, exist_ok=True)
    convert_geometry(s, gn, src_dir, stem, vmap, out, world, dry)
    for mt in s.find(name='Material', pool='MaterialPool'):
        i, j = s.block(mt)
        s.recs[i + 1:j] = [r for r in s.recs[i + 1:j] if r.name != 'RenderCaps']
    native_scene(s)

    # the Camera node becomes the graph root 'Layout' (in place: no node renumbering), parented to Scene
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    cam = next(x for x in nodes if kids(s, x)['Type'].names()[0] == 'Camera')
    layout_ref = nodes.index(cam)
    ra, rb = ref.block(ref.find(name='Node', pool='SceneTreeNodePool')[1])
    blk = _reindex(ref, s, ref.recs[ra:rb])
    bk = {r.name: r for r in blk[1:]}
    bk['NodeName'].pay = s.add_name(LAYOUT).to_bytes(2, 'little')
    a, b = s.block(cam)
    s.recs[a:b] = blk
    scene = next(i for i, x in enumerate(nodes) if kids(s, x)['Type'].names()[0] == 'Scene')
    kids(s, s.recs[a])['ParentNodeReferences'].set_ints([scene])
    for x in s.find(name='Node', pool='SceneTreeNodePool'):   # 1.0 'Layout' nodes would shadow the root
        k = kids(s, x)
        if x is not s.recs[a] and 'NodeName' in k and k['NodeName'].names()[0] == LAYOUT:
            k['NodeName'].pay = s.add_name('Layout_src%d' % s.recs.index(x)).to_bytes(2, 'little')

    # associations
    dropped, sock = Counter(), []
    for asc in reversed(s.find(name='Association', pool='AssociationPool')):
        t = kids(s, asc)['Type'].names()[0]
        if t not in KEEP_ASSOC:
            i, j = s.block(asc); del s.recs[i:j]; dropped[t] += 1
    print('  associations dropped (graph bindings, plan B): %s' % dict(dropped))
    for asc in s.find(name='Association', pool='AssociationPool'):
        k = kids(s, asc)
        t = k['Type'].names()[0]
        if t == 'Expendable':      # resolves NodeName + '|Transform' as a graph DataNode (FUN_01604820)
            sock.append(wpath[k['NodeName'].names()[0]])
            k['NodeName'].pay = s.add_name('%s|SOCK_%d' % (LAYOUT, len(sock) - 1)).to_bytes(2, 'little')
        elif t == 'CollisionShapeToCollisionInstance':
            k['NodeName'].pay = s.add_name('%s|COL_%d' % (LAYOUT, k['CollisionInstanceRef'].ints()[0])).to_bytes(2, 'little')
    add, tmpl = [], {}
    for asc in ref.find(name='Association', pool='AssociationPool'):
        k = kids(ref, asc)
        t = k['Type'].names()[0]
        ai, aj = ref.block(asc)
        if t == 'CollisionInstanceToDataNode':
            tmpl[t] = (ai, aj)
        if t not in ADD_ASSOC:
            continue
        b2 = _reindex(ref, s, ref.recs[ai:aj])
        bk = {r.name: r for r in b2[1:]}
        if 'SceneTreeNodeRef' in bk:
            bk['SceneTreeNodeRef'].set_int(layout_ref)
        if t == 'CollisionAggregate':
            bk['NodeName'].pay = s.add_name('COL_0').to_bytes(2, 'little')
            bk['SceneTreeNodeRef'].set_int(-1)
        add.append(b2)
    n_inst = sum(1 for r in s.recs if r.name == 'Instance' and r.pool == 'CollisionInstancePool')
    for i in range(n_inst):   # static scheme, proven in-game: the instance Transform places the body
        ai, aj = tmpl['CollisionInstanceToDataNode']
        b2 = _reindex(ref, s, ref.recs[ai:aj])
        bk = {r.name: r for r in b2[1:]}
        bk['NodeName'].pay = s.add_name('COL_%d' % i).to_bytes(2, 'little')
        bk['CollisionInstanceRef'].set_int(i)
        add.append(b2)
    for asc in intro.find(name='Association', pool='AssociationPool'):   # the start locator
        k = kids(intro, asc)
        if k['Type'].names()[0] == 'MeridianLocator':
            ai, aj = intro.block(asc)
            b2 = _reindex(intro, s, intro.recs[ai:aj])
            # the shipped path (emp_0_spaceempty): '...|Transform|In WorldBasis.Transform', the Transform the
            # slot's processors output. Bound to the slot Dilation instead, the locator got the wrong node
            # type (debugger: zone + player checks pass, transform fails); bound here, the output stayed at
            # its identity default (origin). So the outputs are also pre-filled with the start (below).
            {r.name: r for r in b2[1:]}['NodeName'].pay = s.add_name(
                '%s|%s|Transform|In WorldBasis.Transform' % (LAYOUT, START)).to_bytes(2, 'little')
            add.append(b2)
    _, end = s.block(pool_rec(s, 'AssociationPool'))
    s.recs[end:end] = [r for b2 in add for r in b2]
    s = _roundtrip(s)
    for i, asc in enumerate(s.find(name='Association', pool='AssociationPool')):
        asc.pay = s.add_name(str(i)).to_bytes(2, 'little')

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
    # world is translated to put IN_City_StartPoint 1 below the origin -- applied ONLY to world-space
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
    for r, q in reversed(orient):
        s.recs.insert(s.recs.index(r) + 1, type(r)(s, r.level, r.type, s.add_name('Orientation'), r.pay).set_floats(q))
    s = _roundtrip(s)
    print('  graph: %d sockets + start, headers %d, datanodes %d'
          % (len(sock), gp['VariableHeaderCount'], gp['VariableDataNodeCount']))
    moved = Counter()
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    scene_i = next(i for i, x in enumerate(nodes) if kids(s, x)['Type'].names()[0] == 'Scene')
    for i, x in enumerate(nodes):
        k = kids(s, x)
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

    for r in s.recs:            # internal file names follow the install name
        if r.type in (0x05, 0x0B):
            try:
                v = s.name(int.from_bytes(r.pay[:2], 'little'))
            except Exception:
                continue
            if stem in v:
                r.pay = s.add_name(v.replace(stem, world)).to_bytes(2, 'little')

    # pools in the order every shipped world uses: the graph (SubNetworkPool), locators, layout and assets
    # BEFORE AssociationPool. Appended after it, the graph-bound links (expendable sockets, the start
    # locator) were resolved before the graph existed: 2 exploder archives opened instead of 85.
    order = ['NavMeshPool', 'BoundingVolumePool', 'ExternalFilePool', 'SurfaceTypesPool', 'CurvesPool',
             'SubNetworkPool', 'CollisionShapePool', 'CollisionInstancePool', 'SoundSourcePool', 'LightRigPool',
             'ShadowCasterPool', 'DisplayLayerPool', 'CellPool', 'CellAssignmentPool', 'MaterialBundlePool',
             'MaterialPool', 'VertexBufferPool', 'IndexBufferPool', 'SceneTreeNodePool', 'SceneNodeRefsPool',
             'MeridianLocatorPool', 'ExpendablePool', 'LayoutPool', 'AssetPool', 'AssociationPool',
             'ExporterDate', 'ExporterTime']
    tops = [i for i, r in enumerate(s.recs) if r.level == 1]
    head = s.recs[:tops[0]]
    blocks = [s.recs[a:b] for a, b in zip(tops, tops[1:] + [len(s.recs)])]
    assert all(b[0].name in order for b in blocks), [b[0].name for b in blocks if b[0].name not in order]
    blocks.sort(key=lambda b: order.index(b[0].name))
    s.recs = head + [r for b in blocks for r in b]
    s = _roundtrip(s)
    print('  pool order: %s' % [r.name for r in s.recs if r.level == 1])

    import tempfile, foreign
    tmp = Path(tempfile.mkdtemp()) / 'check.oct'
    s.save(tmp)
    left = [k for k in foreign.foreign_in(tmp) if k[0] != 'ExpendablePool' and k[1] != 'ExpendableNodeRef']
    assert not left, '%d record kinds 3.0 never ships remain, e.g. %s' % (len(left), left[:8])
    print('  0 record kinds foreign to 3.0')
    if dry:
        print('(dry run)'); return
    s.save(out / ('%s.oct' % world))
    for ext in ('.mtc', '.ap'):
        shutil.copy2(REF / ('out_intro2%s' % ext), out / ('%s%s' % (world, ext)))
    build_mer(out / ('%s.mer' % world))
    (out / ('%s.mtb' % world)).write_bytes(data)
    for f in tocopy:
        copy_texture(f, INSTALL.parent / 'textures' / f.name)
    import btdconv
    bd = btdconv.convert_file((src_dir / ('%s.btd' % stem)).read_bytes())
    (out / ('%s.btd' % world)).write_bytes(bd)
    (out / ('%s.btm' % world)).write_bytes(btdconv.btm(bd))
    print('  btd: %d blocks, native coordinates' % len(btdconv.blocks(bd, 3)))
    print('wrote %s (native) into %s' % (world, out))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', default=str(SRC))
    ap.add_argument('--world', default='out_intro2')
    ap.add_argument('--translate', action='store_true', help='move the start to the origin (old workaround)')
    ap.add_argument('--dry', action='store_true')
    a = ap.parse_args()
    convert(a.source, a.world, a.translate, a.dry)

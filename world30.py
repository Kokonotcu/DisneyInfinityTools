"""Convert ANY 1.0 world to 3.0. Generalises sumo30.py to N geometry nodes.

sumo30.py assumed one geometry node. Real worlds have many, each carrying
several primitives, so the buffer layout and the index numbering both have to
respect two rules measured on shipped content:

  each geometry NODE owns a disjoint vertex region        6602/6602 nodes
  within a node, indices are GLOBAL and baseVertex is
  RELATIVE to that node's first primitive, restarting
  at 0 for every node                                     psx_l3, tcw_geonosis

    padBase1  baseVertex [0, 941, 995, 1048]
              s0 offsets [68976, 84032, 84896, 85744]   (84032-68976)/16 = 941

Layout produced here, per node in order:
    [stream0 of every primitive of the node][stream1 of every primitive] ...
    then the next node's region

  python world30.py --source <1.0 world dir> --world <install world name>
                    [--offset 0,-20,0] [--bundle x.mtb | auto] [--dry]
"""
import argparse, importlib, shutil, struct, subprocess, sys
from pathlib import Path
from oct import Oct
import graph
import vtxf

REF = Path(r'D:\DI-Extract\3.0\assets\worlds\out_intro2')
INSTALL = Path(r'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition'
               r'\assets\worlds')
DROP_POOLS = ('BoundingVolumePool', 'MeridianLocatorPool', 'NavMeshPool',
              'SceneNodeRefsPool', 'SurfaceTypesPool')
DROP_ASSOC = ('MeridianLocator', 'PrimaryAsset', 'Asset')
LAYOUT = 'Layout'
ADD_ASSOC = ('CollisionAggregate', 'Transform', 'Anchor', 'PrimaryAnchor',
             'PlacementNode')


def kids(f, rec):
    i, j = f.block(rec)
    return {r.name: r for r in f.recs[i + 1:j]}


def pool_rec(f, name):
    r = [x for x in f.recs if x.level == 1 and x.name == name]
    return r[0] if r else None


def geo_nodes(f):
    """[(nodeRecord, kids, [primitiveRecords])] in scene tree order."""
    out = []
    for n in f.find(name='Node', pool='SceneTreeNodePool'):
        k = kids(f, n)
        if not (k.get('Type') and k['Type'].names()[0] == 'Geometry'):
            continue
        i, j = f.block(n)
        prims = [r for r in f.recs[i:j] if r.name == 'Primitive']
        out.append((n, k, prims))
    return out


def _roundtrip(f):
    import tempfile, os
    p = Path(tempfile.mkdtemp()) / 'rt.oct'
    f.save(p)
    g = Oct.load(p)
    os.remove(p)
    return g


def _reindex(src, dst, recs):
    out = []
    for r in recs:
        c = type(r)(dst, r.level, r.type, dst.add_name(src.name(r.name_idx)), r.pay)
        if r.type in (0x05, 0x0B):
            c.pay = dst.add_name(src.name(int.from_bytes(r.pay[:2], 'little'))
                                 ).to_bytes(2, 'little')
        out.append(c)
    return out


COLLISION_ASSOC = ('CollisionInstanceToSceneTreeNode', 'CollisionInstanceToDataNode',
                   'CollisionShapeToCollisionInstance')
KEEP_STATIC = COLLISION_ASSOC + ('CellLink', 'VisHierarchyLink')
TRANSFORM_FIELDS = ('NodeName', 'Uuid', 'DisplayLayer', 'Type', 'ParentNodeReferences',
                    'LocalToParentMatrix')   # a shipped 3.0 Transform node, exactly
IDENT = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def _mul(a, b):
    """Row-major 4x4, row vectors (translation at [12:15]): world = local x parent."""
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]


def flatten_static(s, gn):
    """Static scenery for worlds with a real hierarchy (in_city: 945 Transforms, 131 LodGroups,
    geometry nested under Transform/LodGroup/Geometry). Every geometry node gets its WORLD matrix,
    so it can sit directly under Layout; geometry below any LodGroup child but the first (child 0 is
    the most detailed, Distances [70, -1]) is made invisible; repeated geometry names get a suffix.
    -> {1.0 path of a geometry node: its new name} for re-pointing associations."""
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    K = [kids(s, n) for n in nodes]
    typ = [k['Type'].names()[0] if 'Type' in k else '' for k in K]
    par = [k['ParentNodeReferences'].ints()[0] if 'ParentNodeReferences' in k else None for k in K]
    loc = [k['LocalToParentMatrix'].floats() if 'LocalToParentMatrix' in k else IDENT for k in K]
    name = [k['NodeName'].names()[0] if 'NodeName' in k else '' for k in K]
    W = {}

    def world(i):
        if i not in W:
            W[i] = loc[i] if par[i] is None else _mul(loc[i], world(par[i]))
        return W[i]

    def chain(i):
        out = []
        while i is not None:
            out.append(i); i = par[i]
            assert len(out) <= len(nodes), 'scene tree parent cycle through node %d' % out[0]
        return out

    lod_first = {}
    for i, t in enumerate(typ):
        if t == 'LodGroup':
            ch = [c for c in range(len(nodes)) if par[c] == i]
            lod_first[i] = ch[0] if ch else None
    paths, seen, hidden, moved = {}, {}, 0, 0
    for n, k, _ in gn:
        i = nodes.index(n)
        anc = chain(i)
        path = '|'.join(name[a] for a in reversed(anc) if typ[a] != 'Scene')
        if any(typ[p] == 'LodGroup' and lod_first[p] != c for c, p in zip(anc, anc[1:])):
            k['Visible'].set_int(0); hidden += 1
        w = world(i)
        if w != loc[i]:
            k['LocalToParentMatrix'].set_floats(w); moved += 1
        nm = name[i]
        seen[nm] = seen.get(nm, 0) + 1
        if seen[nm] > 1:
            nm = '%s_%d' % (nm, seen[nm])
            k['NodeName'].pay = s.add_name(nm).to_bytes(2, 'little')
        paths[path] = nm
    for i, k in enumerate(K):      # the world's own 'Layout' transforms would shadow ours
        if typ[i] != 'Geometry' and name[i] == LAYOUT:
            k['NodeName'].pay = s.add_name('%s_src%d' % (LAYOUT, i)).to_bytes(2, 'little')
    print('  static: %d geometry nodes given world matrices, %d lower LODs hidden, %d renamed'
          % (moved, hidden, sum(v - 1 for v in seen.values())))
    return paths


def build_buffers(src_dir, stem, nodes_prims, out_v, out_i, out_i2):
    """nodes_prims = [[(Idata, Vdata) per primitive] per node] from the SOURCE.

    1.0 already gives each node its own vertex region and its index values are
    RELATIVE to that primitive's own Vdata[3]:

        Plant_E_1  Vdata [2, 464, 0,     0, 16, 0,  7424, 16]
        Plant_A_1  Vdata [2, 235, 0, 14848, 16, 0, 18608, 16]

    so a vertex is at  Vdata[3 + 3*s] + value * stride, never at value * stride
    from the start of the buffer. Reading from the buffer start works only when
    a single node owns the whole mesh (sumo) and silently gives every other
    node the FIRST node's vertices.

    Each primitive may also be converted to another vertex format (vtxf.plan): 3.0 nodes mix formats
    freely, each primitive carrying its own stream offsets/strides in Vdata, with Idata[2] = number
    of node vertices before it and node-global index values (2,420 shipped mixed nodes).

    nodes_prims entries: (Idata, Vdata, src vformatCRC, dst vformatCRC). A node with more than 65535
    vertices gets 32-bit indices in IndexBuffer 2 (out_i2), as the 3 such shipped 3.0 nodes do.
    """
    import os
    if len(nodes_prims) < 64:                       # small world: no pool overhead
        _init_worker(src_dir, stem)
        work = map(_node_work, nodes_prims)
        pool = None
    else:                                           # one process per core; nodes are independent
        from concurrent.futures import ProcessPoolExecutor
        pool = ProcessPoolExecutor(os.cpu_count(), initializer=_init_worker, initargs=(src_dir, stem))
        work = pool.map(_node_work, nodes_prims, chunksize=8)

    new_v, new_i, new_i2, out = bytearray(), bytearray(), bytearray(), []
    for prims, (streams, strides, subs, counts) in zip(nodes_prims, work):
        node_off = [[None] * len(st) for st in strides]
        for s in range(max(len(st) for st in strides)):   # stream-major within the node
            for i, st in enumerate(strides):
                if s < len(st):
                    node_off[i][s] = len(new_v)
                    new_v += streams[i][s]
        wide = sum(counts) > 0xffff
        blocks, vbase = [], 0
        for i, sub in enumerate(subs):
            buf = new_i2 if wide else new_i
            ioff = len(buf)
            buf += struct.pack('<%d%s' % (len(sub), 'I' if wide else 'H'), *[x + vbase for x in sub])
            vdata = [len(strides[i]), counts[i]]
            for s in range(len(strides[i])):
                vdata += [0, node_off[i][s], strides[i][s]]
            blocks.append({'Vdata': vdata, 'Idata': [2 if wide else 0, ioff, vbase, len(sub)],
                           'vformatCRC': prims[i][3]})
            vbase += counts[i]
        out.append(blocks)
    if pool:
        pool.shutdown()
    Path(out_v).write_bytes(bytes(new_v))
    Path(out_i).write_bytes(bytes(new_i))
    if new_i2:
        Path(out_i2).write_bytes(bytes(new_i2))
    return out, len(new_v), len(new_i), len(new_i2)


_VB = _IDX = None
_PLANS = {}


def _init_worker(src_dir, stem):
    global _VB, _IDX
    _VB = (Path(src_dir) / ('%s_0.vbuf' % stem)).read_bytes()
    _IDX = memoryview((Path(src_dir) / ('%s_0.ibuf' % stem)).read_bytes()).cast('H')


def _node_work(prims):
    """One node: -> ([per prim: [bytes per dst stream]], [dst strides], [local index lists], [vertex counts])"""
    import vtxf
    vb = _VB
    streams, strides, subs, counts = [], [], [], []
    for ida, vda, src, dst in prims:
        sub = _IDX[ida[1]:ida[1] + ida[3]]
        order, remap = [], {}
        for v in sub:
            if v not in remap:
                remap[v] = len(order)
                order.append(v)
        if (src, dst) not in _PLANS:
            _PLANS[src, dst] = vtxf.plan(src, dst)
        ss, ds, ops, _, _ = _PLANS[src, dst]
        have = [vda[4 + 3 * s] for s in range(vda[0])]
        assert have == ss, 'vformat %08x declares strides %s, primitive has %s' % (src, ss, have)
        identical = dst in vtxf.candidates_1to3(src)
        per = [bytearray() for _ in ds]
        for v in order:
            src_streams = []
            for s, st in enumerate(ss):
                lo = vda[3 + s * 3] + v * st
                assert lo + st <= len(vb), 'vertex %d at %d runs past the vbuf' % (v, lo)
                src_streams.append(vb[lo:lo + st])
            got = src_streams if identical else vtxf.convert_vertex(src_streams, ops, ds)
            for s in range(len(ds)):
                per[s] += got[s]
        streams.append([bytes(x) for x in per]); strides.append(ds)
        subs.append([remap[v] for v in sub]); counts.append(len(order))
    return streams, strides, subs, counts


def convert(source, world, offset=(0.0, 0.0, 0.0), bundle=None, dry=False, static=False):
    src_dir = Path(source)
    stem = src_dir.name
    vmap, tocopy = {}, []
    s = Oct.load(src_dir / ('%s.oct' % stem))
    if bundle == 'auto':   # the world's own materials: real, same-shader or generic (autobundle.py)
        import autobundle
        data, problems, vmap, tiers, tocopy = autobundle.resolve(src_dir, s)
        if problems:
            raise SystemExit('--bundle auto: %d unresolved, nothing written\n  ' % len(problems)
                             + '\n  '.join(problems[:20]))
        print('  materials: %s, %d textures to copy into the pool'
              % (dict(__import__('collections').Counter(tiers)), len(tocopy)))
        bundle = data
    ref = Oct.load(REF / 'out_intro2.oct')

    gn = geo_nodes(s)
    assert gn, 'no geometry nodes in %s' % stem
    print('%s: %d geometry nodes, %d primitives, %d materials'
          % (stem, len(gn), sum(len(p) for _, _, p in gn),
             len(s.find(name='Material', pool='MaterialPool'))))

    for name in DROP_POOLS:
        p = pool_rec(s, name)
        if p:
            i, j = s.block(p)
            del s.recs[i:j]
    if static:   # only the donor's pool set; LayoutPool/AssetPool are appended from the donor later
        keep = {r.name for r in ref.recs if r.level == 1} - {'LayoutPool', 'AssetPool'}
        gone = []
        for p in [r for r in s.recs if r.level == 1 and r.name not in keep]:
            i, j = s.block(p)
            del s.recs[i:j]
            gone.append(p.name)
        print('  static: dropped 1.0-only pools %s' % gone)

    # --- buffers, before the tree is touched -----------------------------
    # each primitive's target format: the one its material accepts (autobundle), else the best
    # 3.0 target for its 1.0 format (identical twin first)
    def target(p):
        k = kids(s, p)
        src = k['vformatCRC'].ints()[0] & 0xffffffff
        t = vmap.get((k['MaterialReference'].ints()[0], src)) or vtxf.targets_1to3(src)[0]
        return src, t[0]
    nodes_prims = [[(kids(s, p)['Idata'].ints(), kids(s, p)['Vdata'].ints(), *target(p))
                    for p in prims] for _, _, prims in gn]
    out = INSTALL / world
    out.mkdir(parents=True, exist_ok=True)
    tv, ti, ti2 = (out / ('%s_%d.%s' % (world, n, e)) for n, e in ((0, 'vbuf'), (0, 'ibuf'), (2, 'ibuf')))
    blocks, nv, ni, ni2 = build_buffers(src_dir, stem, nodes_prims,
                                        *(x if not dry else x.with_name(x.name + '.dry') for x in (tv, ti, ti2)))
    conv = __import__('collections').Counter()
    for (_, _, prims), blks in zip(gn, blocks):
        for p, b in zip(prims, blks):
            k = kids(s, p)
            old = k['vformatCRC'].ints()[0] & 0xffffffff
            conv['%08x -> %08x' % (old, b['vformatCRC'])] += 1
            k['Idata'].set_ints(b['Idata'])
            k['Vdata'].set_ints(b['Vdata'])
            k['vformatCRC'].pay = b['vformatCRC'].to_bytes(len(k['vformatCRC'].pay), 'little')
    print('  vformatCRC per primitive: %s' % dict(conv))
    for r in s.find(name='Size', pool='VertexBufferPool'):
        if r.ints()[0]:
            r.set_int(nv)
            break
    ibs = s.find(name='IndexBuffer', pool='IndexBufferPool')
    for n_ib, size in ((0, ni), (2, ni2)):
        k = kids(s, ibs[n_ib])
        assert k['Width'].ints()[0] == (2 if n_ib == 0 else 4)
        k['Size'].set_int(size)
        if size and 'FileName' not in k:   # IndexBuffer 2 = 32-bit, its own file (dor_tp_mc_2.ibuf)
            fn = kids(s, ibs[0])['FileName']
            i, j = s.block(ibs[n_ib])
            s.recs.insert(j, type(fn)(s, fn.level, fn.type, fn.name_idx,
                                      s.add_name('%s_2.ibuf' % stem).to_bytes(2, 'little')))
    print('  buffers: %d nodes -> vbuf %d B, ibuf %d B, 32-bit ibuf %d B' % (len(gn), nv, ni, ni2))

    # --- 1.0-only records on materials -----------------------------------
    n = 0
    for m in s.find(name='Material', pool='MaterialPool'):
        i, j = s.block(m)
        for r in [x for x in s.recs[i + 1:j] if x.name == 'RenderCaps']:
            s.recs.remove(r)
            n += 1
    print('  stripped %d MaterialPool.RenderCaps' % n)

    # --- scene tree: a Camera becomes the Layout transform ----------------
    # 3.0 keeps Camera nodes (18 across 120 worlds), so converting one in place
    # avoids renumbering every node reference in the file.
    paths = flatten_static(s, gn) if static else None   # on the untouched 1.0 tree
    if static:   # 1.0 Light/LodGroup nodes carry fields 3.0 never ships: make them plain Transforms
        n = 0
        for x in s.find(name='Node', pool='SceneTreeNodePool'):
            k = kids(s, x)
            if k.get('Type') and k['Type'].names()[0] in ('Light', 'LodGroup'):
                k['Type'].pay = s.add_name('Transform').to_bytes(2, 'little')
                i, j = s.block(x)
                s.recs[i + 1:j] = [r for r in s.recs[i + 1:j]
                                   if r.level == x.level + 1 and r.name in TRANSFORM_FIELDS]
                n += 1
        print('  static: %d Light/LodGroup nodes -> Transform' % n)
        # DisplayLayerPool is 1.0-only and dropped; 724 in_city nodes pointed at layer 0 of it.
        # The donor (and the dropped pool's absence) means "no layer" = -1.
        dl = [r for r in s.recs if r.name == 'DisplayLayer' and r.pool == 'SceneTreeNodePool' and r.ints()[0] != -1]
        for r in dl:
            r.set_int(-1)
        print('  static: %d DisplayLayer refs -> -1' % len(dl))
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    cam = [x for x in nodes if kids(s, x).get('Type')
           and kids(s, x)['Type'].names()[0] == 'Camera']
    assert cam, 'no Camera node to convert into the Layout transform'
    ra, rb = ref.block(ref.find(name='Node', pool='SceneTreeNodePool')[1])
    blk = _reindex(ref, s, ref.recs[ra:rb])
    {r.name: r for r in blk[1:]}['NodeName'].pay = s.add_name(LAYOUT).to_bytes(2, 'little')
    layout_ref = nodes.index(cam[0])
    a, b = s.block(cam[0])
    s.recs[a:b] = blk
    if static:   # the copied reference Layout keeps the reference's parent index; root it at Scene
        scene = next(i for i, x in enumerate(nodes) if kids(s, x)['Type'].names()[0] == 'Scene')
        kids(s, s.recs[a])['ParentNodeReferences'].set_ints([scene])
    tmpl = next(k['ParentNodeReferences'] for _, k, _ in gn if 'ParentNodeReferences' in k)
    for n, k, _ in gn:
        if 'ParentNodeReferences' not in k:      # root-level in 1.0: give it a parent record
            i, j = s.block(n)
            at = next(x for x in range(i, j) if s.recs[x].name == 'LocalToParentMatrix')
            s.recs.insert(at, type(tmpl)(s, tmpl.level, tmpl.type, tmpl.name_idx, tmpl.pay))
            k['ParentNodeReferences'] = s.recs[at]
        k['ParentNodeReferences'].set_ints([layout_ref])
    names = [k['NodeName'].names()[0] for _, k, _ in gn]
    print('  scene tree: node %d Camera -> Transform %r, %d geometry nodes reparented'
          % (layout_ref, LAYOUT, len(gn)))

    # --- associations ------------------------------------------------------
    dropped = __import__('collections').Counter()
    for a in reversed(s.find(name='Association', pool='AssociationPool')):
        k = kids(s, a)
        t = k['Type'].names()[0]
        if static:   # keep world links and collision bound to a geometry node, by its 1.0 path
            # Source collision is NOT kept: a CollisionInstance whose shape link is dropped becomes a
            # physics body with a null shape (crash at 0x7bcc0c, shape->getAabb), and multi-mesh .btd
            # files are not converted yet. Static mode uses the donor's floor collision instead.
            keep = t in KEEP_STATIC and t not in COLLISION_ASSOC
        else:
            keep = t not in DROP_ASSOC
        if not keep:
            i, j = s.block(a)
            del s.recs[i:j]
            dropped[t] += 1
    if static:
        print('  static: dropped associations %s' % dict(dropped))
    byname = set(names)
    for a in s.find(name='Association', pool='AssociationPool'):
        k = kids(s, a)
        nm = k['NodeName'].names()[0]
        if static and k['Type'].names()[0] in COLLISION_ASSOC:
            nm = paths[nm]
        if nm in byname:
            new = '%s|%s' % (LAYOUT, nm)
            if k['Type'].names()[0] == 'CollisionInstanceToDataNode':
                new += '|Transform|In WorldBasis.Transform'
            k['NodeName'].pay = s.add_name(new).to_bytes(2, 'little')
    add = []
    for a in ref.find(name='Association', pool='AssociationPool'):
        k = kids(ref, a)
        if k['Type'].names()[0] not in ADD_ASSOC + (COLLISION_ASSOC if static else ()):
            continue
        ai, aj = ref.block(a)
        blk2 = _reindex(ref, s, ref.recs[ai:aj])
        bk = {r.name: r for r in blk2[1:]}
        nm = k['NodeName'].names()[0].replace('Default_Floor', names[0])
        bk['NodeName'].pay = s.add_name(nm).to_bytes(2, 'little')
        if 'SceneTreeNodeRef' in bk:
            floor = static and k['Type'].names()[0] == 'CollisionInstanceToSceneTreeNode'
            bk['SceneTreeNodeRef'].set_int(nodes.index(gn[0][0]) if floor else layout_ref)
        add.append(blk2)
    _, end = s.block(pool_rec(s, 'AssociationPool'))
    s.recs[end:end] = [r for blk2 in add for r in blk2]

    s = _roundtrip(s)
    for i, a in enumerate(s.find(name='Association', pool='AssociationPool')):
        a.pay = s.add_name(str(i)).to_bytes(2, 'little')

    if static:   # the donor's self-consistent floor: 1 TriangleList shape, 1 instance, its .btd
        for name in ('CollisionShapePool', 'CollisionInstancePool'):
            i, j = s.block(pool_rec(s, name))
            a2, b2 = ref.block(pool_rec(ref, name))
            s.recs[i:j] = _reindex(ref, s, ref.recs[a2:b2])
        s = _roundtrip(s)

    # --- the 3.0-only pools -----------------------------------------------
    for name in ('LayoutPool', 'AssetPool'):
        src = pool_rec(ref, name)
        i, j = ref.block(src)
        s.recs.extend(_reindex(ref, s, ref.recs[i:j]))
    old = pool_rec(s, 'SubNetworkPool')
    i, j = s.block(old)
    src = pool_rec(ref, 'SubNetworkPool')
    a2, b2 = ref.block(src)
    s.recs[i:j] = _reindex(ref, s, ref.recs[a2:b2])
    s = _roundtrip(s)

    gp = graph.expand(s, names, LAYOUT)
    print('  graph n=%d: headers %d, datanodes %d, processors %d, variantgroups %d'
          % (len(names), gp['VariableHeaderCount'], gp['VariableDataNodeCount'],
             gp['ProcessorNodeCount'], gp['VariantGroupCount']))
    s = _roundtrip(s)
    ag = s.find(name='AggregateCount')
    if ag:
        ag[0].set_int(len(names))

    # --- placement: each node keeps its OWN position ----------------------
    # graph.expand writes every per-node Dilation translation as zero, and the
    # graph is where runtime placement lives. A world whose nodes are scattered
    # (plants, barricades) therefore collapses to the origin with every mesh
    # piled on every other -- which reads on screen as vertices wired wrongly.
    # Each node's position comes from its own LocalToParentMatrix; the global
    # --offset is added on top. The triples are in node order and only the
    # Dilation with BasisRef 1 carries the position.
    ox, oy, oz = offset
    gn2 = geo_nodes(s)
    pos = []
    for _, k, _ in gn2:
        m = k['LocalToParentMatrix'].floats()
        p3 = [m[12] + ox, m[13] + oy, m[14] + oz]
        m[12], m[13], m[14] = p3
        k['LocalToParentMatrix'].set_floats(m)
        for nm in ('BoundingSphereCenter', 'BoundingOBBCenter'):
            if nm in k:
                b = k[nm].floats()
                k[nm].set_floats([b[0] + ox, b[1] + oy, b[2] + oz])
        pos.append(p3)
    dn, moved = None, 0
    for r in s.recs:
        if r.pool != 'DataNodePool':
            continue
        if r.name == 'DataNode':
            dn = {'type': None, 'basis': None}
        elif dn is not None and r.name == 'Type':
            dn['type'] = r.names()[0]
        elif dn is not None and r.name == 'BasisRef':
            dn['basis'] = r.ints()[0]
        elif dn is not None and r.name == 'Translation':
            if dn['type'] == 'Dilation' and dn['basis'] == 1 and moved < len(pos):
                r.set_floats(pos[moved])
                moved += 1
    assert moved == len(gn2), 'placed %d of %d nodes' % (moved, len(gn2))
    print('  placement: %d nodes at their own positions, offset %s'
          % (moved, (ox, oy, oz)))

    # --- internal file names ----------------------------------------------
    for r in s.recs:
        if r.type in (0x05, 0x0B):
            try:
                v = s.name(int.from_bytes(r.pay[:2], 'little'))
            except Exception:
                continue
            if stem in v:
                r.pay = s.add_name(v.replace(stem, world)).to_bytes(2, 'little')

    if static:   # nothing 3.0 has never shipped may remain (1.0 leftovers corrupted the heap)
        import tempfile, foreign
        tmp = Path(tempfile.mkdtemp()) / 'check.oct'
        s.save(tmp)
        left = foreign.foreign_in(tmp)
        assert not left, 'static: %d record kinds 3.0 never ships remain, e.g. %s' % (len(left), left[:5])
        print('  static: 0 record kinds foreign to 3.0')

    if dry:
        print('(dry run)')
        return
    s.save(out / ('%s.oct' % world))
    for ext in ('.mer', '.mtc', '.ap', '.btm'):
        p = REF / ('out_intro2%s' % ext)
        if p.exists():
            shutil.copy2(p, out / ('%s%s' % (world, ext)))
    for f in tocopy:
        shutil.copy2(f, INSTALL.parent / 'textures' / f.name)
    if isinstance(bundle, bytes):
        (out / ('%s.mtb' % world)).write_bytes(bundle)
    elif bundle:
        shutil.copy2(bundle, out / ('%s.mtb' % world))
    off = ','.join('%g' % x for x in offset)
    if static:
        shutil.copy2(REF / 'out_intro2.btd', out / ('%s.btd' % world))
        print('  btd: donor floor (static mode)')
        print('wrote %s into %s' % (world, out))
        return
    r = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                        '-Command', "& '%s' -Source '%s' -Offset %s -OutPath '%s'"
                        % (Path(__file__).parent / 'btd-convert.ps1',
                           src_dir / ('%s.btd' % stem), off,
                           out / ('%s.btd' % world))],
                       capture_output=True, text=True)
    print('  btd: %s' % ('ok' if r.returncode == 0 else 'FAILED ' + (r.stderr or r.stdout)[:200]))
    print('wrote %s into %s' % (world, out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', required=True)
    ap.add_argument('--world', default='out_intro2')
    ap.add_argument('--offset', default='0,0,0')
    ap.add_argument('--bundle', default=None)
    ap.add_argument('--dry', action='store_true')
    ap.add_argument('--static', action='store_true',
                    help='static scenery: flatten the hierarchy, LOD0 only, gameplay links dropped')
    a = ap.parse_args()
    convert(a.source, a.world, tuple(float(x) for x in a.offset.split(',')),
            a.bundle, a.dry, a.static)


if __name__ == '__main__':
    main()

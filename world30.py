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


def build_buffers(src_dir, stem, nodes_prims, out_v, out_i):
    """nodes_prims = [[(Idata, Vdata) per primitive] per node] from the SOURCE.

    1.0 already gives each node its own vertex region and its index values are
    RELATIVE to that primitive's own Vdata[3]:

        Plant_E_1  Vdata [2, 464, 0,     0, 16, 0,  7424, 16]
        Plant_A_1  Vdata [2, 235, 0, 14848, 16, 0, 18608, 16]

    so a vertex is at  Vdata[3 + 3*s] + value * stride, never at value * stride
    from the start of the buffer. Reading from the buffer start works only when
    a single node owns the whole mesh (sumo) and silently gives every other
    node the FIRST node's vertices.
    """
    vb = (src_dir / ('%s_0.vbuf' % stem)).read_bytes()
    ib = (src_dir / ('%s_0.ibuf' % stem)).read_bytes()
    idx = list(struct.unpack('<%dH' % (len(ib) // 2), ib))

    new_v, new_i, out = bytearray(), bytearray(), []
    for prims in nodes_prims:
        S = prims[0][1][0]
        strides = [prims[0][1][4 + i * 3] for i in range(S)]
        orders, subs = [], []
        for ida, vda in prims:
            sub = idx[ida[1]:ida[1] + ida[3]]
            order, remap = [], {}
            for v in sub:
                if v not in remap:
                    remap[v] = len(order)
                    order.append(v)
            orders.append(order)
            subs.append([remap[v] for v in sub])
        node_off = [[0] * S for _ in prims]
        for s in range(S):
            for i, (order, (ida, vda)) in enumerate(zip(orders, prims)):
                base, st = vda[3 + s * 3], vda[4 + s * 3]
                node_off[i][s] = len(new_v)
                for v in order:
                    lo = base + v * st
                    assert lo + st <= len(vb),                         'vertex %d of a block at %d runs past the vbuf' % (v, base)
                    new_v += vb[lo:lo + st]
        base0 = node_off[0][0]
        blocks = []
        for i, sub in enumerate(subs):
            vbase = (node_off[i][0] - base0) // strides[0]
            ioff = len(new_i)
            new_i += struct.pack('<%dH' % len(sub), *[x + vbase for x in sub])
            vdata = [S, len(orders[i])]
            for s in range(S):
                vdata += [0, node_off[i][s], strides[s]]
            blocks.append({'Vdata': vdata, 'Idata': [0, ioff, vbase, len(sub)]})
        out.append(blocks)
    Path(out_v).write_bytes(bytes(new_v))
    Path(out_i).write_bytes(bytes(new_i))
    return out, len(new_v), len(new_i)


def convert(source, world, offset=(0.0, 0.0, 0.0), bundle=None, dry=False):
    src_dir = Path(source)
    stem = src_dir.name
    vmap = {}
    if bundle == 'auto':   # the world's own materials, by GUID, from shipped 3.0 bundles
        import autobundle
        data, problems, vmap = autobundle.resolve(src_dir)
        if problems:
            raise SystemExit('--bundle auto: %d unresolved, nothing written\n  ' % len(problems)
                             + '\n  '.join(problems[:20]))
        bundle = data
    s = Oct.load(src_dir / ('%s.oct' % stem))
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

    # --- buffers, before the tree is touched -----------------------------
    vd0 = kids(s, gn[0][2][0])['Vdata'].ints()
    strides = [vd0[4 + i * 3] for i in range(vd0[0])]
    layouts = {tuple(kids(s, p)['Vdata'].ints()[:1] +
                     [kids(s, p)['Vdata'].ints()[4 + i * 3]
                      for i in range(kids(s, p)['Vdata'].ints()[0])])
               for _, _, prims in gn for p in prims}
    assert len(layouts) == 1, \
        'world uses %d vertex layouts %s -- only a single layout is supported' % (
            len(layouts), layouts)
    nodes_prims = [[(kids(s, p)['Idata'].ints(), kids(s, p)['Vdata'].ints())
                    for p in prims] for _, _, prims in gn]
    out = INSTALL / world
    out.mkdir(parents=True, exist_ok=True)
    tv, ti = out / ('%s_0.vbuf' % world), out / ('%s_0.ibuf' % world)
    blocks, nv, ni = build_buffers(src_dir, stem, nodes_prims,
                                   tv if not dry else tv.with_suffix('.dry'),
                                   ti if not dry else ti.with_suffix('.dry'))
    for (_, _, prims), blks in zip(gn, blocks):
        for p, b in zip(prims, blks):
            k = kids(s, p)
            k['Idata'].set_ints(b['Idata'])
            k['Vdata'].set_ints(b['Vdata'])
    for pool, size in (('VertexBufferPool', nv), ('IndexBufferPool', ni)):
        for r in s.find(name='Size', pool=pool):
            if r.ints()[0]:
                r.set_int(size)
                break
    print('  buffers: %d nodes -> vbuf %d B, ibuf %d B' % (len(gn), nv, ni))

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
    for _, k, _ in gn:
        k['ParentNodeReferences'].set_ints([layout_ref])
    names = [k['NodeName'].names()[0] for _, k, _ in gn]
    print('  scene tree: node %d Camera -> Transform %r, %d geometry nodes reparented'
          % (layout_ref, LAYOUT, len(gn)))

    # --- associations ------------------------------------------------------
    for a in reversed(s.find(name='Association', pool='AssociationPool')):
        if kids(s, a)['Type'].names()[0] in DROP_ASSOC:
            i, j = s.block(a)
            del s.recs[i:j]
    byname = set(names)
    for a in s.find(name='Association', pool='AssociationPool'):
        k = kids(s, a)
        nm = k['NodeName'].names()[0]
        if nm in byname:
            new = '%s|%s' % (LAYOUT, nm)
            if k['Type'].names()[0] == 'CollisionInstanceToDataNode':
                new += '|Transform|In WorldBasis.Transform'
            k['NodeName'].pay = s.add_name(new).to_bytes(2, 'little')
    add = []
    for a in ref.find(name='Association', pool='AssociationPool'):
        k = kids(ref, a)
        if k['Type'].names()[0] not in ADD_ASSOC:
            continue
        ai, aj = ref.block(a)
        blk2 = _reindex(ref, s, ref.recs[ai:aj])
        bk = {r.name: r for r in blk2[1:]}
        nm = k['NodeName'].names()[0].replace('Default_Floor', names[0])
        bk['NodeName'].pay = s.add_name(nm).to_bytes(2, 'little')
        if 'SceneTreeNodeRef' in bk:
            bk['SceneTreeNodeRef'].set_int(layout_ref)
        add.append(blk2)
    _, end = s.block(pool_rec(s, 'AssociationPool'))
    s.recs[end:end] = [r for blk2 in add for r in blk2]

    s = _roundtrip(s)
    for i, a in enumerate(s.find(name='Association', pool='AssociationPool')):
        a.pay = s.add_name(str(i)).to_bytes(2, 'little')

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

    # --- vertex format ids: 1.0 CRC -> declaration-identical 3.0 CRC -------
    remapped = {}
    for r in s.recs:
        if r.name == 'vformatCRC':
            old = r.ints()[0] & 0xffffffff
            new = remapped.setdefault(old, vmap.get(old) or vtxf.remap_1to3(old))
            r.pay = new.to_bytes(len(r.pay), 'little')
    print('  vformatCRC: ' + ', '.join('%08x -> %08x' % kv for kv in remapped.items()))

    if dry:
        print('(dry run)')
        return
    s.save(out / ('%s.oct' % world))
    for ext in ('.mer', '.mtc', '.ap', '.btm'):
        p = REF / ('out_intro2%s' % ext)
        if p.exists():
            shutil.copy2(p, out / ('%s%s' % (world, ext)))
    if isinstance(bundle, bytes):
        (out / ('%s.mtb' % world)).write_bytes(bundle)
    elif bundle:
        shutil.copy2(bundle, out / ('%s.mtb' % world))
    off = ','.join('%g' % x for x in offset)
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
    a = ap.parse_args()
    convert(a.source, a.world, tuple(float(x) for x in a.offset.split(',')),
            a.bundle, a.dry)


if __name__ == '__main__':
    main()

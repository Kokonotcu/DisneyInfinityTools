"""Proven 1.0 -> 3.0 conversion primitives (extracted from the retired world30.py).

Geometry: per-node vertex regions, stream-major primitives, global indices + relative base vertex,
Idata byte offsets, vertex-format conversion (vtxf), RenderCaps 1.0 -> 3.0.
Records: kids/pool_rec/geo_nodes, cross-file copy (_reindex) and pool re-tagging (_roundtrip).
"""
import struct
from pathlib import Path
from oct import Oct
import vtxf

REF = Path(r'D:\DI-Extract\3.0\assets\worlds\out_intro2')
INSTALL = Path(r'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition'
               r'\assets\worlds')

LAYOUT = 'Layout'

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


TRANSFORM_FIELDS = ('NodeName', 'Uuid', 'DisplayLayer', 'Type', 'ParentNodeReferences',
                    'LocalToParentMatrix')   # a shipped 3.0 Transform node, exactly
IDENT = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def world_by_path(s):
    """{scene path 'A|B|C' (Scene node excluded): world matrix} for every node, on the 1.0 tree."""
    nodes = s.find(name='Node', pool='SceneTreeNodePool')
    K = [kids(s, n) for n in nodes]
    typ = [k['Type'].names()[0] if 'Type' in k else '' for k in K]
    par = [k['ParentNodeReferences'].ints()[0] if 'ParentNodeReferences' in k else None for k in K]
    loc = [k['LocalToParentMatrix'].floats() if 'LocalToParentMatrix' in k else IDENT for k in K]
    name = [k['NodeName'].names()[0] if 'NodeName' in k else '' for k in K]
    W, out = {}, {}

    def world(i):
        if i not in W:
            W[i] = loc[i] if par[i] is None or par[i] < 0 else _mul(loc[i], world(par[i]))
        return W[i]
    for i in range(len(nodes)):
        chain, j = [], i
        while j is not None and j >= 0:
            if typ[j] != 'Scene':
                chain.append(name[j])
            j = par[j]
        out['|'.join(reversed(chain))] = world(i)
    return out


def mat2quat(m):
    """Row-major 4x4 -> DataNode Orientation [x, y, z, w], rows normalised. Convention measured on
    shipped data: 46,036 Dilation Orientations predicted from their nodes' LocalToParentMatrix."""
    import math
    R = [[m[r * 4 + c] / (math.sqrt(sum(m[r * 4 + k] ** 2 for k in range(3))) or 1) for c in range(3)]
         for r in range(3)]
    t = R[0][0] + R[1][1] + R[2][2]
    if t > 0:
        q = math.sqrt(t + 1) * 2
        return [(R[2][1] - R[1][2]) / q, (R[0][2] - R[2][0]) / q, (R[1][0] - R[0][1]) / q, q / 4]
    if R[0][0] > R[1][1] and R[0][0] > R[2][2]:
        q = math.sqrt(1 + R[0][0] - R[1][1] - R[2][2]) * 2
        return [q / 4, (R[0][1] + R[1][0]) / q, (R[0][2] + R[2][0]) / q, (R[2][1] - R[1][2]) / q]
    if R[1][1] > R[2][2]:
        q = math.sqrt(1 + R[1][1] - R[0][0] - R[2][2]) * 2
        return [(R[0][1] + R[1][0]) / q, q / 4, (R[1][2] + R[2][1]) / q, (R[0][2] - R[2][0]) / q]
    q = math.sqrt(1 + R[2][2] - R[0][0] - R[1][1]) * 2
    return [(R[0][2] + R[2][0]) / q, (R[1][2] + R[2][1]) / q, q / 4, (R[1][0] - R[0][1]) / q]


def _mul(a, b):
    """Row-major 4x4, row vectors (translation at [12:15]): world = local x parent."""
    return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]


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


def rendercaps30(v):
    """Primitive RenderCaps 1.0 -> 3.0. 1.0 sets 0x1000 on every drawn primitive; 0x1000 never ships in
    3.0, whose equivalent is 0x200 with the same low bits (0x200-0x207 on 44,191 of 44,232 non-zero
    shipped primitives). 0x2000 -> 0x400 is a guess (2 in_city primitives; 3.0 ships 0x600).
    1.0 0 is drawn in 1.0 but NOT in 3.0: every Metroville building body (109 nodes) and 1,407 of 1,535
    exploder primitives carried 0 and were invisible, so 0 also maps to 0x200."""
    if v == 0:
        return 0x200
    return (v & 0xfff) | (0x200 if v & 0x1000 else 0) | (0x400 if v & 0x2000 else 0)


def convert_geometry(s, gn, src_dir, stem, vmap, out, name, dry=False):
    """Vertex formats + buffers of every geometry node; writes <out>/<name>_0.vbuf/ibuf (and _2.ibuf)."""
    # each primitive's target format: the one its material accepts (autobundle), else the best
    # 3.0 target for its 1.0 format (identical twin first)
    def target(p):
        k = kids(s, p)
        src = k['vformatCRC'].ints()[0] & 0xffffffff
        t = vmap.get((k['MaterialReference'].ints()[0], src)) or vtxf.targets_1to3(src)[0]
        return src, t[0]
    nodes_prims = [[(kids(s, p)['Idata'].ints(), kids(s, p)['Vdata'].ints(), *target(p))
                    for p in prims] for _, _, prims in gn]
    tv, ti, ti2 = (out / ('%s_%d.%s' % (name, n, e)) for n, e in ((0, 'vbuf'), (0, 'ibuf'), (2, 'ibuf')))
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
            if 'RenderCaps' in k:
                k['RenderCaps'].set_int(rendercaps30(k['RenderCaps'].ints()[0]))
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



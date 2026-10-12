"""Proven 1.0 -> 3.0 conversion primitives (extracted from the retired world30.py; used by native30, realm30
and expl30).

Pipeline every converter shares: prepare() (materials, dropped pools, geometry, RenderCaps), then the tool's own
record surgery, then rename_files() / check_foreign() / copy_textures().
Geometry: per-node vertex regions, stream-major primitives, global indices + relative base vertex,
Idata byte offsets, vertex-format conversion (vtxf), RenderCaps 1.0 -> 3.0.
Records: pool_rec/geo_nodes, cross-file copy (_reindex), pool re-tagging (_roundtrip), the shipped pool order.
"""
import struct
from collections import Counter
from pathlib import Path
import paths, vtxf

LAYOUT = 'Layout'


def copy_texture(src, dst):
    """Copy a 1.0 .tbody into the pool. D3D11 rejects block-compressed textures below 4x4
    (CreateTexture2D E_INVALIDARG, the only 2 throws in Metroville); a 1x1/2x2 single-mip DXT file
    already holds one whole 4x4 block, so only the header size changes."""
    d = bytearray(Path(src).read_bytes())
    h, w = struct.unpack_from('<II', d, 12)
    if d[:4] == b'DDS ' and d[84:87] == b'DXT' and (w < 4 or h < 4) and struct.unpack_from('<I', d, 28)[0] <= 1:
        struct.pack_into('<II', d, 12, max(h, 4), max(w, 4))
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    Path(dst).write_bytes(bytes(d))


def pool_rec(f, name):
    r = [x for x in f.recs if x.level == 1 and x.name == name]
    return r[0] if r else None


def geo_nodes(f):
    """[(nodeRecord, kids, [primitiveRecords])] in scene tree order."""
    out = []
    for n in f.find(name='Node', pool='SceneTreeNodePool'):
        k = f.kids(n)
        if not (k.get('Type') and k['Type'].names()[0] == 'Geometry'):
            continue
        i, j = f.block(n)
        prims = [r for r in f.recs[i:j] if r.name == 'Primitive']
        out.append((n, k, prims))
    return out


def _roundtrip(f):
    return f.reparse()


def _reindex(src, dst, recs):
    out = []
    for r in recs:
        c = type(r)(dst, r.level, r.type, dst.add_name(src.name(r.name_idx)), r.pay)
        if r.type in (0x05, 0x0B):
            c.set_name(src.name(int.from_bytes(r.pay[:2], 'little')))
        out.append(c)
    return out


TRANSFORM_FIELDS = ('NodeName', 'Uuid', 'DisplayLayer', 'Type', 'ParentNodeReferences',
                    'LocalToParentMatrix')   # a shipped 3.0 Transform node, exactly
IDENT = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def _tree(s):
    """Per scene node, in SceneTreeNodePool order: (type, parent index or None, local matrix, name)."""
    K = [s.kids(n) for n in s.find(name='Node', pool='SceneTreeNodePool')]
    return ([k['Type'].names()[0] if 'Type' in k else '' for k in K],
            [k['ParentNodeReferences'].ints()[0] if 'ParentNodeReferences' in k else None for k in K],
            [k['LocalToParentMatrix'].floats() if 'LocalToParentMatrix' in k else IDENT for k in K],
            [k['NodeName'].names()[0] if 'NodeName' in k else '' for k in K])


def world_matrices(s):
    """World matrix of every scene node, in SceneTreeNodePool order."""
    typ, par, loc, _ = _tree(s)
    W = {}

    def world(i):
        if i not in W:
            W[i] = loc[i] if par[i] is None or par[i] < 0 else _mul(loc[i], world(par[i]))
        return W[i]
    return [world(i) for i in range(len(typ))]


def world_by_path(s):
    """{scene path 'A|B|C' (Scene node excluded): world matrix} for every node, on the 1.0 tree."""
    typ, par, _, name = _tree(s)
    out = {}
    for i, w in enumerate(world_matrices(s)):
        chain, j = [], i
        while j is not None and j >= 0:
            if typ[j] != 'Scene':
                chain.append(name[j])
            j = par[j]
        out['|'.join(reversed(chain))] = w
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
    out_* = None: build and measure only (dry run).
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
    if out_v:
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
        k = s.kids(p)
        src = k['vformatCRC'].ints()[0] & 0xffffffff
        t = vmap.get((k['MaterialReference'].ints()[0], src)) or vtxf.targets_1to3(src)[0]
        return src, t[0]
    nodes_prims = [[(s.kids(p)['Idata'].ints(), s.kids(p)['Vdata'].ints(), *target(p))
                    for p in prims] for _, _, prims in gn]
    # 1.0 index values are read as u16, relative to the primitive's own Vdata; Idata[0] (index buffer) and
    # Idata[2] (base vertex) are not used. Say so if a source sets them.
    odd = Counter((i[0], i[2]) for ps in nodes_prims for i, *_ in ps if i[0] or i[2])
    if odd:
        print('  NOTE: %d primitives set Idata[0]/Idata[2] (ignored): %s' % (sum(odd.values()), dict(odd.most_common(4))))
    tv, ti, ti2 = (Path(out) / ('%s_%d.%s' % (name, n, e)) for n, e in ((0, 'vbuf'), (0, 'ibuf'), (2, 'ibuf')))
    blocks, nv, ni, ni2 = build_buffers(src_dir, stem, nodes_prims, *((None,) * 3 if dry else (tv, ti, ti2)))
    conv = Counter()
    for (_, _, prims), blks in zip(gn, blocks):
        for p, b in zip(prims, blks):
            k = s.kids(p)
            old = k['vformatCRC'].ints()[0] & 0xffffffff
            conv['%08x -> %08x' % (old, b['vformatCRC'])] += 1
            k['Idata'].set_ints(b['Idata'])
            k['Vdata'].set_ints(b['Vdata'])
            k['vformatCRC'].set_int(b['vformatCRC'])
            if 'RenderCaps' in k:
                k['RenderCaps'].set_int(rendercaps30(k['RenderCaps'].ints()[0]))
    print('  vformatCRC per primitive: %s' % dict(conv))
    for r in s.find(name='Size', pool='VertexBufferPool'):
        if r.ints()[0]:
            r.set_int(nv)
            break
    ibs = s.find(name='IndexBuffer', pool='IndexBufferPool')
    for n_ib, size in ((0, ni), (2, ni2)):
        k = s.kids(ibs[n_ib])
        assert k['Width'].ints()[0] == (2 if n_ib == 0 else 4)
        k['Size'].set_int(size)
        if size and 'FileName' not in k:   # IndexBuffer 2 = 32-bit, its own file (dor_tp_mc_2.ibuf)
            fn = s.kids(ibs[0])['FileName']
            i, j = s.block(ibs[n_ib])
            s.recs.insert(j, type(fn)(s, fn.level, fn.type, fn.name_idx, bytes(fn.pay)).set_name('%s_2.ibuf' % name))
    print('  buffers: %d nodes -> vbuf %d B, ibuf %d B, 32-bit ibuf %d B' % (len(gn), nv, ni, ni2))


# --- the pipeline every converter shares ---------------------------------------------------------------

def prepare(s, src_dir, out, name, drop=(), dry=False):
    """The steps native30, realm30 and expl30 share, in this order: materials resolved (autobundle, on the untouched
    source), the pools in drop removed, geometry + vertex formats written as <out>/<name>_0.vbuf/ibuf (nothing
    written when dry), the 1.0-only Material RenderCaps removed.
    -> (bundle bytes, material tiers, .tbody files to copy, geometry nodes)"""
    import autobundle
    src_dir = Path(src_dir)
    data, problems, vmap, tiers, tocopy = autobundle.resolve(src_dir, s)
    assert not problems, '%s: %d materials unresolved: %s' % (src_dir.name, len(problems), problems[:3])
    print('%s: materials %s' % (src_dir.name, dict(Counter(tiers))))
    gn = geo_nodes(s)
    gone = []
    for name_ in drop:
        p = pool_rec(s, name_)
        if p:
            i, j = s.block(p); del s.recs[i:j]; gone.append(name_)
    if gone:
        print('  dropped pools %s' % gone)
    if not dry:
        Path(out).mkdir(parents=True, exist_ok=True)
    convert_geometry(s, gn, src_dir, src_dir.name, vmap, out, name, dry)
    for mt in s.find(name='Material', pool='MaterialPool'):
        i, j = s.block(mt)
        s.recs[i + 1:j] = [r for r in s.recs[i + 1:j] if r.name != 'RenderCaps']
    return data, tiers, tocopy, gn


def rename_files(s, old, new):
    """Name records naming the source's files (old_0.vbuf, ...) follow the install name."""
    if old == new:
        return
    for r in s.recs:
        if r.type in (0x05, 0x0B):
            v = s.name(int.from_bytes(r.pay[:2], 'little'))
            if old in v:
                r.set_name(v.replace(old, new))


def check_foreign(s, allow=()):
    """Assert no record kind 3.0 never ships is left (foreign.py); allow = pools or record names kept on purpose."""
    import foreign
    left = [k for k in foreign.foreign_in(s) if k[0] not in allow and k[1] not in allow]
    assert not left, '%d record kinds 3.0 never ships remain, e.g. %s' % (len(left), left[:8])
    print('  0 record kinds foreign to 3.0' + (' (kept: %s)' % ', '.join(allow) if allow else ''))


def copy_textures(files, folder):
    for f in files:
        copy_texture(f, Path(folder) / f.name)


def _tops(p):
    from oct import Oct
    try:
        return [r.name for r in Oct.load(p).recs if r.level == 1]
    except Exception:
        return None


def shipped_pools():
    """Top-level pools of every shipped 3.0 .oct (worlds, realms, exploders, ...) in the one order they all agree
    on: 0 conflicting pairs over 3,905 files. Topological over every file's sequence; pools no file holds
    together tie by their mean relative position. Cached."""
    def build():
        from par import pmap
        seqs = [q for q in pmap(_tops, sorted(paths.ext30().rglob('*.oct')), chunksize=32) if q]
        after, pos = {}, {}
        for q in seqs:
            for i, a in enumerate(q):
                after.setdefault(a, set()).update(q[i + 1:])
                pos.setdefault(a, []).append(i / len(q))
        clash = [(a, b) for a in after for b in after[a] if a in after.get(b, ())]
        assert not clash, 'shipped worlds disagree on pool order: %s' % clash[:4]
        indeg = Counter(b for a in after for b in after[a])
        key = lambda n: (sum(pos[n]) / len(pos[n]), n)
        ready, order = sorted((n for n in after if not indeg[n]), key=key), []
        while ready:
            n = ready.pop(0)
            order.append(n)
            for b in after[n]:
                indeg[b] -= 1
                if not indeg[b]:
                    ready.append(b)
            ready.sort(key=key)
        return order
    return paths.cached('shippedpools.json', build, 2)


def foreign_pools(s, keep=()):
    """Top-level pools of s that no shipped 3.0 .oct has (1.0's Realm*/SurfaceTypes...), except keep."""
    shipped = set(shipped_pools())
    return [r.name for r in s.recs if r.level == 1 and r.name not in shipped and r.name not in keep]


def sort_pools(s, extra=None):
    """Top-level blocks into the shipped order; extra = {pool 3.0 never ships: the pool it goes right before}."""
    order = list(shipped_pools())
    for p, before in (extra or {}).items():
        order.insert(order.index(before), p)
    tops = [i for i, r in enumerate(s.recs) if r.level == 1]
    head = s.recs[:tops[0]]
    blocks = [s.recs[a:b] for a, b in zip(tops, tops[1:] + [len(s.recs)])]
    assert all(b[0].name in order for b in blocks), [b[0].name for b in blocks if b[0].name not in order]
    blocks.sort(key=lambda b: order.index(b[0].name))
    s.recs = head + [r for b in blocks for r in b]
    return _roundtrip(s)

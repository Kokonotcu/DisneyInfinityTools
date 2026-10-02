"""Split a shared vertex buffer into one disjoint block per primitive.

1.0 authors a welded mesh: one vertex buffer, and each primitive is just an
index sub-range into it. Every shipped 3.0 multi-node world instead gives each
geometry node its OWN vertex block --

  dor_bay    node0 Vdata=[2,2040,0,    0,16,0, 32640,16]
             node1 Vdata=[2, 720,0,65280,16,0, 76800,16]
  lb_zonetwo nodes at 0 / 128 / 256

-- with node i's two streams contiguous and node i+1 starting after them. Not
one of the five multi-node worlds measured shares a range.

This rebuilds .vbuf/.ibuf that way: for each index range, gather the vertices it
actually references, copy them into a fresh contiguous block, and renumber the
indices locally. Vertices on a seam get duplicated into both blocks, so the
buffer grows slightly; nothing else about the mesh changes.

Returns the new per-node Vdata/Idata for the caller to write into the .oct.
"""
import struct
from pathlib import Path

STRIDE = 16


def split(vbuf_path, ibuf_path, ranges, out_vbuf, out_ibuf, verts_total, stride=STRIDE):
    """ranges = [(firstIndexUnits, indexCount)] in SOURCE (1.0) index units."""
    vb = Path(vbuf_path).read_bytes()
    ib = Path(ibuf_path).read_bytes()
    idx = list(struct.unpack('<%dH' % (len(ib) // 2), ib))
    s0 = 0                       # stream 0 base
    s1 = verts_total * stride    # stream 1 base
    assert len(vb) >= s1 + verts_total * stride, 'vbuf too small for %d verts' % verts_total

    new_v = bytearray()
    new_i = bytearray()
    out = []
    for first, count in ranges:
        sub = idx[first:first + count]
        order = []
        remap = {}
        for v in sub:
            if v not in remap:
                remap[v] = len(order)
                order.append(v)
        base = len(new_v)
        for v in order:                       # stream 0 for this node
            new_v += vb[s0 + v * stride: s0 + (v + 1) * stride]
        mid = len(new_v)
        for v in order:                       # stream 1, immediately after
            new_v += vb[s1 + v * stride: s1 + (v + 1) * stride]
        ioff = len(new_i)
        new_i += struct.pack('<%dH' % len(sub), *[remap[v] for v in sub])
        out.append({
            'verts': len(order),
            'Vdata': [2, len(order), 0, base, stride, 0, mid, stride],
            'Idata': [0, ioff, 0, len(sub)],   # Idata[1] is a BYTE offset in 3.0
        })
    Path(out_vbuf).write_bytes(bytes(new_v))
    Path(out_ibuf).write_bytes(bytes(new_i))
    return out, len(new_v), len(new_i)


def split_stream_major(vbuf_path, ibuf_path, ranges, out_vbuf, out_ibuf,
                       verts_total, strides, global_indices=True):
    """Same split, but laid out STREAM-major: all of stream 0 for every
    primitive, then all of stream 1, and so on.

    split() above is primitive-major -- each primitive's streams adjacent --
    which is what shipped multi-NODE worlds do, one region per node. A node
    carrying several PRIMITIVES uses the other layout, unanimously: over the
    retail 3.0 worlds, 4915 of 4915 multi-primitive multi-stream nodes keep each
    stream's region disjoint, and not one is primitive-major.

      psx_l3_planet_main node 0, 3 primitives x 3 streams
        s0  0 -> 6688 -> 8224 -> 28224        then s1 begins at 28224
        s1  28224 -> 34912 -> 35680 -> 55680  then s2 begins at 55680

    strides is one stride per stream, so streams may differ in width the way
    that node's do (16, 16, 4).
    """
    vb = Path(vbuf_path).read_bytes()
    ib = Path(ibuf_path).read_bytes()
    idx = list(struct.unpack('<%dH' % (len(ib) // 2), ib))
    S = len(strides)
    src_base, acc = [], 0
    for st in strides:                    # where each stream starts in the SOURCE
        src_base.append(acc)
        acc += verts_total * st
    assert len(vb) >= acc, 'vbuf too small: %d < %d' % (len(vb), acc)

    orders, subs = [], []
    for first, count in ranges:
        sub = idx[first:first + count]
        order, remap = [], {}
        for v in sub:
            if v not in remap:
                remap[v] = len(order)
                order.append(v)
        orders.append(order)
        subs.append([remap[v] for v in sub])

    new_v = bytearray()
    offs = [[0] * S for _ in ranges]
    for s in range(S):
        st = strides[s]
        for i, order in enumerate(orders):
            offs[i][s] = len(new_v)
            for v in order:
                new_v += vb[src_base[s] + v * st: src_base[s] + (v + 1) * st]

    # Indices are GLOBAL across the node in shipped content, not local to each
    # primitive's block: psx_l3_planet_main's node runs 0..417, 418..513,
    # 514..1763 with baseVertex 0 / 418 / 514 -- each primitive's index range
    # starts exactly at its own base. Renumbering locally (0-based per block)
    # makes every primitive after the first address primitive 0's vertices.
    new_i = bytearray()
    out = []
    for i, sub in enumerate(subs):
        ioff = len(new_i)
        vbase = offs[i][0] // strides[0] if global_indices else 0
        new_i += struct.pack('<%dH' % len(sub), *[x + vbase for x in sub])
        vdata = [S, len(orders[i])]
        for s in range(S):
            vdata += [0, offs[i][s], strides[s]]
        out.append({'verts': len(orders[i]), 'Vdata': vdata,
                    'Idata': [0, ioff, vbase, len(sub)]})
    Path(out_vbuf).write_bytes(bytes(new_v))
    Path(out_ibuf).write_bytes(bytes(new_i))
    return out, len(new_v), len(new_i)


def _selftest_stream_major():
    """Every triangle must still reach the same vertex bytes in EVERY stream,
    and each stream's region must stay disjoint from the next -- the property
    measured on 4915 shipped nodes."""
    import tempfile, os
    verts, strides = 6, [4, 4]
    vb = bytearray()
    for v in range(verts):
        vb += bytes([v] * 4)              # stream 0 = id
    for v in range(verts):
        vb += bytes([v + 0x40] * 4)       # stream 1 = id + 0x40
    idx = [0, 1, 2, 2, 3, 4, 4, 5, 0]
    d = tempfile.mkdtemp()
    vp, ip = os.path.join(d, 'v'), os.path.join(d, 'i')
    open(vp, 'wb').write(bytes(vb))
    open(ip, 'wb').write(struct.pack('<9H', *idx))
    ov, oi = os.path.join(d, 'ov'), os.path.join(d, 'oi')
    ranges = [(0, 3), (3, 3), (6, 3)]
    out, nv, ni = split_stream_major(vp, ip, ranges, ov, oi, verts, strides)
    nvb = open(ov, 'rb').read()
    nib = open(oi, 'rb').read()
    nidx = struct.unpack('<%dH' % (len(nib) // 2), nib)
    for n, (blk, (first, count)) in enumerate(zip(out, ranges)):
        local = nidx[blk['Idata'][1] // 2: blk['Idata'][1] // 2 + count]
        for s in range(len(strides)):
            base, st = blk['Vdata'][3 + s * 3], blk['Vdata'][4 + s * 3]
            vbase = blk['Idata'][2]
            for k, li in enumerate(local):
                orig = idx[first + k]
                got = nvb[base + (li - vbase) * st: base + (li - vbase + 1) * st]
                assert got == bytes([orig + 0x40 * s] * 4), (n, s, k, got, orig)
    for s in range(len(strides) - 1):     # streams must not interleave
        end = max(b['Vdata'][3 + s * 3] + b['Vdata'][1] * b['Vdata'][4 + s * 3] for b in out)
        start = min(b['Vdata'][3 + (s + 1) * 3] for b in out)
        assert end <= start, 'stream %d overlaps stream %d' % (s, s + 1)
    want = sum(b['Vdata'][1] * st for b in out for st in strides)
    assert nv == len(nvb) == want, 'vbuf size %d != %d' % (nv, want)
    print('split-mesh stream-major selftest: %d blocks, %d B vbuf, %d B ibuf, '
          'indices resolve in every stream, regions disjoint' % (len(out), nv, ni))


def _selftest():
    """Round-trip check: every triangle must still reference the same vertex
    bytes after renumbering, and the blocks must not overlap."""
    import tempfile, os
    verts = 6
    stride = 4
    # stream0 = vertex id in a float-ish byte pattern, stream1 = id + 0x40
    vb = bytearray()
    for v in range(verts):
        vb += bytes([v, v, v, v])
    for v in range(verts):
        vb += bytes([v + 0x40] * 4)
    idx = [0, 1, 2, 2, 3, 4, 4, 5, 0]
    ib = struct.pack('<9H', *idx)
    d = tempfile.mkdtemp()
    vp, ip = os.path.join(d, 'v'), os.path.join(d, 'i')
    open(vp, 'wb').write(vb); open(ip, 'wb').write(ib)
    ov, oi = os.path.join(d, 'ov'), os.path.join(d, 'oi')
    out, nv, ni = split(vp, ip, [(0, 3), (3, 3), (6, 3)], ov, oi, verts, stride)
    nvb = open(ov, 'rb').read(); nib = open(oi, 'rb').read()
    nidx = struct.unpack('<%dH' % (len(nib) // 2), nib)
    for n, (blk, (first, count)) in enumerate(zip(out, [(0, 3), (3, 3), (6, 3)])):
        _, nverts, _, b0, st, _, b1, _ = blk['Vdata']
        byte_off = blk['Idata'][1]
        local = nidx[byte_off // 2: byte_off // 2 + count]
        for k, li in enumerate(local):
            orig = idx[first + k]
            got0 = nvb[b0 + li * st: b0 + (li + 1) * st]
            got1 = nvb[b1 + li * st: b1 + (li + 1) * st]
            assert got0 == bytes([orig] * 4), (n, k, got0, orig)
            assert got1 == bytes([orig + 0x40] * 4), (n, k, got1, orig)
        assert b1 == b0 + nverts * st, 'streams not contiguous'
    # blocks must tile without overlap
    at = 0
    for blk in out:
        assert blk['Vdata'][3] == at, 'vertex block gap/overlap'
        at += blk['Vdata'][1] * stride * 2
    assert at == nv == len(nvb), 'vbuf size mismatch'
    print('split-mesh selftest: %d blocks, %d B vbuf, %d B ibuf, all indices resolve'
          % (len(out), nv, ni))


if __name__ == '__main__':
    _selftest()
    _selftest_stream_major()

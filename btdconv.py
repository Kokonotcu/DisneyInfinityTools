"""1.0 -> 3.0 collision .btd, every block.
A .btd is one block per TriangleList shape, packed back to back, each 16-byte aligned (btdblocks.py).

Block layout produced, matching stock 3.0 (323 of 330 shipped single-block pairs rebuilt byte-exact):
  +0  u64 vertexOffset  +8 u64 indexOffset  +16 u32 vertexCount  +20 u32 triangleCount
  +24 section A offset, length  +32 section B offset, length  +40 end of index array  +44 1
  +48 per-triangle array offset, +52 count   +56 0c 00 06 00 02 00 00 00 (all 6,866 shipped blocks)
  section A (the tree; +80 holds the AABB min/max = the root bounds, 1.0 margin) is carried from 1.0,
      only its descriptor changes:  A[0..63] carried, A[64..183] the 120-byte constant btd-a-const.bin
      (identical in all 322 pairs it was derived from), A[184..199] = 1.0 A[64..79] (tag 0x114, node count, flags),
      A[200..203] = 1.0 A[180..183], A[204..] = 1.0 A[188..] (the tree body, unchanged)
  section B: 4 x triangles + 72 in 3.0 (+ 56 in 1.0): its four leading pointer fields widen u32 -> u64,
      so 32 zero bytes, then 1.0 B from +16, size field (+16) = 72
  then vertices f32x3, indices u16 x3, an 8-byte gap (written 0, ffffffff; shipped files hold exporter junk
  in the first word in 37%), a per-triangle u32 array of zeros
1.0 header: +0 A offset, +4 A length, +8 B offset, +12 B length, +16 vertexOffset, +20 vertexCount,
  +24 indexOffset, +28 triangleCount. The tree cannot be synthesised (it depends on how the geometry
  is distributed; a donor's 480-triangle tree in front of 7,056 triangles gave no collision at all).

python btdconv.py <1.0 .btd> <out .btd> [--offset x,y,z]   convert (+ <out>.btm), vertices + AABB shifted
python btdconv.py --selftest [--ext30 DIR] [--ext10 DIR]     .btm rule (317/317 shipped rebuilt); with 1.0: rebuild
                                                             shipped 3.0 .btd from their 1.0 twins
"""
import struct
from pathlib import Path
from btdblocks import blocks

A_CONST = (Path(__file__).parent / 'btd-a-const.bin').read_bytes()
HDR_TAIL = bytes.fromhex('0c00060002000000')     # block header +56..63
assert len(A_CONST) == 120


def convert(src, offset=(0.0, 0.0, 0.0)):
    """One 1.0 block -> one 3.0 block."""
    aoff, alen, boff, blen, voff, vcnt, ioff, tcnt = struct.unpack_from('<8I', src, 0)
    assert voff + vcnt * 12 == ioff, 'vertex array does not close'
    assert aoff + alen == boff and boff + blen == voff, 'sections do not close'
    a_new, b_new = alen + 16, blen + 16
    A = 64; B = A + a_new; V = B + b_new; I = V + vcnt * 12
    IE = (I + tcnt * 6 + 3) & ~3; P = IE + 8   # index end rounded up to 4, then an 8-byte gap
    out = bytearray(P + tcnt * 4)
    out[56:64] = HDR_TAIL
    out[A:A + 64] = src[aoff:aoff + 64]
    out[A + 64:A + 184] = A_CONST
    out[A + 184:A + 200] = src[aoff + 64:aoff + 80]
    out[A + 200:A + 204] = src[aoff + 180:aoff + 184]
    out[A + 204:A + a_new] = src[aoff + 188:aoff + alen]
    out[B + 32:B + b_new] = src[boff + 16:boff + blen]
    struct.pack_into('<Q', out, B + 16, 72)
    ox, oy, oz = offset
    for v in range(vcnt):
        x, y, z = struct.unpack_from('<3f', src, voff + 12 * v)
        struct.pack_into('<3f', out, V + 12 * v, x + ox, y + oy, z + oz)
    out[I:I + tcnt * 6] = src[ioff:ioff + tcnt * 6]
    struct.pack_into('<I', out, IE + 4, 0xffffffff)   # 8-byte gap after the indices: 0, ffffffff
    for base in (80, 96):   # AABB min / max, in the copied section A header
        x, y, z = struct.unpack_from('<3f', src, base)
        struct.pack_into('<3f', out, base, x + ox, y + oy, z + oz)
    struct.pack_into('<QQ', out, 0, V, I)
    struct.pack_into('<10I', out, 16, vcnt, tcnt, A, a_new, B, b_new, IE, 1, P, tcnt)
    return bytes(out)


def convert_file(src, offset=(0.0, 0.0, 0.0)):
    out = bytearray()
    for s, e in blocks(src, 1):
        out += b'\0' * (-len(out) % 16)
        out += convert(src[s:e], offset)
    return bytes(out)


def btm(btd3):
    """The .btd's block table: magic, block count, then (offset, 0x00010010) per block.
    Without it the game loads only the blocks the .btm lists (a copied 1-block .btm = no collision)."""
    bl = blocks(btd3, 3)
    return struct.pack('<II', 0xc007d00d, len(bl)) + b''.join(struct.pack('<II', s, 0x10010) for s, _ in bl)


def _pair(three):
    """Any .btd shipped at the same relative path in both games."""
    import paths
    one = paths.ext10(three.relative_to(paths.ext30()))
    if not one.exists():
        return None
    a, b = one.read_bytes(), three.read_bytes()
    d = three.parent
    try:
        nb = len(blocks(a, 1))
        got = convert_file(a)
    except Exception as e:
        return (d.name, 0, 'error ' + str(e)[:60])
    if got == b:
        return (d.name, nb, 'exact')
    if len(got) == len(b):   # the u32 at each block's index end is exporter junk (0 in 63%, no pattern)
        g2, b2 = bytearray(got), bytearray(b)
        for s, _ in blocks(b, 3):
            ie = s + struct.unpack_from('<I', b, s + 40)[0]
            g2[ie - 2:ie + 4] = b2[ie - 2:ie + 4] = b'\0' * 6
        if g2 == b2:
            return (d.name, nb, 'exact')
    # compare block by block (header bytes 56..63 are HDR_TAIL)
    if len(got) == len(b):
        diff = [i for i in range(len(b)) if got[i] != b[i]]
        return (d.name, nb, 'same size, %d bytes differ, first %#x' % (len(diff), diff[0]))
    return (d.name, nb, 'size %d vs %d' % (len(got), len(b)))


if __name__ == '__main__':
    import argparse, paths
    ap = argparse.ArgumentParser(description='1.0 -> 3.0 collision .btd')
    ap.add_argument('src', nargs='?')
    ap.add_argument('out', nargs='?')
    ap.add_argument('--offset', default='0,0,0', help='x,y,z added to vertices and bounds')
    ap.add_argument('--selftest', action='store_true')
    paths.add_args(ap, 'ext30', 'ext10')
    a = paths.use(ap.parse_args())
    if a.selftest:
        from par import pmap
        from collections import Counter
        btds = sorted(paths.ext30().rglob('*.btd'))
        pairs = [(b, b.with_suffix('.btm')) for b in btds if b.with_suffix('.btm').exists()]
        same = sum(btm(b.read_bytes()) == m.read_bytes() for b, m in pairs)
        print('btm(): %d of %d shipped .btm rebuilt byte-exact from their .btd' % (same, len(pairs)))
        assert pairs and same == len(pairs)
        res = [r for r in pmap(_pair, btds, chunksize=8) if r] if paths.where('ext10')[0] else []
        if not res:
            print('(no 1.0 extract: 1.0 -> 3.0 pairs not checked)')
        c = Counter((r[1] > 1, r[2] == 'exact') for r in res)
        if res:
            print('single-block pairs: %d exact of %d' % (c[(False, True)], c[(False, True)] + c[(False, False)]))
            print('multi-block pairs:  %d exact of %d' % (c[(True, True)], c[(True, True)] + c[(True, False)]))
        for r in [r for r in res if r[2] != 'exact'][:12]:
            print('  ', r)
    elif a.src and a.out:
        off = tuple(map(float, a.offset.split(',')))
        data = convert_file(Path(a.src).read_bytes(), off)
        Path(a.out).write_bytes(data)
        Path(a.out).with_suffix('.btm').write_bytes(btm(data))
        print('%s -> %s (+ .btm): %d blocks, %d B, offset %s' % (a.src, a.out, len(blocks(data, 3)), len(data), off))
    else:
        ap.print_help()

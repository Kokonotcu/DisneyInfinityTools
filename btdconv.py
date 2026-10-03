"""1.0 -> 3.0 collision .btd, every block (port of btd-convert.ps1, which handles one block).
A .btd is one block per TriangleList shape, packed back to back, each 16-byte aligned (btdblocks.py).

python btdconv.py <1.0 .btd> <out .btd> [x,y,z]      convert, vertices + AABB shifted by the offset
python btdconv.py --selftest                         rebuild shipped 3.0 files from their 1.0 twins
"""
import struct, sys
from pathlib import Path
from btdblocks import blocks

HERE = Path(__file__).parent
A_CONST = (HERE / 'btd-a-const.bin').read_bytes()
DONOR_HDR = (Path(r'D:\DI-Extract\3.0\assets\worlds\out_intro2\out_intro2.btd')).read_bytes()[:64]
assert len(A_CONST) == 120


def convert(src, offset=(0.0, 0.0, 0.0), shell=DONOR_HDR):
    """One 1.0 block -> one 3.0 block (layout notes in btd-convert.ps1)."""
    aoff, alen, boff, blen, voff, vcnt, ioff, tcnt = struct.unpack_from('<8I', src, 0)
    assert voff + vcnt * 12 == ioff, 'vertex array does not close'
    assert aoff + alen == boff and boff + blen == voff, 'sections do not close'
    a_new, b_new = alen + 16, blen + 16
    A = 64; B = A + a_new; V = B + b_new; I = V + vcnt * 12
    IE = (I + tcnt * 6 + 3) & ~3; P = IE + 8   # index end rounded up to 4, then an 8-byte gap
    out = bytearray(P + tcnt * 4)
    out[:64] = shell
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
    one = Path(r'D:\DI-Extract\1.0') / three.relative_to(r'D:\DI-Extract\3.0')
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
    # shell bytes 56..63 come from the donor; compare the rest block by block
    if len(got) == len(b):
        diff = [i for i in range(len(b)) if got[i] != b[i]]
        return (d.name, nb, 'same size, %d bytes differ, first %#x' % (len(diff), diff[0]))
    return (d.name, nb, 'size %d vs %d' % (len(got), len(b)))


if __name__ == '__main__':
    if sys.argv[1:] == ['--selftest']:
        from par import pmap
        from collections import Counter
        res = [r for r in pmap(_pair, sorted(Path(r'D:\DI-Extract\3.0').rglob('*.btd')), chunksize=8) if r]
        c = Counter((r[1] > 1, r[2] == 'exact') for r in res)
        print('single-block pairs: %d exact of %d' % (c[(False, True)], c[(False, True)] + c[(False, False)]))
        print('multi-block pairs:  %d exact of %d' % (c[(True, True)], c[(True, True)] + c[(True, False)]))
        for r in [r for r in res if r[2] != 'exact'][:12]:
            print('  ', r)
    else:
        off = tuple(map(float, sys.argv[3].split(','))) if len(sys.argv) > 3 else (0.0, 0.0, 0.0)
        data = convert_file(Path(sys.argv[1]).read_bytes(), off)
        Path(sys.argv[2]).write_bytes(data)
        print('%s -> %s: %d blocks, %d B, offset %s' % (sys.argv[1], sys.argv[2],
              len(blocks(data, 3)), len(data), off))

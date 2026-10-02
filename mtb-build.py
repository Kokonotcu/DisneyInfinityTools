"""Build a .mtb as N copies of one known-good 3.0 material block.

This is what mtb-rematerial.ps1 says it does and does not do. The bundle it left
installed on 2026-08-16 declares 3 materials under 1 shader but carries a
404-byte block region; three copies of out_intro2's own 140-byte block need 420,
and that block does not appear anywhere in the file. Material 0 still resolved,
which is why the port renders -- materials 1 and 2 are read out of a region that
holds no valid block, and every launch that referenced them faulted.

Block framing, read off the stock out_intro2.mtb:

  +0   u32  block tag              0x8b552597 for shader 4bcb5ce6...
  +4   8    zero
  +12  128  payload, shader-determined size
       payload+64  ff ff ff ff ff ff ff ff   unset texture handle
       payload+72  u16 index, u16 generation  the live handle

so a block is 12 + 128 = 140 B. Block size is NOT constant across bundles even
for one shader (144 x65, 140 x17, 134 x9, 136 x7 over the 3.0 worlds), so it
must be taken from the donor rather than assumed.

  python mtb-build.py --out <path>            build 3 materials for sumo
  python mtb-build.py --selftest              round-trip the stock bundle
"""
import argparse, struct, sys
from pathlib import Path

DONOR = Path(r'D:\DI-Extract\3.0\assets\worlds\out_intro2\out_intro2.mtb')
TARGET = Path(r'D:\DI-Extract\1.0\assets\worlds\adv_cbt_sumo\adv_cbt_sumo.mtb')
HANDLE = 72          # within the payload
PAYLOAD = 128        # for shader 4bcb5ce6..., established on 98 bundles


def split_region(reg, mc, payload=PAYLOAD):
    """(headerSize, headers, payloads) -- headers are GROUPED FIRST.

    region = mc x header  ++  mc x payload. Verified as a prediction on every
    3.0 bundle using only shader 4bcb5ce6: the unset-handle ff-pair lands at
    mc*hdr + payload*i + 64 in 98 of 98 testable files, none failing. Slicing
    the region at a uniform (hdr+payload) stride instead -- which is what
    mtb-rematerial.ps1 and the first version of this script did -- puts every
    material after the first at the wrong offset.
    """
    assert mc and len(reg) % mc == 0, 'region %d not divisible by %d' % (len(reg), mc)
    hdr = len(reg) // mc - payload
    assert hdr >= 0 and len(reg) == mc * (hdr + payload), \
        'region %d != %d x (hdr + %d)' % (len(reg), mc, payload)
    heads = [reg[i * hdr:(i + 1) * hdr] for i in range(mc)]
    base = mc * hdr
    pays = [reg[base + i * payload: base + (i + 1) * payload] for i in range(mc)]
    return hdr, heads, pays


def chunks(b):
    assert b[:4] == b'BNDL', 'not a BNDL: %r' % b[:4]
    ver, flen, n = struct.unpack_from('<III', b, 4)
    assert flen == len(b), 'fileLength %d != actual %d' % (flen, len(b))
    p = 16
    out = []
    for _ in range(n):
        tag, cver, clen = struct.unpack_from('<4sII', b, p)
        out.append((tag.decode('latin-1'), cver, b[p:p + clen]))
        p += clen
    return ver, out


def cstrings(b, p, n):
    out = []
    for _ in range(n):
        e = b.index(b'\0', p)
        out.append(b[p:e].decode('latin-1'))
        p = e + 1
    return out, p


def parse_matp(c):
    """(materialCount, shaders, guids, blockRegion)."""
    mc, sc = struct.unpack_from('<II', c, 12)
    shaders, p = cstrings(c, 20, sc)
    p = (p + 3) & ~3
    mc2 = struct.unpack_from('<I', c, p)[0]
    p += 4
    guids, p = cstrings(c, p, mc2)
    p = (p + 3) & ~3
    return mc, shaders, guids, c[p:]


def build_matp(ver, shaders, guids, region):
    """Rebuild a MATP chunk. Alignment is relative to the chunk start."""
    body = bytearray(struct.pack('<II', len(guids), len(shaders)))
    for s in shaders:
        body += s.encode('latin-1') + b'\0'
    while (len(body) + 12) % 4:
        body += b'\0'
    body += struct.pack('<I', len(guids))
    for g in guids:
        body += g.encode('latin-1') + b'\0'
    while (len(body) + 12) % 4:
        body += b'\0'
    body += region
    return struct.pack('<4sII', b'MATP', ver, len(body) + 12) + bytes(body)


def build(donor, target, n=None, tex_for=lambda i: 2 * i):
    """N copies of the donor's block, the target's TEXB and GUIDs."""
    _, dch = chunks(Path(donor).read_bytes())
    tver, tch = chunks(Path(target).read_bytes())
    dmatp = [c for t, v, c in dch if t == 'MATP'][0]
    dver = [v for t, v, c in dch if t == 'MATP'][0]
    tmatp = [c for t, v, c in tch if t == 'MATP'][0]
    texb = [c for t, v, c in tch if t == 'TEXB'][0]

    dmc, dsh, dgu, dblock = parse_matp(dmatp)
    tmc, tsh, tgu, tblock = parse_matp(tmatp)
    assert dmc == 1, 'donor must declare exactly 1 material, got %d' % dmc
    n = n or tmc
    assert n <= len(tgu), 'want %d materials, target has %d GUIDs' % (n, len(tgu))
    hdr, dheads, dpays = split_region(dblock, dmc)
    size = hdr + PAYLOAD
    ntex = struct.unpack_from('<I', texb, 12)[0]

    # Headers grouped first. Material 0 keeps the donor's own header; the rest
    # get zeros, which is the shipped form -- dor_bay's second material has an
    # all-zero header and renders.
    heads = [dheads[0]] + [b'\0' * hdr] * (n - 1)
    pays = []
    for i in range(n):
        pay = bytearray(dpays[0])
        t = tex_for(i)
        if t is not None and t < ntex:    # repoint into the target's TEXB
            struct.pack_into('<HH', pay, HANDLE, t, 1)
        pays.append(bytes(pay))
    region = b''.join(heads) + b''.join(pays)

    matp = build_matp(dver, dsh, tgu[:n], region)
    body = texb + matp
    out = struct.pack('<4sIII', b'BNDL', tver, 16 + len(body), 2) + body
    return out, dict(materials=n, shader=dsh[0], block=size, region=len(region),
                     textures=ntex, guids=tgu[:n])


FF = b'\xff' * 8


def verify(data, donor, n):
    """Re-parse and assert the grouped layout, on the same test the corpus passed."""
    _, dch = chunks(Path(donor).read_bytes())
    dmc, _, _, dblock = parse_matp([c for t, v, c in dch if t == 'MATP'][0])
    _, dheads, dpays = split_region(dblock, dmc)
    _, ch = chunks(data)
    tags = [t for t, v, c in ch]
    assert tags == ['TEXB', 'MATP'], 'chunks %s' % tags
    mc, sh, gu, region = parse_matp([c for t, v, c in ch if t == 'MATP'][0])
    assert mc == n, 'materials %d != %d' % (mc, n)
    assert len(sh) == 1, 'shaders %d != 1' % len(sh)
    assert len(gu) == n, 'guids %d != %d' % (len(gu), n)
    hdr, heads, pays = split_region(region, n)
    assert hdr == len(dheads[0]), 'header %d != donor %d' % (hdr, len(dheads[0]))
    handles = []
    for i, pay in enumerate(pays):
        # the same check the 98 shipped bundles pass
        assert pay[64:72] == FF, 'payload %d has no unset-handle pair at +64' % i
        idx, gen = struct.unpack_from('<HH', pay, HANDLE)
        assert gen == 1, 'payload %d generation %d' % (i, gen)
        handles.append(idx)
        blk = bytearray(pay)
        struct.pack_into('<HH', blk, HANDLE, *struct.unpack_from('<HH', dpays[0], HANDLE))
        assert bytes(blk) == dpays[0], 'payload %d differs from the donor beyond its handle' % i
    assert heads[0] == dheads[0], 'material 0 lost the donor header'
    assert all(h == b'\0' * hdr for h in heads[1:]), 'trailing headers not zero'
    print('verified: %d x %d B header (grouped) + %d x %d B payload = %d, '
          'shader %s, handles %s'
          % (n, hdr, n, PAYLOAD, len(region), sh[0], handles))


def selftest():
    """Rebuilding the stock donor from itself must reproduce it byte for byte."""
    data, info = build(DONOR, DONOR, 1, tex_for=lambda i: None)
    orig = DONOR.read_bytes()
    _, a = chunks(data)
    _, b = chunks(orig)
    assert [t for t, v, c in a] == [t for t, v, c in b], 'chunk list differs'
    for (t1, v1, c1), (t2, v2, c2) in zip(a, b):
        assert c1 == c2, '%s chunk differs (%d vs %d B)' % (t1, len(c1), len(c2))
    assert data == orig, 'rebuild differs from the original (%d vs %d B)' % (len(data), len(orig))
    print('mtb-build selftest: stock out_intro2.mtb rebuilt byte-exactly (%d B)' % len(orig))

    # n=1 cannot tell a grouped region from an interleaved one -- both are
    # hdr+payload. That blind spot is what shipped a wrong bundle, so read a
    # real multi-material bundle back and check the layout on it.
    W = Path(r'D:\DI-Extract\3.0\assets\worlds')
    for w, want_mc in (('dor_bay', 2), ('dor_intro', 2), ('holocombat_cover', 2)):
        p = W / w / (w + '.mtb')
        if not p.exists():
            continue
        _, ch = chunks(p.read_bytes())
        mc, sh, gu, reg = parse_matp([c for t, v, c in ch if t == 'MATP'][0])
        assert mc == want_mc, '%s materials %d' % (w, mc)
        hdr, heads, pays = split_region(reg, mc)
        for i, pay in enumerate(pays):
            assert pay[64:72] == FF, '%s payload %d: ff-pair not at +64 -- layout wrong' % (w, i)
        assert heads[-1] == b'\0' * hdr, '%s last header not zero' % w
        print('  %s: %d x %d B header + %d x %d B payload, ff-pair at +64 in all'
              % (w, mc, hdr, mc, PAYLOAD))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--donor', default=str(DONOR))
    ap.add_argument('--target', default=str(TARGET))
    ap.add_argument('--materials', type=int, default=None)
    ap.add_argument('--out')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    if a.selftest:
        selftest()
        return
    if not a.out:
        sys.exit('--out is required')
    data, info = build(a.donor, a.target, a.materials)
    verify(data, a.donor, info['materials'])
    Path(a.out).write_bytes(data)
    print('wrote %s  %d B  (%d materials, %d textures, shader %s)'
          % (a.out, len(data), info['materials'], info['textures'], info['shader']))


if __name__ == '__main__':
    main()

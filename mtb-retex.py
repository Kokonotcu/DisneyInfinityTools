"""Repoint a WORKING bundle's textures at different .tbody files.

Merging payloads from several source bundles produces a file with the right
shape that still renders nothing -- sumo3c.mtb is 3 materials / 1 shader /
header 20 / region 404, exactly like the shipped tb_th_building_roof_thatched
that DOES render, and it is invisible. Something inside the payloads or the
per-material header hashes does not survive being mixed between bundles.

So do not synthesise. Take a bundle that is known to render, byte for byte, and
change only its TEXB entries -- an entry is 8 bytes of .tbody name hash plus 4
spare, and nothing else in the file refers to the texture except the handle
index, which keeps pointing at the same slot. The material parameters stay the
donor's; the artwork becomes ours.

  python mtb-retex.py --base <working.mtb> --out <new.mtb> <hash1> <hash2> ...
  python mtb-retex.py --base <working.mtb> --out <new.mtb> --from a.mtb b.mtb
"""
import argparse, struct
from pathlib import Path


def chunks(b):
    assert b[:4] == b'BNDL', 'not a BNDL'
    ver, flen, n = struct.unpack_from('<III', b, 4)
    assert flen == len(b), 'fileLength %d != %d' % (flen, len(b))
    out, p = [], 16
    for _ in range(n):
        tag, cver, clen = struct.unpack_from('<4sII', b, p)
        out.append((tag.decode('latin-1'), cver, p, clen))
        p += clen
    return out


def texb_entries(path):
    b = Path(path).read_bytes()
    for tag, cver, off, clen in chunks(b):
        if tag == 'TEXB':
            n = struct.unpack_from('<I', b, off + 12)[0]
            return [b[off + 20 + i * 12: off + 32 + i * 12] for i in range(n)]
    return []


def retex(base, out_path, hashes, tex_dir=None):
    b = bytearray(Path(base).read_bytes())
    slot = None
    for tag, cver, off, clen in chunks(bytes(b)):
        if tag == 'TEXB':
            slot = (off, struct.unpack_from('<I', b, off + 12)[0])
    assert slot, 'no TEXB in %s' % base
    off, n = slot
    assert len(hashes) <= n, 'have %d hashes, bundle has %d slots' % (len(hashes), n)
    print('%s: %d texture slots' % (Path(base).name, n))
    for i, h in enumerate(hashes):
        raw = bytes.fromhex(h) if isinstance(h, str) else h[:8]
        assert len(raw) == 8, 'texture name is 8 bytes, got %d' % len(raw)
        old = bytes(b[off + 20 + i * 12: off + 28 + i * 12])
        b[off + 20 + i * 12: off + 28 + i * 12] = raw
        print('   slot %d  %s -> %s' % (i, old.hex(), raw.hex()))
        if tex_dir and not (Path(tex_dir) / (raw.hex() + '.tbody')).exists():
            raise SystemExit('   MISSING on disk: %s.tbody' % raw.hex())
    Path(out_path).write_bytes(bytes(b))
    # the file length field and every chunk offset are untouched, so a reparse
    # of the result must still succeed and must be the same size as the base
    assert len(bytes(b)) == Path(base).stat().st_size, 'size changed'
    chunks(bytes(b))
    print('wrote %s  %d B (structure byte-identical to the base)'
          % (out_path, len(b)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--tex-dir', default=None, help='verify each .tbody exists here')
    ap.add_argument('--from', dest='src', nargs='*', default=None,
                    help='take texture hashes from these bundles, in order')
    ap.add_argument('hashes', nargs='*')
    a = ap.parse_args()
    hashes = list(a.hashes)
    if a.src:
        for s in a.src:
            for e in texb_entries(s):
                hashes.append(e[:8].hex())
    retex(a.base, a.out, hashes, a.tex_dir)


if __name__ == '__main__':
    main()

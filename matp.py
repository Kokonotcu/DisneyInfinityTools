""".mtb material bundle codec, built from the engine's own parser (Ghidra, DisneyInfinity3.exe):

BNDL  u32 ver(1)  u32 fileSize  u32 nChunks
TEXB  u32 ver(3)  u32 chunkSize u32 nTex  u32 0   nTex x (8-byte .tbody name hash, u32 0xffffffff)
MATP  u32 ver(2)  u32 chunkSize u32 nMat                          FUN_00472590
      align4 u32 nShaders, nShaders x NUL-terminated shader name   (FUN_0045c820 lookup)
      align4 u32 nMat,     nMat x NUL-terminated material name     (FUN_00459a90)
      align2 u16 shaderIndex[nMat]
      align4 u32 hash[nMat]                                        (FUN_00468500 material lookup)
      align16 per material: block A (shd u16 @0x6a bytes) + block B (shd u16 @0xa2 bytes)
      block B starts with shd[0xa1] resource slots of 8 B: u32 TEXB index (0xffffffff = unset), u32
All alignment is file-relative (the bundle loads at a 16-aligned address).

python matp.py <file.mtb>        dump
python matp.py --selftest        predict every shipped 3.0 bundle's layout from its shaders
"""
import struct, sys, os
from pathlib import Path

SHD_DIR = Path(r'D:\DI-Extract\3.0\assets\shaders\shaders')
SHD_DIR_10 = Path(r'D:\DI-Extract\1.0\assets\shaders\shaders')
# .shd offsets: u16 block A size, u16 block B size, u8 B texture-slot count; then slot stride.
# 3.0 from the exe; 1.0 fitted to 350/350 single-material bundles (same layout, 0x24 earlier)
# and its slots are a bare u32 index (3.0: u32 index + u32).
SHD_LAYOUT = {SHD_DIR: (0x6a, 0xa2, 0xa1, 8), SHD_DIR_10: (0x46, 0x6a, 0x69, 4)}
_shd = {}


def shader_sizes(name, shd_dir=None):
    """-> (blockA, blockB, nTexSlots) from the .shd, which the engine maps in place."""
    d = shd_dir or SHD_DIR
    if (d, name) not in _shd:
        b = (d / f'{name}.shd').read_bytes()
        oa, ob, os_, _ = SHD_LAYOUT[d]
        _shd[d, name] = (struct.unpack_from('<H', b, oa)[0], struct.unpack_from('<H', b, ob)[0], b[os_])
    return _shd[d, name]


def _al(x, a):
    return (x + a - 1) & ~(a - 1)


def _cstr(b, o):
    e = b.index(b'\0', o)
    return b[o:e].decode('latin-1'), e + 1


def material_names(data):
    """MATP material names in ordinal order, without sizing any block -- works on 1.0 bundles too."""
    b = bytes(data)
    n = struct.unpack_from('<I', b, 12)[0]
    o = 16
    for _ in range(n):
        if b[o:o + 4] == b'MATP':
            nmat = struct.unpack_from('<I', b, o + 12)[0]
            p = _al(o + 16, 4)
            nsh = struct.unpack_from('<I', b, p)[0]; p += 4
            for _ in range(nsh):
                _, p = _cstr(b, p)
            p = _al(p, 4)
            assert struct.unpack_from('<I', b, p)[0] == nmat; p += 4
            names = []
            for _ in range(nmat):
                s, p = _cstr(b, p); names.append(s)
            return names
        o += struct.unpack_from('<I', b, o + 8)[0]
    return []


class Bundle:
    def __init__(self, data, shd_dir=None):
        self.shd_dir = shd_dir
        b = self.raw = bytes(data)
        assert b[:4] == b'BNDL', 'not a bundle'
        self.ver, self.size, n = struct.unpack_from('<III', b, 4)
        self.textures, self.materials = [], []
        o = 16
        for _ in range(n):
            tag = b[o:o + 4]
            ver, csize = struct.unpack_from('<II', b, o + 4)
            if tag == b'TEXB':
                cnt = struct.unpack_from('<I', b, o + 12)[0]
                self.texb_ver = ver
                self.textures = [b[o + 20 + 12 * i:o + 28 + 12 * i] for i in range(cnt)]
            elif tag == b'MATP':
                self._matp(o, csize)
            o += csize
        self.end = o
        if not hasattr(self, 'matp_end'):
            self.matp_data_end = self.matp_end = None

    def _matp(self, o, csize):
        b = self.raw
        nmat = struct.unpack_from('<I', b, o + 12)[0]
        p = _al(o + 16, 4)
        nsh = struct.unpack_from('<I', b, p)[0]; p += 4
        shaders = []
        for _ in range(nsh):
            s, p = _cstr(b, p); shaders.append(s)
        p = _al(p, 4)
        assert struct.unpack_from('<I', b, p)[0] == nmat; p += 4
        names = []
        for _ in range(nmat):
            s, p = _cstr(b, p); names.append(s)
        p = _al(p, 2)
        idx = struct.unpack_from(f'<{nmat}H', b, p); p += 2 * nmat
        p = _al(p, 4)
        hashes = struct.unpack_from(f'<{nmat}I', b, p); p += 4 * nmat
        p = _al(p, 16)
        for i in range(nmat):
            sh = shaders[idx[i]]
            a, bb, slots = shader_sizes(sh, self.shd_dir)
            self.materials.append(dict(name=names[i], shader=sh, hash=hashes[i],
                                       A=b[p:p + a], B=b[p + a:p + a + bb], slots=slots,
                                       stride=SHD_LAYOUT[self.shd_dir or SHD_DIR][3]))
            p += a + bb
        self.matp_data_end, self.matp_end = p, o + csize

    def tex_slots(self, m):
        return [struct.unpack_from('<I', m['B'], m['stride'] * s)[0] for s in range(m['slots'])]


def build(textures, materials):
    """textures: [8-byte hash]; materials: dicts as produced by Bundle (A, B already reindexed)."""
    texb = struct.pack('<4sIII', b'TEXB', 3, 0, len(textures)) + struct.pack('<I', 0)
    texb += b''.join(t + b'\xff\xff\xff\xff' for t in textures)
    texb += b'\0' * (_al(len(texb), 16) - len(texb))
    texb = texb[:8] + struct.pack('<I', len(texb)) + texb[12:]

    shaders = list(dict.fromkeys(m['shader'] for m in materials))
    base = 16 + len(texb)  # MATP chunk's file offset, for file-relative alignment
    m = bytearray(struct.pack('<4sIII', b'MATP', 2, 0, len(materials)))

    def pad(a):
        m.extend(b'\0' * (_al(base + len(m), a) - base - len(m)))
    pad(4); m += struct.pack('<I', len(shaders))
    for s in shaders:
        m += s.encode() + b'\0'
    pad(4); m += struct.pack('<I', len(materials))
    for x in materials:
        m += x['name'].encode('latin-1') + b'\0'
    pad(2); m += struct.pack(f'<{len(materials)}H', *[shaders.index(x['shader']) for x in materials])
    pad(4); m += struct.pack(f'<{len(materials)}I', *[x['hash'] for x in materials])
    pad(16)
    for x in materials:
        a, bb, _ = shader_sizes(x['shader'])
        assert len(x['A']) == a and len(x['B']) == bb
        m += x['A'] + x['B']
    struct.pack_into('<I', m, 8, len(m))
    body = texb + bytes(m)
    return struct.pack('<4sIII', b'BNDL', 1, 16 + len(body), 2) + body


def merge(sources):
    """[(Bundle, material index)] -> bundle bytes. Copies each material's blocks verbatim and only
    rebases its texture-slot indices into the merged TEXB."""
    textures, mats = [], []
    for bnd, i in sources:
        m = dict(bnd.materials[i])
        B = bytearray(m['B'])
        for s, t in enumerate(bnd.tex_slots(m)):
            if t != 0xffffffff:
                h = bnd.textures[t]
                if h not in textures:
                    textures.append(h)
                struct.pack_into('<I', B, m['stride'] * s, textures.index(h))
        m['B'] = bytes(B)
        mats.append(m)
    return build(textures, mats)


def _check_one(f):
    """-> 'skip' | ('bad', detail) | ('ok', counted for rebuild?, rebuilt byte-exact?)"""
    try:
        bnd = Bundle(f.read_bytes())
    except (FileNotFoundError, AssertionError):
        return 'skip'                    # shader not in this extract / not a BNDL
    good = (bnd.end == len(bnd.raw) and bnd.matp_data_end == bnd.matp_end and
            all(t == 0xffffffff or t < len(bnd.textures) for m in bnd.materials for t in bnd.tex_slots(m)))
    if not good:
        return ('bad', (str(f), bnd.matp_data_end, bnd.matp_end, bnd.end, len(bnd.raw)))
    two = bnd.raw[12] == 2 and bool(bnd.textures) and bool(bnd.materials)   # the shape build() writes
    return ('ok', two, two and build(bnd.textures, bnd.materials) == bnd.raw)


def selftest():
    from par import pmap
    res = pmap(_check_one, Path(r'D:\DI-Extract\3.0\assets').rglob('*.mtb'), chunksize=64)
    skip = res.count('skip')
    bads = [r[1] for r in res if r != 'skip' and r[0] == 'bad']
    oks = [r for r in res if r != 'skip' and r[0] == 'ok']
    ok, bad = len(oks), len(bads)
    two, rebuilt = sum(r[1] for r in oks), sum(bool(r[2]) for r in oks)
    for b in bads[:5]:
        print('MISMATCH', *b)
    print(f'layout predicted exactly: {ok}   mismatched: {bad}   skipped: {skip}   '
          f'TEXB+MATP rebuilt byte-exact: {rebuilt}/{two}')
    assert bad == 0 and rebuilt == two


if __name__ == '__main__':
    if sys.argv[1:] == ['--selftest']:
        selftest()
    else:
        bnd = Bundle(open(sys.argv[1], 'rb').read())
        print(f'{len(bnd.textures)} textures, {len(bnd.materials)} materials')
        for i, t in enumerate(bnd.textures):
            print(f'  tex {i}: {t.hex()}')
        for m in bnd.materials:
            print(f"  {m['name'] or '(no name)':40} shader {m['shader'][:8]} hash {m['hash']:08x} "
                  f"A{len(m['A'])} B{len(m['B'])} tex {bnd.tex_slots(m)}")

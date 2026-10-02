"""For each pass of a .shd: declared vertex format vs the vertex shader's DXBC input signature.

record (80 B from 0xF8): +0 vformatCRC, +0x10 VS file id (<8hex>.vp), +0x18 PS file id (<8hex>.fp)
A semantic the VS reads that the format lacks -> CreateInputLayout throws (layout builder FUN_0044d430).

python shdfmt.py <shd> [<shd> ...]
"""
import struct, sys, os
from vtxf import parse, name_hash, elements

DIR = r'D:\DI-Extract\3.0\assets\shaders\shaders'
VTXF = os.path.join(DIR, 'vformat.vtxf')


def isgn(path):
    """-> [(semantic+index, sysval)] from the DXBC ISGN chunk."""
    b = open(path, 'rb').read()
    d = b.find(b'DXBC')
    assert d >= 0, path
    n = struct.unpack_from('<I', b, d + 28)[0]
    for i in range(n):
        co = d + struct.unpack_from('<I', b, d + 32 + 4 * i)[0]
        if b[co:co + 4] == b'ISGN':
            base = co + 8
            cnt = struct.unpack_from('<I', b, base)[0]
            out = []
            for e in range(cnt):
                no, idx, sv = struct.unpack_from('<III', b, base + 8 + 24 * e)
                name = b[base + no:b.index(b'\0', base + no)].decode()
                out.append((f'{name}{idx}', sv))
            return out
    raise ValueError('no ISGN in ' + path)


def norm(sem):
    return sem if sem[-1].isdigit() else sem + '0'


if __name__ == '__main__':
    fmts = {name_hash(g): f for g, f in parse(VTXF).items()}
    for path in sys.argv[1:]:
        b = open(path, 'rb').read()
        print(os.path.basename(path))
        for off in range(0xF8, len(b) - 79, 80):
            crc, vs = struct.unpack_from('<I', b, off)[0], struct.unpack_from('<I', b, off + 0x10)[0]
            vp = os.path.join(DIR, f'{vs:08x}.vp')
            if crc not in fmts or not os.path.exists(vp):
                continue
            have = {norm(sem) for _, sem, _, _ in elements(fmts[crc])}
            need = [s for s, sv in isgn(vp) if sv == 0]  # sysval 0 = fed by the input layout
            missing = [s for s in need if s not in have]
            print(f'  @{off:#x} vfmt {crc:08x} vs {vs:08x}.vp  needs {need}  MISSING {missing or "-"}')

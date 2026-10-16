"""3.0 .shd shaders: passes, the vertex shader's inputs, and what each material texture slot feeds.

.shd: +0xa1 u8 block B texture-slot count (matp), +0xcc PS stage (low nibble = first t-register, high nibble =
count read; slotcheck), pass records 80 B from 0xF8: +0 vformatCRC, +0x10 VS file id (<8hex>.vp), +0x18 PS file id
(<8hex>.fp). The .vp/.fp are DXBC with their reflection kept: ISGN (VS inputs) and RDEF (bound resources).
  - A semantic the VS reads that the format lacks -> CreateInputLayout throws (layout builder FUN_0044d430).
  - Slot roles: slot i feeds t(first + i) (first = 5 in 821 of 836 shaders); a pass samples a subset, so roles
    are the union over passes -- 834 of 836 shaders name each slot at most once. Over every shipped 3.0 bundle,
    material roles are always filled (diffuse0 14,621 of 14,621, normalMap0 12,976, emissive, specular0 ...)
    and engine-bound ones never (dither_pattern 0 of 11,825, shadow, colorBuffer, normalGlossCopy, bink_*).

python shdfmt.py <shd> [<shd> ...] [--ext30 DIR]   passes vs VS inputs, and slot roles
                                                   (a bare shader name is looked up in the 3.0 extract)
"""
import struct, os
from shd import chunk
from vtxf import formats, name_hash, elements


def _cstr(b, o):
    return b[o:b.index(b'\0', o)].decode()


def isgn(path):
    """-> [(semantic+index, sysval)] from the DXBC ISGN chunk."""
    b = open(path, 'rb').read()
    base = chunk(b, b'ISGN')
    if base is None:
        raise ValueError('no ISGN in ' + str(path))
    out = []
    for e in range(struct.unpack_from('<I', b, base)[0]):
        no, idx, sv = struct.unpack_from('<III', b, base + 8 + 24 * e)
        out.append((f'{_cstr(b, base + no)}{idx}', sv))
    return out


def textures(path):
    """{t-register: name} of the textures a DXBC shader binds (RDEF; resource type 2 = texture)."""
    b = open(path, 'rb').read()
    base = chunk(b, b'RDEF')
    if base is None:
        return {}
    n, off = struct.unpack_from('<II', b, base + 8)
    out = {}
    for r in range(n):
        no, typ, _, _, _, bind, _, _ = struct.unpack_from('<8I', b, base + off + 32 * r)
        if typ == 2:
            out[bind] = _cstr(b, base + no)
    return out


def passes(b):
    """[(vformatCRC, VS id, PS id)] of a .shd's bytes."""
    return [(struct.unpack_from('<I', b, o)[0], struct.unpack_from('<I', b, o + 0x10)[0],
             struct.unpack_from('<I', b, o + 0x18)[0]) for o in range(0xF8, len(b) - 79, 80)]


def shd_path(shader):
    import paths
    return paths.ext30('shaders', 'shaders', f'{shader}.shd')


_roles = {}


def slot_roles(shader):
    """Role of each block-B texture slot of a 3.0 shader ('diffuse0', 'normalMap0', 'multi_blendMap', 'shadow', ...;
    the g_ prefix and _texture suffix dropped), None where no pass samples the slot."""
    if shader not in _roles:
        p = shd_path(shader)
        b = p.read_bytes()
        first, tex = b[0xcc] & 0xf, {}
        for _, _, ps in passes(b):
            fp = p.with_name(f'{ps:08x}.fp')
            if fp.exists():
                for reg, name in textures(fp).items():
                    tex.setdefault(reg, name)
        clean = lambda n: n[2:] if n.startswith('g_') else n
        _roles[shader] = [clean(tex[first + i]).removesuffix('_texture') if first + i in tex else None
                          for i in range(b[0xa1])]
    return _roles[shader]


def norm(sem):
    return sem if sem[-1].isdigit() else sem + '0'


if __name__ == '__main__':
    import argparse, paths
    ap = argparse.ArgumentParser(description='.shd passes: vertex format vs the VS input signature; slot roles')
    ap.add_argument('shd', nargs='+')
    paths.add_args(ap, 'ext30')
    a = paths.use(ap.parse_args())
    DIR = str(paths.ext30('shaders', 'shaders'))
    fmts = {name_hash(g): f for g, f in formats(3).items()}
    for path in a.shd:
        path = path if os.path.exists(path) else os.path.join(DIR, path if path.endswith('.shd') else path + '.shd')
        b = open(path, 'rb').read()
        print(os.path.basename(path), ' slots:', slot_roles(os.path.basename(path)[:-4]))
        for crc, vs, _ in passes(b):
            vp = os.path.join(DIR, f'{vs:08x}.vp')
            if crc not in fmts or not os.path.exists(vp):
                continue
            have = {norm(sem) for _, sem, _, _ in elements(fmts[crc])}
            need = [s for s, sv in isgn(vp) if sv == 0]  # sysval 0 = fed by the input layout
            missing = [s for s in need if s not in have]
            print(f'  vfmt {crc:08x} vs {vs:08x}.vp  needs {need}  MISSING {missing or "-"}')

"""Build a 3.0 world bundle from a 1.0 world's OWN materials, found by GUID in 3.0's shipped bundles.

The 1.0 .mtb lists its materials' GUIDs in MaterialPool ordinal order (matp.material_names).
3.0 ships many as standalone bundles under assets/materials, many more inside other bundles. Each one is copied verbatim
and only its texture indices are rebased (matp.merge).

Each material is checked: GUID found in 3.0; its shader declares the mesh's (remapped) vertex
format; every .tbody exists in the install.

python autobundle.py <1.0 world dir> [--out x.mtb]      resolve one world
python autobundle.py --survey                         coverage over every 1.0 world
"""
import json, struct, sys, os
from pathlib import Path
import matp, vtxf

MAT30 = Path(r'D:\DI-Extract\3.0\assets\materials')
WORLDS10 = Path(r'D:\DI-Extract\1.0\assets\worlds')
TEX = Path(r'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition\assets\textures')
CACHE = Path(__file__).with_name('matindex.json')


def index():
    """GUID -> [bundle path, material ordinal] over every 3.0 bundle, standalone material bundles
    first (cached). Same GUID = the same material, so any shipped copy is copied verbatim."""
    if CACHE.exists():
        return json.loads(CACHE.read_text())
    idx = {}
    standalone = sorted(MAT30.rglob('*.mtb'))
    rest = sorted(set(MAT30.parent.rglob('*.mtb')) - set(standalone))
    for f in standalone + rest:
        try:
            for i, g in enumerate(matp.material_names(f.read_bytes())):
                if g:
                    idx.setdefault(g, [str(f), i])
        except Exception:
            pass
    CACHE.write_text(json.dumps(idx))
    return idx


def shader_vformats(shader):
    """vformatCRCs a .shd declares a pass for (records from 0xF8, 80 B, CRC first)."""
    b = (matp.SHD_DIR / f'{shader}.shd').read_bytes()
    return {struct.unpack_from('<I', b, o)[0] for o in range(0xF8, len(b) - 79, 80)}


def prim_pairs(world_dir):
    """[(MaterialReference, 1.0 vformatCRC)] per primitive, in record order."""
    from oct import Oct
    stem = Path(world_dir).name
    o = Oct.load(str(Path(world_dir) / f'{stem}.oct'))
    pairs, ref = [], None
    for r in o.recs:
        if r.name == 'MaterialReference':
            ref = r.ints()[0]
        elif r.name == 'vformatCRC':
            pairs.append((ref, r.ints()[0] & 0xffffffff))
    return pairs


def resolve(world_dir):
    """-> (bundle bytes or None, [problem strings], {1.0 vformatCRC: 3.0 vformatCRC})"""
    stem = Path(world_dir).name
    guids = matp.material_names((Path(world_dir) / f'{stem}.mtb').read_bytes())
    idx = index()
    srcs, problems, shaders = [], [], {}
    for k, g in enumerate(guids):
        if g not in idx:
            problems.append(f'{g}: not shipped in 3.0'); continue
        path, i = idx[g]
        bnd = matp.Bundle(Path(path).read_bytes())
        m = bnd.materials[i]
        shaders[k] = shader_vformats(m['shader'])
        missing = [bnd.textures[t].hex() for t in bnd.tex_slots(m)
                   if t != 0xffffffff and not (TEX / f'{bnd.textures[t].hex()}.tbody').exists()]
        if missing:
            problems.append(f'{g}: missing textures {missing}')
        srcs.append((bnd, i))
    # each 1.0 format -> the 3.0 twin every material drawn with it accepts
    users = {}
    for ref, crc in prim_pairs(world_dir):
        users.setdefault(crc, set()).add(ref)
    vmap = {}
    for crc, refs in users.items():
        cands = vtxf.candidates_1to3(crc)
        if not cands:
            problems.append(f'vformat {crc:08x}: no 3.0 twin (needs vertex conversion)'); continue
        ok = [c for c in cands if all(c in shaders.get(r, {c}) for r in refs)]
        if not ok:
            problems.append(f'vformat {crc:08x}: no twin {[hex(c) for c in cands]} accepted by materials {sorted(refs)}')
        else:
            vmap[crc] = ok[0]
    return (matp.merge(srcs) if not problems else None), problems, vmap


def survey():
    ok = 0; rows = []
    for d in sorted(p for p in WORLDS10.iterdir() if (p / f'{p.name}.mtb').exists()):
        try:
            out, probs, _ = resolve(d)
            n = len(matp.material_names((d / f'{d.name}.mtb').read_bytes()))
        except Exception as e:
            out, probs, n = None, [f'error: {e}'], 0
        ok += out is not None
        rows.append((d.name, n, len(probs), probs[:1]))
    for name, n, bad, first in rows:
        print(f'{"OK " if not bad else "-- "} {name:45} mats {n:4}  problems {bad:4}  {first[0][:90] if first else ""}')
    print(f'{ok} of {len(rows)} 1.0 worlds resolve completely to their own 3.0 materials')


if __name__ == '__main__':
    if sys.argv[1:] == ['--survey']:
        survey()
    else:
        out, probs, vmap = resolve(sys.argv[1])
        print('  vformat', {f'{a:08x}': f'{b:08x}' for a, b in vmap.items()})
        for p in probs:
            print(' ', p)
        if out and '--out' in sys.argv:
            Path(sys.argv[sys.argv.index('--out') + 1]).write_bytes(out)
        print('resolved' if out else 'NOT resolved', len(out or b''), 'B')

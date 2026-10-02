"""Build a 3.0 world bundle for a 1.0 world, material by material, most faithful option first:

  real       the same material GUID ships in 3.0 -> copied verbatim (matp.merge)
  same-sh    3.0 has a material using the SAME logical shader -> its blocks, the 1.0 material's own
             textures put into its slots (same shader = same slot meaning)
  generic    shader absent from 3.0 -> a common 3.0 material whose shader accepts the primitive's
             vertex format, with the 1.0 material's textures in slot order

Substitutes keep the 1.0 material's GUID and lookup hash: the engine caches materials by hash
(FUN_00468500), so a donor's hash could hand back the donor's already-loaded material.

Per primitive the material's shader must declare the remapped vertex format; between identical
3.0 vformat twins the one the materials accept is chosen. 1.0 textures not yet in the 3.0 pool
are returned for copying from the world's textures/ folder.

python autobundle.py <1.0 world dir> [--out x.mtb]      resolve one world
python autobundle.py --survey                         coverage over every 1.0 world
"""
import json, struct, sys
from collections import Counter
from pathlib import Path
from types import SimpleNamespace
import matp, vtxf

MAT30 = Path(r'D:\DI-Extract\3.0\assets\materials')
WORLDS10 = Path(r'D:\DI-Extract\1.0\assets\worlds')
TEX = Path(r'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition\assets\textures')
CACHE = Path(__file__).with_name('matindex.json')
DONORS = Path(__file__).with_name('donors.json')
UNSET = 0xffffffff


def _bundles():
    standalone = sorted(MAT30.rglob('*.mtb'))
    return standalone + sorted(set(MAT30.parent.rglob('*.mtb')) - set(standalone))


def index():
    """GUID -> [bundle path, material ordinal] over every 3.0 bundle, standalone material bundles
    first (cached). Same GUID = the same material, so any shipped copy is copied verbatim."""
    if CACHE.exists():
        return json.loads(CACHE.read_text())
    idx = {}
    for f in _bundles():
        try:
            for i, g in enumerate(matp.material_names(f.read_bytes())):
                if g:
                    idx.setdefault(g, [str(f), i])
        except Exception:
            pass
    CACHE.write_text(json.dumps(idx))
    return idx


def donors():
    """3.0 shader -> [bundle path, material ordinal, number of shipped materials using it] (cached)."""
    if DONORS.exists():
        return json.loads(DONORS.read_text())
    d, n = {}, Counter()
    for f in _bundles():
        try:
            b = matp.Bundle(f.read_bytes())
        except Exception:
            continue
        for i, m in enumerate(b.materials):
            n[m['shader']] += 1
            d.setdefault(m['shader'], [str(f), i])
    d = {s: v + [n[s]] for s, v in d.items()}
    DONORS.write_text(json.dumps(d))
    return d


_svf = {}


def shader_vformats(shader):
    """vformatCRCs a 3.0 .shd declares a pass for (records from 0xF8, 80 B, CRC first)."""
    if shader not in _svf:
        b = (matp.SHD_DIR / f'{shader}.shd').read_bytes()
        _svf[shader] = {struct.unpack_from('<I', b, o)[0] for o in range(0xF8, len(b) - 79, 80)}
    return _svf[shader]


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


def _substitute(donor_path, di, m10, b10):
    """Donor's blocks + the 1.0 material's own textures, as a one-material bundle-like source."""
    bnd = matp.Bundle(Path(donor_path).read_bytes())
    dm = bnd.materials[di]
    used = [t for t in b10.tex_slots(m10)]
    if dm['shader'] != m10['shader']:          # generic: textures in order, unset slots dropped
        used = [t for t in used if t != UNSET]
    B = bytearray(dm['B'])
    for s in range(dm['slots']):
        struct.pack_into('<I', B, dm['stride'] * s, used[s] if s < len(used) else UNSET)
    # Block A = the material's parameters (first float4 = tint). Over 421 GUIDs shipped in both games
    # with the same shader, 3.0's A is 1.0's A followed by new trailing params in 401; across a
    # shader change the first float4 still matches in 85%. B's data after the slots (samplers)
    # changed layout between games in every pair, so the donor's is kept.
    a10, a = m10['A'], dm['A']
    if dm['shader'] == m10['shader'] and len(a10) <= len(a):
        a = a10 + a[len(a10):]
    elif len(a10) >= 16 and len(a) >= 16:
        a = a10[:16] + a[16:]
    m = dict(dm, A=a, B=bytes(B), name=m10['name'], hash=m10['hash'])
    return SimpleNamespace(textures=b10.textures, materials=[m],
                           tex_slots=lambda mm: matp.Bundle.tex_slots(None, mm)), 0


def resolve(world_dir):
    """-> (bundle bytes or None, problems, vformat map, tiers per material, .tbody files to copy)"""
    world_dir = Path(world_dir)
    stem = world_dir.name
    b10 = matp.Bundle((world_dir / f'{stem}.mtb').read_bytes(), matp.SHD_DIR_10)
    idx, don = index(), donors()
    users, drawn = {}, {}
    for ref, crc in prim_pairs(world_dir):
        users.setdefault(crc, set()).add(ref)
        drawn.setdefault(ref, set()).add(crc)
    problems, tiers, srcs, shaders, tocopy = [], [], [], {}, set()
    for k, m10 in enumerate(b10.materials):
        g = m10['name']
        if g in idx:
            path, i = idx[g]
            bnd = matp.Bundle(Path(path).read_bytes()); src = (bnd, i); tier = 'real'
        else:
            if m10['shader'] in don:
                path, i, _ = don[m10['shader']]; tier = 'same-sh'
            else:   # most common 3.0 shader accepting a twin of every format this material is drawn with
                need = [set(vtxf.candidates_1to3(c)) for c in drawn.get(k, ())]
                ok = [(n, s, p, i) for s, (p, i, n) in don.items()
                      if all(shader_vformats(s) & c for c in need) and matp.shader_sizes(s)[2] >= 1]
                if not ok:
                    problems.append(f'{g}: no 3.0 shader accepts its vertex format'); continue
                _, _, path, i = max(ok); tier = 'generic'
            src = _substitute(path, i, m10, b10)
            for t in b10.tex_slots(m10):
                if t != UNSET and not (TEX / f'{b10.textures[t].hex()}.tbody').exists():
                    f = world_dir / 'textures' / f'{b10.textures[t].hex()}.tbody'
                    if f.exists():
                        tocopy.add(f)
                    else:
                        problems.append(f'{g}: texture {b10.textures[t].hex()} nowhere')
        bnd, i = src
        m = bnd.materials[i]
        shaders[k] = shader_vformats(m['shader'])
        if tier == 'real':
            missing = [bnd.textures[t].hex() for t in bnd.tex_slots(m)
                       if t != UNSET and not (TEX / f'{bnd.textures[t].hex()}.tbody').exists()]
            if missing:
                problems.append(f'{g}: missing textures {missing}')
        srcs.append(src); tiers.append(tier)
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
    out = matp.merge(srcs) if not problems else None
    return out, problems, vmap, tiers, sorted(tocopy)


def survey():
    ok = 0; rows = []; tc = Counter(); pc = Counter()
    for d in sorted(p for p in WORLDS10.iterdir() if (p / f'{p.name}.mtb').exists()):
        try:
            out, probs, _, tiers, _ = resolve(d)
        except Exception as e:
            out, probs, tiers = None, [f'error: {e}'], []
        ok += out is not None and bool(tiers)
        tc.update(tiers)
        pc.update('no twin' if 'twin' in p else 'error' if p.startswith('error') else 'other' for p in probs)
        rows.append((d.name, Counter(tiers), probs[:1]))
    for name, t, first in rows:
        print(f'{"OK " if not first else "-- "} {name:45} {dict(t)}  {first[0][:80] if first else ""}')
    print(f'material tiers: {dict(tc)}   problems: {dict(pc)}')
    print(f'{ok} of {len(rows)} 1.0 worlds (with materials) resolve completely')


if __name__ == '__main__':
    if sys.argv[1:] == ['--survey']:
        survey()
    else:
        out, probs, vmap, tiers, tocopy = resolve(sys.argv[1])
        print('  vformat', {f'{a:08x}': f'{b:08x}' for a, b in vmap.items()})
        print('  tiers  ', dict(Counter(tiers)), ' textures to copy:', len(tocopy))
        for p in probs:
            print(' ', p)
        if out and '--out' in sys.argv:
            Path(sys.argv[sys.argv.index('--out') + 1]).write_bytes(out)
        print('resolved' if out else 'NOT resolved', len(out or b''), 'B')

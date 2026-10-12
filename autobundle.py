"""Build a 3.0 world bundle for a 1.0 world, material by material, most faithful option first:

  real       the same material GUID ships in 3.0 -> copied verbatim (matp.merge)
  same-sh    3.0 has a material using the SAME logical shader -> its blocks, the 1.0 material's own
             textures put into its slots (same shader = same slot meaning)
  generic    shader absent from 3.0 -> a common 3.0 material whose shader accepts the primitive's
             vertex format, with the 1.0 material's textures in slot order -- into the slots whose role
             shipped materials fill (diffuse, normal, emissive ...), never the engine-bound ones (shadow,
             dither, colorBuffer ...: filled by no shipped material; shdfmt.slot_roles)

Substitutes keep the 1.0 material's GUID and lookup hash: the engine caches materials by hash
(FUN_00468500), so a donor's hash could hand back the donor's already-loaded material.

Per primitive the material's shader must declare the remapped vertex format; between identical
3.0 vformat twins the one the materials accept is chosen. 1.0 textures not yet in the 3.0 pool
are returned for copying (from the world's textures/ folder, else wherever an extract holds them) unless
3.0 ships them in its global pool (textures/<first byte>.zip).

python autobundle.py <1.0 world dir> [--out x.mtb]      resolve one world
python autobundle.py --survey                         coverage over every 1.0 world
(both take --ext10 / --ext30; the corpus indexes are cached next to this file, paths relative to the extract)
"""
import struct
from collections import Counter
from pathlib import Path
from types import SimpleNamespace
import matp, paths, shdfmt, vtxf

CACHE_VERSION = 2      # 1 stored absolute paths of one machine's extract
UNSET = 0xffffffff
# generic tier: 1.0 slots to use, in 3.0 (colour, normal, ...) order, for 1.0 shaders that are not
# colour-first. d0e71afc = blend mask, colour A, normal, colour B, normal: in order, the mask became the
# colour (Metroville's black/white mottled walls).
# ponytail: one hand-made entry; the NRM-first shaders (030a6710, 255f3180, 1fe46d84, 01ae42b4) need one too if seen
GENERIC_SLOTS = {'d0e71afc8ef520e49044db932c324993': [1, 2]}
# GUIDs that ship in 3.0 but only on a shader that draws nothing in our worlds, so are substituted instead.
# i_buildingrooftop's 3.0 copy is on debris shader a3399c16 (groundpounddebris2): Metroville roofs invisible.
NOT_REAL = {'66780fdf-5a90-4c77-9d67-0f326bc80cdc'}


def _bundles():
    standalone = sorted(paths.ext30('materials').rglob('*.mtb'))
    return standalone + sorted(set(paths.ext30().rglob('*.mtb')) - set(standalone))


def _rel(f, game=3):
    return '%d:%s' % (game, Path(f).relative_to(paths.ext30() if game == 3 else paths.ext10()).as_posix())


def _abs(r):
    """'3:materials/x/y.mtb' -> that file in the current 3.0 (or 1.0) extract."""
    game, rel = r.split(':', 1)
    return (paths.ext30 if game == '3' else paths.ext10)(rel)




def _names_of(f):
    try:
        return f, matp.material_names(f.read_bytes())
    except Exception:
        return f, []


def _shaders_of(f):
    try:
        return f, [m['shader'] for m in matp.Bundle(f.read_bytes()).materials]
    except Exception:
        return f, []


def index():
    """GUID -> [bundle, material ordinal] over every 3.0 bundle, standalone material bundles
    first (cached). Same GUID = the same material, so any shipped copy is copied verbatim."""
    def build():
        from par import pmap
        idx = {}
        for f, names in pmap(_names_of, _bundles(), chunksize=64):   # in order: standalone first
            for i, g in enumerate(names):
                if g:
                    idx.setdefault(g, [_rel(f), i])
        return idx
    return paths.cached('matindex.json', build, CACHE_VERSION)


def donors():
    """3.0 shader -> [bundle, material ordinal, number of shipped materials using it] (cached)."""
    def build():
        from par import pmap
        d, n = {}, Counter()
        for f, shaders in pmap(_shaders_of, _bundles(), chunksize=64):
            for i, sh in enumerate(shaders):
                n[sh] += 1
                d.setdefault(sh, [_rel(f), i])
        return {s: v + [n[s]] for s, v in d.items()}
    return paths.cached('donors.json', build, CACHE_VERSION)


def tex_index():
    """.tbody hash -> a file holding it, over both extracts (3.0 first; cached). Many assets keep
    their textures in their own textures/ folder instead of the global pool."""
    def build():
        idx = {}
        for game, root in ((3, paths.ext30()), (1, paths.ext10())):
            for f in root.rglob('*.tbody'):
                idx.setdefault(f.stem, _rel(f, game))
        return idx
    return paths.cached('texindex.json', build, CACHE_VERSION)


def in_pool(h):
    """3.0 ships texture h in its global pool (textures/<h[:2]>.zip), where every world finds it."""
    return paths.ext30('textures', h[:2], 'textures', f'{h}.tbody').exists()


def _need(h, world_dir, tocopy, problems, g):
    """Make sure texture h will be in the pool: shipped there, or queue a copy."""
    if in_pool(h):
        return
    f = world_dir / 'textures' / f'{h}.tbody'
    if not f.exists():
        r = tex_index().get(h)
        f = _abs(r) if r else None
    if f:
        tocopy.add(f)
    else:
        problems.append(f'{g}: texture {h} nowhere')


_svf = {}


def shader_vformats(shader):
    """vformatCRCs a 3.0 .shd declares a pass for."""
    if shader not in _svf:
        _svf[shader] = {crc for crc, _, _ in shdfmt.passes(shdfmt.shd_path(shader).read_bytes())}
    return _svf[shader]


def _roles_filled(f):
    try:
        bnd = matp.Bundle(f.read_bytes())
    except Exception:
        return []
    return [(r, t != UNSET) for m in bnd.materials for r, t in zip(shdfmt.slot_roles(m['shader']), bnd.tex_slots(m))]


def filled_roles():
    """Slot roles some shipped 3.0 material fills with a texture (cached). The others are engine-bound."""
    def build():
        from par import pmap
        return sorted({r for res in pmap(_roles_filled, _bundles(), chunksize=64) for r, f in res if f and r})
    return set(paths.cached('filledroles.json', build))


def prim_pairs(world_dir, o=None):
    """[(MaterialReference, 1.0 vformatCRC)] per primitive, in record order."""
    from oct import Oct
    stem = Path(world_dir).name
    o = o or Oct.load(str(Path(world_dir) / f'{stem}.oct'))
    pairs, ref = [], None
    for r in o.recs:
        if r.name == 'MaterialReference':
            ref = r.ints()[0]
        elif r.name == 'vformatCRC':
            pairs.append((ref, r.ints()[0] & 0xffffffff))
    return pairs


def _substitute(donor_path, di, m10, b10):
    """Donor's blocks + the 1.0 material's own textures, as a one-material bundle-like source."""
    bnd = matp.Bundle(_abs(donor_path).read_bytes())
    dm = bnd.materials[di]
    used = [t for t in b10.tex_slots(m10)]
    if dm['shader'] != m10['shader']:          # generic: textures in order, unset slots dropped,
        if m10['shader'] in GENERIC_SLOTS:     # into the donor slots of material roles only
            used = [used[i] for i in GENERIC_SLOTS[m10['shader']]]
        used = [t for t in used if t != UNSET]
        fill = filled_roles()
        targets = [s for s, r in enumerate(shdfmt.slot_roles(dm['shader'])) if r in fill]
        used = [used[targets.index(s)] if s in targets and targets.index(s) < len(used) else UNSET
                for s in range(dm['slots'])]
    B = bytearray(dm['B'])
    # whole 8-B slot as shipped: (index, 0) or (UNSET, UNSET). Writing only the index left the
    # donor's second word: (i, ffffffff) / (ffffffff, 0) never ship and crashed Metroville.
    for s in range(dm['slots']):
        t = used[s] if s < len(used) else UNSET
        struct.pack_into('<II', B, dm['stride'] * s, t, UNSET if t == UNSET else 0)
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


def resolve(world_dir, o=None):
    """-> (bundle bytes or None, problems, vformat map, tiers per material, .tbody files to copy)"""
    world_dir = Path(world_dir)
    stem = world_dir.name
    b10 = matp.Bundle((world_dir / f'{stem}.mtb').read_bytes(), 1)
    idx, don = index(), donors()
    users, drawn = {}, {}
    for ref, crc in prim_pairs(world_dir, o):
        users.setdefault(crc, set()).add(ref)
        drawn.setdefault(ref, set()).add(crc)
    problems, tiers, srcs, shaders, tocopy = [], [], [], {}, set()
    for k, m10 in enumerate(b10.materials):
        g = m10['name']
        if g in idx and g not in NOT_REAL:
            path, i = idx[g]
            bnd = matp.Bundle(_abs(path).read_bytes()); src = (bnd, i); tier = 'real'
        else:
            if m10['shader'] in don:
                path, i, _ = don[m10['shader']]; tier = 'same-sh'
            else:   # most common 3.0 shader accepting a twin of every format this material is drawn with
                need = [{h for h, _, _ in vtxf.targets_1to3(c)} for c in drawn.get(k, ())]
                ok = [(n, s, p, i) for s, (p, i, n) in don.items()
                      if all(shader_vformats(s) & c for c in need) and matp.shader_sizes(s)[2] >= 1]
                if not ok:
                    problems.append(f'{g}: no 3.0 shader accepts its vertex format'); continue
                _, _, path, i = max(ok); tier = 'generic'
            src = _substitute(path, i, m10, b10)
            for t in b10.tex_slots(m10):
                if t != UNSET:
                    _need(b10.textures[t].hex(), world_dir, tocopy, problems, g)
        bnd, i = src
        m = bnd.materials[i]
        shaders[k] = shader_vformats(m['shader'])
        if tier == 'real':
            for t in bnd.tex_slots(m):
                if t != UNSET:
                    _need(bnd.textures[t].hex(), world_dir, tocopy, problems, g)
        srcs.append(src); tiers.append(tier)
    # (material, 1.0 vformatCRC) -> (3.0 vformatCRC, dropped, padded): the best target THIS material's
    # shader accepts -- twin first, else a conversion. Per pair, because 3.0 nodes mix vertex formats
    # freely (2,420 shipped nodes do) and one 1.0 format is drawn by materials with different shaders.
    vmap = {}
    for crc, refs in users.items():
        targets = vtxf.targets_1to3(crc)
        for r in refs:
            ok = [t for t in targets if t[0] in shaders.get(r, {t[0]})]
            if not ok:
                problems.append(f'vformat {crc:08x}: no 3.0 format accepted by material {r}')
            else:
                vmap[r, crc] = ok[0]
    out = matp.merge(srcs) if not problems else None
    return out, problems, vmap, tiers, sorted(tocopy)


def _survey_one(d):
    try:
        out, probs, _, tiers, _ = resolve(d)
    except Exception as e:
        out, probs, tiers = None, [f'error: {e}'], []
    return d.name, out is not None and bool(tiers), tiers, probs


def survey():
    from par import pmap
    index(); donors(); tex_index()          # build caches once, before the workers read them
    ok = 0; rows = []; tc = Counter(); pc = Counter()
    worlds = sorted(p for p in paths.ext10('worlds').iterdir() if (p / f'{p.name}.mtb').exists())
    for name, good, tiers, probs in pmap(_survey_one, worlds, chunksize=2):
        ok += good
        tc.update(tiers)
        pc.update('no twin' if 'twin' in p else 'error' if p.startswith('error') else 'other' for p in probs)
        rows.append((name, Counter(tiers), probs[:1]))
    for name, t, first in rows:
        print(f'{"OK " if not first else "-- "} {name:45} {dict(t)}  {first[0][:80] if first else ""}')
    print(f'material tiers: {dict(tc)}   problems: {dict(pc)}')
    print(f'{ok} of {len(rows)} 1.0 worlds (with materials) resolve completely')


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description='Build a 3.0 world bundle for a 1.0 world')
    ap.add_argument('world', nargs='?', help='1.0 world folder')
    ap.add_argument('--out', help='write the bundle here')
    ap.add_argument('--survey', action='store_true', help='coverage over every 1.0 world')
    paths.add_args(ap, 'ext30', 'ext10')
    a = paths.use(ap.parse_args())
    if a.survey:
        survey()
    elif not a.world:
        ap.print_help()
    else:
        out, probs, vmap, tiers, tocopy = resolve(a.world)
        from collections import Counter as _C
        print('  vformat', dict(_C(f'{c:08x}->{t[0]:08x}' + (f' drop {t[1]}' if t[1] else '') + (f' pad {t[2]}' if t[2] else '') for (r, c), t in vmap.items())))
        print('  tiers  ', dict(Counter(tiers)), ' textures to copy:', len(tocopy))
        for p in probs:
            print(' ', p)
        if out and a.out:
            Path(a.out).write_bytes(out)
        print('resolved' if out else 'NOT resolved', len(out or b''), 'B')

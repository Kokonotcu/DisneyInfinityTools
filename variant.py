"""One-variable variants of the INSTALLED world .oct, for bisecting a crash by launches.
  python variant.py --visible 0:1000      hide every geometry node outside [a:b) (by node order)
  python variant.py --rendercaps          Primitive RenderCaps 1.0 0x1000|low -> 3.0 0x200|low
  python variant.py --materials 178       keep the first N materials (.oct + .mtb), refs >= N -> ref % N
  python variant.py --restore             put the pre-variant .oct/.mtb back
Always starts from the backup, so variants never stack."""
import argparse, shutil
from pathlib import Path
from oct import Oct

OCT = Path(r'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition\assets\worlds\out_intro2\out_intro2.oct')
BAK = Path(r'D:\DI-Extract\_backup\variant-base.oct')
MTB, MBAK = OCT.with_suffix('.mtb'), BAK.with_suffix('.mtb')


def kids(o, rec):
    i, j = o.block(rec)
    return {r.name: r for r in o.recs[i + 1:j] if r.level == rec.level + 1}


ap = argparse.ArgumentParser()
ap.add_argument('--visible')
ap.add_argument('--rendercaps', action='store_true')
ap.add_argument('--materials', type=int)
ap.add_argument('--skip', type=int, default=0, help='with --materials N: keep [skip, skip+N)')
ap.add_argument('--keep', help='material ranges to keep, e.g. 178-188,199-220')
ap.add_argument('--dedupe', action='store_true')
ap.add_argument('--refmax', type=int)
ap.add_argument('--padtex', action='store_true', help='with --materials: keep every texture in TEXB')
ap.add_argument('--restore', action='store_true')
a = ap.parse_args()

if not BAK.exists():
    shutil.copy2(OCT, BAK)
    print('backed up', OCT, '->', BAK)
if not MBAK.exists():
    shutil.copy2(MTB, MBAK)
shutil.copy2(MBAK, MTB)
if a.restore:
    shutil.copy2(BAK, OCT)
    raise SystemExit('restored')

o = Oct.load(str(BAK))
if a.visible:
    lo, hi = map(int, a.visible.split(':'))
    geo = [n for n in o.find(name='Node', pool='SceneTreeNodePool')
           if 'Type' in kids(o, n) and kids(o, n)['Type'].names()[0] == 'Geometry']
    hid = 0
    for i, n in enumerate(geo):
        if not lo <= i < hi and kids(o, n)['Visible'].ints()[0]:
            kids(o, n)['Visible'].set_int(0); hid += 1
    shown = sum(kids(o, n)['Visible'].ints()[0] != 0 for n in geo)
    print('%d geometry nodes, hid %d more, %d visible' % (len(geo), hid, shown))
if a.rendercaps:
    rc = [r for r in o.find(name='RenderCaps', pool='SceneTreeNodePool') if r.value() & ~0x3f]
    for r in rc:
        r.set_int(0x200 | (r.value() & 0x3f))
    print('RenderCaps -> 0x200|low on %d primitives' % len(rc))
if a.materials and not a.keep:
    a.keep = '%d-%d' % (a.skip, a.skip + a.materials - 1)
if a.keep:
    import matp
    keep = sorted({i for part in a.keep.split(',') for i in
                   range(int(part.split('-')[0]), int(part.split('-')[-1]) + 1)})
    n, k, pos = len(keep), keep[0], {m: p for p, m in enumerate(keep)}
    bnd = matp.Bundle(MBAK.read_bytes())
    MTB.write_bytes(matp.merge([(bnd, i) for i in keep]))
    if a.padtex:   # append every unused texture: same materials, full-size TEXB
        nb = matp.Bundle(MTB.read_bytes())
        tex = nb.textures + [t for t in bnd.textures if t not in nb.textures]
        MTB.write_bytes(matp.build(tex, nb.materials))
        print('TEXB padded %d -> %d' % (len(nb.textures), len(tex)))
    mats = o.find(name='Material', pool='MaterialPool')
    for i_, m in reversed(list(enumerate(mats))):
        if i_ not in pos:
            i, j = o.block(m)
            del o.recs[i:j]
    refs = [r for r in o.find(name='MaterialReference', pool='SceneTreeNodePool') if r.ints()[0] not in pos]
    for r in o.find(name='MaterialReference', pool='SceneTreeNodePool'):   # contiguous: same as before
        r.set_int(pos.get(r.ints()[0], (r.ints()[0] - k) % n))
    print('materials %d -> %d, %d references remapped' % (len(mats), n, len(refs)))
if a.refmax:   # bundle untouched; primitives stop USING materials >= N
    refs = [r for r in o.find(name='MaterialReference', pool='SceneTreeNodePool') if r.ints()[0] >= a.refmax]
    for r in refs:
        r.set_int(r.ints()[0] % a.refmax)
    print('%d primitive references >= %d remapped; bundle keeps every material' % (len(refs), a.refmax))
if a.dedupe:   # byte-identical materials (shader, block A, block B incl. texture slots) -> one
    import matp
    bnd = matp.Bundle(MBAK.read_bytes())
    first, remap = {}, []
    for i, m in enumerate(bnd.materials):
        remap.append(first.setdefault((m['shader'], m['A'], m['B']), len(first)))
    keep = [i for i in range(len(remap)) if remap.index(remap[i]) == i]   # first occurrences, in order
    MTB.write_bytes(matp.merge([(bnd, i) for i in keep]))
    mats = o.find(name='Material', pool='MaterialPool')
    for i in reversed(range(len(mats))):
        if i not in keep:
            s_, e_ = o.block(mats[i])
            del o.recs[s_:e_]
    refs = o.find(name='MaterialReference', pool='SceneTreeNodePool')
    for r in refs:
        r.set_int(remap[r.ints()[0]])
    print('materials %d -> %d unique, %d references remapped' % (len(mats), len(keep), len(refs)))
o.save(str(OCT))
assert Oct.load(str(OCT)).f14 in (0, len(Oct.load(str(OCT)).recs))
print('installed', OCT)

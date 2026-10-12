"""Checks what the PS texture binder (FUN_0045a1c0) will read for each material.

The draw loop passes shader+0xa0 as the binder's descriptor, so:
  desc[1]    = shd[0xa1]  block B slot count (matp)
  desc[0x2c] = shd[0xcc]  PS stage: low nibble = first t-register, high nibble = slot count read
The binder reads blockB[i*8] (a texture pointer after load) for i < PS count; an index past
TEXB or a PS count above the slot count both hand D3D11 a garbage SRV.
The resolver (Shader_ParseBindings, 0x45D5C0, called from the .mtb parser) treats a slot as unset only when
w0 & w1 == 0xffffffff; otherwise it reads table[w0] and bumps that pointer's refcount -- so (ffffffff, 0)
reads table[-1] (garbage pointer + refcount write: the Metroville heap corruption), while (idx, ffffffff)
resolves normally. Shipped slots are only (idx, 0) or (ffffffff, ffffffff); anything else is flagged.
usage: slotcheck.py <bundle.mtb> [--ext30 DIR]     (no bundle: corpus check of every 3.0 .shd)
"""
import argparse, sys, struct
from pathlib import Path
from matp import Bundle, shd_dir
from shdfmt import slot_roles
import paths


def stages(sh):
    d = (shd_dir() / f'{sh}.shd').read_bytes()
    return d[0xa1], [(x & 0xf, x >> 4) for x in d[0xcc:0xd1]]


ap = argparse.ArgumentParser(description='Per-material texture binder check')
ap.add_argument('bundle', nargs='?')
paths.add_args(ap, 'ext30')
a = paths.use(ap.parse_args())
if not a.bundle:
    bad = 0; n = 0
    for f in shd_dir().glob('*.shd'):
        nslots, st = stages(f.stem); n += 1
        if any(cnt > nslots for _, cnt in st):
            bad += 1
            if bad <= 10: print(f.stem, nslots, st)
    print(f'{bad}/{n} shaders have a stage count above their slot count')
    sys.exit(1 if bad or not n else 0)

b = Bundle(Path(a.bundle).read_bytes())
ntex = len(b.textures)
print(f'{len(b.materials)} materials, {ntex} textures')
bad = 0
for i, m in enumerate(b.materials):
    nslots, st = stages(m['shader'])
    idx = b.tex_slots(m)
    probs = []
    if nslots != m['slots']: probs.append(f'slots {m["slots"]} vs shd {nslots}')
    if any(cnt > nslots for _, cnt in st): probs.append(f'stage count > slots {st}')
    oob = [x for x in idx if x != 0xffffffff and x >= ntex]
    if oob: probs.append(f'TEXB index out of range {oob}')
    # shipped slots are always (idx, 0) or (ffffffff, ffffffff); mixed forms crashed Metroville
    mixed = [s for s in range(m['slots']) if m['stride'] == 8 and
             (idx[s] == 0xffffffff) != (struct.unpack_from('<I', m['B'], 8 * s + 4)[0] == 0xffffffff)]
    if mixed: probs.append(f'foreign slot encoding in slots {mixed}')
    ps_end = st[0][0] + st[0][1]
    if probs or ps_end > 8:
        bad += bool(probs)
        print(i, m['name'], m['shader'][:8], 'PS t%d..t%d' % (st[0][0], ps_end - 1),
              ['%s=%s' % (r, 'unset' if x == 0xffffffff else x) for r, x in zip(slot_roles(m['shader']), idx)],
              '; '.join(probs))
print(f'{bad} materials with problems')

"""The render table di1compat reads at boot: the 3.0-side knowledge its load-time material and vertex transcode needs
(plan REN-5), measured with the tested Python functions (autobundle, shdfmt, vtxf). No 1.0 asset content is in it:
the plugin reads the 1.0 materials, textures and vertices from the 1.0 install when the engine asks for them.

Lines (space-separated; the plugin parses them):
  S <3.0 shader> <blockA> <blockB> <slots> <n vformats> <crc...> <n roles> <role|-...>   every shader a 3.0 material uses
  D <3.0 shader> <engine path> <material index> <materials using it> <extract path>       first shipped user (autobundle.donors)
  M <material GUID> <engine path> <material index> <extract path>                         GUID shipped in 3.0 (autobundle.index)
    (extract path = '3:<path in the 3.0 extract>': the generic tier breaks ties by it, and the offline test reads from it)
  F <role>                                                                                 slot roles shipped materials fill
  G <1.0 shader> <1.0 slot...>                                                             autobundle.GENERIC_SLOTS
  N <material GUID>                                                                        autobundle.NOT_REAL
  T <1.0 vformatCRC> <3.0 vformatCRC...>                                                   vtxf.targets_1to3, best first
  P <1.0 crc> <3.0 crc> <n src strides> <s...> <n dst strides> <d...> <n ops> {op}       vtxf.plan
      op = <dst stream> <dst off> <size> C <src stream> <src off>                 copy
         | <dst stream> <dst off> <size> X <src stream> <src off> <src type> <dst type> <n default> <v...>   convert
         | <dst stream> <dst off> <size> K <hex bytes>                            constant (padded element)
  V <name> <cbuffer register> <byte offset> <size>                                          3.0 global constant (gen30.globals30)
  W <technique hash> <depth kind> <X> <bits hex>                                            bits 3.0 adds to a converted pass word (gen30.rules)
  Q <technique hash> <3.0 word hex> <byte>                                                  pass +0x1c byte as 3.0 ships it (gen30.rules)
python rentable.py [--out <file>]   (default: the install's Pentane\\Plugins\\di1compat_render.txt)
"""
import argparse
from pathlib import Path
import autobundle, matp, paths, shdfmt, vtxf


def engine_path(rel):
    """'3:materials/<bucket>/<member>' -> 'materials/<member>' (bucket archives); a per-asset archive keeps its
    folder ('worlds/frontend/frontend.mtb'): the paths the engine opens (di1compat trace)."""
    p = rel.split(':', 1)[1].split('/')
    if len(p) >= 3 and Path(p[-1]).stem.lower() != p[-2].lower():
        p = p[:-2] + [p[-1]]
    return '/'.join(p)


def main():
    ap = argparse.ArgumentParser(description='di1compat render table')
    ap.add_argument('--out')
    paths.add_args(ap)
    a = paths.use(ap.parse_args())
    out = Path(a.out) if a.out else paths.game('Pentane', 'Plugins', 'di1compat_render.txt')
    don, idx, fill = autobundle.donors(), autobundle.index(), autobundle.filled_roles()
    lines = []
    for sh in sorted(don):
        try:
            A, B, slots = matp.shader_sizes(sh)
            vf = sorted(autobundle.shader_vformats(sh))
            roles = shdfmt.slot_roles(sh)
        except Exception as e:
            print('skip shader', sh, e)
            continue
        roles = (list(roles) + [None] * slots)[:slots]
        lines.append('S %s %d %d %d %d %s %d %s' % (sh, A, B, slots, len(vf), ' '.join('%08x' % c for c in vf),
                                                 len(roles), ' '.join(r or '-' for r in roles)))
    for sh, (rel, i, n) in sorted(don.items()):
        lines.append('D %s %s %d %d %s' % (sh, engine_path(rel), i, n, rel))
    for g, (rel, i) in sorted(idx.items()):
        if ' ' not in g and ' ' not in rel:      # the format is space-separated (1 shipped path has a space)
            lines.append('M %s %s %d %s' % (g, engine_path(rel), i, rel))
    lines += ['F %s' % r for r in sorted(fill)]
    lines += ['G %s %s' % (sh, ' '.join(map(str, s))) for sh, s in autobundle.GENERIC_SLOTS.items()]
    lines += ['N %s' % g for g in sorted(autobundle.NOT_REAL)]
    # real 1.0 shaders (gen30): 3.0 global constant layout, the pass-word bits 3.0 adds per technique, pass +0x1c bytes
    import gen30
    lines += ['V %s %d %d %d' % (n, cb, off, sz) for n, (cb, off, sz) in sorted(gen30.globals30().items())]
    learned, b1c = gen30.rules()
    lines += ['W %08x %d %d %x' % (t, k, x, bits) for (t, k, x), bits in sorted(learned.items(), key=str)
              if t is not None and bits]
    lines += ['Q %08x %x %d' % (t, w, v) for (t, w), v in sorted(b1c.items(), key=str) if t is not None and v]
    for g in sorted(vtxf.formats(1)):
        crc = vtxf.name_hash(g)
        try:
            targets = vtxf.targets_1to3(crc)
        except Exception as e:
            print('skip vformat', '%08x' % crc, e)
            continue
        lines.append('T %08x %s' % (crc, ' '.join('%08x' % t for t, _, _ in targets)))
        for t, _, _ in targets:
            ss, ds, ops, _, _ = vtxf.plan(crc, t)
            enc = []
            for dsi, doff, ssi, soff, size, conv in ops:
                if ssi is None:
                    enc.append('%d %d %d K %s' % (dsi, doff, size, bytes(soff).hex() or '-'))
                elif conv is None:
                    enc.append('%d %d %d C %d %d' % (dsi, doff, size, ssi, soff))
                else:
                    st, dt, dflt = conv
                    enc.append('%d %d %d X %d %d %s %s %d %s' % (dsi, doff, size, ssi, soff, st, dt, len(dflt),
                                                               ' '.join(repr(float(x)) for x in dflt)))
            lines.append('P %08x %08x %d %s %d %s %d %s' % (crc, t, len(ss), ' '.join(map(str, ss)), len(ds),
                                                          ' '.join(map(str, ds)), len(enc), ' '.join(enc)))
    out.write_text('\n'.join(lines) + '\n')
    kinds = {}
    for l in lines:
        kinds[l[0]] = kinds.get(l[0], 0) + 1
    print('wrote', out, kinds)


if __name__ == '__main__':
    main()

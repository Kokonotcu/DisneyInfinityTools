"""Particle packages (<zone>.particledata): the 1.0 layout, the 3.0 layout, and the 1.0 -> 3.0 conversion -- the
Python oracle of native/di1compat/vfx.hpp (the shipped path is the C++ one; octtest --pd compares them).

Both engines keep the file in memory and use the records in place (1.0 FUN_006ef500, 3.0 FUN_00672410):
  [records][key table: nkeys x 8 B][index tables][trailer]
  trailer 1.0 FUN_006c81e0: end-0x14, u16 0x123, u16 12, u32 data size, u32 key-table offset, u16 nkeys,
          u16 n_emitter, u16 n_effect, u16 n_param
  trailer 3.0 FUN_00656bd0: end-0x18, version 0x2f, + u16 n14, u16 n16 (3.0-only kinds, 0 here)
  index tables run backwards from the trailer (1.0 FUN_006ef350 / 3.0 FUN_00672120):
    3.0: n16 x 8, n14 x 12, n_param x 8, n_effect x 8, n_emitter x 16;   1.0: n_param, n_effect, n_emitter
  emitter entry 1.0 (u32 off, u32 hash, u16 pool, u16 material, u32 x) -> FUN_006ee910
                3.0 (u32 off, u32 hash, u8 class, u8 0, u16 material, u32 c) -> FUN_00671830
  records: emitter 1.0 0x108 + 2n (n curve-key indices, count at +0x105) / 3.0 0x124 + 2n (+0x121);
           effect (emitter list, FUN_006de3d0 / FUN_00675980) same in both; param 0x24 / 0x34 (6 key indices first).
  keys: 8 B cubic curve keys, the same decode in both (1.0 FUN_006c7210, 3.0 FUN_00662d20); records index them
        package-locally (the loader adds the global base), so the table is copied unchanged.

Emitter fields (1.0 -> 3.0; spawn 1.0 FUN_006dce20 vs 3.0 FUN_00673870, registration FUN_006ee910 vs FUN_00671830;
twin check = the same hash in a 1.0 and a 3.0 package, 1,431 emitters, Disney's own port of those effects):
  0x00-0xa3 same layout (life, emission box, direction+cone, speed, accel, 3 rotation ranges)
  0xa4/0xa8 inherited-velocity range -> 0xb0 (x param_4[0x10..0x12] in both) and 0xb8 (twins: 1427/1431)
  0xac/0xb0 gravity range -> 0xc0;  0xb4..0xd8 five ranges -> 0xd8..0xfc;  0xe4 -> 0x108;  0xe8 child emitter -> 0x10c
  0xdc/0xe0 flags -> 0x100 flags + 0x104/0x106 modifier masks (1.0 three 8-bit masks dc>>4/12/20 and e0>>4;
       3.0 FUN_00659b80 one mask for the colour randoms, one for scale (1.0's avg-scale and uniform-scale masks merged))
       velocity always rotated by the emitter in 1.0 = 3.0 bit 0x10000; acceleration never rotated = 0x20000 clear
  0xec 6 x u16 colour randoms (1.0 v/1023, 3.0 v/1023-32.03) -> 0x110 as 0x8000 + v/2 (8103/8586 twin values)
  0xf8 char[8] mesh name (FUN_006e9270 'particles/<name>') -> 0x11c x131 hash (FUN_00670760 'particles/%08x')
  0x3.0 0xc8/0xcc = 1.0f, 0x120 = 0 (3.0-only, every twin)
"""
import struct, sys
from pathlib import Path

# 1.0 entry u32 x (0..63) -> 3.0 entry u32 c: Disney's conversion as the twins show it (x: c, twin count >= 1 each);
# x absent from the twins takes its lower neighbour's c.
XC = {1: 63, 2: 63, 3: 0, 5: 0, 6: 0, 9: 1, 10: 1, 18: 3, 20: 4, 22: 4, 23: 5, 26: 5, 30: 6, 31: 7, 33: 7, 34: 7,
      35: 8, 39: 9, 40: 9, 41: 9, 44: 10, 46: 10, 47: 11, 48: 11, 49: 11, 50: 11, 51: 12, 52: 12, 55: 13, 58: 13,
      59: 14, 60: 14, 61: 14, 62: 14, 63: 0}


def xc(x):
    if x in XC: return XC[x]
    lo = [k for k in XC if k < x]
    return XC[max(lo)] if lo else 0


def h131(s):
    """The engine's name hash (3.0 FUN_0040bb50, 1.0 FUN_0040bac0): h = h*131 + signed char."""
    h = 0
    for ch in s:
        h = (h * 131 + (ch - 256 if ch > 127 else ch)) & 0xffffffff
    return h


def parse(d):
    for tl, ver in ((0x14, 12), (0x18, 0x2f)):
        t = len(d) - tl
        if t >= 0 and struct.unpack_from('<HH', d, t) == (0x123, ver):
            break
    else:
        return None
    h = struct.unpack_from('<HHIIHHHH' + ('HH' if ver == 0x2f else ''), d, t)
    r = {'ver': ver, 'trailer': t, 'size': h[2], 'keys': h[3], 'nkeys': h[4]}
    names = ['emit', 'effect', 'param'] + (['k14', 'k16'] if ver == 0x2f else [])
    sizes = {'emit': 16, 'effect': 8, 'param': 8, 'k14': 12, 'k16': 8}
    counts = dict(zip(names, h[5:]))
    cur, ent = t, {}
    for k in (['k16', 'k14'] if ver == 0x2f else []) + ['param', 'effect', 'emit']:
        cur -= counts[k] * sizes[k]
        ent[k] = [d[cur + i * sizes[k]:cur + (i + 1) * sizes[k]] for i in range(counts[k])]
    r['ent'], r['tables'] = ent, cur
    return r


def convert_emitter(a):
    o = bytearray(0x124)
    o[0:0xa4] = a[0:0xa4]
    o[0xb0:0xb8] = a[0xa4:0xac]
    o[0xb8:0xc0] = a[0xa4:0xac]
    o[0xc0:0xc8] = a[0xac:0xb4]
    o[0xc8:0xd0] = struct.pack('<ff', 1.0, 1.0)
    o[0xd8:0x100] = a[0xb4:0xdc]
    dc, e0 = struct.unpack_from('<II', a, 0xdc)
    f = (dc & 7) | (dc >> 3 & 1) << 4 | (e0 & 0xf) << 8 | 0x10000 | (dc >> 28 & 1) << 18 | (dc >> 29 & 1) << 20 \
        | (dc >> 30 & 1) << 21 | 0x40000000
    struct.pack_into('<IHH', o, 0x100, f, (dc >> 4 & 0xff) | (e0 >> 4 & 0x1f) << 9,
                     (dc >> 12 & 0xff) | (dc >> 20 & 0xff) | (e0 >> 9 & 0x1f) << 9 | (e0 >> 14 & 0x1f) << 9)
    o[0x108:0x110] = a[0xe4:0xec]
    for i in range(6):
        struct.pack_into('<H', o, 0x110 + 2 * i, 0x8000 + struct.unpack_from('<H', a, 0xec + 2 * i)[0] // 2)
    name = a[0xf8:0x100].split(b'\0')[0]
    struct.pack_into('<I', o, 0x11c, h131(name) if name else 0)
    o[0x121] = a[0x105]
    n = a[0x105]
    out = bytes(o) + a[0x108:0x108 + 2 * n]
    return out + b'\0' * (-len(out) % 4)


def convert_param(a):
    o = bytearray(0x34)
    o[0:0xe] = a[0:0xe]
    o[0xf] = 1
    o[0x10:0x12] = a[0xe:0x10]
    o[0x18:0x28] = a[0x10:0x20]
    v = struct.unpack_from('<I', a, 0x20)[0]
    o[0x30] = 1 if v & 1 else 2 if v & 6 else 0
    o[0x31] = 9
    return bytes(o)


def convert(d):
    """1.0 package bytes -> 3.0 package bytes (None if d is not a 1.0 package)."""
    r = parse(d)
    if not r or r['ver'] != 12:
        return None
    recs = []   # (offset, kind, entry)
    for k in ('emit', 'effect', 'param'):
        for e in r['ent'][k]:
            recs.append((struct.unpack_from('<I', e)[0], k, e))
    recs.sort(key=lambda x: x[0])
    ends = [x[0] for x in recs[1:]] + [r['keys']]
    body, newoff = bytearray(), {}
    for (off, k, e), end in zip(recs, ends):
        src = d[off:end]
        b = convert_emitter(src) if k == 'emit' else convert_param(src) if k == 'param' else src
        newoff[(k, off)] = len(body)
        body += b
        body += b'\0' * (-len(body) % 4)
    body += b'\0' * (-len(body) % 8)
    keys = len(body)
    body += d[r['keys']:r['keys'] + 8 * r['nkeys']]
    emit = b''.join(struct.pack('<IIBBHI', newoff[('emit', struct.unpack_from('<I', e)[0])], struct.unpack_from('<I', e, 4)[0],
                                1, 0, struct.unpack_from('<H', e, 0xa)[0], xc(struct.unpack_from('<I', e, 0xc)[0]))
                    for e in r['ent']['emit'])
    eff = b''.join(struct.pack('<II', newoff[('effect', struct.unpack_from('<I', e)[0])], struct.unpack_from('<I', e, 4)[0])
                   for e in r['ent']['effect'])
    par = b''.join(struct.pack('<II', newoff[('param', struct.unpack_from('<I', e)[0])], struct.unpack_from('<I', e, 4)[0])
                   for e in r['ent']['param'])
    trailer = struct.pack('<HHIIHHHHHH', 0x123, 0x2f, keys, keys, r['nkeys'], len(r['ent']['emit']), len(r['ent']['effect']),
                          len(r['ent']['param']), 0, 0)
    return bytes(body) + emit + eff + par + trailer


def validate30(d):
    """Invariants the 3.0 loader relies on (FUN_00672120), checked on any 3.0-layout package. -> list of problems."""
    r = parse(d)
    if not r or r['ver'] != 0x2f: return ['no 3.0 trailer']
    bad = []
    if r['keys'] + 8 * r['nkeys'] != r['tables']: bad.append('key table does not end at the index tables')
    offs = sorted((struct.unpack_from('<I', e)[0], k) for k in ('emit', 'effect', 'param') for e in r['ent'][k])
    for i, (o, k) in enumerate(offs):
        end = offs[i + 1][0] if i + 1 < len(offs) else r['keys']
        if o % 4: bad.append('%s at %x unaligned' % (k, o))
        if k == 'emit':
            n = d[o + 0x121]
            if o + 0x124 + 2 * n > end: bad.append('emitter at %x overruns' % o)
            for j in range(n):
                v = struct.unpack_from('<H', d, o + 0x124 + 2 * j)[0]
                if v != 0xffff and v >= r['nkeys']: bad.append('emitter at %x key %d out of range' % (o, v))
        elif k == 'param':
            if o + 0x34 > end: bad.append('param at %x overruns' % o)
            for j in range(6):
                v = struct.unpack_from('<H', d, o + 2 * j)[0]
                if v not in (0xffff, 0xfff) and v >= r['nkeys']: bad.append('param at %x key %d out of range' % (o, v))
        else:
            n = d[o]
            if o + 4 + 16 * n > end: bad.append('effect at %x overruns' % o)
    return bad


if __name__ == '__main__':
    import argparse, paths
    ap = argparse.ArgumentParser(description='every 1.0 package converted and checked, every 3.0 package checked')
    ap.add_argument('out', nargs='?', help='also write the converted packages here')
    paths.add_args(ap, 'ext10', 'ext30')
    a = paths.use(ap.parse_args())
    E10, E30 = paths.ext10('particles'), paths.ext30('particles')
    # whole corpus: every 1.0 package converts to a valid 3.0 package that keeps every record, hash and key
    total = 0
    for p in sorted(E10.iterdir()):
        f = p / (p.name + '.particledata')
        if not f.exists(): continue
        d = f.read_bytes(); c = convert(d)
        r1, r3 = parse(d), parse(c)
        probs = validate30(c)
        for k in ('emit', 'effect', 'param'):
            if [e[4:8] for e in r1['ent'][k]] != [e[4:8] for e in r3['ent'][k]]: probs.append(k + ' hashes differ')
        if d[r1['keys']:r1['keys'] + 8 * r1['nkeys']] != c[r3['keys']:r3['keys'] + 8 * r3['nkeys']]: probs.append('keys differ')
        total += len(probs)
        print('%-20s %7d -> %7d B  emitters %4d effects %4d params %4d  %s' % (p.name, len(d), len(c), len(r1['ent']['emit']),
              len(r1['ent']['effect']), len(r1['ent']['param']), 'ok' if not probs else probs[:3]))
        if a.out: Path(a.out, p.name + '.particledata').write_bytes(c)
    # 3.0 packages must pass the same validator (it encodes the loader's rules, not ours)
    for p in sorted(E30.iterdir()):
        f = p / (p.name + '.particledata')
        if f.exists():
            pr = validate30(f.read_bytes())
            total += len(pr)
            if pr: print('3.0', p.name, pr[:3])
    print('problems:', total)
    sys.exit(1 if total else 0)

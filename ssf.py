"""Scene settings: the format (shaders/scenesettingsformat.ssf) and a realm/world bundle's SSET chunk, both games.

.ssf, from the loaders (3.0 FUN_0045d420, 1.0 FUN_004af580): a header like a .shd's but shorter (3.0 0xa8, 1.0 0x64),
  u8 [0] global params, u8 [1] passes; block A descriptor 3.0 @0x38 / 1.0 @0x1c (u8 params, u8 texture slots,
  u16 size), block B @0x70 / @0x40; then param name hashes, param words (hi16 offset in its block, lo16 type),
  slot name hashes A, slot name hashes B, pass name hashes (n x 4), the pass pointer array (3.0 n x 8, 1.0 n x 4,
  filled at load), then per pass, 16-aligned, an embedded .shd (header 0xe0 / 0xa4 + passes + techniques + names +
  words + slots; its block B is that pass's settings).
SSET chunk (3.0 FUN_0046b9d0): 'SSET' u32 ver, u32 chunk size, u32 n, u32 n; n names (GUIDs), 16-aligned, then
  n settings blocks of equal size (chunk end - start) / n, each [global A][global B][pass 0 B][pass 1 B]...

python ssf.py                         both formats: params, slots, passes, block sizes
python ssf.py --sset <file.mtb>...    the SSET chunk(s): names, block size vs the format's prediction
"""
import os, struct, sys
import shd

HDR = {'3.0': dict(size=0xa8, a=0x38, b=0x70, ptr=8, shd='3.0'), '1.0': dict(size=0x64, a=0x1c, b=0x40, ptr=4, shd='1.0')}


def path(game):
    return os.path.join(shd.shader_dir(game), 'scenesettingsformat.ssf')


def _al(x, a):
    return (x + a - 1) & ~(a - 1)


def _shd_len(b, o, game):
    """Bytes the engine's finalize consumes for an embedded .shd at o (no defaults); 1.0 adds its NUL-ended name."""
    L = shd.LAYOUT[game]
    npar, npass, ntech = b[o + L['counts']], b[o + L['counts'] + 1], b[o + L['counts'] + 2]
    p = o + L['data'] + npass * L['psize']
    p = _al(p + 2 * ntech, 4) + 4 * npar + 4 * ntech + 4 * npar
    p += 4 * (b[o + L['a'] + 1] + b[o + L['b'] + 1])
    return p - o


def parse(b, game):
    H = HDR[game]
    npar, npass = b[0], b[1]
    nA, slotsA, sizeA = b[H['a']], b[H['a'] + 1], struct.unpack_from('<H', b, H['a'] + 2)[0]
    slotsB, sizeB = b[H['b'] + 1], struct.unpack_from('<H', b, H['b'] + 2)[0]
    o = H['size']
    names = list(struct.unpack_from('<%dI' % npar, b, o)); o += 4 * npar
    words = list(struct.unpack_from('<%dI' % npar, b, o)); o += 4 * npar
    snA = list(struct.unpack_from('<%dI' % slotsA, b, o)); o += 4 * slotsA
    snB = list(struct.unpack_from('<%dI' % slotsB, b, o)); o += 4 * slotsB
    o = _al(o, 16); defA = b[o:o + sizeA]; o += sizeA          # the default settings, as a .shd's template blocks
    o = _al(o, 16); defB = b[o:o + sizeB]; o += sizeB
    pnames = list(struct.unpack_from('<%dI' % npass, b, o)); o += 4 * npass
    o += H['ptr'] * npass
    passes = []
    for i in range(npass):
        o = _al(o, 16)
        n = _shd_len(b, o, game)
        s = shd.parse(b[o:o + n] + b'\0' * 64, game)
        s['name_hash'] = pnames[i]
        o += n
        o = _al(o, 16); s['defA'] = b[o:o + s['sizeA']]; o += s['sizeA']
        o = _al(o, 16); s['defB'] = b[o:o + s['sizeB']]; o += s['sizeB']
        if game == '1.0':                                           # 1.0 names the pass's shader after its blocks
            e = b.index(b'\0', o); s['name'] = b[o:e].decode('latin-1'); o = e + 1
        passes.append(s)
    params = [dict(hash=h, block='A' if i < nA else 'B', offset=w >> 16, type=w & 0xffff)
              for i, (h, w) in enumerate(zip(names, words))]
    return dict(game=game, params=params, slotsA=snA, slotsB=snB, sizeA=sizeA, sizeB=sizeB, passes=passes,
                defA=defA, defB=defB, end=o, total=len(b))


def load(game):
    return parse(open(path(game), 'rb').read(), game)


def block_size(f):
    """Predicted size of one settings block: global A + B + every pass's block B, each 16-aligned."""
    return _al(f['sizeA'], 16) + _al(f['sizeB'], 16) + sum(_al(p['sizeB'], 16) for p in f['passes'])


def sset(raw):
    """-> (names, block size, [block bytes]) of a bundle's SSET chunk, or None."""
    n = struct.unpack_from('<I', raw, 12)[0]
    o = 16
    for _ in range(n):
        tag, cs = raw[o:o + 4], struct.unpack_from('<I', raw, o + 8)[0]
        if tag == b'SSET':
            cnt = struct.unpack_from('<I', raw, o + 12)[0]
            p, nm = o + 20, []                                       # +16: the count again (MATP-style name list)
            for _ in range(cnt):
                e = raw.index(b'\0', p); nm.append(raw[p:e].decode('latin-1')); p = e + 1
            p = _al(p, 16)
            size = (o + cs - p) // cnt if cnt else 0
            return nm, size, [raw[p + i * size:p + (i + 1) * size] for i in range(cnt)], p - o
        if not cs:
            break
        o += cs
    return None


def layout(f):
    """-> [(where, offset in the block, size, slots, slot stride, params)] for global A, global B and each pass's B;
    where = 'A' / 'B' / pass name hash. Params as (hash, offset in that part, byte size up to the next param)."""
    st = 8 if f['game'] == '3.0' else 4
    out, o = [], 0

    def part(where, size, slots, params):
        nonlocal o
        ps = sorted(params, key=lambda p: p[1])
        sized = [(h, off, (ps[i + 1][1] if i + 1 < len(ps) else size) - off) for i, (h, off) in enumerate(ps)]
        out.append((where, o, size, slots, st, sized))
        o = _al(o + size, 16)
    part('A', f['sizeA'], f['slotsA'], [(p['hash'], p['offset']) for p in f['params'] if p['block'] == 'A'])
    part('B', f['sizeB'], f['slotsB'], [(p['hash'], p['offset']) for p in f['params'] if p['block'] == 'B'])
    for s in f['passes']:
        part(s['name_hash'], s['sizeB'], s['slotsB'], [(p['hash'], p['offset']) for p in s['params'] if p['block'] == 'B'])
    return out


SLOT_ALIAS = {shd.ehash('g_colorgrade_lut'): shd.ehash('g_colorgrade_lut_sampler')}   # 3.0 renamed the LUT slot


def template(f30):
    """A 3.0 settings block holding the format's own defaults."""
    blk = bytearray(block_size(f30))
    parts = layout(f30)
    defs = [f30['defA'], f30['defB']] + [p['defB'] for p in f30['passes']]
    for (where, o, sz, slots, st, _), d in zip(parts, defs):
        blk[o:o + len(d)] = d
        blk[o:o + st * len(slots)] = b'\xff' * (st * len(slots))   # unset slots (as shipped SSET blocks hold them)
    return blk


def convert_block(b10, f10, f30, tex_of=lambda i: i):
    """One 1.0 settings block -> 3.0: the 3.0 defaults, then every 1.0 parameter with the same name (and part) at its
    3.0 offset, and every texture slot by name (index through tex_of; 1.0 4-byte slots -> 3.0 index + 0 word).
    -> (block, stats)"""
    out = template(f30)
    p10 = {w: (o, slots, st, {h: (off, n) for h, off, n in ps}) for w, o, sz, slots, st, ps in layout(f10)}
    st = {'params': 0, 'slots': 0, 'parts': 0}
    for w, o, sz, slots, s30, ps in layout(f30):
        if w not in p10:
            continue
        st['parts'] += 1
        o10, slots10, s10, ps10 = p10[w]
        for h, off, n in ps:
            if h in ps10:
                off10, n10 = ps10[h]
                k = min(n, n10)
                out[o + off:o + off + k] = b10[o10 + off10:o10 + off10 + k]
                st['params'] += 1
        src = {SLOT_ALIAS.get(h, h): i for i, h in enumerate(slots10)}
        for i, h in enumerate(slots):
            if h not in src:
                continue
            j = src[h]
            t = struct.unpack_from('<I', b10, o10 + s10 * j)[0]
            t = 0xffffffff if t == 0xffffffff else tex_of(t)
            struct.pack_into('<II', out, o + s30 * i, t, 0xffffffff if t == 0xffffffff else 0)
            out[o + s30 * len(slots) + i] = b10[o10 + s10 * len(slots10) + j]      # the slot's flag byte
            st['slots'] += 1
    return bytes(out), st


def dump(raw, game):
    import matp
    f = load(game)
    tex = matp.texb(raw)
    names, size, blocks, _ = sset(raw)
    for nm, blk in zip(names, blocks):
        print('settings', nm, len(blk), 'B')
        for where, o, sz, slots, st, params in layout(f):
            w = where if isinstance(where, str) else '%08x' % where
            sl = [struct.unpack_from('<I', blk, o + st * i)[0] for i in range(len(slots))]
            print('  %s @%d size %d slots %s' % (w, o, sz, ['%s=%s' % (shd.nm(h), tex[t] if t < len(tex) else '-')
                                                          for h, t in zip(slots, sl)]))
            for h, off, n in params:
                v = struct.unpack_from('<%df' % min(n // 4, 8), blk, o + off)
                print('     %-34s %s' % (shd.nm(h), ' '.join('%.4g' % x for x in v)))


if __name__ == '__main__':
    if '--dump' in sys.argv:
        a = sys.argv[sys.argv.index('--dump') + 1:]
        dump(open(a[0], 'rb').read(), a[1] if len(a) > 1 else '3.0')
        sys.exit(0)
    import paths
    paths.use(paths.add_args(__import__('argparse').ArgumentParser(), 'ext30', 'ext10').parse_known_args()[0])
    if '--sset' in sys.argv:
        for p in sys.argv[sys.argv.index('--sset') + 1:]:
            r = sset(open(p, 'rb').read())
            print(p, r and (r[0], r[1], 'header', r[3]))
    else:
        for g in ('1.0', '3.0'):
            f = load(g)
            print('==', g, 'params %d (A %d) slots %d/%d sizeA %d sizeB %d passes %d; parsed to %d of %d; block %d' % (
                len(f['params']), sum(p['block'] == 'A' for p in f['params']), len(f['slotsA']), len(f['slotsB']),
                f['sizeA'], f['sizeB'], len(f['passes']), f['end'], f['total'], block_size(f)))
            for p in f['params']:
                print('   %s %-34s @%d t%x' % (p['block'], shd.nm(p['hash']), p['offset'], p['type']))
            print('   slots A', [shd.nm(h) for h in f['slotsA']])
            for s in f['passes']:
                print('   pass %-12s params %d sizeA %d sizeB %d slots %d/%d techs %d passes %d' % (
                    '%08x' % s['name_hash'], len(s['params']), s['sizeA'], s['sizeB'], len(s['slotsA']), len(s['slotsB']),
                    len(s['techs']), len(s['passes'])))

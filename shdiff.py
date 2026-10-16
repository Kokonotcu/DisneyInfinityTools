"""Translation audit: every shader shipped in BOTH games, gen30's translation of the 1.0 copy against the shipped 3.0
copy, field by field -- header bytes, params, slots, techniques, pass records, and each program's real bindings
(texture registers, cbuffers, input / output signatures) -- tallied over all of them, so systematic differences
show up as counts instead of guesses.

python shdiff.py [--only <shader>] [--examples N]
"""
import os, struct
from collections import Counter, defaultdict
import shd, gen30

SKIP_PASS = set(range(0x18, 0x1c)) | set(range(0x28, 0x2c)) | set(range(0x30, 0x34)) | set(range(0x40, 0x48))  # vf, ids, sizes


def sig(b, tag):
    """[(semantic, index, register, mask, rw)] from a DXBC ISGN / OSGN chunk."""
    d = b.find(b'DXBC') if b else -1
    if d < 0:
        return []
    for i in range(struct.unpack_from('<I', b, d + 28)[0]):
        co = d + struct.unpack_from('<I', b, d + 32 + 4 * i)[0]
        if b[co:co + 4] != tag.encode():
            continue
        base = co + 8
        n = struct.unpack_from('<I', b, base)[0]
        out = []
        for k in range(n):
            no, idx, sv, ct, reg, mask, rw = struct.unpack_from('<5IBB', b, base + 8 + 24 * k)
            out.append((b[base + no:b.index(b'\0', base + no)].decode('latin-1'), idx, reg, mask, rw))
        return out
    return []


def binds(b):
    r = shd.rdef(b)
    tex = sorted(x[2] for x in r['res'] if x[1] == 2)
    smp = sorted(x[2] for x in r['res'] if x[1] == 3)
    cbs = sorted(x[2] for x in r['res'] if x[1] == 0)
    return tex, smp, cbs


class Tally:
    def __init__(self):
        self.n = Counter()
        self.ex = defaultdict(list)

    def add(self, key, example, cap):
        self.n[key] += 1
        if len(self.ex[key]) < cap:
            self.ex[key].append(example)


def main(only=None, cap=3):
    d1, d3 = shd.shader_dir('1.0'), shd.shader_dir('3.0')
    both = sorted(set(f[:-4] for f in os.listdir(d1) if f.endswith('.shd')) & set(f[:-4] for f in os.listdir(d3) if f.endswith('.shd')))
    if only:
        both = [only]
    T = Tally()
    stats = Counter()
    for sh in both:
        try:
            g = gen30.generate(sh, True)
        except Exception as e:
            T.add('generate exception %s' % type(e).__name__, sh, cap)
            continue
        for p in g['problems']:
            T.add('generate problem: %s' % (p.split(':', 1)[1].strip()[:50] if ':' in p else p), sh, cap)
        ours_b, ref_b = g['shd'], open(os.path.join(d3, sh + '.shd'), 'rb').read()
        ours, ref = shd.parse(ours_b, '3.0'), shd.parse(ref_b, '3.0')
        stats['shaders'] += 1
        # header bytes
        for o in range(0x08, 0xE0):
            if ours_b[o] != ref_b[o]:
                T.add('header +%#04x' % o, '%s ours %02x ref %02x' % (sh[:8], ours_b[o], ref_b[o]), cap)
        # params / slots
        po = {p['hash']: p for p in ours['params']}
        pr = {p['hash']: p for p in ref['params']}
        for h in pr:
            if h not in po:
                T.add('param only in 3.0', '%s %s' % (sh[:8], shd.nm(h)), cap)
        for h in po:
            if h not in pr:
                T.add('param only in ours', '%s %s' % (sh[:8], shd.nm(h)), cap)
            else:
                for k in ('block', 'offset', 'type'):
                    if po[h][k] != pr[h][k]:
                        T.add('param %s differs' % k, '%s %s ours %s ref %s' % (sh[:8], shd.nm(h), po[h][k], pr[h][k]), cap)
        if ours['slotsB'] != ref['slotsB']:
            T.add('slot list differs', '%s ours %s ref %s' % (sh[:8], [shd.nm(x) for x in ours['slotsB']], [shd.nm(x) for x in ref['slotsB']]), cap)
        if (ours['sizeA'], ours['sizeB']) != (ref['sizeA'], ref['sizeB']):
            T.add('block sizes differ', '%s ours A%d B%d ref A%d B%d' % (sh[:8], ours['sizeA'], ours['sizeB'], ref['sizeA'], ref['sizeB']), cap)
        # techniques and passes
        to = {t['hash']: t for t in ours['techs']}
        tr = {t['hash']: t for t in ref['techs']}
        for h in tr:
            if h not in to:
                T.add('technique only in 3.0', '%s %s' % (sh[:8], shd.nm(h)), cap)
        for h, t in to.items():
            if h not in tr:
                T.add('technique only in ours', '%s %s' % (sh[:8], shd.nm(h)), cap)
                continue
            u = tr[h]
            if t['count'] != u['count']:
                T.add('pass count differs %s' % shd.nm(h), '%s ours %d ref %d' % (sh[:8], t['count'], u['count']), cap)
            for k in range(min(t['count'], u['count'])):
                a, c = ours['passes'][t['first'] + k], ref['passes'][u['first'] + k]
                where = '%s %s/%d' % (sh[:8], shd.nm(h), k)
                stats['passes'] += 1
                if a['word'] != c['word']:
                    T.add('pass word', '%s ours %#x ref %#x' % (where, a['word'], c['word']), cap)
                for o in range(0x50):
                    if o not in SKIP_PASS and o != 0 and o not in (1, 2, 3) and a['raw'][o] != c['raw'][o]:
                        T.add('pass +%#04x' % o, '%s ours %02x ref %02x (word ref %#x)' % (where, a['raw'][o], c['raw'][o], c['word']), cap)
                for kind, key in (('vp', 'vs'), ('fp', 'ps')):
                    pa = g['programs'].get(a[key]) if a[key] else None
                    pc = shd.program('3.0', c[key], kind) if c[key] else None
                    if bool(pa) != bool(pc):
                        T.add('%s present ours %s ref %s' % (kind, bool(pa), bool(pc)), where, cap)
                        continue
                    if not pa:
                        continue
                    if pa[:4] != b'DXBC':
                        T.add('%s not compiled' % kind, where, cap)
                        continue
                    (ta, _, ca), (tc, _, cc) = binds(pa), binds(pc)
                    if ta != tc:
                        T.add('%s textures differ' % kind, '%s ours t%s ref t%s' % (where, ta, tc), cap)
                    if ca != cc:
                        T.add('%s cbuffers differ' % kind, '%s ours b%s ref b%s' % (where, ca, cc), cap)
                    if kind == 'vp':
                        ia = sorted((s[0], s[1]) for s in sig(pa, 'ISGN') if s[4])
                        ic = sorted((s[0], s[1]) for s in sig(pc, 'ISGN') if s[4])
                        if ia != ic:
                            T.add('vp inputs used differ', '%s ours %s ref %s' % (where, ia, ic), cap)
                    else:
                        oa = sorted((s[0], s[1]) for s in sig(pa, 'OSGN'))
                        oc = sorted((s[0], s[1]) for s in sig(pc, 'OSGN'))
                        if oa != oc:
                            T.add('fp outputs differ', '%s ours %s ref %s' % (where, oa, oc), cap)
                        na = sum(1 for s in sig(pa, 'ISGN') if s[4] and s[0] != 'SV_Position')
                        nc = sum(1 for s in sig(pc, 'ISGN') if s[4] and s[0] != 'SV_Position')
                        if na != nc:
                            T.add('fp varyings read differ (count)', '%s ours %d ref %d' % (where, na, nc), cap)
    print('%d shared shaders, %d paired passes' % (stats['shaders'], stats['passes']))
    for key, n in sorted(T.n.items(), key=lambda kv: -kv[1]):
        print('%5d  %s' % (n, key))
        for e in T.ex[key]:
            print('         %s' % e)


if __name__ == '__main__':
    import argparse, paths
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', metavar='SHADER')
    ap.add_argument('--examples', type=int, default=3, metavar='N')
    paths.add_args(ap, 'ext10', 'ext30')
    a = paths.use(ap.parse_args())
    main(a.only, a.examples)

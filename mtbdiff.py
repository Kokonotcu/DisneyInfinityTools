"""Material audit: every material shipped in BOTH games (same bundle path, same material), its 1.0 material run through
gen30's translation (block A as is, block B by block_b30, slots kept) against the shipped 3.0 material, parameter by
parameter and slot by slot, tallied over the whole game -- systematic differences between how 1.0 and 3.0 feed the
same shader show up as counts.

python mtbdiff.py [--examples N] [--limit N]
"""
import os, struct, sys
from collections import Counter, defaultdict
import paths, shd, matp, gen30


def mtbs(root):
    out = {}
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.lower().endswith('.mtb'):
                p = os.path.join(d, f)
                out[os.path.relpath(p, root).lower().replace('\\', '/')] = p
    return out


def floats(b, o, n=16):
    c = b[o:o + n]
    c += bytes(n - len(c))
    return struct.unpack('<%df' % (n // 4), c)


def close(a, b):
    return all(x == y or (x == x and y == y and abs(x - y) <= 1e-4 * max(1.0, abs(x), abs(y))) for x, y in zip(a, b))


def main(argv):
    cap = int(argv[argv.index('--examples') + 1]) if '--examples' in argv else 3
    limit = int(argv[argv.index('--limit') + 1]) if '--limit' in argv else 0
    r10, r30 = str(paths.ext10()), str(paths.ext30())
    m10s, m30s = mtbs(r10), mtbs(r30)
    common = sorted(set(m10s) & set(m30s))
    if limit:
        common = common[:limit]
    d10, d30 = shd.shader_dir('1.0'), shd.shader_dir('3.0')
    shared = set(f[:-4] for f in os.listdir(d10) if f.endswith('.shd')) & set(f[:-4] for f in os.listdir(d30) if f.endswith('.shd'))
    tally, ex = Counter(), defaultdict(list)
    stats = Counter()

    def add(key, e):
        tally[key] += 1
        if len(ex[key]) < cap:
            ex[key].append(e)

    for rel in common:
        try:
            a = matp.Bundle(open(m10s[rel], 'rb').read(), game=1)
            c = matp.Bundle(open(m30s[rel], 'rb').read(), game=3)
        except Exception as e:
            stats['bundle unreadable'] += 1
            continue
        stats['bundles'] += 1
        by30 = {}
        for m in c.materials:
            by30.setdefault(m['name'] or '#%08x' % m['hash'], m)
        for m in a.materials:
            key = m['name'] or '#%08x' % m['hash']
            n = by30.get(key)
            if not n:
                stats['material only in 1.0'] += 1
                continue
            stats['material pairs'] += 1
            where = '%s:%s' % (rel.rsplit('/', 1)[-1], key[:24])
            if m['shader'] != n['shader']:
                add('different shader', '%s 1.0 %s 3.0 %s' % (where, m['shader'][:8], n['shader'][:8]))
                continue
            if m['shader'] not in shared:
                continue
            stats['same shared shader'] += 1
            s10, s30 = shd.load(m['shader'], '1.0'), shd.load(m['shader'], '3.0')
            L = gen30.layout(s10)
            oursB = gen30.block_b30(m['B'], L)
            off10A = {p['hash']: p['offset'] for p in s10['params'] if p['block'] == 'A'}
            for p in s30['params']:
                nm = shd.nm(p['hash'])
                v30 = floats(n['A'] if p['block'] == 'A' else n['B'], p['offset'])
                if p['block'] == 'A':
                    if p['hash'] not in off10A:
                        add('A param missing in 1.0: %s' % nm, where)
                        continue
                    v = floats(m['A'], off10A[p['hash']])
                else:
                    if p['hash'] not in L['B']:
                        add('B param missing in ours: %s' % nm, where)
                        continue
                    v = floats(oursB, L['B'][p['hash']])
                stats['param compares'] += 1
                if not close(v, v30):
                    add('value differs: %s' % nm, '%s ours %s 3.0 %s' % (where, tuple(round(x, 4) for x in v), tuple(round(x, 4) for x in v30)))
            # slots: by name
            s10names = [shd.nm(h) for h in s10['slotsB']]
            for k, h in enumerate(s30['slotsB']):
                nm = shd.nm(h)
                i30 = c.tex_slots(n)[k]
                t30 = c.textures[i30].hex() if i30 != 0xffffffff and i30 < len(c.textures) else None
                if nm not in s10names:
                    add('slot only in 3.0: %s (%s)' % (nm, 'set' if t30 else 'unset'), where)
                    continue
                i10 = a.tex_slots(m)[s10names.index(nm)]
                t10 = a.textures[i10].hex() if i10 != 0xffffffff and i10 < len(a.textures) else None
                stats['slot compares'] += 1
                if (t10 is None) != (t30 is None):
                    add('slot %s: 1.0 %s, 3.0 %s' % (nm, 'set' if t10 else 'unset', 'set' if t30 else 'unset'), '%s 1.0 %s 3.0 %s' % (where, t10, t30))
                elif t10 and t10 != t30:
                    add('slot %s: different texture' % nm, '%s 1.0 %s 3.0 %s' % (where, t10, t30))
    print(dict(stats))
    for k, v in sorted(tally.items(), key=lambda kv: -kv[1]):
        print('%5d  %s' % (v, k))
        for e in ex[k]:
            print('         %s' % e)


if __name__ == '__main__':
    main(sys.argv[1:])

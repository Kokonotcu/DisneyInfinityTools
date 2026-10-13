"""How shared shaders render, ours vs 3.0: for every shader shipped in both games and every paired pass, both pixel
(and vertex) programs disassembled and summarised -- what each output register is written with (the last
instruction writing each component: op, and the literal when it is one), which textures it samples, whether it
discards -- tallied per pass kind (technique / pass index / word) over all shared shaders.

python rendercmp.py [--kind <tech/idx>] [--top N]
"""
import os, re, sys
from collections import Counter, defaultdict
import shd, sm3, gen30

DST = re.compile(r'^\s*(\w+)(?:_sat)?\s+(o\d+|oDepth)(?:\.(\w+))?\s*,\s*(.*)$')


def summary(dxbc):
    """-> (frozenset of 'oN.c=op[ literal]', frozenset of sampled t registers, discards?)"""
    if not dxbc or dxbc[:4] != b'DXBC':
        return None
    text = sm3.d3ddisasm(dxbc)
    last = {}
    tex, disc = set(), False
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith('//') or line.startswith('dcl_'):
            continue
        for t in re.findall(r'\bt(\d+)\.', line):
            if line.split()[0].startswith(('sample', 'ld', 'resinfo', 'gather')):
                tex.add(int(t))
        if line.split()[0] in ('discard_nz', 'discard_z'):
            disc = True
        m = DST.match(line)
        if m:
            op, reg, mask, src = m.group(1), m.group(2), m.group(3) or 'xyzw', m.group(4)
            lit = re.match(r'^l\(([^)]*)\)$', src.strip())
            for c in mask:
                last['%s.%s' % (reg, c)] = op + (' ' + lit.group(1).split(',')['xyzw'.index(c)].strip() if lit and len(lit.group(1).split(',')) == 4 else (' lit' if lit else ''))
    return frozenset('%s=%s' % kv for kv in sorted(last.items())), frozenset(tex), disc


def main(argv):
    top = int(argv[argv.index('--top') + 1]) if '--top' in argv else 6
    only = argv[argv.index('--kind') + 1] if '--kind' in argv else None
    d1, d3 = shd.shader_dir('1.0'), shd.shader_dir('3.0')
    both = sorted(set(f[:-4] for f in os.listdir(d1) if f.endswith('.shd')) & set(f[:-4] for f in os.listdir(d3) if f.endswith('.shd')))
    per = defaultdict(lambda: {'n': 0, 'ours': Counter(), 'ref': Counter(), 'otex': Counter(), 'rtex': Counter(), 'odisc': 0, 'rdisc': 0, 'ex': []})
    for sh in both:
        try:
            g = gen30.generate(sh, True)
        except Exception:
            continue
        ours, ref = shd.parse(g['shd'], '3.0'), shd.load(sh, '3.0')
        tr = {t['hash']: t for t in ref['techs']}
        for t in ours['techs']:
            u = tr.get(t['hash'])
            for k in range(t['count']):
                a = ours['passes'][t['first'] + k]
                c = ref['passes'][u['first'] + k] if u and k < u['count'] else None
                kind = '%s/%d' % (shd.nm(t['hash']), k)
                if only and kind != only:
                    continue
                e = per[kind]
                e['n'] += 1
                sa = summary(g['programs'].get(a['ps'])) if a['ps'] else None
                sc = summary(shd.program('3.0', c['ps'], 'fp')) if c and c['ps'] else None
                e['ours'][sa[0] if sa else 'no PS / dropped'] += 1
                e['ref'][sc[0] if sc else ('pass dropped in 3.0' if not c else 'no PS')] += 1
                if sa:
                    e['otex'][tuple(sorted(sa[1]))] += 1
                    e['odisc'] += sa[2]
                if sc:
                    e['rtex'][tuple(sorted(sc[1]))] += 1
                    e['rdisc'] += sc[2]
                if len(e['ex']) < 2:
                    e['ex'].append(sh[:8])
    for kind, e in sorted(per.items(), key=lambda kv: -kv[1]['n']):
        print('==== %s  (%d passes, e.g. %s)  discards: ours %d, 3.0 %d' % (kind, e['n'], e['ex'], e['odisc'], e['rdisc']))
        for label, cnt in (('3.0 ', e['ref']), ('ours', e['ours'])):
            for outs, n in cnt.most_common(top):
                print('   %s %4d  %s' % (label, n, ' '.join(sorted(outs)) if isinstance(outs, frozenset) else outs))
        print('   3.0  textures:', e['rtex'].most_common(4))
        print('   ours textures:', e['otex'].most_common(4))


if __name__ == '__main__':
    main(sys.argv[1:])

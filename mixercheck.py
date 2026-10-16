"""Oracle for octtest --mixer (mixer10): reads 1.0's GameDB/Core/Mixer.lua from the install's root archives with its own
Lua-table reader and recomputes the at-rest 'Mixer' fader source per group by 1.0's rules (FUN_007141d0 / FUN_00713490 /
FUN_00713290), then compares with octtest's MIXER/CAT lines.
usage: mixercheck.py <1.0 install> <octtest --mixer output>"""
import sys, os, re, glob, zipfile, math

def root_file(inst, path):
    for z in sorted(glob.glob(os.path.join(inst, 'assets', '*.zip'))):
        names = {n.lower(): n for n in zipfile.ZipFile(z).namelist()}
        if path.lower() in names:
            return zipfile.ZipFile(z).read(names[path.lower()]).decode('latin1')
    raise SystemExit('missing ' + path)

TOK = re.compile(r'\s+|--[^\n]*|"(?:\\.|[^"\\])*"|[A-Za-z_]\w*|-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?|[{}=,;]')

def tokens(s):
    out, i = [], 0
    while i < len(s):
        m = TOK.match(s, i)
        if not m: raise ValueError('bad char %r at %d' % (s[i], i))
        t = m.group(0); i = m.end()
        if t.isspace() or t.startswith('--'): continue
        out.append(t)
    return out

def parse(ts, i):
    t = ts[i]
    if t == '{':
        d, arr, i = {}, [], i + 1
        while ts[i] != '}':
            if re.match(r'[A-Za-z_]\w*$', ts[i]) and ts[i + 1] == '=' and ts[i] not in ('true', 'false', 'nil'):
                k = ts[i]; v, i = parse(ts, i + 2); d[k] = v
            else:
                v, i = parse(ts, i); arr.append(v)
            if ts[i] in (',', ';'): i += 1
        d['#'] = arr
        return d, i + 1
    if t.startswith('"'): return t[1:-1], i + 1
    if t == 'true': return True, i + 1
    if t == 'false': return False, i + 1
    if t == 'nil': return None, i + 1
    return float(t), i + 1

def clamp(v, lo, hi): return lo if v < lo else hi if v > hi else v

def main(inst, out):
    core = root_file(inst, 'GameDB/Core/CoreDataMap.lua')
    paths = re.findall(r'Mixer\|([ -{}~]+)', core)
    src = root_file(inst, paths[0])
    ts = tokens(src)
    assert ts[0] == 'Mixer' and ts[1] == '='
    m, _ = parse(ts, 2)
    master = 10 ** (clamp(m['GlobalSettings'].get('MasterVolume', 0), -60, 60) / 20)
    want = {}
    for g in m['Groups']['#']:
        s = 0.0
        for p in m['Presets']['#']:
            if not p.get('ActivateOnStart'): continue
            gain = clamp(p.get('Gain', 1), 0, 1)
            for e in p['#']:
                if e.get('Name') != g['Name']: continue
                for par in e['#']:
                    if par.get('Parameter') == 'Volume':
                        x = clamp(1 - (1 - 10 ** (clamp(par['Value'], -60, 60) / 20)) * gain, 1e-5, 1e4)
                        s += clamp(20 * math.log10(x), -60, 60)
        want[g['Name']] = clamp(10 ** (clamp(s, -60, 60) / 20) * master, 0, 1)
    got, cat = {}, {}
    for line in open(out, encoding='latin1'):
        f = line.split()
        if line.startswith('MIXER group'): got[f[2]] = float(f[3])
        if line.startswith('CAT '): cat[f[1]] = (f[2], float(f[3]))
    bad = 0
    for k, v in sorted(want.items()):
        ok = k in got and abs(got[k] - v) < 1e-5
        bad += not ok
        print('%-40s oracle %.6f octtest %s %s' % (k, v, got.get(k), 'OK' if ok else 'MISMATCH'))
    # applied: a category that exists carries mixer x the 0.5 music option the test sets on music / ui/songs
    for k, (state, v) in sorted(cat.items()):
        if state != 'exists': continue
        exp = want.get(k, 1.0) * (0.5 if k in ('music', 'ui/songs') else 1.0)
        ok = abs(v - exp) < 1e-5
        bad += not ok
        print('CAT %-36s expected %.6f FMOD %.6f %s' % (k, exp, v, 'OK' if ok else 'MISMATCH'))
    print('groups', len(want), 'mismatches', bad)
    return 1 if bad else 0

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('inst', help='the 1.0 install folder')
    ap.add_argument('out', help='octtest --mixer output')
    a = ap.parse_args()
    sys.exit(main(a.inst, a.out))

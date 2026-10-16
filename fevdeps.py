"""fevdeps.py <1.0 install> <octtest --fevdeps output> -- the plugin's 'Load' lists (read through FMOD from each FEV's stem
group and the groups under it, as 1.0 FUN_011348e0 walks them) vs a byte scan of every 'Load' string property in the FEV
files, split by 1.0 FUN_01134440's rule (' ', ',', ';'; no extension = .fev; 'file:group' kept). A difference is either a
'Load' on a group the stem request never walks (Initialize.fev's is on 'Permanent', requested as such) or a property
the naive scan misreads."""
import argparse, struct, os, re
from fevcheck import fevs10


def split(v):
    out = []
    for tok in re.split(r'[ ,;]+', v):
        if not tok: continue
        f, _, g = tok.partition(':')
        if '.' not in f: f += '.fev'
        out.append(f + (':' + g if g else ''))
    return out


def main(inst, out):
    scan = {}
    for p in fevs10(inst).values():
        b = open(p, 'rb').read(); vals = []
        i = b.find(b'Load\x00')
        while i >= 0:
            t, l = struct.unpack_from('<II', b, i + 5)
            if t == 2 and 0 < l < 400: vals.append(b[i + 13:i + 13 + l].split(b'\0')[0].decode('latin-1'))
            i = b.find(b'Load\x00', i + 1)
        scan[os.path.basename(p)] = [x for v in vals for x in split(v)]
    got = {}
    for line in open(out, encoding='latin-1'):
        if line.startswith('DEPS '):
            parts = line.rstrip('\n').split(' ', 2)
            got[parts[1]] = [x for x in parts[2].split('|') if x] if len(parts) > 2 else []
    diff = [k for k in scan if scan[k] != got.get(k)]
    print('files', len(scan), 'same', len(scan) - len(diff), 'differ', len(diff))
    for k in diff: print('  %s scan %s fmod %s' % (k, scan[k], got.get(k)))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('inst', help='the 1.0 install folder (the one octtest --fevdeps was given)')
    ap.add_argument('out', help='octtest --fevdeps output')
    a = ap.parse_args()
    main(a.inst, a.out)

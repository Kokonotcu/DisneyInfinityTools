"""fevcheck.py <octtest --fmod output> -- checks the plugin's FMOD registry against the FEV files themselves (STRR string
tables parsed here, not by FMOD):
  1. every registered event (EV line) is a string of its own .fev
  2. every MISS name: which 1.0 .fev string tables hold it at all (all 316 files in the 1.0 sound folder)
  3. the CUE list of the last Music_<X>.fev against the cue names of that file's 'cues' chunk"""
import sys, os, glob, struct, collections, re
SND = r'D:\SteamLibrary\steamapps\common\Disney Infinity Gold Edition\assets\sound'

def strr(p):
    d = open(p, 'rb').read()
    i = d.find(b'STRR')
    if i < 0: return []
    n = struct.unpack_from('<I', d, i + 4)[0]; s = d[i + 8:i + 8 + n]
    cnt = struct.unpack_from('<I', s, 0)[0]; offs = struct.unpack_from('<%dI' % cnt, s, 4); base = 4 + 4 * cnt
    return [s[base + o:s.index(b'\0', base + o)].decode('latin-1') for o in offs]

fevs = {os.path.basename(p).lower(): p for p in glob.glob(SND + r'\*.fev')}
tables = {k: set(strr(p)) for k, p in fevs.items()}
lines = open(sys.argv[1], encoding='latin-1').read().splitlines()
bad = 0; n = 0
for l in lines:
    if not l.startswith('EV\t'): continue
    _, fev, name, flags = l.split('\t')
    n += 1
    if name not in tables.get(fev, ()): bad += 1; print('NOT IN ITS FEV', fev, name)
print('1. registered events: %d, not a string of their own .fev: %d' % (n, bad))
holders = collections.defaultdict(list)
for k, t in tables.items():
    for s in t: holders[s].append(k)
print('2. misses and the .fev string tables that hold the name:')
for l in lines:
    f = l.split('\t')
    if len(f) == 4 and f[3].startswith('MISS') and f[0] != 'cue':
        print('   %-5s %-5s %-36r %s' % (f[0], f[1], f[2], holders.get(f[2], []) or 'in no 1.0 .fev'))
cues = [l.split('\t')[1] for l in lines if l.startswith('CUE\t')]
last = [l for l in lines if re.match(r'audio10: loaded Music_', l)]
if last:
    fn = last[-1].split()[2].rstrip(':')
    d = open(fevs[fn.lower()], 'rb').read()
    i = d.find(b'cues')
    raw = set(re.findall(rb'[A-Za-z][A-Za-z0-9_]{3,}', d[i:i + struct.unpack_from('<I', d, i + 4)[0] + 8])) if i >= 0 else set()
    names = {c for c in cues}
    print('3. %s: %d cues from FMOD; in the cues chunk bytes: %d; not found there: %s' % (
        fn, len(cues), sum(1 for c in cues if c.encode() in raw), [c for c in cues if c.encode() not in raw]))

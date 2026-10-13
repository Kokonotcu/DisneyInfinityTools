"""vocheck.py <octtest --vo output> <dir with .dlg30>  -- vo10 oracle, reads the 1.0 files on its own:
 DLG: every 1.0 v11 script vs its transcode. The 3.0 file is walked as 3.0 walks it (FUN_0073fb90 header checks and
      tables; FUN_0073daf0 range path: packed upper prefix -> range -> class byte -> entry; named path: entry by hash),
      and every 1.0 line (1.0 FUN_0071c020 '%c%c%03u' map, last insert wins: FUN_00c981e0) and named entry
      (1.0 FUN_00718650 cases 1/2/3/4) must give the same prefix/number/event (h131 of the 3.0 event string == the 1.0
      event hash), priority (1.0 >> 1) and flags.
 NAME: every NAME line of octtest (3.0-layout bank resolved by 3.0's reader) vs the .name read by 1.0 FUN_01132360."""
import struct, sys, os, glob, collections
D = r'D:\SteamLibrary\steamapps\common\Disney Infinity Gold Edition\assets\sound\dialog'
def h131(s):
    h = 0
    for c in s: h = (h * 131 + c) & 0xffffffff
    return h
bad = collections.Counter(); ok = collections.Counter()
def evok(s, ev): return h131(s) == ev or s == b'%08x' % ev   # unresolved: in no 1.0 FEV, kept as hex
def check(cond, what):
    (ok if cond else bad)[what] += 1
    if not cond and bad[what] <= 3: print('BAD', what)

for p in sorted(glob.glob(D + r'\*.dlg')):
    b = open(p, 'rb').read()
    if struct.unpack_from('<I', b, 0)[0] != 11: continue
    name = os.path.splitext(os.path.basename(p))[0]
    t = open(os.path.join(sys.argv[2], name + '.dlg30'), 'rb').read()
    # 3.0 side, as FUN_0073fb90 reads it
    assert t[:4] == b'DLG\0' and t[4] == 0x15 and (t[5] & 1) == 0 and (t[5] & 2), name
    ne, nn, nr = struct.unpack_from('<HHH', t, 6)
    te, tn, tr = (struct.unpack_from('<I', t, o)[0] for o in (0x10, 0x18, 0x20))
    assert max(te + ne * 8, tn + nn * 8, tr + nr * 8) <= len(t)
    evs = [t[o:t.index(b'\0', o)] for o in (struct.unpack_from('<I', t, te + 8 * i)[0] for i in range(ne))]
    ents = [struct.unpack_from('<II', t, tn + 8 * i) for i in range(nn)]
    byhash = {}
    for h, o in ents: byhash.setdefault(h, o)
    ranges = {}
    for i in range(nr):
        pfx, o = struct.unpack_from('<II', t, tr + 8 * i)
        lo, hi = struct.unpack_from('<HH', t, o)
        ranges[pfx] = (lo, hi, o)
    # 1.0 side
    n = struct.unpack_from('<I', b, 4)[0]
    lines = {}
    want = set()
    named = []
    for i in range(n):
        h, off = struct.unpack_from('<II', b, 0x10 + 8 * i)
        e = b[off:]
        if e[0] == 0:
            ev, cnt = struct.unpack_from('<IH', e, 4)
            want.add(ev)
            for k in range(cnt):
                c0, c1, num = struct.unpack_from('<BBH', e, 0xc + 4 * k)
                lines[(c0, c1, num)] = (ev, e[1] >> 1, e[2])      # last insert wins (FUN_00c981e0)
        else:
            named.append((h, off))
    for (c0, c1, num), (ev, pri, fl) in lines.items():
        key = (c0 & 0xdf) | (c1 & 0xdf) << 8                    # FUN_0182bc20's return value
        r = ranges.get(key)
        cls = t[r[2] + 5 + num - r[0]] if r and r[0] <= num <= r[1] else 0xff
        ce = t[ents[cls][1]:] if cls != 0xff and cls < nn else None
        check(ce is not None and ce[0] == 0 and evok(evs[ce[4]], ev) and ce[1] == pri and ce[2] == fl, 'line ' + name)
    for h, off in named:
        e = b[off:]
        o3 = byhash.get(h)
        if o3 is None: check(False, 'named missing ' + name); continue
        x = t[o3:]
        if e[0] == 1:
            cnt = e[0xc]
            items1 = [struct.unpack_from('<BBHI', e, 0x10 + 8 * k) for k in range(cnt)]
            items3 = [struct.unpack_from('<BBxxHBx', x, 0x10 + 8 * k) for k in range(x[0xc])]
            good = x[0] == 1 and x[1] == e[1] >> 1 and x[2] == e[2] and x[5:7] == e[5:7] and x[8:0xd] == e[8:0xd] and x[0xe] == e[0xe] and \
                   [(a, b_, c) for a, b_, c, _ in items1] == [(a, b_, c) for a, b_, c, _ in items3] and \
                   all(evok(evs[i3[3]], i1[3]) for i1, i3 in zip(items1, items3))
            check(good, 'variants ' + name)
        elif e[0] == 2:
            cnt = e[0xc]
            base = off + 0x10 + 12 * cnt
            good = x[0] == 2 and x[1] == e[1] >> 1 and x[0xc] == cnt and x[5:7] == e[5:7] and x[0xe] == e[0xe]
            for k in range(cnt):
                ev, s0, s1, c, last, lp = struct.unpack_from('<IBBBBI', b, off + 0x10 + 12 * k)
                l1 = struct.unpack_from('<%dH' % c, b, base + lp)
                so = 0x10 + 16 * k   # FUN_0073ae20: sub (k+1)*16, list ptr at +0, speaker +8, event +0xc, count +0xd
                lp3, spk, e3, c3 = struct.unpack_from('<I4xIBB', x, so)
                l3 = struct.unpack_from('<%dH' % c3, t, lp3)
                good = good and spk == s0 | s1 << 8 and evok(evs[e3], ev) and l3 == l1
            check(good, 'speakers ' + name)
        else:
            c0, c1, num, ev = struct.unpack_from('<BBHI', e, 0xc)
            i3 = struct.unpack_from('<BBxxHBx', x, 0x10)
            check(x[0] == 1 and x[0xc] == 1 and x[6] == 1 and x[0xe] == 1 and x[5] == 0xff and i3[:3] == (c0, c1, num) and evok(evs[i3[3]], ev), 'once ' + name)

# NAME lines vs 1.0 FUN_01132360
names = {}
for line in open(sys.argv[1], encoding='latin-1'):
    if not line.startswith('NAME '): continue
    _, f, lid, v, nm, sub, verdict = line.split()
    names[(f, int(lid[2:]), int(v))] = (nm, int(sub), verdict)
seen = 0
for p in sorted(glob.glob(D + r'\ENU\*.fsb')):
    f = os.path.basename(p)
    nb = open(p[:-4] + '.name', 'rb').read()
    ver, pfx, tl, lo, hi, subs = struct.unpack_from('<H2sHHHH', nb, 0)
    for l in range(lo, hi + 1):
        v = struct.unpack_from('<H', nb, 0x10 + 2 * (l - lo))[0]
        if v == 0xffff: continue
        cnt = v >> 12 if v >> 12 > 1 else 1
        for k in range(cnt):
            idx = (v & 0xfff) + k
            so = struct.unpack_from('<H', nb, 0x10 + 2 * (tl + idx))[0]
            exp = nb[so:nb.index(b'\0', so)].decode('latin-1')
            got = names.get((f, l, k))
            seen += 1
            check(got is not None and got[0] == exp and got[1] == idx and got[2] == 'ok', 'name ' + f)
check(seen == len(names), 'NAME line count (octtest %d, .name %d)' % (len(names), seen))
tot = collections.Counter()
for k, v in ok.items(): tot[k.split()[0]] += v
print('ok', dict(tot))
print('bad', dict(bad) or 0)

"""vocheck.py <1.0 install> <octtest --vo output> <dir with .dlg30> [<dir of decrypted 3.0 scripts>]  -- vo10 oracle;
reads the source files on its own:
 DLG:  every 1.0 v11 script vs its transcode (and vs the merge with 3.0's own script of that name, when given). The 3.0
       file is walked as 3.0 walks it (FUN_0073fb90 header checks and tables; FUN_0073daf0 range path: packed upper
       prefix -> range -> class byte -> entry, event 0 = the range default; named path: entry by hash). Every 1.0 line
       (1.0 FUN_0071c020 '%c%c%03u' map, last insert wins: FUN_00c981e0) and named entry (1.0 FUN_00718650 cases
       1/2/3/4) must give the same prefix/number/event (h131 of the 3.0 event string == the 1.0 event hash),
       priority (1.0 >> 1) and flags; in a merge, every 3.0 named entry and range line 1.0 does not define must give
       what 3.0's own file gives.
 NAME: every NAME line of octtest (3.0-layout bank resolved by 3.0's reader) vs the .name read by 1.0 FUN_01132360."""
import argparse, struct, os, glob, collections
def h131(s):   # bytes sign-extended, as the engines' FUN_0040bac0 / FUN_0040bb50 (movsx)
    h = 0
    for c in s: h = (h * 131 + (c - 256 if c >= 0x80 else c)) & 0xffffffff
    return h
bad = collections.Counter(); ok = collections.Counter()
def evok(s, ev): return h131(s) == ev or s == b'%08x' % ev   # unresolved: in no 1.0 FEV, kept as hex
def check(cond, what):
    (ok if cond else bad)[what] += 1
    if not cond and bad[what] <= 3: print('BAD', what)

def read3(t, name):
    """a 3.0 DLG as FUN_0073fb90 / FUN_0073daf0 read it"""
    assert t[:4] == b'DLG\0' and t[4] == 0x15 and (t[5] & 1) == 0 and (t[5] & 2), name
    ne, nn, nr = struct.unpack_from('<HHH', t, 6)
    te, tn, tr = (struct.unpack_from('<I', t, o)[0] for o in (0x10, 0x18, 0x20))
    assert max(te + ne * 8, tn + nn * 8, tr + nr * 8) <= len(t), name
    evs = [t[o:t.index(b'\0', o)] for o in (struct.unpack_from('<I', t, te + 8 * i)[0] for i in range(ne))]
    ents = [struct.unpack_from('<II', t, tn + 8 * i) for i in range(nn)]
    byhash = {}
    for h, o in ents:
        if t[o] != 0: byhash[h] = o            # named lookups (a class hash never answers a named request)
    ranges = {}
    for i in range(nr):
        pfx, o = struct.unpack_from('<II', t, tr + 8 * i)
        lo, hi = struct.unpack_from('<HH', t, o)
        ranges[pfx] = (lo, hi, o)
    def line(key, num):                        # -> (priority, flags, event string) or None
        r = ranges.get(key)
        if not r or not r[0] <= num <= r[1]: return None
        c = t[r[2] + 5 + num - r[0]]
        if c == 0xff or c >= nn: return None
        e = t[ents[c][1]:]
        if e[0] != 0: return None
        return e[1], e[2], evs[e[4] or t[r[2] + 4]]
    def lines():
        for key, (lo, hi, o) in ranges.items():
            for num in range(lo, hi + 1):
                if t[o + 5 + num - lo] != 0xff: yield key, num
    def named(h):                              # -> a comparable form of the entry
        o = byhash.get(h)
        if o is None: return None
        x = t[o:]
        if x[0] == 1:
            return (1, x[1], x[2], x[4:7], x[8:0xd], x[0xe],
                    [(x[0x10 + 8 * k:0x14 + 8 * k], struct.unpack_from('<H', x, 0x14 + 8 * k)[0], evs[x[0x16 + 8 * k]]) for k in range(x[0xc])])
        if x[0] == 2:
            subs = []
            for k in range(x[0xc]):
                lp, spk, e3, c3 = struct.unpack_from('<I4xIBB', x, 0x10 + 16 * k)
                subs.append((spk, evs[e3], struct.unpack_from('<%dH' % c3, t, lp)))
            return (2, x[1], x[2], x[4:7], x[8:0xd], x[0xe], subs)
        if x[0] == 3:
            items = []
            for k in range(x[4]):
                pfx, num, so = struct.unpack_from('<IH2xI', x, 8 + 16 * k)
                items.append((pfx, num, t[so:t.index(b'\0', so)]))
            return (3, x[1], x[2], items)
        return ('?', x[0])
    return dict(line=line, lines=lines, named=named, byhash=byhash)

def check10(b, T, name):
    """the 1.0 script's lines and named entries through a 3.0 file; -> (line keys, named hashes) 1.0 defines"""
    n = struct.unpack_from('<I', b, 4)[0]
    lines = {}
    nm = []
    for i in range(n):
        h, off = struct.unpack_from('<II', b, 0x10 + 8 * i)
        e = b[off:]
        if e[0] == 0:
            ev, cnt = struct.unpack_from('<IH', e, 4)
            for k in range(cnt):
                c0, c1, num = struct.unpack_from('<BBH', e, 0xc + 4 * k)
                lines[((c0 & 0xdf) | (c1 & 0xdf) << 8, num)] = (ev, e[1] >> 1, e[2])
        else:
            nm.append((h, off))
    for (key, num), (ev, pri, fl) in lines.items():
        got = T['line'](key, num)
        check(got is not None and got[0] == pri and got[1] == fl and evok(got[2], ev), 'line ' + name)
    for h, off in nm:
        e = b[off:]
        x = T['named'](h)
        if x is None: check(False, 'named missing ' + name); continue
        if e[0] == 1:
            items = [struct.unpack_from('<2sHI', e, 0x10 + 8 * k) for k in range(e[0xc])]
            good = x[0] == 1 and x[1] == e[1] >> 1 and x[2] == e[2] and x[3] == e[4:7] and x[4] == e[8:0xd] and x[5] == e[0xe] and \
                   len(items) == len(x[6]) and all(a[0] == i[0] + b'\0\0' and a[1] == i[1] and evok(a[2], i[2]) for i, a in zip(items, x[6]))
            check(good, 'variants ' + name)
        elif e[0] == 2:
            cnt = e[0xc]
            base = off + 0x10 + 12 * cnt
            good = x[0] == 2 and x[1] == e[1] >> 1 and len(x[6]) == cnt and x[3] == e[4:7] and x[5] == e[0xe]
            for k in range(cnt):
                ev, s0, s1, c, last, lp = struct.unpack_from('<IBBBBI', b, off + 0x10 + 12 * k)
                l1 = struct.unpack_from('<%dH' % c, b, base + lp)
                spk, evs3, l3 = x[6][k]
                good = good and spk == s0 | s1 << 8 and evok(evs3, ev) and l3 == l1
            check(good, 'speakers ' + name)
        else:
            c0, c1, num, ev = struct.unpack_from('<BBHI', e, 0xc)
            good = x[0] == 1 and x[3][1:3] == bytes([0xff, 1]) and x[5] == 1 and len(x[6]) == 1 and \
                   x[6][0][0] == bytes([c0, c1, 0, 0]) and x[6][0][1] == num and evok(x[6][0][2], ev)
            check(good, 'once ' + name)
    return set(lines), {h for h, _ in nm}

def main(inst, out, dlg30, scripts30=None):
    D = os.path.join(inst, 'assets', 'sound', 'dialog')
    dlgs = sorted(glob.glob(os.path.join(D, '*.dlg')))
    if not dlgs:
        raise SystemExit('no .dlg files in %s: pass the 1.0 install folder' % D)
    for p in dlgs:
        b = open(p, 'rb').read()
        if struct.unpack_from('<I', b, 0)[0] != 11: continue
        name = os.path.splitext(os.path.basename(p))[0]
        check10(b, read3(open(os.path.join(dlg30, name + '.dlg30'), 'rb').read(), name), name)
        mp = os.path.join(dlg30, name + '.merged.dlg30')
        src3 = os.path.join(scripts30, '3.0_assets_sound_dialog_' + name.lower() + '.dlg') if scripts30 is not None else ''
        if not (os.path.exists(mp) and os.path.exists(src3)): continue
        M = read3(open(mp, 'rb').read(), name + ' merged')
        lines10, named10 = check10(b, M, name + ' merged')
        S = read3(open(src3, 'rb').read(), name + ' 3.0')
        for key, num in S['lines']():
            if (key, num) in lines10: continue
            check(M['line'](key, num) == S['line'](key, num), '3.0 line kept ' + name)
        for h in S['byhash']:
            if h in named10: continue
            check(M['named'](h) == S['named'](h), '3.0 named kept ' + name)

    # NAME lines vs 1.0 FUN_01132360
    names = {}
    for line in open(out, encoding='latin-1'):
        if not line.startswith('NAME '): continue
        _, f, lid, v, nm, sub, verdict = line.split()
        names[(f, int(lid[2:]), int(v))] = (nm, int(sub), verdict)
    seen = 0
    for p in sorted(glob.glob(os.path.join(D, 'ENU', '*.fsb'))):
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
    for k, v in ok.items():
        w = k.split()
        tot[' '.join(w[:3]) if k.startswith('3.0') else w[0] + (' merged' if 'merged' in k else '')] += v
    print('ok', dict(tot))
    print('bad', dict(bad) or 0)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('inst', help='the 1.0 install folder (the one octtest --vo was given)')
    ap.add_argument('out', help='octtest --vo output')
    ap.add_argument('dlg30', help="octtest --vo's out dir (.dlg30, .merged.dlg30)")
    ap.add_argument('scripts30', nargs='?', help='decrypted 3.0 dialog scripts (3.0_assets_sound_dialog_<name>.dlg)')
    a = ap.parse_args()
    main(a.inst, a.out, a.dlg30, a.scripts30)

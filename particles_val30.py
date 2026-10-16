"""Checks a 3.0-form .particledata the way 3.0's loader reads it (FUN_00672120 + FUN_00656bd0/FUN_00656c00/FUN_00671830),
independent of the transcoder: trailer, tables, record bounds, key indices < nkeys, emitter count byte vs record size,
effect slots naming emitters of the package (or of 3.0 universal).
usage: particles_val30.py [--ext30 DIR] file.particledata [...]"""
import argparse, struct, collections
import paths
import particles as P


def main(files):
    uni30 = set()
    d = paths.ext30('particles', 'universal', 'universal.particledata').read_bytes()
    r = P.parse(d)
    for e in r['ent']['emit']: uni30.add(struct.unpack_from('<I', e, 4)[0])

    bad_total = 0
    for fn in files:
        b = open(fn, 'rb').read()
        t = len(b) - 0x18
        tag, ver, size, keys, nkeys, ne, nf, np_, n14, n16 = struct.unpack_from('<HHIIHHHHHH', b, t)
        errs = collections.Counter()
        if (tag, ver) != (0x123, 0x2f): errs['trailer'] += 1
        if keys + 8 * nkeys > t or keys % 4: errs['key table'] += 1
        cur = t - 8 * n16 - 12 * n14
        par = [struct.unpack_from('<II', b, cur - 8 * np_ + 8 * i) for i in range(np_)]; cur -= 8 * np_
        eff = [struct.unpack_from('<II', b, cur - 8 * nf + 8 * i) for i in range(nf)]; cur -= 8 * nf
        emi = [struct.unpack_from('<IIBBHI', b, cur - 16 * ne + 16 * i) for i in range(ne)]; cur -= 16 * ne
        if cur != keys + 8 * nkeys: errs['tables not right after keys'] += 1
        offs = sorted([e[0] for e in emi] + [e[0] for e in eff] + [p[0] for p in par])
        if len(set(offs)) != len(offs): errs['shared record offset'] += 1
        nxt = {o: (offs[i + 1] if i + 1 < len(offs) else keys) for i, o in enumerate(offs)}
        def idx_ok(at, n):
            for k in range(n):
                v = struct.unpack_from('<H', b, at + 2 * k)[0]
                if v != 0xffff and v >= nkeys: return False
            return True
        hashes = set()
        for off, h, cls, z, mat, c in emi:
            hashes.add(h)
            if off % 4: errs['emitter misaligned'] += 1
            n = b[off + 0x121]
            if off + 0x124 + 2 * n > nxt[off]: errs['emitter keys past record'] += 1
            elif not idx_ok(off + 0x124, n): errs['emitter key index >= nkeys'] += 1
            if z: errs['emitter byte 9 nonzero'] += 1
            if c > 63: errs['emitter c'] += 1
        for off, h in par:
            if nxt[off] - off < 0x34: errs['param short'] += 1
            elif not idx_ok(off, 6): errs['param key index >= nkeys'] += 1
        miss = 0
        for off, h in eff:
            n = b[off]
            if n > 10 or off + 4 + 16 * n > nxt[off]: errs['effect slots past record'] += 1; continue
            for i in range(n):
                eh = struct.unpack_from('<I', b, off + 4 + 16 * i)[0]
                if eh not in hashes and eh not in uni30: miss += 1
        print('%-40s %d emitters %d effects %d params %d keys; effect slots naming no emitter here or in 3.0 universal: %d; %s'
              % (fn.split('\\')[-1], ne, nf, np_, nkeys, miss, dict(errs) or 'OK'))
        bad_total += sum(errs.values())
    print('TOTAL violations', bad_total)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*', metavar='file.particledata')
    paths.add_args(ap, 'ext30')
    main(paths.use(ap.parse_args()).files)

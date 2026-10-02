"""Octane scene container (.oct/.mer/.bent/.banm/.bct) — read, edit, write.

Port of oct.ps1's walker plus the write side it never had. Records keep their
payload as raw bytes, so save() is byte-exact unless something is edited.

  from oct import Oct
  f = Oct.load(path)
  for r in f.find(name='Vdata'): print(r.pool, r.ints())
  f.find1(name='NumPrimitives').set_int(2)
  f.save(out)

10-bit type field: type = flag & 0x3ff, level = flag >> 10.
"""
import struct, sys
from pathlib import Path

MAGIC = 0x45017629
HDR = 0x3c

# type -> fixed payload length
FIXED = {0x01: 0, 0x05: 2, 0x0B: 2, 0x0F: 4, 0x13: 4, 0x1B: 1, 0x1F: 3,
         0x11B: 2, 0x21B: 3, 0x31B: 4}
# type -> (bytes before the count, count width, element width)
ARR = {0x0A: (0, 1, 2), 0x12: (0, 1, 4), 0x1A: (0, 1, 1), 0x23: (0, 1, 1),
       0x11A: (0, 1, 2), 0x21A: (0, 1, 3), 0x31A: (0, 1, 4),
       0x4A: (0, 2, 2), 0x52: (0, 2, 4), 0x5A: (0, 2, 1), 0x15A: (0, 2, 2),
       0x25A: (0, 2, 3), 0x35A: (0, 2, 4), 0x63: (0, 2, 1), 0xA3: (0, 3, 1),
       0x16: (2, 1, 4), 0x56: (2, 2, 4),
       # count width steps by +0x40: u8 -> u16 -> 3-byte. Published for
       # 0x23/0x63/0xA3; 0x92 is the same step off 0x12/0x52 and is what
       # aladdin_ribbon_whitebox_2_4_2.oct stalls on.
       0x92: (0, 3, 4)}
FLOAT_T = {0x12, 0x52, 0x16, 0x56, 0x13}
NAME_T = {0x05, 0x0B, 0x0A, 0x4A, 0x0F, 0x1F}
# scalar/array type per integer width, for widening on write
SCALAR_W = {1: 0x1B, 2: 0x11B, 3: 0x21B, 4: 0x31B}
ARRAY_W = {1: 0x01A, 2: 0x11A, 3: 0x21A, 4: 0x31A}


def _u(b, w):
    return int.from_bytes(b[:w], 'little')


def _p(v, w):
    return int(v).to_bytes(w, 'little', signed=(v < 0))


def paylen(t, buf, q):
    """Payload length of a record whose payload starts at buf[q]. -1 = unknown."""
    if t in FIXED:
        return FIXED[t]
    if t in ARR:
        pre, cw, ew = ARR[t]   # bounded slice: buf[q + pre:] copied the rest of the file per record
        return pre + cw + int.from_bytes(buf[q + pre:q + pre + cw], 'little') * ew
    return -1


class Rec:
    __slots__ = ('f', 'level', 'type', 'name_idx', 'pay', 'pool', 'off')

    def __init__(self, f, level, type_, name_idx, pay):
        self.f, self.level, self.type, self.name_idx, self.pay = f, level, type_, name_idx, pay
        self.pool = None
        self.off = -1

    @property
    def name(self):
        return self.f.name(self.name_idx)

    def _split(self):
        """(prefix bytes, count, element width) for array types."""
        pre, cw, ew = ARR[self.type]
        return self.pay[:pre], _u(self.pay[pre:], cw), ew

    def ints(self):
        """Payload as a list of ints — works for both scalar and array types."""
        if self.type in FIXED:
            return [int.from_bytes(self.pay, 'little', signed=(self.type == 0x1B))]
        pre, n, ew = self._split()
        base = len(pre) + ARR[self.type][1]
        signed = self.type == 0x01A
        return [int.from_bytes(self.pay[base + i * ew: base + (i + 1) * ew], 'little', signed=signed)
                for i in range(n)]

    def floats(self):
        if self.type == 0x13:
            return [struct.unpack('<f', self.pay)[0]]
        pre, n, ew = self._split()
        base = len(pre) + ARR[self.type][1]
        return list(struct.unpack_from('<%df' % n, self.pay, base))

    def names(self):
        if self.type in (0x05, 0x0B):
            return [self.f.name(_u(self.pay, 2))]
        if self.type == 0x0F:
            return [self.f.name(_u(self.pay, 2)), self.f.name(_u(self.pay[2:], 2))]
        if self.type == 0x1F:
            return [self.f.name(_u(self.pay, 2)), self.f.name(self.pay[2:3], )]
        pre, n, ew = self._split()
        base = len(pre) + ARR[self.type][1]
        return [self.f.name(_u(self.pay[base + i * 2:], 2)) for i in range(n)]

    def value(self):
        t = self.type
        if t == 0x01:
            return None
        if t in NAME_T:
            return self.names()
        if t in FLOAT_T:
            v = self.floats()
            return v[0] if t == 0x13 else v
        if t in FIXED:
            return self.ints()[0]
        if t in (0x63, 0x23, 0x5A, 0xA3):
            return self.pay  # opaque blob
        return self.ints()

    # --- edits -------------------------------------------------------------
    def set_int(self, v):
        """Scalar write; widens the type if the value no longer fits."""
        assert self.type in FIXED and self.type != 0x13, self.type
        w = 1 if -128 <= v <= 127 else 2 if -32768 <= v <= 32767 else 3 if v < 1 << 23 else 4
        w = max(w, len(self.pay))          # never narrow: keeps diffs minimal
        self.type = SCALAR_W[w]
        self.pay = _p(v, w)
        return self

    def set_ints(self, vals):
        """Array write, keeping the element width unless a value outgrows it."""
        pre, _, ew = self._split()
        need = max((1 if -128 <= v <= 127 else 2 if v < 1 << 15 else 3 if v < 1 << 23 else 4)
                   for v in vals) if vals else 1
        ew = max(ew, need)
        _, cw, _ = ARR[self.type]
        wide = {t + 0x40 for t in ARRAY_W.values()}
        if self.type in ARRAY_W.values() or self.type in wide:
            was_wide = self.type in wide          # never narrow: keeps diffs minimal
            self.type = ARRAY_W[ew]
            cw = 1
            if was_wide or len(vals) > 0xff:      # count width steps by +0x40: u8 -> u16
                self.type += 0x40
                cw = 2
        assert len(vals) < 1 << (8 * cw), '%d values do not fit a %d-byte count' % (len(vals), cw)
        self.pay = bytes(pre) + _p(len(vals), cw) + b''.join(_p(v, ew) for v in vals)
        return self

    def set_floats(self, vals):
        if self.type == 0x13:
            self.pay = struct.pack('<f', vals[0] if isinstance(vals, (list, tuple)) else vals)
            return self
        pre, _, _ = self._split()
        _, cw, _ = ARR[self.type]
        self.pay = bytes(pre) + _p(len(vals), cw) + struct.pack('<%df' % len(vals), *vals)
        return self

    def raw(self):
        return struct.pack('<HH', (self.level << 10) | self.type, self.name_idx) + bytes(self.pay)

    def __repr__(self):
        v = self.value()
        if isinstance(v, (bytes, bytearray)):
            v = '<%d b>' % len(v)
        return 'L%-2d 0x%03X %s%s%s' % (self.level, self.type, '  ' * min(self.level, 12),
                                        self.name, '' if v is None else ' = %s' % (v,))


class Oct:
    def __init__(self):
        self.path = None
        self.names = []
        self.recs = []
        self.f14 = 0
        self.tail = b''      # bytes after the last parsed record (should be empty)

    # --- io ----------------------------------------------------------------
    @classmethod
    def load(cls, path):
        f = cls()
        f.path = Path(path)
        b = f.path.read_bytes()
        if len(b) < HDR or _u(b, 4) != MAGIC:
            raise ValueError('%s: bad magic' % path)
        nb, db, f.f14 = struct.unpack_from('<III', b, 12)
        ds = HDR + nb
        de = ds + db
        if de > len(b):
            raise ValueError('%s: size invariant broken' % path)
        f.names = b[HDR:ds].split(b'\0')[:-1]
        f.names = [n.decode('latin-1') for n in f.names]

        stack = [None] * 64
        p = ds
        while p + 4 <= de:
            flag, nidx = struct.unpack_from('<HH', b, p)
            t, lvl = flag & 0x3ff, flag >> 10
            n = paylen(t, b, p + 4)
            if n < 0 or p + 4 + n > de:
                raise ValueError('%s: unknown type 0x%X at +%d' % (path, t, p - ds))
            r = Rec(f, lvl, t, nidx, b[p + 4: p + 4 + n])
            r.off = p - ds
            if lvl < 64:
                stack[lvl] = r.name
                for z in range(lvl + 1, 64):
                    stack[z] = None
            for z in range(lvl, -1, -1):
                if stack[z] and stack[z].endswith('Pool'):
                    r.pool = stack[z]
                    break
            f.recs.append(r)
            p += 4 + n
        f.tail = b[p:de]
        return f

    def name(self, i):
        return self.names[i] if 0 <= i < len(self.names) else '?%d' % i

    def add_name(self, s):
        if s in self.names:
            return self.names.index(s)
        self.names.append(s)
        return len(self.names) - 1

    def to_bytes(self):
        nt = b''.join(n.encode('latin-1') + b'\0' for n in self.names)
        data = b''.join(r.raw() for r in self.recs) + self.tail
        h = bytearray(HDR)
        # 0x14 is the RECORD COUNT, and the engine ALLOCATES FROM IT. It matches
        # the walked count in 43,346 of 43,356 shipped files (the other 10 are
        # 0 -- an unwritten field, preserved here). Carrying the source file's
        # value over after adding records makes the loader reserve room for too
        # few objects and construct the last ones past the end of the block:
        # an undersized allocation whose overrun lands wherever the shortfall
        # puts it. Every build that added a node or a primitive did this; ones
        # that only removed records over-allocated and appeared to work, which
        # is why 1 and 2 primitives loaded and 3 never did.
        count = 0 if self.f14 == 0 else len(self.recs)
        struct.pack_into('<IIIIII', h, 0, MAGIC, 0x3F8CCCCD, 0, len(nt), len(data), count)
        return bytes(h) + nt + data

    def save(self, path):
        Path(path).write_bytes(self.to_bytes())
        return path

    # --- queries -----------------------------------------------------------
    def find(self, name=None, pool=None, type=None, level=None):
        return [r for r in self.recs
                if (name is None or r.name == name)
                and (pool is None or r.pool == pool)
                and (type is None or r.type == type)
                and (level is None or r.level == level)]

    def find1(self, **kw):
        h = self.find(**kw)
        if not h:
            raise KeyError('no record matching %s in %s' % (kw, self.path))
        return h[0]

    def idx(self, rec):
        """Position of rec. Cached: list.index was O(n) per call -- 21 s of a 413k-record in_city
        conversion. A stale entry is detected (recs[p] is not rec) and the map rebuilt once."""
        pos = getattr(self, '_pos', None)
        p = pos.get(id(rec)) if pos else None
        if p is not None and p < len(self.recs) and self.recs[p] is rec:
            return p
        self._pos = {id(r): i for i, r in enumerate(self.recs)}
        if id(rec) not in self._pos:
            raise ValueError('record not in this file')
        return self._pos[id(rec)]

    def block(self, rec):
        """(start, end) record indices for rec and its children."""
        i = self.idx(rec)
        j = i + 1
        while j < len(self.recs) and self.recs[j].level > rec.level:
            j += 1
        return i, j

    def clone_block(self, rec):
        """Deep copy of rec's subtree as a fresh list of records."""
        i, j = self.block(rec)
        return [Rec(self, r.level, r.type, r.name_idx, bytes(r.pay)) for r in self.recs[i:j]]

    def replace_block(self, rec, new_recs):
        i, j = self.block(rec)
        self.recs[i:j] = new_recs
        return self

    def dump(self, **kw):
        for r in (self.find(**kw) if kw else self.recs):
            print('  +%-7d %r' % (r.off, r))


def _roundtrip_one(p):
    """None if p round-trips byte-exactly, else (p, reason)."""
    try:
        f = Oct.load(p)
        if f.to_bytes() != p.read_bytes():
            return (p, 'mismatch')
        if f.tail:
            return (p, 'trailing bytes')
    except Exception as e:
        return (p, e)
    return None


def _selftest():
    """Round-trip every .oct we can reach; a codec that can't reproduce the
    input byte-for-byte cannot be trusted to edit one."""
    roots = [Path(r'D:\DI-Extract\3.0\assets\worlds'), Path(r'D:\DI-Extract\1.0\assets\worlds')]
    files = [p for r in roots if r.exists() for p in r.rglob('*.oct')]
    assert files, 'no .oct found to test against'
    from par import pmap
    bad = [r for r in pmap(_roundtrip_one, files) if r]
    print('round-trip %d/%d exact' % (len(files) - len(bad), len(files)))
    for p, e in bad[:10]:
        print('  FAIL', p.name, e)
    assert not bad, '%d files failed' % len(bad)


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--selftest':
        _selftest()
    elif len(sys.argv) > 1:
        f = Oct.load(sys.argv[1])
        print('== %s  names=%d  records=%d' % (f.path.name, len(f.names), len(f.recs)))
        kw = dict(a.split('=', 1) for a in sys.argv[2:])
        f.dump(**kw)
    else:
        print(__doc__)

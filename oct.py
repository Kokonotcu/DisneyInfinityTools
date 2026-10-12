"""Octane tuple files (.oct/.mer/.merdebug/.bent/.banm/.bct) -- read, edit, write.

Records keep their payload as raw bytes, so save() is byte-exact unless something is edited.

  from oct import Oct
  f = Oct.load(path)
  for r in f.find(name='Vdata'): print(r.pool, r.ints())
  f.find1(name='NumPrimitives').set_int(2)
  f.save(out)

Header (0x3c B): magic 0x45017629, 1.1f, 0, name table size, record data size, record count (0x14: the
engine allocates from it), zeros. Record: u16 flag (type = flag & 0x3ff, level = flag >> 10), u16 name index,
payload. The type is a bit field -- every type in 5,079 shipped 3.0 tuple files (3,905 .oct, 584 .mer,
584 .merdebug, 6 .bent) decodes by it, which no type table did (.mer files carry 0x17 and 0x27):
  bits 0-1  shape   1 node, 2 list (count + elements), 3 scalar (a blob is a scalar with a count)
  bit  2    keyed   payload starts with a u16 name (0x05 keyed node, 0x0F key+name, 0x1F key+int8 --
                    shipped as _DebugOnly_ParamValue = <param name>, 0 / 255)
  bits 3-5  kind    1 name (u16 index), 2 float32, 3 int, 4 byte (blob)
  bits 6-7  count   width - 1 of the element count (u8 / u16 / u24)
  bits 8-9  int     width - 1 of each int (1..4 bytes)
Ints are SIGNED: the only shipped values with the top bit set are LinkedNodeRefs/Nodes = -1 (0xff, 0xffff),
and the exporter picks the narrowest signed width (set_int/set_ints do the same, never narrowing).

python oct.py <file> [name=X pool=Y level=N]   dump records
python oct.py --selftest [--ext30 DIR] [--ext10 DIR]   round-trip every tuple file of both extracts
"""
import struct
from pathlib import Path

MAGIC = 0x45017629
HDR = 0x3c
EXTS = ('.oct', '.mer', '.merdebug', '.bent', '.banm', '.bct')
NODE, LIST, SCALAR = 1, 2, 3
NAME, FLOAT, INT, BYTE = 1, 2, 3, 4
_EL = {NAME: 2, FLOAT: 4, BYTE: 1}
_spec = {}


def spec(t):
    """type -> (shape, keyed, kind, count width, element width), or None if the bits do not form a type."""
    if t not in _spec:
        shape, keyed, kind, cw, iw = t & 3, (t >> 2) & 1, (t >> 3) & 7, ((t >> 6) & 3) + 1, ((t >> 8) & 3) + 1
        ok = (shape == NODE and kind == 0 and cw == iw == 1 or
              shape in (LIST, SCALAR) and kind in (NAME, FLOAT, INT, BYTE) and (kind == INT or iw == 1) and
              (shape == LIST or kind == BYTE or cw == 1))
        _spec[t] = (shape, keyed, kind, cw if shape == LIST or kind == BYTE else 0,
                    iw if kind == INT else _EL.get(kind, 0)) if ok else None
    return _spec[t]


def paylen(t, buf, q):
    """Payload length of a record whose payload starts at buf[q]. -1 = not a type."""
    s = spec(t)
    if s is None:
        return -1
    shape, keyed, kind, cw, ew = s
    pre = 2 * keyed
    if shape == NODE:
        return pre
    if not cw:
        return pre + ew
    return pre + cw + int.from_bytes(buf[q + pre:q + pre + cw], 'little') * ew


def _u(b, w):
    return int.from_bytes(b[:w], 'little')


def _width(vals):
    """Narrowest signed width holding every value."""
    w = 1
    for v in vals:
        while not -(1 << (8 * w - 1)) <= v < 1 << (8 * w - 1):
            w += 1
    assert w <= 4, 'value does not fit 32 bits'
    return w


def children(recs, deep=True):
    """{name: record} below recs[0], where recs is a record and its subtree (Oct.block, clone_block, a template
    block copied from another file). deep: every level, a later (deeper) record of the same name winning;
    otherwise direct children only."""
    lvl = recs[0].level
    return {r.name: r for r in recs[1:] if deep or r.level == lvl + 1}


class Rec:
    __slots__ = ('f', 'level', 'type', 'name_idx', 'pay', 'pool', 'off')

    def __init__(self, f, level, type_, name_idx, pay):
        self.f, self.level, self.type, self.name_idx, self.pay = f, level, type_, name_idx, pay
        self.pool = None
        self.off = -1

    @property
    def name(self):
        return self.f.name(self.name_idx)

    @property
    def key(self):
        """The u16 name a keyed record starts with, else None."""
        return self.f.name(_u(self.pay, 2)) if spec(self.type)[1] else None

    def _items(self):
        """(element width, [element bytes]) of the value part (key and count stripped)."""
        shape, keyed, kind, cw, ew = spec(self.type)
        p = 2 * keyed
        n = _u(self.pay[p:], cw) if cw else (0 if shape == NODE else 1)
        p += cw
        return ew, [self.pay[p + i * ew:p + (i + 1) * ew] for i in range(n)]

    def ints(self):
        """Value as ints: signed for int kinds, name indices / bytes unsigned. Floats are refused."""
        kind = spec(self.type)[2]
        assert kind != FLOAT, 'float record %s' % self.name
        return [int.from_bytes(e, 'little', signed=(kind == INT)) for e in self._items()[1]]

    def floats(self):
        assert spec(self.type)[2] == FLOAT, 'not a float record: %s 0x%x' % (self.name, self.type)
        return [struct.unpack('<f', e)[0] for e in self._items()[1]]

    def names(self):
        """Key (if keyed) followed by the name values (if a name kind)."""
        shape, keyed, kind, _, _ = spec(self.type)
        out = [self.key] if keyed else []
        return out + ([self.f.name(_u(e, 2)) for e in self._items()[1]] if kind == NAME else [])

    def value(self):
        """None (node), a scalar, a list; [key, ...] for keyed records; raw payload for unkeyed blobs."""
        shape, keyed, kind, cw, ew = spec(self.type)
        if kind == NAME or shape == NODE:
            return self.names() or None
        if kind == BYTE:
            return [self.key, b''.join(self._items()[1])] if keyed else self.pay
        v = self.floats() if kind == FLOAT else self.ints()
        if shape == SCALAR:
            v = v[0]
        return ([self.key] + (v if isinstance(v, list) else [v])) if keyed else v

    # --- edits -------------------------------------------------------------
    def _rebuild(self, kind, elems, ew, cw):
        shape, keyed = self.type & 3, (self.type >> 2) & 1
        cw = cw if shape == LIST else 1
        iw = ew if kind == INT else 1
        self.type = shape | keyed << 2 | kind << 3 | (cw - 1) << 6 | (iw - 1) << 8
        self.pay = bytes(self.pay[:2 * keyed]) + (len(elems).to_bytes(cw, 'little') if shape == LIST else b'') + \
            b''.join(elems)
        assert spec(self.type), self.type
        return self

    def _count_width(self, n):
        cw = max(spec(self.type)[3], 1)
        while n >= 1 << (8 * cw):
            cw += 1
        assert cw <= 3, '%d elements do not fit a 3-byte count' % n
        return cw

    def set_int(self, v):
        """Scalar int write; widens the type if the value no longer fits (never narrows)."""
        shape, _, kind, _, ew = spec(self.type)
        assert shape == SCALAR and kind == INT, 'not an int scalar: %s 0x%x' % (self.name, self.type)
        if v >= 1 << 31:                   # unsigned 32-bit (CRCs): same bytes
            v -= 1 << 32
        w = max(_width([v]), ew)
        return self._rebuild(INT, [v.to_bytes(w, 'little', signed=True)], w, 1)

    def set_ints(self, vals):
        """Int list write, keeping the element and count widths unless the values outgrow them."""
        shape, _, kind, _, ew = spec(self.type)
        assert shape == LIST and kind == INT, 'not an int list: %s 0x%x' % (self.name, self.type)
        w = max(_width(vals), ew)
        return self._rebuild(INT, [v.to_bytes(w, 'little', signed=True) for v in vals], w, self._count_width(len(vals)))

    def set_floats(self, vals):
        shape, _, kind, _, _ = spec(self.type)
        assert kind == FLOAT, 'not a float record: %s 0x%x' % (self.name, self.type)
        vals = list(vals) if isinstance(vals, (list, tuple)) else [vals]
        assert shape == LIST or len(vals) == 1
        return self._rebuild(FLOAT, [struct.pack('<f', v) for v in vals], 4, self._count_width(len(vals)))

    def set_names(self, strings):
        """Name value(s) write (adds the strings to the file's name table); a keyed node or a keyed scalar
        takes one string: its key."""
        shape, keyed, kind, _, _ = spec(self.type)
        idx = [self.f.add_name(s).to_bytes(2, 'little') for s in strings]
        if kind != NAME:
            assert keyed and len(idx) == 1, 'no name in %s 0x%x' % (self.name, self.type)
            self.pay = idx[0] + bytes(self.pay[2:])
            return self
        if keyed and shape == SCALAR:                 # 0x0F: key, name
            assert len(idx) == 2
            self.pay = idx[0] + idx[1]
            return self
        assert shape == LIST or len(idx) == 1
        return self._rebuild(NAME, idx, 2, self._count_width(len(idx)))

    def set_name(self, s):
        return self.set_names([s])

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
        self.hdr = struct.pack('<IfI', MAGIC, 1.1, 0) + bytes(HDR - 12)
        self.f14 = 0
        self.tail = b''      # bytes after the last parsed record (should be empty)

    # --- io ----------------------------------------------------------------
    @classmethod
    def load(cls, path):
        return cls.parse(Path(path).read_bytes(), path)

    def reparse(self):
        """A fresh parse of this file's current bytes: inserted records get their pool tags."""
        return Oct.parse(self.to_bytes(), self.path)

    @classmethod
    def parse(cls, b, path=None):
        f = cls()
        f.path = Path(path) if path else None
        if len(b) < HDR or _u(b, 4) != MAGIC:
            raise ValueError('%s: bad magic' % path)
        f.hdr = b[:HDR]
        nb, db, f.f14 = struct.unpack_from('<III', b, 12)
        ds = HDR + nb
        de = ds + db
        if de > len(b):
            raise ValueError('%s: size invariant broken' % path)
        f.names = [n.decode('latin-1') for n in b[HDR:ds].split(b'\0')[:-1]]

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
        h = bytearray(self.hdr)
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
        struct.pack_into('<III', h, 12, len(nt), len(data), count)
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

    def kids(self, rec, deep=True):
        """{name: record} below rec (children(); deep=False: direct children only)."""
        i, j = self.block(rec)
        return children(self.recs[i:j], deep)

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


def is_tuple_file(p):
    """True if p starts with the tuple magic (.mer/.merdebug/.ap/.mtc also exist in other formats)."""
    with open(p, 'rb') as fh:
        return _u(fh.read(4), 4) == MAGIC


def _roundtrip_one(p):
    """None if p round-trips byte-exactly (or is not a tuple file), else (p, reason)."""
    try:
        if not is_tuple_file(p):
            return None
        f = Oct.load(p)
        if f.to_bytes() != p.read_bytes():
            return (p, 'mismatch')
        if f.tail:
            return (p, 'trailing bytes')
        for r in f.recs:                    # every record decodes, and a rewrite of its value is a no-op
            r.value()
            shape, keyed, kind, _, _ = spec(r.type)
            before = (r.type, bytes(r.pay))
            if kind == INT:
                r.set_int(r.ints()[0]) if shape == SCALAR else r.set_ints(r.ints())
            elif kind == FLOAT:
                r.set_floats(r.floats())
            if (r.type, bytes(r.pay)) != before:
                return (p, 'rewrite of %s 0x%x changed it' % (r.name, before[0]))
    except Exception as e:
        return (p, e)
    return None


def _selftest():
    """Round-trip every tuple file we can reach; a codec that can't reproduce the input byte-for-byte
    cannot be trusted to edit one."""
    import paths
    from par import pmap
    roots = []
    for r in ('ext30', 'ext10'):
        p, _ = paths.where(r)
        if p:
            roots.append(getattr(paths, r)())
    assert roots, 'no extract to test against: ' + paths.HINT['ext30']
    files = [p for root in roots for p in root.rglob('*') if p.suffix in EXTS and is_tuple_file(p)]
    bad = [r for r in pmap(_roundtrip_one, files, chunksize=32) if r]
    print('round-trip %d/%d exact (%s)' % (len(files) - len(bad), len(files), ', '.join(map(str, roots))))
    for p, e in bad[:10]:
        print('  FAIL', p, e)
    assert not bad, '%d files failed' % len(bad)


if __name__ == '__main__':
    import argparse, paths
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('file', nargs='?')
    ap.add_argument('filters', nargs='*', help='name=X pool=Y level=N type=0x1b')
    ap.add_argument('--selftest', action='store_true')
    paths.add_args(ap, 'ext30', 'ext10')
    a = paths.use(ap.parse_args())
    if a.selftest:
        _selftest()
    elif a.file:
        f = Oct.load(a.file)
        print('== %s  names=%d  records=%d' % (f.path.name, len(f.names), len(f.recs)))
        kw = dict(x.split('=', 1) for x in a.filters)
        for k in ('level', 'type'):
            if k in kw:
                kw[k] = int(kw[k], 0)
        f.dump(**kw)
    else:
        ap.print_help()

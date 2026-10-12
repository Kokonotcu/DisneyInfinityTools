"""3.0 asset archives (the '.zip' files under assets/, e.g. exploders/ps/psx_barrel.zip): read and WRITE.

Not a plain zip. Layout, all decoded from shipped files:
  [8 B header 'PK\\xff\\xff' + count][count x (MurmurHash3_x86_32(name, seed 0), offset)]   index sorted
      by hash (the engine bsearches it, FUN_012ea270 -> FUN_012e9e60); AES-128-CTR, counter from 0
  members: zip local header (30 B) + name + data (deflate); cipher restarts at each member's offset and
      covers the header, the name and the first 0x200 bytes of the data only (.dct members: all of it)
  central directory (zip PK0102 records) + end record (22 B): one keystream, END RECORD FIRST
      (positions 0..21), then the directory (22..). Each directory record carries a 23-byte extra:
      'KF' 0x13 'MD5' + MD5 of the uncompressed member (1,789 of 1,789 checked)
  key: the PSX key when 'psx' appears anywhere in the archive's path under assets/ (1,047 of 1,047
      shipped PSX-key archives, and no other -- choreographies/psx/.../temp_set.zip included); else the
      default key. 789 archives (sound/dialog) are not encrypted at all. Readers detect the key from
      the header; writers take it from key_for(path).
The engine finds a member only through the index.

python di3arc.py list <archive>
python di3arc.py unpack <archive> <out dir>
python di3arc.py pack <dir> <out.zip> [--key default|psx|none]   key default: key_for(<out.zip> path)
python di3arc.py --selftest [--game DIR]   re-pack every shipped exploder archive byte-exact
"""
import argparse, hashlib, struct, zlib
from pathlib import Path
from Crypto.Cipher import AES

KEY_DEFAULT = bytes.fromhex('681BBEEA63160188F9B7945104A51499')
KEY_PSX = bytes.fromhex('7DDD6D92F3A46ABAF061EBC3C01D7D88')
KEYS = {'default': KEY_DEFAULT, 'psx': KEY_PSX, 'none': None}


def key_for(path):
    """Key for an archive at path (its path under assets/, or any path ending in it)."""
    return KEY_PSX if 'psx' in Path(path).as_posix().lower() else KEY_DEFAULT


def detect_key(raw):
    """The key that turns raw's first bytes into the index magic; None = unencrypted."""
    for k in (None, KEY_DEFAULT, KEY_PSX):
        if _x(k, raw[:4]) == b'PK\xff\xff':
            return k
    raise ValueError('not a 3.0 archive: no key yields PK\\xff\\xff')


def _x(key, b, at=0):
    """XOR with the keystream from position at (AES-128-CTR, big-endian counter from 0)."""
    if key is None:
        return bytes(b)
    pad = at % 16
    c = AES.new(key, AES.MODE_CTR, nonce=b'', initial_value=at // 16)
    return c.encrypt(bytes(pad) + bytes(b))[pad:]


def mm3(data, seed=0):
    M, h, n = 0xffffffff, seed, len(data)
    c1, c2 = 0xcc9e2d51, 0x1b873593
    for i in range(0, n // 4 * 4, 4):
        k = (int.from_bytes(data[i:i + 4], 'little') * c1) & M
        k = (((k << 15) | (k >> 17)) & M) * c2 & M
        h ^= k
        h = ((((h << 13) | (h >> 19)) & M) * 5 + 0xe6546b64) & M
    t, k = data[n // 4 * 4:], 0
    for j, b in enumerate(t):
        k |= b << (8 * j)
    if t:
        k = (k * c1) & M
        k = (((k << 15) | (k >> 17)) & M) * c2 & M
        h ^= k
    h ^= n
    h ^= h >> 16; h = (h * 0x85ebca6b) & M
    h ^= h >> 13; h = (h * 0xc2b2ae35) & M
    return h ^ (h >> 16)


def _span(name, comp):
    return comp if name.lower().endswith('.dct') else min(0x200, comp)


def read(path):
    """-> (members in file order: dicts with name, off, lh (30 B header + name + extra), data (compressed),
    cd (46 B), cd_extra; end record, key)"""
    raw = Path(path).read_bytes()
    key = detect_key(raw)
    count = struct.unpack('<I', _x(key, raw[:8])[4:])[0]
    eocd = _x(key, raw[-22:])
    _, _, _, n1, n2, cdsize, cdoff, _ = struct.unpack('<4s4H2IH', eocd)
    cd = _x(key, raw[cdoff:cdoff + cdsize], 22)
    cds, p = {}, 0
    while p < len(cd):
        nl, el, cl = struct.unpack_from('<3H', cd, p + 28)
        name = cd[p + 46:p + 46 + nl].decode('latin-1')
        assert name not in cds, 'duplicate member %s' % name
        cds[name] = (cd[p:p + 46], cd[p + 46 + nl:p + 46 + nl + el + cl])
        p += 46 + nl + el + cl
    members = []
    for name, (hdr, extra) in cds.items():
        off = struct.unpack_from('<I', hdr, 42)[0]
        lh = _x(key, raw[off:off + 30])
        nl, el = struct.unpack_from('<2H', lh, 26)
        comp = struct.unpack_from('<I', lh, 18)[0]
        body = bytearray(raw[off:off + 30 + nl + el + comp])
        n = 30 + nl + el + _span(name, comp)
        body[:n] = _x(key, bytes(body[:n]))
        members.append(dict(name=name, off=off, lh=bytes(body[:30 + nl + el]), data=bytes(body[30 + nl + el:]),
                            cd=hdr, cd_extra=extra))
    members.sort(key=lambda m: m['off'])
    assert len(members) == count == n2
    return members, eocd, key


def unpack(path):
    out = {}
    for m in read(path)[0]:
        method = struct.unpack_from('<H', m['lh'], 8)[0]
        out[m['name']] = zlib.decompress(m['data'], -15) if method == 8 else m['data']
    return out


def unpack_index(path):
    """Members found through the leading hash index (PK\\xff\\xff, n, n x (mm3(name), offset)), as the game
    does. Third-party tools (the Anyone Can Cook mod, QuickBMS reimport) rewrite the index and leave the
    zip central directory stale, so read()/unpack() fail on their archives."""
    raw = Path(path).read_bytes()
    key = detect_key(raw)
    n = struct.unpack('<4sI', _x(key, raw[:8]))[1]
    idx = _x(key, raw[:8 + 8 * n])[8:]
    out = {}
    for i in range(n):
        h, off = struct.unpack_from('<II', idx, 8 * i)
        lh = _x(key, raw[off:off + 30])
        method, comp, size, nl, el = struct.unpack_from('<H8xII2H', lh, 8)
        name = _x(key, raw[off:off + 30 + nl])[30:].decode('latin-1')
        k = 30 + nl + el + _span(name, comp)
        head = _x(key, raw[off:off + k])
        body = head[30 + nl + el:] + raw[off + k:off + 30 + nl + el + comp]
        assert mm3(name.encode('latin-1')) == h, (name, h)
        out[name] = zlib.decompress(body, -15) if method == 8 else body
        assert len(out[name]) == size, (name, len(out[name]), size)
    return out


def write(members, key, eocd_tail=b'\0\0'):
    """members: [{name, lh, data, cd, cd_extra}] in file order (lh/cd fields complete except offsets)."""
    n = len(members)
    pos = 8 + 8 * n
    body, offsets = bytearray(), []
    for m in members:
        offsets.append(pos + len(body))
        blob = bytearray(m['lh'] + m['data'])
        k = len(m['lh']) + _span(m['name'], len(m['data']))
        blob[:k] = _x(key, bytes(blob[:k]))
        body += blob
    index = struct.pack('<4sI', b'PK\xff\xff', n) + b''.join(
        struct.pack('<II', h, o) for h, o in sorted((mm3(m['name'].encode('latin-1')), o)
                                                     for m, o in zip(members, offsets)))
    cd = bytearray()
    for m, o in zip(members, offsets):
        hdr = bytearray(m['cd'])
        struct.pack_into('<I', hdr, 42, o)
        cd += hdr + m['name'].encode('latin-1') + m['cd_extra']
    cdoff = pos + len(body)
    eocd = struct.pack('<4s4H2I', b'PK\x05\x06', 0, 0, n, n, len(cd), cdoff) + eocd_tail
    tail = _x(key, eocd + bytes(cd))
    return _x(key, index) + bytes(body) + tail[22:] + tail[:22]


def member(name, raw):
    """A new member with the field values shipped archives use (deflate 9, MD5 extra)."""
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    data = c.compress(raw) + c.flush()
    nm, crc = name.encode('latin-1'), zlib.crc32(raw)
    extra = b'KF\x13\x00MD5' + hashlib.md5(raw).digest()
    lh = struct.pack('<4s5H3I2H', b'PK\x03\x04', 20, 0, 8, 0, 0x21, crc, len(data), len(raw), len(nm), 0) + nm
    cd = struct.pack('<4s6H3I5H2I', b'PK\x01\x02', 20, 20, 0, 8, 0, 0x21, crc, len(data), len(raw),
                     len(nm), len(extra), 0, 0, 0, 0, 0)
    return dict(name=name, lh=lh, data=data, cd=cd, cd_extra=extra)


def pack_dir(src_dir, key):
    """New archive from loose files (members may sit in folders: textures/x.tbody)."""
    members = [member(f.relative_to(src_dir).as_posix(), f.read_bytes())
               for f in sorted(p for p in Path(src_dir).rglob('*') if p.is_file())]
    members.sort(key=lambda m: (not m['name'].endswith('.oct'), m['name']))
    return write(members, key)


def add_files(archive, files):
    """Existing members kept byte-for-byte; {member name: bytes} appended (same name = replaced)."""
    members, eocd, key = read(archive)
    members = [m for m in members if m['name'] not in files]
    members += [member(n, b) for n, b in files.items()]
    return write(members, key, eocd[20:])


def _selftest():
    import paths, tempfile
    root = paths.game('assets', 'exploders')
    ok = bad = 0
    for z in sorted(root.rglob('*.zip')):
        raw = z.read_bytes()
        try:
            members, eocd, key = read(z)
            again = write(members, key, eocd[20:])
        except Exception as e:
            bad += 1; print('  ERR', z.name, e); continue
        if again == raw:
            ok += 1
        else:
            bad += 1
            if bad <= 5:
                print('  DIFF', z.name, len(again), len(raw))
    print('re-packed byte-exact: %d of %d shipped exploder archives' % (ok, ok + bad))
    # a freshly packed archive must read back to the same files, through both readers, with shipped extras
    one = next(root.rglob('*.zip'))
    files = unpack(one)
    d = Path(tempfile.mkdtemp()) / 'src'
    for k, v in files.items():
        (d / k).parent.mkdir(parents=True, exist_ok=True)
        (d / k).write_bytes(v)
    p2 = d.parent / one.name
    p2.write_bytes(pack_dir(d, key_for(one.relative_to(root.parent))))
    assert unpack(p2) == files == unpack_index(p2), 'pack_dir round trip failed'
    ship = {m['name']: m['cd_extra'] for m in read(one)[0]}
    assert {m['name']: m['cd_extra'] for m in read(p2)[0]} == ship, 'new members lack the shipped MD5 extra'
    print('pack_dir round trip OK on', one.name, '(MD5 extras identical to shipped)')
    assert not bad


if __name__ == '__main__':
    import paths
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('cmd', choices=('list', 'unpack', 'pack'), nargs='?')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--key', choices=KEYS, help='pack: override key_for(out path)')
    ap.add_argument('--selftest', action='store_true')
    paths.add_args(ap, 'game')
    a = paths.use(ap.parse_args())
    if a.selftest:
        _selftest()
    elif a.cmd == 'list':
        for m in read(a.args[0])[0]:
            print('%8d  %s' % (len(m['data']), m['name']))
    elif a.cmd == 'unpack':
        out = Path(a.args[1])
        for name, data in unpack_index(a.args[0]).items():
            dst = (out / name).resolve()
            assert out.resolve() in dst.parents, 'member escapes the output folder: %s' % name
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
        print('unpacked', a.args[0], '->', out)
    elif a.cmd == 'pack':
        key = KEYS[a.key] if a.key else key_for(a.args[1])
        Path(a.args[1]).write_bytes(pack_dir(a.args[0], key))
        print('packed', a.args[0], '->', a.args[1])
    else:
        ap.print_help()

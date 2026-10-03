"""3.0 asset archives (the '.zip' files under assets/, e.g. exploders/ps/psx_barrel.zip): read and WRITE.

Not a plain zip. Layout, all decoded from shipped files:
  [8 B header 'PK\\xff\\xff' + count][count x (MurmurHash3_x86_32(name, seed 0), offset)]   index sorted
      by hash (the engine bsearches it, FUN_012ea270 -> FUN_012e9e60); AES-128-CTR, counter from 0
  members: zip local header (30 B) + name + data (deflate); cipher restarts at each member's offset and
      covers the header, the name and the first 0x200 bytes of the data only (.dct members: all of it)
  central directory (zip PK0102 records, 23-byte extra) + end record (22 B): one keystream, END RECORD
      FIRST (positions 0..21), then the directory (22..)
  key: archives named psx* / _psx* / rlm_psx* use the PSX key, everything else the default key
The engine finds a member only through the index; an asset that is not in an archive is never opened
(loose exploder folders are invisible to it).

python di3arc.py --selftest           re-pack every shipped exploder archive byte-exact
python di3arc.py pack <dir> <out.zip>  pack every file in <dir>
python di3arc.py list <archive>
"""
import struct, sys, zlib
from pathlib import Path
from Crypto.Cipher import AES

KEY_DEFAULT = bytes.fromhex('681BBEEA63160188F9B7945104A51499')
KEY_PSX = bytes.fromhex('7DDD6D92F3A46ABAF061EBC3C01D7D88')


def key_for(archive_name):
    n = archive_name.lower()
    return KEY_PSX if n.startswith(('psx', '_psx', 'rlm_psx')) else KEY_DEFAULT


def _ks(key, n):
    e = AES.new(key, AES.MODE_ECB)
    return b''.join(e.encrypt(i.to_bytes(16, 'big')) for i in range((n + 15) // 16))[:n]


def _x(key, b):
    return bytes(a ^ c for a, c in zip(b, _ks(key, len(b))))


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
    """-> (members in file order: dicts with name, lh (30 B header fields), data (compressed), cd (46 B),
    cd_extra), eocd fields)"""
    raw = Path(path).read_bytes()
    key = key_for(Path(path).stem)
    count = struct.unpack('<I', _x(key, raw[:8])[4:])[0]
    eocd = _x(key, raw[-22:])
    _, _, _, n1, n2, cdsize, cdoff, _ = struct.unpack('<4s4H2IH', eocd)
    cd = bytes(a ^ c for a, c in zip(raw[cdoff:cdoff + cdsize], _ks(key, 22 + cdsize)[22:]))
    cds, p = {}, 0
    while p < len(cd):
        nl, el, cl = struct.unpack_from('<3H', cd, p + 28)
        name = cd[p + 46:p + 46 + nl].decode('latin-1')
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
    return members, eocd


def unpack(path):
    out = {}
    for m in read(path)[0]:
        method = struct.unpack_from('<H', m['lh'], 8)[0]
        out[m['name']] = zlib.decompress(m['data'], -15) if method == 8 else m['data']
    return out


def write(members, archive_name, eocd_tail=b'\0\0'):
    """members: [{name, lh, data, cd, cd_extra}] in file order (lh/cd fields complete except offsets)."""
    key = key_for(archive_name)
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


def pack_dir(src_dir, archive_name):
    """New archive from loose files, with the field values shipped archives use."""
    members = []
    for f in sorted(p for p in Path(src_dir).rglob('*') if p.is_file()):
        raw = f.read_bytes()
        c = zlib.compressobj(9, zlib.DEFLATED, -15)
        data = c.compress(raw) + c.flush()
        rel = f.relative_to(src_dir).as_posix()           # members may sit in folders: textures/x.tbody
        nm = rel.encode('latin-1')
        crc = zlib.crc32(raw)
        lh = struct.pack('<4s5H3I2H', b'PK\x03\x04', 20, 0, 8, 0, 0x21, crc, len(data), len(raw), len(nm), 0) + nm
        cd = struct.pack('<4s6H3I5H2I', b'PK\x01\x02', 20, 20, 0, 8, 0, 0x21, crc, len(data), len(raw),
                         len(nm), 0, 0, 0, 0, 0, 0)
        members.append(dict(name=rel, lh=lh, data=data, cd=cd, cd_extra=b''))
    members.sort(key=lambda m: (not m['name'].endswith('.oct'), m['name']))
    return write(members, archive_name)


def _selftest():
    root = Path(r'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition\assets\exploders')
    ok = bad = 0
    for z in sorted(root.rglob('*.zip')):
        raw = z.read_bytes()
        try:
            members, eocd = read(z)
            again = write(members, z.stem, eocd[20:])
        except Exception as e:
            bad += 1; print('  ERR', z.name, e); continue
        if again == raw:
            ok += 1
        else:
            bad += 1
            if bad <= 5:
                print('  DIFF', z.name, len(again), len(raw))
    print('re-packed byte-exact: %d of %d shipped exploder archives' % (ok, ok + bad))
    # a freshly packed archive must read back to the same files
    one = next(root.rglob('*.zip'))
    files = unpack(one)
    import tempfile
    d = Path(tempfile.mkdtemp()) / 'src'
    for k, v in files.items():
        (d / k).parent.mkdir(parents=True, exist_ok=True)
        (d / k).write_bytes(v)
    p2 = d.parent / one.name
    p2.write_bytes(pack_dir(d, one.stem))
    assert unpack(p2) == files, 'pack_dir round trip failed'
    print('pack_dir round trip OK on', one.name)


if __name__ == '__main__':
    a = sys.argv[1:]
    if a == ['--selftest']:
        _selftest()
    elif a[:1] == ['pack']:
        Path(a[2]).write_bytes(pack_dir(a[1], Path(a[2]).stem))
        print('packed', a[1], '->', a[2])
    elif a[:1] == ['list']:
        for m in read(a[1])[0]:
            print('%8d  %s' % (len(m['data']), m['name']))

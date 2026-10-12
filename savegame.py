"""Save files (PC, SavedGame/user0/<file>): read, edit, write.

Layout, decoded from the 16 saves of one PC profile (all little-endian):
  0x00 u32 0x207   0x04 u32 file size   0x08 u32 1   0x0C u32 text size   0x10 u32 file size - 0x40
  0x14 zero to 0x40
  0x40 'CMP1'   0x44 u32 text size   0x48 u32 packed size   0x4C u32 lookup2(text)   0x50 u32 lookup2(packed)
  0x54 packed: a zlib stream (78 01) of the text, then zero bytes to the next multiple of 64 (none when on one)
lookup2 is Bob Jenkins' hash() with seed 0 (dct.lookup2); the text hash covers the final NUL. The text is the
whole save, a Lua-like tree ('Root =\r\n{\r\n\tKEY = value\r\n\tChild =\r\n\t{ ... }\r\n}\r\n'): CRLF only,
ASCII only, one NUL at the end and none before it. load() drops the NUL and dump() adds it back. The text stays
bytes, since no save shows how non-ASCII is encoded.
Files seen (areas RR1, RO0, BA0): P0 CGameSettings; S<area>A the Toy Box (root zone or zoneglobal, holding
RumpusSaveGame with RR_SAVE_VERSION = 127); SH<area>A its CSaveGameSlot header; SC<area>A its screenshot
(CSaveGameSlot SCREENSHOT = $ and hex rows); E<area> HelpManager; EH<area> CSaveGameSlot with
INFINITY_VERSION = 3. Every kind has the same container, and nothing here depends on the file name.
PC saves are not encrypted. A big-endian 0x207 is a console save (Phelix-encrypted, according to the
third-party LiteralToyBoxDecryptor). load() rejects it because there are none to test.
zlib level 1 with the other parameters at their defaults reproduces all 16 stored streams (zlib 1.3.1),
so dump() always recompresses. A zlib that packs level 1 differently, such as zlib-ng, still writes a
valid stream but not the same bytes.
Measured on 16 files: every header word above holds in all 16, and both checksums reproduce in all 16.
load/dump is exact for 16, and export->import is exact for 16. Whether the game accepts an edited save
is untested.

  d = load(raw); d['text'] = d['text'].replace(b'TIMEPLAYED = 21', b'TIMEPLAYED = 99'); raw = dump(d)

python savegame.py show <file>                         header words, then the text
python savegame.py export <file> <out.txt>             the text as stored (CRLF), without the NUL
python savegame.py import <in.txt> <template> <out>    sizes and checksums recomputed, bare LF -> CRLF
python savegame.py --selftest <dir>                    every 0x207 file under dir
"""
import argparse, re, struct, sys, zlib
from pathlib import Path
from dct import lookup2

MAGIC = b'\x07\x02\0\0'


def load(raw):
    """-> {'text': bytes without the final NUL, then the stored header words}. A ValueError names
    each check that fails, so a file that loads also dumps back to the same bytes."""
    if raw[:4] != MAGIC:
        raise ValueError('big-endian 0x207: a console save (encrypted), not handled' if raw[:4] == MAGIC[::-1]
                         else 'not a save: no u32 0x207 at 0')
    size, one, tsize, psize = struct.unpack_from('<4I', raw, 4)
    cmp1, tsize2, plen, th, ph = struct.unpack_from('<4s4I', raw, 0x40)
    packed = raw[0x54:0x54 + plen]
    z = zlib.decompressobj()
    text = z.decompress(packed)
    bad = [k for k, ok in {
        'file size': size == len(raw), 'word 0x08 = 1': one == 1, 'zero 0x14-0x40': not any(raw[0x14:0x40]),
        'CMP1': cmp1 == b'CMP1', 'payload size': psize == len(raw) - 0x40 == (0x14 + plen + 63) // 64 * 64,
        'zero padding': not any(raw[0x54 + plen:]), 'zlib stream ends': z.eof and not z.unused_data,
        'text sizes': tsize == tsize2 == len(text), 'single NUL at the end': text.find(b'\0') == len(text) - 1,
        'text lookup2': lookup2(text, 0) == th, 'packed lookup2': lookup2(packed, 0) == ph}.items() if not ok]
    if bad:
        raise ValueError('not a PC save, or a variant none of the 16 measured has: ' + ', '.join(bad))
    return {'text': text[:-1], 'file_size': size, 'word_08': one, 'text_size': tsize, 'payload_size': psize,
            'packed_size': plen, 'text_lookup2': th, 'packed_lookup2': ph}


def dump(d):
    """Only d['text'] is used: every header word is derived from it."""
    text = d['text'] + b'\0'
    assert text.index(b'\0') == len(text) - 1, 'NUL inside the text'
    packed = zlib.compress(text, 1)
    body = b'CMP1' + struct.pack('<4I', len(text), len(packed), lookup2(text, 0), lookup2(packed, 0)) + packed
    body += bytes(-len(body) % 64)
    return struct.pack('<5I', 0x207, 0x40 + len(body), 1, len(text), len(body)).ljust(0x40, b'\0') + body


def from_txt(b):
    """The text of an edited export. A UTF-8 BOM is dropped, and bare LF becomes CRLF because the game writes CRLF only."""
    return re.sub(rb'\r?\n', b'\r\n', b.removeprefix(b'\xef\xbb\xbf'))


def _check(raw):
    """(round trip exact, export -> LF line endings -> import exact, edit -> dump -> load gives it back)"""
    d = load(raw)
    edited = d['text'] + b''.join(b'SELFTEST%d = %d\r\n' % (i, i * 7919 % 10007) for i in range(40))
    e = load(dump(dict(d, text=edited)))
    return (dump(d) == raw, dump({'text': from_txt(d['text'].replace(b'\r\n', b'\n'))}) == raw,
            e['text'] == edited and e['text_size'] == len(edited) + 1)


def _selftest(folder):
    files = sorted(p for p in Path(folder).rglob('*') if p.is_file())
    saves = [p for p in files if p.read_bytes()[:4] == MAGIC]
    assert saves, 'no 0x207 file under %s' % folder
    print('%d saves under %s (%d other files skipped: %s)' % (len(saves), folder, len(files) - len(saves),
                                                             ', '.join(p.name for p in files if p not in saves)))
    res = []
    for p in saves:
        try:
            res.append(_check(p.read_bytes()))
        except (ValueError, zlib.error) as e:
            res.append(None)
            print('  FAIL', p, e)
    ok = [r for r in res if r]
    rt, ji, ed = (sum(c) for c in zip(*ok)) if ok else (0, 0, 0)
    print('decoded, header words and both lookup2 checksums verified: %d of %d' % (len(ok), len(saves)))
    print('round-trip exact (zlib level 1, zlib %s): %d of %d' % (zlib.ZLIB_RUNTIME_VERSION, rt, len(saves)))
    print('export -> LF line endings -> import exact: %d of %d' % (ji, len(saves)))
    print('edit (+40 lines) -> dump -> load gives the edited text back: %d of %d' % (ed, len(saves)))
    for p, r in zip(saves, res):
        if r and not all(r):
            print('  FAIL', p, r)
    assert len(ok) == rt == ji == ed == len(saves), 'failures above'


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('cmd', choices=('show', 'export', 'import'), nargs='?')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--selftest', metavar='DIR')
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    if a.cmd and len(a.args) != {'show': 1, 'export': 2, 'import': 3}[a.cmd]:
        ap.error('wrong number of arguments for ' + a.cmd)
    if a.selftest:
        _selftest(a.selftest)
    elif a.cmd == 'show':
        d = load(Path(a.args[0]).read_bytes())
        for k, v in d.items():
            if k != 'text':
                print('%-15s %s' % (k, '%08x' % v if k.endswith('lookup2') else v))
        print()
        print(d['text'].decode('utf-8', 'replace').replace('\r\n', '\n'))
    elif a.cmd == 'export':
        Path(a.args[1]).write_bytes(load(Path(a.args[0]).read_bytes())['text'])
        print('exported', a.args[0], '->', a.args[1])
    elif a.cmd == 'import':
        d = load(Path(a.args[1]).read_bytes())
        d['text'] = from_txt(Path(a.args[0]).read_bytes())
        Path(a.args[2]).write_bytes(dump(d))
        print('imported', a.args[0], '+', a.args[1], '->', a.args[2])
    else:
        ap.print_help()

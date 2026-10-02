"""A .btd is one collision mesh per TriangleList shape (SerializeIndex = block ordinal), packed back
to back. Each block ends where its per-triangle u32 array ends:
    1.0: u32 @0x28 = array offset, u32 @0x2c = count        3.0: u32 @48 / u32 @52
python btdblocks.py --selftest     predict the block count of every .btd in both games
"""
import struct, sys
from pathlib import Path
from par import pmap

ARR = {1: (0x28, 0x2c), 3: (48, 52)}


def blocks(b, game):
    """-> [(start, end)] tiling b, or raise if the rule does not tile it."""
    oa, oc = ARR[game]
    out, o = [], 0
    while len(b) - o >= 64:          # a block header is 64 bytes; anything shorter is tail padding
        a, c = struct.unpack_from('<I', b, o + oa)[0], struct.unpack_from('<I', b, o + oc)[0]
        end = o + a + 4 * c
        if not (o < end <= len(b)):
            raise ValueError('block at %#x ends at %#x (file %#x)' % (o, end, len(b)))
        out.append((o, end))
        o = end
    return out


def _check(args):
    d, game = args
    from oct import Oct
    f, b = d / f'{d.name}.oct', d / f'{d.name}.btd'
    if not f.exists() or not b.exists():
        return None
    try:
        n = sum(1 for r in Oct.load(str(f)).recs if r.name == 'SerializeIndex')
    except Exception:
        return None
    try:
        got = len(blocks(b.read_bytes(), game))
    except (ValueError, struct.error) as e:
        return (d.name, game, n, str(e))
    return (d.name, game, n, got)


if __name__ == '__main__':
    jobs = [(d, g) for g, root in ((1, r'D:\DI-Extract\1.0\assets\worlds'), (3, r'D:\DI-Extract\3.0\assets\worlds'))
            for d in sorted(Path(root).iterdir())]
    res = [r for r in pmap(_check, jobs, chunksize=8) if r]
    ok = [r for r in res if r[2] == r[3]]
    bad = [r for r in res if r[2] != r[3]]
    for g in (1, 3):
        print(f'{g}.0: block count == TriangleList shapes in {sum(r[1] == g for r in ok)} of {sum(r[1] == g for r in res)} worlds')
    for r in bad[:10]:
        print('  MISMATCH', r)

"""Render-context frame arenas, read from a minidump. ctx = [[0x02150d68] + 0x48]; the contexts form a
ring (ctx[0] -> next). Each arena is {cur, end, base, ...}: FUN_0045ace0 resets cur = base per frame,
FUN_0047e0e0 / FUN_0047e030 bump-allocate from cur and return NULL when cur passes end.
python arenas.py <dump.dmp> ..."""
import struct
from pathlib import Path
from mdmp import Dump

ARENAS = (('A', 0x2520), ('B draw buckets', 0x2530), ('C', 0x2548))


def show(path):
    d = Dump(path)
    ctx = struct.unpack('<I', d.read(d.u32(0x02150d68) + 0x48, 4))[0]
    print(Path(path).name, 'exe base', hex(d.exe_base), 'render ctx', hex(ctx))
    ring = [ctx]
    while len(ring) < 4:
        nxt = struct.unpack('<I', d.read(ring[-1], 4))[0]
        if nxt in ring or nxt == 0:
            break
        ring.append(nxt)
    for c in ring:
        for name, off in ARENAS:
            cur, end, base = struct.unpack('<3I', d.read(c + off, 12))
            used = (cur - base) / 1024 if cur >= base else float('nan')
            print(f'   ctx {c:#x} {name:16} base {base:#x} end {end:#x} size {(end - base) / 1024:8.0f} KB  used {used:8.0f} KB')


if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description="Render contexts' frame arenas from minidumps")
    ap.add_argument('dumps', nargs='+', help='.dmp files (full memory)')
    for p in ap.parse_args().dumps:
        show(p)

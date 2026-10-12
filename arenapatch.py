"""Enlarge the render frame arenas in DisneyInfinity3.exe.

FUN_00aae3e0 fills the render config: +4 arena A = 0x140000, +8 arena B (draw commands) = 0x300000,
+0x10 arena C = 0x4000. Each of the 3 render contexts carves A|B|C from one block (read live from
the Aug-16 dumps: 1280 KB / 3072 KB / 16 KB, contiguous). The bump allocators (FUN_0047e0e0,
FUN_0047e030) return NULL when an arena runs out and the retail build does not check: Metroville
(5,480 primitives) crashed in the draw loop at 0x47d330 walking a NULL draw list.

python arenapatch.py [--exe PATH | --game DIR] [--backup PATH]   patch (verifies the exact original bytes first,
                                                                  backs the exe up: default <exe>.prearena)
python arenapatch.py --revert                                     restore the original immediates
"""
import argparse, hashlib, shutil, struct
from pathlib import Path
import paths

FUNC = 0x6ad7e0                                   # file offset of FUN_00aae3e0
#            (offset of the imm32 inside the function, original, patched)
FIELDS = {'A': (0x07, 0x140000, 0x500000),        # mov [eax+4],  imm
          'B': (0x0e, 0x300000, 0x1000000),       # mov [eax+8],  imm
          'C': (0x15, 0x4000, 0x10000)}           # mov [eax+10], imm
PREFIX = bytes.fromhex('8b442404c74004')          # mov eax,[esp+4]; mov dword [eax+4],...


def main(revert, exe, backup):
    exe = Path(exe)
    backup = Path(backup) if backup else exe.with_name(exe.name + '.prearena')
    b = bytearray(exe.read_bytes())
    assert b[FUNC:FUNC + 7] == PREFIX, 'FUN_00aae3e0 not where expected: wrong exe build'
    frm, to = (2, 1) if revert else (1, 2)
    for name, f in FIELDS.items():
        cur = struct.unpack_from('<I', b, FUNC + f[0])[0]
        if cur == f[to]:
            print(f'  {name}: already {cur:#x}'); continue
        assert cur == f[frm], f'{name}: found {cur:#x}, expected {f[frm]:#x} -- refusing to patch'
        struct.pack_into('<I', b, FUNC + f[0], f[to])
        print(f'  {name}: {cur:#x} -> {f[to]:#x}')
    if not revert and not backup.exists():
        shutil.copy2(exe, backup)
        print('  backed up', backup)
    exe.write_bytes(b)
    print('exe md5', hashlib.md5(b).hexdigest())


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description='Enlarge the render frame arenas in DisneyInfinity3.exe')
    ap.add_argument('--revert', action='store_true')
    ap.add_argument('--exe', help='default: <game>/DisneyInfinity3.exe')
    ap.add_argument('--backup', help='copy of the unpatched exe (default: <exe>.prearena)')
    paths.add_args(ap, 'game')
    a = paths.use(ap.parse_args())
    main(a.revert, a.exe or paths.game('DisneyInfinity3.exe'), a.backup)

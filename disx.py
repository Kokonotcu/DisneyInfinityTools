"""Disassemble around a runtime address from pentane.log.

The base is ASLR'd and changes between runs -- 0x00F60000 on the Aug-16 launches,
0x00D00000 on the Aug-18 ones -- so read it from line 1 of the log instead of
hardcoding it. Override with  --base 0xXXXXXXXX.
"""
import sys, re, pefile
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

GAME = Path(r'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition')
EXE = str(GAME / 'DisneyInfinity3.exe')

argv = sys.argv[1:]
if '--base' in argv:
    i = argv.index('--base')
    BASE = int(argv[i + 1], 16)
    del argv[i:i + 2]
else:
    head = (GAME / 'pentane.log').read_text(errors='ignore')[:200]
    BASE = int(re.search(r'EXE base = ([0-9A-Fa-f]+)', head).group(1), 16)
    print('EXE base from pentane.log: %08x' % BASE)
pe = pefile.PE(EXE, fast_load=True)
IB = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = False
data = open(EXE, 'rb').read()

def rva2off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None

def show(runtime, back=64, fwd=32, label=''):
    rva = runtime - BASE
    off = rva2off(rva)
    if off is None:
        print('  %08x -> no section' % runtime); return
    start = off - back
    code = data[start:off + fwd]
    print('\n=== %s runtime %08x  rva %08x  static %08x' % (label, runtime, rva, IB + rva))
    for i in md.disasm(code, IB + rva - back):
        mark = '  <<< FAULT' if (IB + rva) == i.address else ''
        print('   %08x  %-24s %s%s' % (i.address, i.bytes.hex(), i.mnemonic + ' ' + i.op_str, mark))

for a in argv:
    show(int(a, 16))

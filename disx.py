"""Disassemble around a runtime address from pentane.log.

The base is ASLR'd and changes between runs -- 0x00F60000 on the Aug-16 launches,
0x00D00000 on the Aug-18 ones -- so read it from the log instead of hardcoding it: the LAST
'EXE base = ' line anywhere in it (Pentane prints it after config errors and plugin loading, so it is not
always near the top). Override with  --base 0xXXXXXXXX.

python disx.py <runtime addr> [...] [--base 0x...] [--log pentane.log] [--exe PATH | --game DIR]
"""
import argparse, re, pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
import paths

ap = argparse.ArgumentParser(description='Disassemble around runtime addresses from pentane.log')
ap.add_argument('addrs', nargs='+')
ap.add_argument('--base', help='exe load address of that run (default: from the log)')
ap.add_argument('--log', help='default: <game>/pentane.log')
ap.add_argument('--exe', help='default: <game>/DisneyInfinity3.exe')
paths.add_args(ap, 'game')
a = paths.use(ap.parse_args())
EXE = a.exe or str(paths.game('DisneyInfinity3.exe'))
if a.base:
    BASE = int(a.base, 16)
else:
    log = a.log or paths.game('pentane.log')
    found = re.findall(r'EXE base = ([0-9A-Fa-f]+)', open(log, errors='ignore').read())
    assert found, 'no "EXE base = " line in %s: pass --base' % log
    BASE = int(found[-1], 16)
    print('EXE base from %s: %08x' % (log, BASE))
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

for x in a.addrs:
    show(int(x, 16))

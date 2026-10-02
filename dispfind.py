"""Find every instruction in DisneyInfinity3.exe whose memory operand uses a given displacement
(e.g. a struct field offset). Linear sweep of .text with capstone, split across cores.
python dispfind.py 0x2534 [0x2530 ...]     python dispfind.py imm:0x300000"""
import sys
from par import pmap

EXE = r'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition\DisneyInfinity3.exe'
WANT = set()


def _chunk(args):
    start, end, data, base, want = args
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.skipdata = True
    hits = []
    imm = any(isinstance(w, str) for w in want)
    tags = [w[4:] if isinstance(w, str) else '+ 0x%x]' % w for w in want]
    for ins in md.disasm(data[start:end], base + start):
        s = ins.op_str
        if any((t in s.split(', ') or s.endswith(t)) if imm else t in s for t in tags):
            hits.append('%08x  %s %s' % (ins.address, ins.mnemonic, s))
    return hits


if __name__ == '__main__':
    import pefile
    # 0x2534 = a [reg + disp] operand; imm:0x300000 = an immediate operand
    want = [x if x.startswith('imm:') else int(x, 16) for x in sys.argv[1:]]
    pe = pefile.PE(EXE, fast_load=True)
    text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    data = text.get_data()
    base = pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress
    n = 64
    step = len(data) // n
    # chunks overlap by 64 bytes so an instruction split at a boundary is still seen
    jobs = [(i * step, min(len(data), (i + 1) * step + 64), data, base, want) for i in range(n)]
    seen = set()
    for hits in pmap(_chunk, jobs, chunksize=1):
        for h in hits:
            if h not in seen:
                seen.add(h); print(h)

"""Minimal minidump reader: read process memory by virtual address, rebased by the exe's load address.

    d = Dump(path); d.read_static(0x02170664, 0x34)   # static (Ghidra, base 0x400000) address
"""
import struct, mmap


class Dump:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        sig, ver, nstreams, rva = struct.unpack_from('<IIII', self.m, 0)
        assert sig == 0x504d444d, 'not a minidump'
        self.ranges, self.modules = [], []
        for i in range(nstreams):
            typ, size, srva = struct.unpack_from('<III', self.m, rva + 12 * i)
            if typ == 9:  # Memory64ListStream
                n, base_rva = struct.unpack_from('<QQ', self.m, srva)
                off = base_rva
                for j in range(n):
                    start, sz = struct.unpack_from('<QQ', self.m, srva + 16 + 16 * j)
                    self.ranges.append((start, sz, off))
                    off += sz
            elif typ == 4:  # ModuleListStream
                n = struct.unpack_from('<I', self.m, srva)[0]
                for j in range(n):
                    e = srva + 4 + 108 * j
                    base, sz = struct.unpack_from('<QI', self.m, e)
                    name_rva = struct.unpack_from('<I', self.m, e + 20)[0]
                    ln = struct.unpack_from('<I', self.m, name_rva)[0]
                    name = self.m[name_rva + 4:name_rva + 4 + ln].decode('utf-16le')
                    self.modules.append((name, base, sz))
        self.exe_base = next(b for n, b, s in self.modules if n.lower().endswith('disneyinfinity3.exe'))

    def read(self, va, n):
        for start, sz, off in self.ranges:
            if start <= va and va + n <= start + sz:
                return self.m[off + va - start:off + va - start + n]
        raise KeyError(f'{va:#x} not in dump')

    def read_static(self, addr, n):
        return self.read(addr - 0x400000 + self.exe_base, n)

    def u32(self, addr):
        return struct.unpack('<I', self.read_static(addr, 4))[0]

"""Lua 5.1 bytecode: re-encode the game's 32-bit chunks (size_t = 4) for a 64-bit Lua 5.1 (lupa), so
expendable packages can be executed and inspected offline. Only size_t fields (string lengths) differ.

    from luabc import to64, run_package
    pkg = run_package(path)      # -> Python dict of the chunk's ExpendablesPackage global
"""
import struct


def to64(b):
    assert b[:5] == b'\x1bLua\x51' and b[8] == 4, 'expected a 32-bit Lua 5.1 chunk'
    E = '<' if b[6] else '>'          # the game's chunks are BIG-endian (header byte 6 = 0)
    out = bytearray(b[:12]); out[6] = 1; out[8] = 8   # -> little-endian, size_t 8
    p = 12

    def words(n, size):
        nonlocal p
        for _ in range(n):
            out.extend(b[p:p + size][::-1] if E == '>' else b[p:p + size]); p += size

    def i32():
        nonlocal p
        v = struct.unpack_from(E + 'i', b, p)[0]; p += 4
        out.extend(struct.pack('<i', v))
        return v

    def byte():
        nonlocal p
        v = b[p]; p += 1
        out.append(v)
        return v

    def string():
        nonlocal p
        n = struct.unpack_from(E + 'I', b, p)[0]; p += 4
        out.extend(struct.pack('<Q', n))
        out.extend(b[p:p + n]); p += n

    def function():
        nonlocal p
        string(); i32(); i32()
        for _ in range(4):
            byte()
        words(i32(), 4)                                              # code
        for _ in range(i32()):                                       # constants
            t = byte()
            if t == 1:
                byte()
            elif t == 3:
                words(1, 8)
            elif t == 4:
                string()
        for _ in range(i32()):                                       # prototypes
            function()
        words(i32(), 4)                                              # line info
        for _ in range(i32()):                                       # locals
            string(); i32(); i32()
        for _ in range(i32()):                                       # upvalue names
            string()
    function()
    assert p == len(b), 'trailing bytes: parsed %d of %d' % (p, len(b))
    return bytes(out)


def to_py(v):
    if hasattr(v, 'items'):
        d = {k: to_py(x) for k, x in v.items()}
        return d
    return v


def run_package(path):
    import lupa.lua51 as L
    lua = L.LuaRuntime()
    raw = open(path, 'rb').read()
    f, err = (lua.eval('loadstring')(to64(raw) if raw[:4] == b'\x1bLua' else raw), None)
    if isinstance(f, tuple):
        f, err = f
    assert f, err
    f()
    return to_py(lua.globals().ExpendablesPackage)


if __name__ == '__main__':
    import sys, glob
    ok = 0
    for f in glob.glob(r'D:\DI-Extract\3.0\assets\expendables_data\expendables_data\*.lua'):
        b = open(f, 'rb').read()
        ok += to64(b) is not None
    print('re-encoded', ok, 'packages')
    pkg = run_package(sys.argv[1]) if len(sys.argv) > 1 else None
    if pkg:
        print({k: type(v).__name__ for k, v in pkg.items()})

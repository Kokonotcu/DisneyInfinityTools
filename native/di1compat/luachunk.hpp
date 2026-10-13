// Lua 5.1 bytecode (x86: little-endian, int/size_t/instruction 4 bytes, number 8): rewrite string constants in place
// of a chunk, every function included. Debug strings (source, locals, upvalues) are left alone. Constant strings are
// length-prefixed, so changing one moves nothing else.
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>

namespace luachunk {

struct Reader {
    const uint8_t* p;
    size_t n, at = 0;
    uint32_t u32() { need(4); uint32_t v; memcpy(&v, p + at, 4); at += 4; return v; }
    uint8_t u8() { need(1); return p[at++]; }
    void skip(size_t k) { need(k); at += k; }
    void need(size_t k) const { if (at + k > n) throw std::runtime_error("lua chunk truncated"); }
};

// fn(s) -> true if it changed s (s without the trailing NUL). Returns how many constants changed.
inline int rewrite(const std::string& in, std::string& out, const std::function<bool(std::string&)>& fn)
{
    static const uint8_t HDR[12] = {0x1b, 'L', 'u', 'a', 0x51, 0, 1, 4, 4, 4, 8, 0};
    if (in.size() < 12 || memcmp(in.data(), HDR, 12)) throw std::runtime_error("not a Lua 5.1 x86 chunk");
    Reader r{(const uint8_t*)in.data(), in.size(), 12};
    out.assign(in, 0, 12);
    size_t copied = 12;   // in[copied..r.at) is still to be copied verbatim
    int changed = 0;
    auto flush = [&]() { out.append(in, copied, r.at - copied); copied = r.at; };
    auto str = [&]() { uint32_t len = r.u32(); r.skip(len); };
    std::function<void()> function = [&]() {
        str();                                   // source
        r.skip(4 + 4 + 4);                       // linedefined, lastlinedefined, nups/params/vararg/maxstack
        r.skip((size_t)r.u32() * 4);             // code
        uint32_t nk = r.u32();
        for (uint32_t i = 0; i < nk; ++i) {
            uint8_t t = r.u8();
            if (t == 1) r.skip(1);
            else if (t == 3) r.skip(8);
            else if (t == 4) {
                flush();
                uint32_t len = r.u32();
                r.skip(len);
                std::string s = len ? in.substr(r.at - len, len - 1) : std::string();
                if (len && fn(s)) {
                    uint32_t nl = (uint32_t)s.size() + 1;
                    out.append((const char*)&nl, 4);
                    out += s;
                    out.push_back('\0');
                    ++changed;
                } else out.append(in, r.at - len - 4, len + 4);
                copied = r.at;
            } else if (t != 0) throw std::runtime_error("bad constant type");
        }
        uint32_t np = r.u32();
        for (uint32_t i = 0; i < np; ++i) function();
        r.skip((size_t)r.u32() * 4);             // lineinfo
        uint32_t nl = r.u32();
        for (uint32_t i = 0; i < nl; ++i) { str(); r.skip(8); }   // locvars
        uint32_t nu = r.u32();
        for (uint32_t i = 0; i < nu; ++i) str();                 // upvalue names
    };
    function();
    r.at = in.size();
    flush();
    return changed;
}

}  // namespace luachunk

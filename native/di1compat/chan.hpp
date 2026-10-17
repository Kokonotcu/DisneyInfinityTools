// vfxchan10: curve channels re-laid for the shader a 1.0 material is served on (chanroles.hpp).
#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "convert/chanroles.hpp"
#include "formats/shd.hpp"
#include "gen30.hpp"

namespace chan {

// D3DDisassemble of a D3D9 (raw tokens) or DXBC program, "" if it fails
inline std::string disasm(const std::string& prog)
{
    struct Blob : IUnknown {
        virtual void* STDMETHODCALLTYPE GetBufferPointer() = 0;
        virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
    };
    typedef HRESULT(WINAPI * Fn)(LPCVOID, SIZE_T, UINT, LPCSTR, Blob**);
    static Fn fn = [] {
        HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
        return m ? (Fn)GetProcAddress(m, "D3DDisassemble") : (Fn) nullptr;
    }();
    size_t o = prog.find("DXBC");
    std::string b = o != std::string::npos && o < 64 ? prog.substr(o) : prog;
    Blob* out = nullptr;
    if (!fn || b.empty() || FAILED(fn(b.data(), b.size(), 0, nullptr, &out)) || !out) return "";
    std::string t((const char*)out->GetBufferPointer(), out->GetBufferSize());
    out->Release();
    t.erase(std::remove(t.begin(), t.end(), '\0'), t.end());
    return t;
}

// 1.0: COLOR0.w alpha, COLOR0.xyz colour, COLOR1 emissive
inline constexpr chanroles::RoleRule kRules10Table[] = {{"COLOR0.w", 'A'}, {"COLOR0.", 'C'}, {"COLOR1.", 'E'}};
inline constexpr chanroles::RoleRules kRules10 = {kRules10Table, std::size(kRules10Table)};

using Read = std::function<bool(const std::string& path, std::string& out)>;

// Roles of a shader's first-pass VS, cached per game and shader. Programs: 1.0 '%x.vp', 3.0 '3:...%08x.vp'
inline std::vector<char> shaderRoles(const Read& read, const std::string& guid, bool is30)
{
    static std::mutex mx;
    static std::map<std::string, std::vector<char>> cache;
    std::string key = (is30 ? "3:" : "1:") + guid;
    {
        std::lock_guard<std::mutex> l(mx);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    std::vector<char> r;
    std::string shd, prog;
    std::string pre = is30 ? "3:shaders/shaders/" : "shaders/shaders/";
    const shd::Layout& L = is30 ? shd::kLayout30 : gen30::kShd10;
    const size_t at = L.data + L.passVs, cnt = L.counts + 1;
    if (read(pre + guid + ".shd", shd) && shd.size() >= at + 4 && (uint8_t)shd[cnt] > 0) {
        uint32_t vs;
        memcpy(&vs, shd.data() + at, 4);
        char a[24], b[24];
        sprintf_s(a, "%x.vp", vs);
        sprintf_s(b, "%08x.vp", vs);
        const chanroles::Disasm syntax = is30 ? chanroles::Disasm::Sm4 : chanroles::Disasm::Sm3;
        const bool unpadded = !is30 && read(pre + a, prog);   // 1.0's '%x' name first
        const chanroles::RoleRules& rules = is30 ? chanroles::kRules30 : kRules10;
        if (unpadded || read(pre + b, prog)) r = chanroles::roles(disasm(prog), syntax, rules);
    }
    std::lock_guard<std::mutex> l(mx);
    return cache[key] = r;
}

// The channel layout for an emitter drawn with a 1.0 material on shader s10 that is served on shader s30: false
// (keep the 1.0 channels) when either program has no channel input, the material went onto its translated 1.0
// shader ('x' + GUID), or the layout comes out the same.
inline bool layout(const Read& read10, const Read& read30, const std::string& s10, const std::string& s30, int n10,
                   std::vector<int>& chans)
{
    if (s10.empty() || s30.empty() || s30[0] == 'x') return false;
    std::vector<char> r10 = shaderRoles(read10, s10, false), r30 = shaderRoles(read30, s30, true);
    if (r10.empty() || r30.empty()) return false;
    chans = chanroles::mapChannels(r10, n10, r30);
    bool same = (int)chans.size() == n10;
    for (int i = 0; same && i < n10; ++i) same = chans[i] == i;
    return !same;
}

}  // namespace chan

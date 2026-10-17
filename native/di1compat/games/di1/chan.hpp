// vfxchan10: 1.0 curve channels re-laid for the material's served shader. D3DDisassemble, base, formats; any thread.
#pragma once
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../convert/chanroles.hpp"
#include "../../formats/shd.hpp"
#include "gen30.hpp"

namespace chan {

using Read = std::function<bool(const std::string& path, std::string& out)>;

// 1.0: COLOR0.w alpha, COLOR0.xyz colour, COLOR1 emissive
inline constexpr chanroles::RoleRule kRules10Table[] = {{"COLOR0.w", 'A'}, {"COLOR0.", 'C'}, {"COLOR1.", 'E'}};
inline constexpr chanroles::RoleRules kRules10 = {kRules10Table, std::size(kRules10Table)};

// Where a game keeps a shader's first VS and how its roles read
struct Game {
    const char* tag;                     // the role cache's key prefix
    const char* dir;                     // where its .shd and program files are read
    const shd::Layout* shd;
    chanroles::Disasm syntax;
    const chanroles::RoleRules* rules;
    bool unpaddedFirst;                  // 1.0 names a program '%x' before '%08x'
};
inline constexpr Game kGame10 = {"1:", "shaders/shaders/", &gen30::kShd10, chanroles::Disasm::Sm3, &kRules10, true};
inline constexpr Game kGame30 = {"3:", "3:shaders/shaders/", &shd::kLayout30, chanroles::Disasm::Sm4,
                                 &chanroles::kRules30, false};

namespace detail {

constexpr size_t kDxbcWithin = 64;   // a DXBC tag found past this is program data, not the container start

// ID3DBlob as far as a D3DDisassemble result is read
struct Blob : IUnknown {
    virtual void* STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};
typedef HRESULT(WINAPI* DisasmFn)(LPCVOID, SIZE_T, UINT, LPCSTR, Blob**);

}  // namespace detail

// What shaderRoles keeps between calls. Its owner keeps one per process, so each shader's roles are found once.
struct Roles {
    // D3DDisassemble is looked up on first use: install() runs under the loader lock (H10)
    std::once_flag looked;
    detail::DisasmFn disasm = nullptr;                // set once under looked
    std::mutex mx;                                    // guards cache; never held across a read or a disassembly
    std::map<std::string, std::vector<char>> cache;   // game tag + GUID -> roles; misses stay; never cleared
};

namespace detail {

// D3DDisassemble of a D3D9 (raw tokens) or DXBC program, "" if it fails
inline std::string disasm(Roles& roles, const std::string& prog)
{
    std::call_once(roles.looked, [&roles] {
        HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
        roles.disasm = m ? (DisasmFn)GetProcAddress(m, "D3DDisassemble") : nullptr;
    });
    const size_t o = prog.find("DXBC");
    const std::string b = o != std::string::npos && o < kDxbcWithin ? prog.substr(o) : prog;
    if (!roles.disasm || b.empty()) return "";
    Blob* out = nullptr;
    const HRESULT hr = roles.disasm(b.data(), b.size(), 0, nullptr, &out);
    if (FAILED(hr) || !out) return "";
    std::string t((const char*)out->GetBufferPointer(), out->GetBufferSize());
    out->Release();
    t.erase(std::remove(t.begin(), t.end(), '\0'), t.end());
    return t;
}

// The first pass's VS id; false when the description has none
inline bool firstVs(const std::string& desc, const shd::Layout& L, uint32_t& vs)
{
    const size_t at = L.data + L.passVs;
    if (!bytes::fits(desc.size(), at, sizeof vs) || (uint8_t)desc[L.counts + shd::kCountPasses] == 0) return false;
    vs = bytes::u32OrZero(desc, at);
    return true;
}

}  // namespace detail

// Roles of a shader's first-pass VS, cached in roles per game and shader
inline std::vector<char> shaderRoles(Roles& roles, const Read& read, const std::string& guid, const Game& game)
{
    const std::string key = game.tag + guid;
    {
        std::lock_guard<std::mutex> l(roles.mx);
        auto it = roles.cache.find(key);
        if (it != roles.cache.end()) return it->second;
    }
    // computed unlocked: two threads may both compute a key and store the same roles; misses stay cached
    std::vector<char> r;
    std::string desc, prog;
    const std::string dir = game.dir;
    uint32_t vs = 0;
    if (read(dir + guid + ".shd", desc) && detail::firstVs(desc, *game.shd, vs)) {
        char unpadded[24], padded[24];
        snprintf(unpadded, sizeof unpadded, "%x.vp", vs);
        snprintf(padded, sizeof padded, "%08x.vp", vs);
        const bool first = game.unpaddedFirst && read(dir + unpadded, prog);
        if (first || read(dir + padded, prog))
            r = chanroles::roles(detail::disasm(roles, prog), game.syntax, *game.rules);
    }
    std::lock_guard<std::mutex> l(roles.mx);
    return roles.cache[key] = r;
}

// The two games' reads: 1.0 by path, 3.0 by '3:' extract path
struct Reads { const Read& read10; const Read& read30; };

// Channels re-laid from 1.0 shader s10 onto served s30; false: keep the 1.0 ones, whatever chans holds
inline bool layout(Roles& roles, const Reads& io, const std::pair<std::string, std::string>& shaders, int n10,
                   std::vector<int>& chans)
{
    constexpr char kTranslated = 'x';   // a material on its translated 1.0 shader is served as 'x' + GUID
    const std::string& s10 = shaders.first;
    const std::string& s30 = shaders.second;
    if (s10.empty() || s30.empty() || s30[0] == kTranslated) return false;
    std::vector<char> r10 = shaderRoles(roles, io.read10, s10, kGame10);
    std::vector<char> r30 = shaderRoles(roles, io.read30, s30, kGame30);
    if (r10.empty() || r30.empty()) return false;
    chans = chanroles::mapChannels(r10, n10, r30);
    bool same = (int)chans.size() == n10;
    for (int i = 0; same && i < n10; ++i) same = chans[i] == i;
    return !same;
}

}  // namespace chan

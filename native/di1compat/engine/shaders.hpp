// Shaders a game makes at first use: DXBC compile and cache, programs registered as the engine's own. core.
// Loader threads.
#pragma once
#include <windows.h>
#include <malloc.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../base/hash.hpp"
#include "../core/hooks.hpp"
#include "../core/log.hpp"

namespace engine::shaders {

// ID3DBlob as far as compile uses it (d3dcompiler_47).
struct Blob : IUnknown {
    virtual void* STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};
typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, Blob**,
                                      Blob**);

// HLSL to DXBC: hlsl, target ("vs_4_0", "ps_4_0") -> code; on false, msg may say why and code is invalid.
using Compile = std::function<bool(const std::string& hlsl, const char* target, std::string& code, std::string& msg)>;

// A generated program: the generator's id (as the .shd's passes name it), its stage and its DXBC.
struct Program {
    uint32_t id;
    bool pixel;
    std::string code;
};

// A generated shader: a 3.0 .shd whose passes name the generator's program ids, and those programs.
struct Generated {
    std::string shd;
    std::vector<Program> programs;
};

struct Compiler {
    D3DCompileFn compile = nullptr;   // D3DCompile; nullptr: a cache miss fails
    std::string cacheDir;             // the DXBC cache folder
};

struct Config {
    Compiler compiler;
    int bufView = 0;        // test views of the game's lit pixel programs (bufview10)
    bool magenta = false;   // test: every pixel program the game makes is solid magenta
    std::function<bool(const char* name, const Compile& compile, Generated& out)> generate;   // false: none
};

// Keeps c for make(); installs no hook.
inline bool install(const Config& c);
// The game's shader name, generated and registered if new; nullptr when that failed, then and later.
inline uint8_t* make(const char* name);
// The game's shader name if already made; nullptr otherwise.
inline uint8_t* find(const char* name);

namespace detail {

namespace eng {
// H4 open: the engine functions below are called without a prologue check (X-12)
constexpr uint32_t kRegisterVertex = 0x0045C230;   // FUN_0045c230, cdecl (id, data, size): CreateVertexShader, appended
constexpr uint32_t kRegisterPixel = 0x0045C350;    // FUN_0045c350: the same for pixel shaders
constexpr uint32_t kFinalize = 0x0045CF40;         // FUN_0045cf40: finalizes a loaded .shd (EDI = shader, ECX = cursor)

// The program tables (VS, PS): 8-byte {id, handle} entries and a u16 count (FUN_00472050 reads them).
constexpr uint32_t kProgramEntries[2] = {0x0215EFC8, 0x0216B050};
constexpr uint32_t kProgramCounts[2] = {0x0215EFC4, 0x0216B04C};
constexpr uint32_t kEntrySize = 8;
constexpr uint32_t kHandle = 4;                     // an entry's handle, after its id
constexpr uint16_t kMaxPrograms = 2047;             // pass-state records hold 11-bit program indices; 0x7ff is a miss
constexpr uint32_t kFirstProgramId = 0xF0000000u;   // ours sort after every shipped id, keeping the tables sorted
constexpr uint64_t kPixelKeySalt = 0x9e3779b97f4a7c15ull;   // equal VS and PS code get separate program keys

// .shd (shd.py, 3.0): name pointer at +0, size at +4, pass count at +0x19, passes from +0xE0.
constexpr size_t kShdSize = 4;
constexpr size_t kShdPassCount = 0x19;
constexpr size_t kShdData = 0xE0;
constexpr size_t kPassSize = 0x50;
constexpr size_t kPassVs = 0x28;
constexpr size_t kPassPs = 0x30;
constexpr uint32_t kRecordCount = 0x02173A66;   // pass-state records in use, u16 (FUN_00472050)

constexpr size_t kDxbcHeader = 32;   // magic, checksum, version, size and chunk count
constexpr size_t kAlign = 16;        // our copies of programs and shaders
constexpr const char* kZeroAlbedo = "o.o1 = float4(0, 0, 0, 0);";
constexpr const char* kMagenta = "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(1, 0, 1, 1); }";
constexpr int kIdColourView = 8;
constexpr int kFirstSolidView = 6;
constexpr int kLastSolidView = 7;   // 6: lit final passes, 7: passes without a second target
constexpr int kBufViews = 5;
constexpr const char* kBufView[kBufViews] = {
    "cbuffer perView : register(b1) { float4 CB1[18]; }; Texture2D t3 : register(t3); SamplerState s3 : register(s3);\n"
    "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(t3.Sample(s3, p.xy * CB1[17].zw).rgb, 1); }",
    "cbuffer perView : register(b1) { float4 CB1[18]; }; Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
    "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(t0.Sample(s0, p.xy * CB1[17].zw).xxx, 1); }",
    "cbuffer perView : register(b1) { float4 CB1[18]; }; Texture2D t4 : register(t4); SamplerState s4 : register(s4);\n"
    "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(t4.Sample(s4, p.xy * CB1[17].zw).rgb, 1); }",
    "cbuffer perInstance : register(b4) { float4 CB4[1]; };\n"
    "float4 main(float4 p : SV_Position) : SV_Target0 "
    "{ return float4(CB4[0].rgb * 0.5 + float3(0.5, 0, 0) * (CB4[0].a == 0), 1); }",
    "Texture2D t5 : register(t5); SamplerState s5 : register(s5);\n"
    "struct PSIn { float4 pos : SV_Position; float4 TEXCOORD0 : TEXCOORD0; float4 TEXCOORD1 : TEXCOORD1; "
    "float4 TEXCOORD2 : TEXCOORD2; };\n"
    "float4 main(PSIn i) : SV_Target0 { return float4(t5.Sample(s5, i.TEXCOORD2.xy).rgb, 1); }"};
}  // namespace eng

struct State {
    Config cfg;   // set once in install()
    // generation one at a time: held across D3DCompile and engine registration (RE-39)
    std::mutex mx;
    std::unordered_map<std::string, uint8_t*> shaders;   // guarded by mx; by name (nullptr: failed), never freed (S5)
    std::unordered_map<uint64_t, uint32_t> programs;     // guarded by mx; code key -> engine id
    uint32_t nextId[2] = {eng::kFirstProgramId, eng::kFirstProgramId};   // guarded by mx; VS, PS
};
inline State g_state;

// The cache file at path, read into code: true when it holds DXBC (a whole header, "DXBC" first).
inline bool cachedDxbc(const std::string& path, std::string& code)
{
    FILE* f = nullptr;
    const errno_t err = fopen_s(&f, path.c_str(), "rb");
    if (err || !f) return false;
    char buf[65536];
    code.clear();
    for (size_t got = fread(buf, 1, sizeof(buf), f); got > 0; got = fread(buf, 1, sizeof(buf), f))
        code.append(buf, got);
    fclose(f);
    return code.size() > eng::kDxbcHeader && !code.compare(0, 4, "DXBC");
}

// The colour test views give shader name: two bits of its fnv64 per channel.
inline void viewColour(const char* name, double rgb[3])
{
    const uint64_t h = hash::fnv64(name);
    rgb[0] = (h & 3) / 3.0;
    rgb[1] = (h >> 2 & 3) / 3.0;
    rgb[2] = (h >> 4 & 3) / 3.0;
}

// Test view 8: G-buffer albedo carries the shader's colour; lit final passes skip ("" when neither).
inline std::string idColourView(const char* name, const std::string& hlsl)
{
    if (hlsl.find("tLight") != std::string::npos)
        return "float4 main(float4 p : SV_Position) : SV_Target0 { discard; return 0; }";
    const size_t at = hlsl.find(eng::kZeroAlbedo);
    if (at == std::string::npos) return std::string();
    double rgb[3];
    viewColour(name, rgb);
    char c[160];
    snprintf(c, sizeof(c), "o.o1 = float4(sqrt(float3(%.2f, %.2f, %.2f)), 0.2236);", rgb[0], rgb[1], rgb[2]);
    std::string out = hlsl;
    out.replace(at, strlen(eng::kZeroAlbedo), c);
    core::logf("idcolor %s = %.2f %.2f %.2f", name, rgb[0], rgb[1], rgb[2]);
    return out;
}

// Test views 6 and 7: the pass draws one solid colour of its own, logged with its shader.
inline std::string solidView(const char* name)
{
    double rgb[3];
    viewColour(name, rgb);
    char c[200];
    snprintf(c, sizeof(c), "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(%.2f, %.2f, %.2f, 1); }",
             rgb[0], rgb[1], rgb[2]);
    core::logf("idcolor %s = %.2f %.2f %.2f", name, rgb[0], rgb[1], rgb[2]);
    return c;
}

// The test view that replaces a pixel program's HLSL (bufview10, magenta10); "" when none does.
inline std::string testView(const Config& c, const char* name, const std::string& hlsl)
{
    const bool lit = hlsl.find("tLight") != std::string::npos;
    std::string view = c.bufView == eng::kIdColourView ? idColourView(name, hlsl) : std::string();
    const bool solid = c.bufView >= eng::kFirstSolidView && c.bufView <= eng::kLastSolidView;
    const bool solidHere = c.bufView == eng::kLastSolidView ? hlsl.find("SV_Target1") == std::string::npos : lit;
    if (solid && solidHere) view = solidView(name);
    if (c.magenta) return eng::kMagenta;
    if (!view.empty()) return view;
    if (c.bufView && lit) return eng::kBufView[(c.bufView - 1) % eng::kBufViews];
    return std::string();
}

// Compiles shader name's program (or its test view) through the DXBC cache; a fresh compile is cached.
inline bool compile(const char* name, const std::string& source, const char* target, std::string& code,
                    std::string& msg)
{
    const Config& c = g_state.cfg;
    const std::string view = target[0] == 'p' ? testView(c, name, source) : std::string();
    const std::string& hlsl = view.empty() ? source : view;
    const std::string path = c.compiler.cacheDir + "\\" + hash::shaderCacheName(target, hlsl);
    if (cachedDxbc(path, code)) return true;
    if (!c.compiler.compile) {
        msg = "no d3dcompiler_47";
        return false;
    }
    Blob* out = nullptr;
    Blob* err = nullptr;
    const HRESULT hr =
        c.compiler.compile(hlsl.data(), hlsl.size(), "di1compat", nullptr, nullptr, "main", target, 0, 0, &out, &err);
    if (err) msg.assign((const char*)err->GetBufferPointer(), err->GetBufferSize());
    if (err) err->Release();
    if (out && FAILED(hr)) out->Release();
    if (FAILED(hr) || !out) return false;
    code.assign((const char*)out->GetBufferPointer(), out->GetBufferSize());
    out->Release();
    FILE* f = nullptr;
    const errno_t open = fopen_s(&f, path.c_str(), "wb");
    if (open || !f) return true;
    fwrite(code.data(), 1, code.size(), f);
    fclose(f);
    return true;
}

// A program into its stage's engine table (mx held) -> its engine id; identical code is registered once.
inline std::optional<uint32_t> registerProgram(const Program& p)
{
    State& s = g_state;
    const uint64_t key = hash::fnv64(p.code) ^ (p.pixel ? eng::kPixelKeySalt : 0);
    if (auto it = s.programs.find(key); it != s.programs.end()) return it->second;
    const int stage = p.pixel ? 1 : 0;
    const uint32_t entries = eng::kProgramEntries[stage];
    const uint16_t n = *(uint16_t*)core::rt(eng::kProgramCounts[stage]);
    if (n >= eng::kMaxPrograms) {
        core::logf("shaders10: %s table full (%u)", p.pixel ? "PS" : "VS", n);
        return std::nullopt;
    }
    const uint32_t last = n ? *(uint32_t*)core::rt(entries + eng::kEntrySize * (n - 1)) : 0;
    uint32_t& next = s.nextId[stage];
    if (next <= last) next = last + 1;
    const uint32_t id = next++;
    // kept (S5): the engine reads VS bytecode again for input layouts
    void* data = _aligned_malloc(p.code.size(), eng::kAlign);
    memcpy(data, p.code.data(), p.code.size());
    typedef void(__cdecl * RegFn)(uint32_t id, const void* data, uint32_t size);
    ((RegFn)core::rt(p.pixel ? eng::kRegisterPixel : eng::kRegisterVertex))(id, data, (uint32_t)p.code.size());
    const bool counted = *(uint16_t*)core::rt(eng::kProgramCounts[stage]) == n + 1;
    if (!counted || !*(uint32_t*)core::rt(entries + eng::kEntrySize * n + eng::kHandle)) {
        core::logf("shaders10: program %08X not created", id);
        return std::nullopt;
    }
    s.programs[key] = id;
    return id;
}

// FUN_0045cf40 as the loader calls it: EDI = shader, ECX = &cursor (B-44: kept as written).
inline void finalizeShd(uint8_t* shd)
{
    uint32_t cursor[2] = {(uint32_t)(uintptr_t)(shd + eng::kShdData), 0};
    uint32_t* pc = cursor;
    void* fn = (void*)core::rt(eng::kFinalize);
    __asm {
        push edi
        mov ecx, pc
        mov edi, shd
        call fn
        pop edi
    }
}

// g's .shd with engine program ids, registered and finalized (mx held); allocated once, never freed (S5).
inline uint8_t* addShader(const char* name, Generated& g)
{
    std::map<uint32_t, uint32_t> engineId;   // generator id -> engine id
    for (const Program& p : g.programs) {
        const std::optional<uint32_t> id = registerProgram(p);
        if (!id) return nullptr;
        engineId[p.id] = *id;
    }
    std::string& shd = g.shd;
    for (int i = 0; i < (uint8_t)shd[eng::kShdPassCount]; ++i)
        for (size_t off : {eng::kPassVs, eng::kPassPs}) {
            uint32_t& pid = *(uint32_t*)&shd[eng::kShdData + eng::kPassSize * i + off];
            if (pid) pid = engineId.at(pid);
        }
    uint8_t* mem = (uint8_t*)_aligned_malloc(shd.size(), eng::kAlign);
    memcpy(mem, shd.data(), shd.size());
    char* nm = _strdup(name);
    *(uint32_t*)mem = (uint32_t)(uintptr_t)nm;
    *(uint32_t*)(mem + eng::kShdSize) = (uint32_t)shd.size();
    finalizeShd(mem);
    core::logf("shaders10 %s: %u passes, %u programs registered (pass-state records in use: %u)", name,
               (unsigned)mem[eng::kShdPassCount], (unsigned)g.programs.size(), *(uint16_t*)core::rt(eng::kRecordCount));
    return mem;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::g_state.cfg = c;
    return true;
}

inline uint8_t* find(const char* name)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    auto it = s.shaders.find(name);
    return it != s.shaders.end() ? it->second : nullptr;
}

inline uint8_t* make(const char* name)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    if (auto it = s.shaders.find(name); it != s.shaders.end()) return it->second;
    uint8_t*& out = s.shaders[name];
    out = nullptr;
    const Compile compile = [name](const std::string& hlsl, const char* target, std::string& code, std::string& msg) {
        return detail::compile(name, hlsl, target, code, msg);
    };
    Generated g;
    if (!s.cfg.generate || !s.cfg.generate(name, compile, g)) return nullptr;
    out = detail::addShader(name, g);
    return out;
}

}  // namespace engine::shaders

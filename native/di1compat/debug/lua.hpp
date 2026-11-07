// Watch: Lua calls into C by function and calling chunk, and Lua errors. core, chain. Lua threads.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"
#include "chain.hpp"

namespace dbg::lua {

// Calls of one C function from one chunk. unreturned: calls cut short by a Lua error, or running now.
struct CallRow {
    std::string function, caller;
    uint32_t calls, unreturned;
};

struct ErrorRow {
    std::string message;
    uint32_t count;
};

struct Config {
    chain::Module exe, compat;                // set once: where a C function lives names it
    void (*onError)(const char*) = nullptr;   // also told of each error message
};

// Hooks precall, the error path and the engine's binder as one watch (H1, H2).
inline bool install(const Config& c);
inline std::vector<CallRow> calls();
inline std::vector<ErrorRow> errors();

namespace eng {
constexpr uint32_t kPrecall = 0x013140E0;    // FUN_013140e0 luaD_precall, cdecl (L, func, nresults) -> int
constexpr uint32_t kErrorMsg = 0x01316120;   // FUN_01316120 luaG_errormsg, cdecl (L); longjmps (FUN_01314050)
constexpr uint32_t kBind = 0x0154EBE0;       // FUN_0154ebe0 thiscall (name, obj, fn, adj, extra; ret 0x14)
constexpr uint32_t kDispatch = 0x00B41520;   // FUN_00b41520: the closure kBind makes; calls its upvalue's payload[1]
// TValue = {value, tag}; tag 0xFFFF0000 | type (FUN_0130d6d0)
constexpr uint32_t kTagString = 0xFFFF0004;
constexpr uint32_t kTagFunction = 0xFFFF0006;
constexpr uint32_t kTagUserdata = 0xFFFF0007;
constexpr size_t kTop = 0x8;              // L->top (FUN_0130e270)
constexpr size_t kCi = 0x14;              // L->ci (FUN_013140e0)
constexpr size_t kCiFunc = 0x4;           // ci->func, a TValue*
constexpr size_t kIsC = 0x6;              // closure byte: C closure (FUN_013140e0)
constexpr size_t kClosureFn = 0x10;       // C closure: f; Lua closure: its Proto
constexpr size_t kUpvalue0 = 0x18;        // C closure upvalue[0] (FUN_0130ced0, upvalueindex 1)
constexpr size_t kUdataPayload = 0x18;    // userdata header size (FUN_0130e270 returns +0x18)
constexpr size_t kPayloadFn = 0x4;        // kBind's payload {obj, fn, adj}: fn (0x0154ebff)
constexpr size_t kProtoSource = 0x20;     // Proto->source, a TString (FUN_013160c0)
constexpr size_t kStringText = 0x10;      // TString text (FUN_0130d5a0)
constexpr size_t kTValueWords = 2;
constexpr size_t kMaxErrors = 2000;       // distinct messages kept; later new ones are dropped
}  // namespace eng

namespace detail {

typedef int(__cdecl* PrecallFn)(uint8_t* L, uint32_t* func, int nresults);
typedef void(__cdecl* ErrorMsgFn)(uint8_t* L);
typedef void*(__fastcall* BindFn)(void* self, void* edx, const char* name, void* obj, uint32_t fn, uint32_t adj,
                                  int extra);

// One (C function, caller chunk) pair. Allocated once, never freed: precall keeps pointers to it.
struct Rec {
    uint32_t fn = 0;
    std::string caller;
    std::atomic<uint32_t> calls{0}, returns{0};
};

struct State {
    PrecallFn g_precall = nullptr;   // set once by installWatch
    ErrorMsgFn g_errorMsg = nullptr;
    BindFn g_bind = nullptr;
    Config cfg;                      // set once in install()
    std::mutex mx;                   // guards recs, names, errs; a leaf lock
    std::unordered_map<uint64_t, std::unique_ptr<Rec>> recs;   // fn << 32 | source TString
    std::unordered_map<uint32_t, std::string> names;           // C function -> its kBind name
    std::unordered_map<std::string, uint32_t> errs;            // message -> count
};
inline State g_state;

// The C function a call runs (kBind's payload fn for its closures); 0 for a Lua function or a non-function.
inline uint32_t cFunction(const uint32_t* func)
{
    if (func[1] != eng::kTagFunction) return 0;
    const uint8_t* cl = (const uint8_t*)func[0];
    if (!cl[eng::kIsC]) return 0;
    const uint32_t f = *(const uint32_t*)(cl + eng::kClosureFn);
    const uint32_t* up = (const uint32_t*)(cl + eng::kUpvalue0);
    if (f != core::rt(eng::kDispatch) || up[1] != eng::kTagUserdata) return f;
    return *(const uint32_t*)(up[0] + eng::kUdataPayload + eng::kPayloadFn);
}

// The source TString of the Lua function that makes the call; 0 when C code calls.
inline uint32_t callerSource(const uint8_t* L)
{
    const uint8_t* ci = *(uint8_t* const*)(L + eng::kCi);
    const uint32_t* fv = *(uint32_t* const*)(ci + eng::kCiFunc);
    if (fv[1] != eng::kTagFunction) return 0;
    const uint8_t* cl = (const uint8_t*)fv[0];
    if (cl[eng::kIsC]) return 0;
    const uint8_t* proto = *(uint8_t* const*)(cl + eng::kClosureFn);
    return *(const uint32_t*)(proto + eng::kProtoSource);
}

// ponytail: keyed by the source TString's address; a collected chunk's reused address mislabels, never seen.
inline Rec* record(uint32_t fn, uint32_t src)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    std::unique_ptr<Rec>& r = s.recs[(uint64_t)fn << 32 | src];
    if (r) return r.get();
    r = std::make_unique<Rec>();
    r->fn = fn;
    char text[MAX_PATH] = "[C]";
    if (src) core::safeCopy(text, sizeof(text), (const char*)(src + eng::kStringText));
    r->caller = text;
    return r.get();
}

inline Rec* noteCall(const uint8_t* L, const uint32_t* func) noexcept
{
    try {
        const uint32_t fn = cFunction(func);
        if (!fn) return nullptr;
        Rec* r = record(fn, callerSource(L));
        r->calls.fetch_add(1, std::memory_order_relaxed);
        return r;
    } catch (...) {
        return nullptr;
    }
}

// FUN_013140e0, cdecl. Lua threads, per call. No C++ objects here: a Lua error longjmps through this frame.
inline int __cdecl hkPrecall(uint8_t* L, uint32_t* func, int nresults)
{
    Rec* r = noteCall(L, func);
    const int ret = g_state.g_precall(L, func, nresults);
    if (r) r->returns.fetch_add(1, std::memory_order_relaxed);
    return ret;
}

inline void noteError(const uint8_t* L) noexcept
{
    try {
        State& s = g_state;
        const uint32_t* v = *(uint32_t* const*)(L + eng::kTop) - eng::kTValueWords;
        char msg[300] = "<not a string>";
        if (v[1] == eng::kTagString) core::safeCopy(msg, sizeof(msg), (const char*)(v[0] + eng::kStringText));
        if (s.cfg.onError) s.cfg.onError(msg);
        std::lock_guard<std::mutex> lock(s.mx);
        const bool room = s.errs.size() < eng::kMaxErrors || s.errs.count(msg);
        if (room) ++s.errs[msg];
    } catch (...) {
    }
}

// FUN_01316120, cdecl. Lua threads, per error; never returns. No C++ objects here (longjmp).
inline void __cdecl hkErrorMsg(uint8_t* L)
{
    noteError(L);
    g_state.g_errorMsg(L);
}

inline void noteName(uint32_t fn, const char* name) noexcept
{
    try {
        char n[128];
        core::safeCopy(n, sizeof(n), name);
        std::lock_guard<std::mutex> lock(g_state.mx);
        g_state.names.emplace(fn, n);
    } catch (...) {
    }
}

// FUN_0154ebe0, thiscall. Main thread, once per binding while the engine builds its Lua API.
inline void* __fastcall hkBind(void* self, void*, const char* name, void* obj, uint32_t fn, uint32_t adj, int extra)
{
    noteName(fn, name);
    return g_state.g_bind(self, nullptr, name, obj, fn, adj, extra);
}

// fn's kBind name, else where it lives. Caller holds mx.
inline std::string nameOf(uint32_t fn)
{
    const State& s = g_state;
    auto it = s.names.find(fn);
    if (it != s.names.end()) return it->second;
    char b[48];
    if (s.cfg.exe.contains(fn)) snprintf(b, sizeof(b), "FUN_%08x", core::toStaticVa(fn));
    else if (s.cfg.compat.contains(fn)) snprintf(b, sizeof(b), "di1compat+%x", fn - (uint32_t)s.cfg.compat.lo);
    else snprintf(b, sizeof(b), "C@%08x", fn);
    return b;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.cfg = c;
    return chain::installWatch(
        "lua",
        {{"luaD_precall (lua watch)", eng::kPrecall, "8B4424088178040600FFFF5556", (void*)&detail::hkPrecall,
          (void**)&s.g_precall},
         {"luaG_errormsg (lua watch)", eng::kErrorMsg, "568B7424088B466485C0", (void*)&detail::hkErrorMsg,
          (void**)&s.g_errorMsg},
         {"Lua binder (lua watch)", eng::kBind, "8B44240456578B7C2410", (void*)&detail::hkBind, (void**)&s.g_bind}},
        c.compat);
}

inline std::vector<CallRow> calls()
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    std::vector<CallRow> out;
    out.reserve(s.recs.size());
    for (const auto& kv : s.recs) {
        const detail::Rec& r = *kv.second;
        const uint32_t n = r.calls.load(), back = r.returns.load();
        out.push_back({detail::nameOf(r.fn), r.caller, n, n > back ? n - back : 0});
    }
    return out;
}

inline std::vector<ErrorRow> errors()
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    std::vector<ErrorRow> out;
    out.reserve(s.errs.size());
    for (const auto& kv : s.errs) out.push_back({kv.first, kv.second});
    return out;
}

}  // namespace dbg::lua

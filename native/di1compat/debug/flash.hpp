// Watch: Flash ExternalInterface calls by screen and name, with Lua errors inside. core, chain. Main thread.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"
#include "chain.hpp"

namespace dbg::flash {

struct Row {
    std::string screen, name;
    uint32_t calls, errors;   // errors: Lua errors raised while the call ran
};

// Hooks the ExternalInterface handler (H1, H2).
inline bool install(const chain::Module& compat);
inline std::vector<Row> rows();
// A Lua error: counted against the call running on this thread, if any.
inline void noteError(const char* message);

namespace eng {
// 0x01564790, cdecl (a1, a2, name, args, argc); not a Ghidra function. di1compat hooks it too (movies.hpp).
constexpr uint32_t kFlashCall = 0x01564790;
// a GFx Value (movies.hpp eng): type word, value word; managed strings take one more hop
constexpr size_t kGfxType = 0xC, kGfxValue = 0x10;
constexpr uint32_t kGfxTypeMask = 0x8F, kGfxString = 6, kGfxManaged = 0x40;
}  // namespace eng

namespace detail {

typedef void(__cdecl* FlashCallFn)(void* a1, void* a2, const char* name, uint8_t* args, void* argc);

// One (screen, name) pair. Allocated once, never freed: running points to it.
struct Rec {
    std::string screen, name;
    std::atomic<uint32_t> calls{0}, errors{0};
};

struct State {
    FlashCallFn g_flashCall = nullptr;   // set once by installWatch
    std::mutex mx;                       // guards recs; a leaf lock
    std::unordered_map<std::string, std::unique_ptr<Rec>> recs;   // screen + name -> counts
    std::atomic<Rec*> running{nullptr};   // the call in progress
    std::atomic<DWORD> runningThread{0};  // its thread
};
inline State g_state;

// A GFx Value's string, or null (movies.hpp gfxString, read-only here).
inline const char* gfxString(const uint8_t* v)
{
    if (!core::readable(v, eng::kGfxValue + 4)) return nullptr;
    const uint32_t t = *(const uint32_t*)(v + eng::kGfxType);
    const uint8_t* p = *(const uint8_t* const*)(v + eng::kGfxValue);
    if ((t & eng::kGfxTypeMask) != eng::kGfxString) return nullptr;
    if (t & eng::kGfxManaged) p = core::readable(p, 4) ? *(const uint8_t* const*)p : nullptr;
    return (const char*)p;
}

inline Rec* noteCall(const char* name, const uint8_t* args) noexcept
{
    try {
        char screen[64] = "", call[64] = "";
        core::safeCopy(screen, sizeof(screen), args ? gfxString(args) : nullptr);
        core::safeCopy(call, sizeof(call), name);
        std::lock_guard<std::mutex> lock(g_state.mx);
        std::unique_ptr<Rec>& r = g_state.recs[std::string(screen) + '\x01' + call];
        if (!r) {
            r = std::make_unique<Rec>();
            r->screen = screen;
            r->name = call;
        }
        r->calls.fetch_add(1, std::memory_order_relaxed);
        return r.get();
    } catch (...) {
        return nullptr;
    }
}

// 0x01564790, cdecl. Main thread, per call. Calls nest: the outer call is restored after the inner one.
inline void __cdecl hkFlashCall(void* a1, void* a2, const char* name, uint8_t* args, void* argc)
{
    State& s = g_state;
    Rec* outer = s.running.load();
    const DWORD outerThread = s.runningThread.load();
    s.runningThread = GetCurrentThreadId();
    s.running = noteCall(name, args);
    s.g_flashCall(a1, a2, name, args, argc);
    s.running = outer;
    s.runningThread = outerThread;
}

}  // namespace detail

inline void noteError(const char*)
{
    detail::State& s = detail::g_state;
    detail::Rec* r = s.running.load();
    if (r && s.runningThread.load() == GetCurrentThreadId()) r->errors.fetch_add(1, std::memory_order_relaxed);
}

inline bool install(const chain::Module& compat)
{
    detail::State& s = detail::g_state;
    return chain::installWatch("flash",
                               {{"movie ExternalInterface call (flash watch)", eng::kFlashCall,
                                 "64A1000000006AFF68????????506489250000000081ECC8000000",
                                 (void*)&detail::hkFlashCall, (void**)&s.g_flashCall}},
                               compat);
}

inline std::vector<Row> rows()
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    std::vector<Row> out;
    out.reserve(s.recs.size());
    for (const auto& kv : s.recs) out.push_back({kv.second->screen, kv.second->name, kv.second->calls.load(),
                                                 kv.second->errors.load()});
    return out;
}

}  // namespace dbg::flash

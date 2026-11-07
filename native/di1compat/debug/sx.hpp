// Watch: native calls of served 1.0 Slang scripts, by import and answerer. core, slangvm, chain. Script threads.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"
#include "../engine/slangvm.hpp"
#include "../formats/slang.hpp"
#include "chain.hpp"

namespace dbg::sx {

// Who answers an import: 3.0's own native, a di1compat port, a di1compat stub, or nothing.
enum class Answer { Engine, Ported, Stub, Unbound };

struct Row {
    std::string name;
    Answer answer;
    uint32_t calls, imports;   // calls made; import entries bound this way across linked scripts
};

// Hooks the Slang linker (H1, H2); compat tells di1compat's natives from the engine's.
inline bool install(const chain::Module& compat);
inline std::vector<Row> rows();
inline const char* answerText(Answer a);

namespace eng {
// FUN_00736da0, thiscall (registry; script header; ret 4) -> AL linked
constexpr uint32_t kLink = 0x00736DA0;
}  // namespace eng

namespace detail {

typedef uint8_t(__fastcall* LinkFn)(void* reg, void* edx, uint8_t* hdr);
using engine::slangvm::NativeFn;

// One (import name, answer) pair's counts. Allocated once, never freed: Hops point to it.
struct Agg {
    std::string name;
    Answer answer = Answer::Engine;
    std::atomic<uint32_t> calls{0};
    uint32_t imports = 0;   // guarded by mx
};

// What a wrapped import slot held, so the counting native can call on.
struct Hop {
    NativeFn fn = nullptr;
    Agg* agg = nullptr;
};

struct State {
    LinkFn g_link = nullptr;   // set once by installWatch
    chain::Module compat;      // set once in install()
    std::shared_mutex mx;      // guards hops and aggs; shared on the per-call path
    std::unordered_map<uint8_t*, Hop> hops;                    // import entry -> its slot's native
    std::unordered_map<std::string, std::unique_ptr<Agg>> aggs;   // name + answer -> counts
};
inline State g_state;

// Bound into every wrapped slot: counts, then runs the native the slot held. Script threads, per call.
inline void __cdecl countingNative(uint8_t* ctx)
{
    Hop h;
    {
        std::shared_lock<std::shared_mutex> lock(g_state.mx);
        auto it = g_state.hops.find(engine::slangvm::detail::callingEntry(ctx));
        if (it != g_state.hops.end()) h = it->second;
    }
    if (h.agg) h.agg->calls.fetch_add(1, std::memory_order_relaxed);
    // a wrapped entry always has a hop: wrap() writes the hop before the slot
    h.fn(ctx);
}

// The answer an import slot gives: di1compat's binder leaves its Native* in the spare word (slangvm::bindImports).
inline Answer answerOf(const uint8_t* e)
{
    const uintptr_t fn = *(const uintptr_t*)(e + slang::kSlotAt);
    if (!fn) return Answer::Unbound;
    if (!g_state.compat.contains(fn)) return Answer::Engine;
    const auto* n = *(const engine::slangvm::Native* const*)(e + slang::kSpareAt);
    const bool readable = core::readable(n, sizeof(*n));
    return readable && n->fn ? Answer::Ported : Answer::Stub;
}

// The counts of name answered this way. Caller holds mx exclusively.
inline Agg* aggregate(const char* name, Answer a)
{
    char n[96];
    core::safeCopy(n, sizeof(n), name);
    std::unique_ptr<Agg>& g = g_state.aggs[std::string(n) + '\x01' + answerText(a)];
    if (!g) {
        g = std::make_unique<Agg>();
        g->name = n;
        g->answer = a;
    }
    return g.get();
}

// Points one import entry at countingNative, keeping what it held. Caller holds mx exclusively.
inline void wrap(uint8_t* e)
{
    NativeFn& slot = *(NativeFn*)(e + slang::kSlotAt);
    if (slot == &countingNative) return;
    const Answer a = answerOf(e);
    Agg* g = aggregate(*(const char* const*)e, a);
    ++g->imports;
    if (a == Answer::Unbound) return;
    g_state.hops[e] = {slot, g};
    slot = &countingNative;
}

// A linked served script's imports, all wrapped. Requesting threads, per link.
inline void wrapScript(uint8_t* hdr)
{
    if (!engine::slangvm::isServed(hdr)) return;
    const int n = *(const int*)(hdr + slang::kImportCountAt);
    uint8_t* t = *(uint8_t**)(hdr + slang::kImportTableAt);
    std::unique_lock<std::shared_mutex> lock(g_state.mx);
    for (int i = 0; i < n; ++i) wrap(t + slang::kEntry * i);
}

// FUN_00736da0, thiscall. Requesting threads, per link. di1compat binds inside the original; we wrap after.
inline uint8_t __fastcall hkLink(void* reg, void*, uint8_t* hdr)
{
    const uint8_t ok = g_state.g_link(reg, nullptr, hdr);
    if (!ok) return ok;
    try {
        wrapScript(hdr);
    } catch (...) {   // never unwind into engine code (H6)
    }
    return ok;
}

}  // namespace detail

inline const char* answerText(Answer a)
{
    switch (a) {
    case Answer::Engine: return "3.0";
    case Answer::Ported: return "ported";
    case Answer::Stub: return "stub";
    case Answer::Unbound: return "unbound";
    }
    return "?";
}

inline bool install(const chain::Module& compat)
{
    detail::State& s = detail::g_state;
    s.compat = compat;
    return chain::installWatch("sx",
                               {{"Slang linker (sx watch)", eng::kLink, "51538B5C240C568BF18BCBE8",
                                 (void*)&detail::hkLink, (void**)&s.g_link}},
                               compat);
}

inline std::vector<Row> rows()
{
    detail::State& s = detail::g_state;
    std::shared_lock<std::shared_mutex> lock(s.mx);
    std::vector<Row> out;
    out.reserve(s.aggs.size());
    for (const auto& kv : s.aggs) {
        const detail::Agg& g = *kv.second;
        out.push_back({g.name, g.answer, g.calls.load(), g.imports});
    }
    return out;
}

}  // namespace dbg::sx

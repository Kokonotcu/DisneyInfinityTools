// Slang VM natives: cells, the stub, the binder behind the engine's linker, call-by-name. core. Script threads.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "../base/bytes.hpp"
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"
#include "../formats/slang.hpp"

namespace engine::slangvm {

// A VM cell (push float 0x731df0, pop int 0x731fd0, pop pointer 0x732140).
struct Cell {
    int32_t type;
    union {
        int32_t i;
        float f;
        const char* s;
        void* p;
    };
};
constexpr int32_t kInt = 0;
constexpr int32_t kFloat = 1;
constexpr int32_t kPointer = 2;   // a string literal or an object; 3 is a hash, 3.0 only

typedef void(__cdecl* NativeFn)(uint8_t* ctx);

// A native the binder puts in import slots. Rows live in static storage: slots keep pointers to them (S5).
struct Native {
    const char* name;
    int8_t args, results;   // the 1.0 body's pops and pushes
    NativeFn fn;            // nullptr: the stub
    long hits;              // the stub's calls; interlocked
};

// The game's look at a call-by-name before it runs. Called under __try: it must hold no objects (H7).
typedef void (*CallObserver)(uint8_t* ctx, const char* name);

struct Config {
    std::vector<Native*> natives;          // bound by name; a later row of the same name wins
    CallObserver onCallByName = nullptr;   // nullptr: call-by-name is not hooked
};

// Installs the linker hook (and call-by-name with an observer) as one feature (H2); false when refused.
inline bool install(const Config& c);

namespace eng {
constexpr size_t kScript = 4;      // VM context +4: the running script's header
constexpr size_t kCells = 0xc;     // ***(ctx +0xc): the cells
constexpr size_t kPc = 0x1c;       // the CALL's pc, in u16 words from the code (CALL handler 0x738345)
constexpr size_t kTop = 0x20;      // **(ctx +0x20): the top index
constexpr size_t kCallImport = 2;  // the CALL's u16 import index follows its opcode word
}  // namespace eng

// The VM stack of a context; at(0) is the top cell, the last argument.
struct Vm {
    uint8_t* c;
    int& top() const { return **(int**)(c + eng::kTop); }
    Cell* base() const { return *(Cell**)*(uint8_t**)*(uint8_t**)(c + eng::kCells); }
    Cell& at(int k) const { return base()[top() - 1 - k]; }
    Cell pop() const { return base()[--top()]; }
    void push(Cell v) const { base()[top()++] = v; }
    void pushInt(int v) const
    {
        Cell x;
        x.type = kInt;
        x.i = v;
        push(x);
    }
    void pushPointer(const char* s) const
    {
        Cell x;
        x.type = kPointer;
        x.s = s;
        push(x);
    }
};

// The header of the script ctx runs.
inline uint8_t* script(uint8_t* ctx) { return *(uint8_t**)(ctx + eng::kScript); }

// Whether a loaded script's import table carries upgrade()'s mark.
inline bool isServed(const uint8_t* hdr)
{
    const uint8_t* t = *(uint8_t* const*)(hdr + slang::kImportTableAt);
    const bool imports = *(const int*)(hdr + slang::kImportCountAt) > 0;
    return imports && t && bytes::u32At(t + slang::kMarkAt) == slang::kServedMark;
}

// A pointer cell's text into out; false and out empty for null or unreadable memory (SX-05).
inline bool cellText(uint8_t* ctx, const char* p, char* out, size_t cap);

namespace detail {

typedef uint8_t(__fastcall* LinkFn)(void* reg, void* edx, uint8_t* hdr);
typedef void(__fastcall* CallByNameFn)(uint8_t* ctx, void* edx, const char* name, int arg);

struct State {
    std::unordered_map<std::string_view, Native*> natives;   // set once in install(), before the linker hook
    std::mutex bindMx;                                        // guards names
    std::unordered_map<uint8_t*, std::string> names;          // header -> path; grows, no unload hook (SX-07)
    CallObserver onCallByName = nullptr;                      // set once in install()
    LinkFn g_link = nullptr;                                  // set once by installFeature
    CallByNameFn g_callByName = nullptr;
};
inline State g_state;

// The import entry the running CALL went through.
inline uint8_t* callingEntry(uint8_t* ctx)
{
    uint8_t* hdr = script(ctx);
    uint8_t* ip = *(uint8_t**)(hdr + slang::kCodeAt) + sizeof(uint16_t) * *(int*)(ctx + eng::kPc);
    return *(uint8_t**)(hdr + slang::kImportTableAt) + slang::kEntry * *(uint16_t*)(ip + eng::kCallImport);
}

// Copies a C string under __try, the second line behind cellText's checks (H7: no objects here).
inline bool copyText(const char* p, char* out, size_t cap)
{
    __try {
        size_t i = 0;
        for (; p && i + 1 < cap && p[i]; ++i) out[i] = p[i];
        out[i] = 0;
        return p != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out[0] = 0;
        return false;
    }
}

// p lies in the running script's image, where its string literals are: readable without a system call.
inline bool inScript(uint8_t* ctx, const char* p)
{
    const uint8_t* hdr = script(ctx);
    const uintptr_t at = (uintptr_t)p, from = (uintptr_t)hdr;
    return at >= from && at - from < bytes::u32At(hdr + slang::kSizeAt);
}

}  // namespace detail

// Bound for a native without a port (SX-4): pops its args, pushes int 0 per result, logs its first call.
inline void __cdecl stub(uint8_t* ctx)
{
    Native* n = *(Native**)(detail::callingEntry(ctx) + slang::kSpareAt);
    Vm v{ctx};
    v.top() -= n->args;
    for (int i = 0; i < n->results; ++i) v.pushInt(0);
    const long hits = _InterlockedIncrement(&n->hits);
    if (hits == 1) core::logf("sx stub %s first called (pops %d, pushes %d zero)", n->name, n->args, n->results);
}

namespace detail {

// Points a served script's imports at the natives: counts for the log, unbound names after a space each.
struct BindCount {
    int dll = 0, ported = 0, eng = 0, none = 0;
    std::string unbound;
};
inline void bindImports(uint8_t* t, int n, BindCount& k)
{
    const State& s = g_state;
    for (int i = 0; i < n; ++i) {
        uint8_t* e = t + slang::kEntry * i;
        const char* nm = *(const char**)e;
        auto it = nm ? s.natives.find(nm) : s.natives.end();
        if (it != s.natives.end()) {
            Native* nat = it->second;
            *(Native**)(e + slang::kSpareAt) = nat;
            *(NativeFn*)(e + slang::kSlotAt) = nat->fn ? nat->fn : &stub;
            ++k.dll;
            k.ported += nat->fn != nullptr;
        } else if (*(void**)(e + slang::kSlotAt)) {
            ++k.eng;
        } else {
            ++k.none;
            k.unbound += std::string(" ") + (nm ? nm : "?");
        }
    }
}

}  // namespace detail

inline bool cellText(uint8_t* ctx, const char* p, char* out, size_t cap)
{
    // a pointer that fails readable would fault in copyText, which returns the same: false, out empty (H8)
    const bool safe = p == nullptr || detail::inScript(ctx, p) || core::readable(p, 1);
    if (!safe) {
        out[0] = 0;
        return false;
    }
    return detail::copyText(p, out, cap);
}

// After the engine linked a script, a served one's imports go to the natives (SX-3). Idempotent: relinks rewrite slots.
inline void bind(uint8_t* hdr)
{
    detail::State& s = detail::g_state;
    int n = *(int*)(hdr + slang::kImportCountAt);
    uint8_t* t = *(uint8_t**)(hdr + slang::kImportTableAt);
    if (n <= 0 || !t || bytes::u32At(t + slang::kMarkAt) != slang::kServedMark) return;
    // a first link: entry 0's spare word holds the path offset; once bound, a Native* above any size (SX-08)
    uint32_t size = bytes::u32At(hdr + slang::kSizeAt), w3 = bytes::u32At(t + slang::kSpareAt);
    std::string name;
    bool first = false;
    {
        std::lock_guard<std::mutex> lock(s.bindMx);
        if (w3 && w3 < size) {
            s.names[hdr] = (const char*)hdr + w3;
            first = true;
            *(uint32_t*)(t + slang::kSpareAt) = 0;
        }
        name = s.names[hdr];
    }
    detail::BindCount k;
    detail::bindImports(t, n, k);
    if (first)
        core::logf("sx link %s: %d imports: %d engine, %d DI1 natives (%d ported, %d stubs), %d unbound%s",
                   name.c_str(), n, k.eng, k.dll, k.ported, k.dll - k.ported, k.none, k.unbound.c_str());
}

namespace detail {

// FUN_00736da0, thiscall (registry [0x22040d4], script header; ret 4), AL linked. Requesting threads, per request.
inline uint8_t __fastcall hkLink(void* reg, void*, uint8_t* hdr)
{
    uint8_t ok = g_state.g_link(reg, nullptr, hdr);
    if (!ok) return ok;
    try {
        bind(hdr);
    } catch (...) {   // never unwind into engine code (H6)
    }
    return ok;
}

// The observer under __try (H7: no objects here); a fault needs an already corrupt VM.
inline void observe(uint8_t* ctx, const char* name)
{
    __try {
        g_state.onCallByName(ctx, name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// FUN_00739090, thiscall (VM context; name, arg; ret 8). Script threads, per call. Nothing after the original (SX-18).
inline void __fastcall hkCallByName(uint8_t* ctx, void*, const char* name, int arg)
{
    observe(ctx, name);
    g_state.g_callByName(ctx, nullptr, name, arg);
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    for (Native* n : c.natives) s.natives[n->name] = n;
    s.onCallByName = c.onCallByName;
    std::vector<core::HookSpec> rows = {{"Slang linker (1.0 natives)", 0x00736DA0, "51538B5C240C568BF18BCBE8",
                                         (void*)&detail::hkLink, (void**)&s.g_link}};
    if (c.onCallByName)
        rows.push_back({"Slang call by name (1.0 behavior type)", 0x00739090, "56578B7C240C8BF18B4E04",
                        (void*)&detail::hkCallByName, (void**)&s.g_callByName});
    return core::installFeature("sxnatives10", rows);
}

}  // namespace engine::slangvm

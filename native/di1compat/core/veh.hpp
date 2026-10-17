// Vectored exception handlers (first AV, trace DR0 watchpoint) that never fault or allocate (H9); the hooks' start.
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include "hooks.hpp"
#include "log.hpp"
#include "memcheck.hpp"
#include "stack.hpp"

namespace core {

namespace detail {
struct VehState {
    std::atomic<LONG> avLogged{0};         // avHandler's one-shot; any thread
    std::atomic<uintptr_t> watchAddr{0};   // the watched address: set once by watch()
    std::atomic<LONG> watchHits{0};        // watchpoint hits so far; any thread
};
inline VehState g_veh;
// DR7 fields of debug breakpoint 0 (Intel SDM vol. 3, 17.2.4)
constexpr unsigned long kDr7Slot0 = 0xF0003ul;      // L0, G0, R/W0 and LEN0
constexpr unsigned long kDr7L0 = 1ul;               // local enable
constexpr unsigned long kDr7Rw0Write = 1ul << 16;   // break on data writes
constexpr unsigned long kDr7Len0Dword = 3ul << 18;  // 4 bytes
constexpr unsigned long kDr6B0 = 1ul;               // DR6: breakpoint 0 fired
constexpr LONG kWatchHitsLogged = 10;               // watch hits that get a line
constexpr int kWatchStackWords = 400;               // stack words a watch hit scans
constexpr int kWatchStackFound = 14;                // exe addresses a watch hit lists
constexpr DWORD kArmWaitMs = 5000;                  // watch() waits this long for the arming thread
}  // namespace detail

// First access violation: its address and exe call stack, as static addresses. Logged once; the search continues.
inline LONG CALLBACK avHandler(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || detail::g_veh.avLogged.exchange(1))
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t eip = (uintptr_t)e->ExceptionRecord->ExceptionAddress;
    const ULONG_PTR* info = e->ExceptionRecord->ExceptionInformation;
    // never cut: at most 54 characters, plus 24 stack words of 9 (stackOf)
    char line[900];
    const unsigned at = (unsigned)(inExeImage(eip) ? toStaticVa(eip) : eip);
    int len = snprintf(line, sizeof(line), "ACCESS VIOLATION at %08X (%s %08X), stack:", at, info[0] ? "write" : "read",
                       (unsigned)info[1]);
    stackOf((const uint32_t*)e->ContextRecord->Esp, line, sizeof(line), len);
    logf("%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Trace only: DR0 write watchpoint on one address in every thread; logs the writer's EIP and stack (first 10 hits).
inline LONG CALLBACK watchHandler(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & detail::kDr6B0))
        return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Dr6 = 0;
    if (++detail::g_veh.watchHits <= detail::kWatchHitsLogged) {
        // never cut: at most 56 characters, plus 14 stack words of 9
        char line[700];
        uintptr_t eip = e->ContextRecord->Eip;
        const int value = *(int*)detail::g_veh.watchAddr.load();
        int len = snprintf(line, sizeof(line), "watch hit: value now %d, eip %08X, stack:", value,
                           (unsigned)(inExeImage(eip) ? toStaticVa(eip) : eip));
        const uint32_t* sp = (const uint32_t*)e->ContextRecord->Esp;
        int words = readableWords(sp, detail::kWatchStackWords);
        for (int i = 0, found = 0; i < words && found < detail::kWatchStackFound; ++i) {
            uint32_t v = sp[i];
            if (!inExeImage(v)) continue;
            len += snprintf(line + len, sizeof(line) - len, " %08X", (unsigned)toStaticVa(v));
            ++found;
        }
        logf("%s", line);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Sets DR0/DR7 in every other thread of the process (a thread cannot set its own context).
inline DWORD WINAPI armWatch(void*)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    int n = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        SuspendThread(t);
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(t, &c)) {
            c.Dr0 = detail::g_veh.watchAddr.load();
            c.Dr7 = (c.Dr7 & ~detail::kDr7Slot0) | detail::kDr7L0 | detail::kDr7Rw0Write | detail::kDr7Len0Dword;
            if (SetThreadContext(t, &c)) ++n;
        }
        ResumeThread(t);
        CloseHandle(t);
    }
    CloseHandle(snap);
    logf("watch armed on %08X in %d threads", (unsigned)detail::g_veh.watchAddr.load(), n);
    return 0;
}

// Arms the watchpoint on addr once (later calls return at once); waits up to 5 s for the arming thread.
inline void watch(uintptr_t addr)
{
    if (detail::g_veh.watchAddr.load()) return;
    detail::g_veh.watchAddr.store(addr);
    AddVectoredExceptionHandler(1, watchHandler);
    HANDLE h = CreateThread(nullptr, 0, armWatch, nullptr, 0, nullptr);   // a thread cannot set its own context
    if (!h) return;
    WaitForSingleObject(h, detail::kArmWaitMs);
    CloseHandle(h);
}

// avHandler first in the handler chain, then MinHook; false: no hook can be installed. Before any hook.
inline bool startHooks()
{
    AddVectoredExceptionHandler(1, avHandler);
    if (MH_Initialize() == MH_OK) return true;
    logf("MinHook init failed; no hooks");
    return false;
}

}  // namespace core

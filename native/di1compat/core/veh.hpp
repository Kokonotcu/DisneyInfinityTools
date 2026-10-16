// The plugin's vectored exception handlers: avHandler logs the first access violation with its stack; the DR0
// watchpoint (trace) logs who writes one address. Both run inside the handler chain, so they and all they call never
// fault and never allocate (CLAUDE.md H9). core/log.hpp, core/stack.hpp, core/memcheck.hpp.
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include "hooks.hpp"
#include "log.hpp"
#include "memcheck.hpp"
#include "stack.hpp"

namespace core {

namespace detail {
inline volatile LONG g_avLogged;         // avHandler's one-shot, set by InterlockedExchange
inline volatile uintptr_t g_watchAddr;   // the watched address: set once by watch()
inline volatile LONG g_watchHits;        // watchpoint hits so far (Interlocked)
// DR7 fields of debug breakpoint 0 (Intel SDM vol. 3, 17.2.4)
constexpr unsigned long kDr7Slot0 = 0xF0003ul;      // L0, G0, R/W0 and LEN0
constexpr unsigned long kDr7L0 = 1ul;               // local enable
constexpr unsigned long kDr7Rw0Write = 1ul << 16;   // break on data writes
constexpr unsigned long kDr7Len0Dword = 3ul << 18;  // 4 bytes
}  // namespace detail

// First access violation in the exe: the faulting address and a call stack (stack words that point into the exe right
// after a call instruction: E8 rel32 whose target is plausible, or an FF /2 indirect call), static addresses. Logged
// once, then the exception continues to the game's / CrabeLoader's handlers.
inline LONG CALLBACK avHandler(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || InterlockedExchange(&detail::g_avLogged, 1)) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t eip = (uintptr_t)e->ExceptionRecord->ExceptionAddress;
    char line[900];
    int len = sprintf_s(line, "ACCESS VIOLATION at %08X (%s %08X), stack:", (unsigned)(inExeImage(eip) ? toStaticVa(eip) : eip),
                        e->ExceptionRecord->ExceptionInformation[0] ? "write" : "read", (unsigned)e->ExceptionRecord->ExceptionInformation[1]);
    stackOf((const uint32_t*)e->ContextRecord->Esp, line, sizeof(line), len);
    logf("%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Trace only: DR0 write watchpoint on one address in every thread; logs the writer's EIP and stack (first 10 hits).
inline LONG CALLBACK watchHandler(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Dr6 = 0;
    if (InterlockedIncrement(&detail::g_watchHits) <= 10) {
        char line[700];
        uintptr_t eip = e->ContextRecord->Eip;
        int len = sprintf_s(line, "watch hit: value now %d, eip %08X, stack:", *(int*)detail::g_watchAddr,
                            (unsigned)(inExeImage(eip) ? toStaticVa(eip) : eip));
        const uint32_t* sp = (const uint32_t*)e->ContextRecord->Esp;
        int words = readableWords(sp, 400);
        for (int i = 0, found = 0; i < words && found < 14; ++i) {
            uint32_t v = sp[i];
            if (inExeImage(v)) { len += sprintf_s(line + len, sizeof(line) - len, " %08X", (unsigned)toStaticVa(v)); ++found; }
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
            c.Dr0 = detail::g_watchAddr;
            c.Dr7 = (c.Dr7 & ~detail::kDr7Slot0) | detail::kDr7L0 | detail::kDr7Rw0Write | detail::kDr7Len0Dword;
            if (SetThreadContext(t, &c)) ++n;
        }
        ResumeThread(t);
        CloseHandle(t);
    }
    CloseHandle(snap);
    logf("watch armed on %08X in %d threads", (unsigned)detail::g_watchAddr, n);
    return 0;
}

// Arms the watchpoint on addr once (later calls return at once); waits up to 5 s for the arming thread.
inline void watch(uintptr_t addr)
{
    if (detail::g_watchAddr) return;
    detail::g_watchAddr = addr;
    AddVectoredExceptionHandler(1, watchHandler);
    HANDLE h = CreateThread(nullptr, 0, armWatch, nullptr, 0, nullptr);   // a thread cannot set its own context
    if (h) { WaitForSingleObject(h, 5000); CloseHandle(h); }
}

}  // namespace core

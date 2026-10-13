// NPC track, runtime side (hooks). Included by di1compat.cpp after its helpers (logf, stackOf, hook, g_cfg).
#pragma once

namespace npcrt {

// Pressing E on an NPC faulted in the spinlock FUN_00412ef0 (lock cmpxchg [ecx] at 0x412efd, ecx = 0x30: a NULL
// object's lock at +0x30, main thread; CrabeLoader report 2026-10-13 04:46). 346 call sites, none with a direct
// 'lea ecx,[r+0x30]' (scan of the exe), and the report's stack memory did not hold the fault frame, so the caller is
// unknown. This logs it: the first NULL-page lock with the call-site-validated stack, then the lock runs as before.
typedef void*(__fastcall* SpinLockFn)(void* self, void* edx);
static SpinLockFn g_spinLock;
static volatile LONG g_nullLock;
static void* __fastcall hkSpinLock(void* self, void* edx)
{
    if ((uintptr_t)self < 0x10000 && !InterlockedExchange(&g_nullLock, 1)) {
        char line[900];
        int len = sprintf_s(line, "NULL-object lock %p (FUN_00412ef0), thread %lu, stack:", self, GetCurrentThreadId());
        stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, 32);
        logf("%s", line);
    }
    return g_spinLock(self, edx);
}

inline void install()
{
    if (g_cfg.nullLock10)
        hook("spinlock NULL-object log", 0x00412EF0, "51568BF1B901000000", (void*)&hkSpinLock, (void**)&g_spinLock);
}

}  // namespace npcrt

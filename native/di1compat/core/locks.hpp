// The plugin's lock order in one place (CLAUDE.md L2), as data, and the CRITICAL_SECTION guard (L5).
#pragma once
#include <windows.h>

namespace core {

// Holds a CRITICAL_SECTION for one scope (CLAUDE.md L5).
class CsLock {
public:
    explicit CsLock(CRITICAL_SECTION& cs) : cs_(cs) { EnterCriticalSection(&cs_); }
    ~CsLock() { LeaveCriticalSection(&cs_); }
    CsLock(const CsLock&) = delete;
    CsLock& operator=(const CsLock&) = delete;

private:
    CRITICAL_SECTION& cs_;
};

// One nesting the audit found (core-part1 C5, core-part2 B-08, audio.md): inner is taken while outer is held.
struct LockNesting {
    const char* outer;
    const char* inner;
    const char* where;
};

// Take locks outer to inner, never against a row. None is inverted anywhere.
inline constexpr LockNesting kLockOrder[] = {
    {"di1::gamedb30 mx", "di1::vfs mx", "prepare -> read10s"},
    {"di1::gamedb30 mx", "di1::session mx", "prepare -> keep"},
    {"di1::vfs mx", "di1::session mx", "session::indexArchive"},
    {"engine::shaders mx (shaders10)", "di1::vfs mx", "make -> generateShader10 -> gen30::generate -> read10s"},
    {"di1::render10 sceneMx", "di1::vfs mx", "sceneSettingsIO -> read10s"},
    {"di1::render10 sceneMx", "arc30's cipherLock", "sceneSettingsIO -> read30x"},
    {"engine::langkeys mx", "arc30's cipherLock", "keys -> read30x"},
    {"the engine's DialogueManager lock (+0x58)", "engine::audio::dialogue mx", "aliasScripts"},
    {"engine::audio::dialogue mx", "engine::audio::system mx", "open, unload, seqTick, hkPostExt"},
    {"engine::audio::system mx", "engine::audio::positions mx", "place, tick, stopObject, hkQueuedPost"},
    {"engine::audio::system mx", "engine::audio::finished mx", "playLine, act, tick and FMOD's callback in it"},
    {"di1::currency mx", "its LogOnce", "currency10"},
};

// Every other plugin lock and every LogOnce lock is a leaf. The log lock (core/log.hpp) is the last of all.

// Fibers (B-09): a std::mutex held across a yielding engine call deadlocks its own thread. Add none here.
inline constexpr const char* kHeldAcrossEngineCalls[] = {
    "di1::gamedb30 mx (prepare's engine opens)",
    "engine::shaders mx (program registration)",
    "engine::shaders recCs (input layouts)",
};

// Never hold a plugin lock across engine::assets::open, read30, readStream or an engine loader.

// Loader lock (B-N1): Pentane_Main and install() run in DllMain; nothing they call waits on another thread.

}  // namespace core

// The plugin's lock order in one place (CLAUDE.md L2), as data: no code uses it.
#pragma once

namespace core {

// One nesting the audit found (core-part1 C5, core-part2 B-08, audio.md): inner is taken while outer is held.
struct LockNesting {
    const char* outer;
    const char* inner;
    const char* where;
};

// Take locks outer to inner, never against a row. None is inverted anywhere.
inline constexpr LockNesting kLockOrder[] = {
    {"g_dbLock (npcrt)", "g_zipLock", "prepare -> read10s"},
    {"g_dbLock (npcrt)", "g_sessLock", "prepare"},
    {"g_zipLock", "g_sessLock", "indexZip10"},
    {"g_sh10Lock (shaders10)", "g_zipLock", "shader10 -> gen30::generate -> read10s"},
    {"sceneSettingsIO's mx", "g_zipLock", "read10s"},
    {"sceneSettingsIO's mx", "arc30's cipherLock", "read30x"},
    {"lang30's mx", "arc30's cipherLock", "read30x"},
    {"the engine's DialogueManager lock (+0x58)", "vo.mx", "audio.hpp"},
    {"vo.mx", "g.mx", "audio.hpp"},
    {"g.mx", "posMx", "audio.hpp"},
    {"g.mx", "doneMx", "audio.hpp"},
    {"g_currency10Lock", "its LogOnce", "currency10"},
};

// Every other plugin lock and every LogOnce lock is a leaf. The log lock (core/log.hpp) is the last of all.

// Fibers (B-09): a std::mutex held across a yielding engine call deadlocks its own thread. Add none here.
inline constexpr const char* kHeldAcrossEngineCalls[] = {
    "g_defsMx (fallbackDef -> read30)",
    "g_dbLock (prepare's engine opens)",
    "g_sh10Lock (program registration)",
    "g_recCs (input layouts)",
};

// Never hold a plugin lock across g_open, read30, readStream or an engine loader.

// Loader lock (B-N1): Pentane_Main and install() run in DllMain; nothing they call waits on another thread.

}  // namespace core

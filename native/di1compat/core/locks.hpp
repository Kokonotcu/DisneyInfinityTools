// The plugin's lock order, in one place (CLAUDE.md L2). Documentation only: no code.
#pragma once

// Take locks outer to inner, never against an arrow. Nestings found by the audit (core-part1 C5, core-part2 B-08,
// audio.md "Lock order"); none is inverted anywhere:
//   g_dbLock (npcrt) -> g_zipLock -> g_sessLock          prepare -> read10s; indexZip10; g_dbLock -> g_sessLock too
//   g_sh10Lock (shaders10) -> g_zipLock                  shader10 -> gen30::generate -> read10s
//   sceneSettingsIO's mx -> g_zipLock, arc30's cipherLock     read10s, read30x
//   lang30's mx -> arc30's cipherLock                          read30x
//   the engine's DialogueManager lock (+0x58) -> vo.mx -> g.mx -> posMx, doneMx     audio.hpp
//   g_currency10Lock -> its LogOnce
// Every other plugin lock, and every LogOnce lock, is a leaf: no other plugin lock is taken while it is held.
// The log lock (core/log.hpp) is last: logf may run under any lock, and nothing is taken while the log lock is held.
//
// Fibers (B-09): engine loads run on fibers that share the frame thread, so a std::mutex held across an engine call
// that yields can be re-locked by another fiber on that thread (a self-deadlock). Held across engine calls today:
// g_defsMx (fallbackDef -> read30), g_dbLock (prepare's engine opens), g_sh10Lock (program registration), g_recCs
// (input layouts). Do not add another; do not hold a plugin lock across g_open, read30, readStream or a loader.
// Loader lock (B-N1): Pentane_Main and install() run inside DllMain; nothing they call may wait on another thread.

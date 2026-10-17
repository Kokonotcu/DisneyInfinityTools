# Engineering audit: di1compat and the toolkit (2026-10-16)

This audit documents what makes the native plugin hard to understand, hard to change safely, or wasteful for the CPU,
plus which Python/PowerShell tools are stale and what changed in CrabeLoader. **No code was changed.** A later pass will
fix these issues, and that pass **must not change behaviour**. Every finding therefore says how to fix it without
changing behaviour, what could break, and whether the fix is a pure refactor or needs your decision.

Audited at commit `684539b` (master, clean). CrabeLoader was compared at v1.0.0 `2928d52` and 1.1.0-dev `a780daa`.

## Fix pass status (2026-10-17)

The fix pass is merged on the `refactor` branch. Each lane kept the golden baseline (`golden.py check`: 0
differences); a behaviour change went in only with the owner's approval, in its own commit. Line numbers in the area
files below refer to `684539b`: the code has since moved into the layout of `CLAUDE.md`.

**Phases.** Done: 0 (safety net), 1 (shared helpers), 2 (structure), 3 (god functions), 5 (approved decisions), 6
(Python, CI, CrabeLoader v1.1.0). Phase 4: the measured hoists and caches are in; lock-scope changes wait for a game run.

**Lanes** (`git log --first-parent 23514ce..refactor`):

| Lane | Work | Audit IDs |
|---|---|---|
| tools | Python oracles fixed, CI builds both targets, `/W4` | OT-28 (part), OT-31, OT-33..36, decision 9 |
| core1 | log, hooks, config table, lock order, memory checks into `core/` | X-01, X-02, X-09, X-12, A-13, A-28, A-49, SY-02 |
| net | `golden.py` | X-13, OT-08, OT-18, OT-22, OT-27 |
| core2 | the 1.0 transcode pipeline in `games/di1`, run by octtest too | A-02, A-22, A-23, OT-07, OT-09..13, CO-06 |
| codecs | codecs into `formats/` and `convert/` | CO-01, CO-03, CO-04, CO-13 (part), CO-32, CO-36, GR-11 (part), X-06, X-07 |
| render | render, shader and particle code | RE-03/04/06/07, RE-10/14/15, RE-25..32 |
| core3 | hook tables, `install()`, `engine/` and `trace/` modules, hkOpen stages | X-03, X-04, B-01, B-02, GP-01 |
| graphs | cho, mer, mernode, mission | GR-01/02/04/05/08/10/11/14/29 |
| core4 | `di1compat.cpp` is the Pentane entry only | A-01, A-04, B-01 |
| octtest | octtest as a mode table split by job | OT-01/05/06/15/21/23/25/26 |
| audio | FMOD host, VO and music into `engine/audio` and `games/di1` | AU-01/02/03, AU-17, AU-20 |
| scripts | sx, sys, npc, npcrt | SX-01/05, SY-01/02/05, NP-10, NR-03/09 |
| gameplay | gameplay, ui, gprt, bldrt, caprt, vfxrt | GP-02..05, GP-12, GP-23, RE-32/35/37 |
| decisions | the approved A items below | A-12, A-35, A-37, A-38 |
| final | root forwarding headers removed, `base/` helpers merged, whole-tree rule sweep, Release PDB | X-06, OT-23, OT-29 |

**Owner-approved behaviour changes applied.**
- Before the lanes: `trace` and `lockTrace10` default off (`hkInst` stays under `trace`); A-36; all-or-nothing hook
  features (X-11, B-05, GP-41); damaged 1.0 input fails cleanly (X-07 and the IDs in its area files); 11 Python tools
  removed ([python-tools.md](python-tools.md)).
- A-06: `renderdoc10` and `hide_ps` work without their diagnostic switch.
- A-12: a 1.0 archive whose open failed transiently is retried. A-35: the first 10 access violations are logged.
- A-37: text that does not fit its buffer is cut, not a fail-fast. A-38: asset defs are built with no lock held.
- AU-42: audio's x131 hash sign-extends like the engine. AU-30 was applied, then reverted on the owner's call: FMOD Ex
  4.44 handles carry a reuse counter, so a late stop never reaches a newer play.
- NR-01: gamedb30 builds a playset's supplement before it replaces the old one.
- OT-02/03/04/24: octtest usage text, exit codes and bad-input handling.

**Still open.**
- BEHAVIOUR-CHANGE findings not approved, such as CO-24 and CO-28: see the area files. rulecheck's whole-tree run
  reports 0.
- Timing changes that need a game run first: the lock scopes of B-07, RE-39, RE-N1, A-11, NR-02, AU-06/07/18, and
  the rest of CO-13.
- Build changes that change code bytes: `/permissive-` (OT-28), LTCG (OT-30), one CRT for octtest (OT-31/32).
- `trace/start.hpp` is shared by two trace modules (M1); fixing it needs `golden.py` to read hook rows from a function.

**In-game checklist.** What the lanes could not prove offline. Compare with a `di1compat.log` from before the
refactor:
1. `shaders10`: the `linked ...` and `shader linking deferred ...` lines; translated shaders draw.
2. Cutscene sound stays in sync (cinesync).
3. GameDB: the include hook, and the unload when a zone is left.
4. `avperc10`: its two lines and the per-zone list line.
5. Slang linker: the `sx link` line's counts.
6. `spawner10`, `skin10`: their lines; the quarterback spawns; skinned 1.0 models draw.
7. Meridian: `meridian native: n of n classes registered`.
8. UI: Tab and SelectMenu (the `ribbon:` lines), the Toy Store.
9. Catalog text keys show their text.
10. Cape and hoverboard work.
11. Capsules appear at a distance.
12. `building10`: buildings stand on their lots.
13. `universal10` particles (`vfx: 1.0 universal package ...`).
14. CORE4's three narrowed lock scopes: npcrt's `playset30 ... Playset record` line (logged after the session lock),
    the render transcode's keys (built outside the lock), the lipsync `where` text (set after the archive lock). A zone
    loads and plays with no stall; the lines read as before.
15. `vfxchan10`: 1.0 particles take their re-laid channels (golden reaches `chan` only through octtest).

## How this was produced

1. **First pass.** Eleven reviewers each read a slice of the code in full, line by line: the two halves of
   `di1compat.cpp`, `audio.hpp`, the format codecs, graphs/choreography, rendering/shaders, scripts/system/NPC,
   gameplay/UI runtime, `octtest.cpp` plus the build, CrabeLoader compatibility, and the Python/PowerShell tools.
2. **Verification.** Nine fresh verifiers re-opened every cited line of the C++ reports and marked each finding
   CONFIRMED, CORRECTED, UNVERIFIED or REJECTED. Of 411 findings, 177 (43%) needed a correction and 3 were rejected,
   and the verifiers added 12 new ones. The corrections were wrong line numbers, overstated
   severity, wrong thread or frequency claims, and "fixes" that would have changed behaviour.
3. **Spot checks by the coordinator.** The coordinating session checked the Python and CrabeLoader reports, the
   cross-cutting facts. It settled two questions by disassembling the shipped binaries: the engine's name hash in both
   games' executables, and Pentane's crash handler (see "Facts established"). The verifiers settled further engine
   questions the same way; their scripts are in the session scratchpad.
4. **Unverified items are labelled.** Anything not provable from the code or binaries is marked **UNVERIFIED**, with
   what would settle it, usually one thread-id log line or one in-game run.

## Files

| File | Covers | Findings (High / Med / Low) |
|---|---|---|
| [core-part1.md](core-part1.md) | `di1compat.cpp` 1–2210: logging, `hook()`, config, 1.0 VFS, transcode dispatch, crash handling, trace probes | 51 (1 / 20 / 30) |
| [core-part2.md](core-part2.md) | `di1compat.cpp` 2210–4361: profiler, trace detours, GameDB hooks, `hkOpen`, shaders10 runtime, `Pentane_Main` | 51 (1 / 13 / 37) |
| [audio.md](audio.md) | `audio.hpp`: FMOD host beside Wwise, music, dialogue | 44 (0 / 11 / 33) |
| [codecs.md](codecs.md) | `oct`, `octx`, `bent`, `btd`, `chd`, `dct`, `datamap`, `arc30`, `luachunk` | 36 (2 / 13 / 21) |
| [graphs.md](graphs.md) | `cho`, `ports10`, `mer`, `mernode`, `mission` | 40 (1 / 10 / 29) |
| [render.md](render.md) | `render`, `gen30`, `sm3`, `vfx`, `chan`, `vfxrt`, shaders10 runtime | 52 (1 / 23 / 28) |
| [scripts-sys-npc.md](scripts-sys-npc.md) | `sx`, `sys`, `npc`, `npcrt` | 61 (0 / 16 / 45) |
| [gameplay-ui.md](gameplay-ui.md) | `gameplay`, `gprt`, `ui`, `bldrt`, `caprt`, `hkOpen` | 48 (1 / 8 / 39) |
| [octtest-build.md](octtest-build.md) | `octtest.cpp`, CMake, CI: the refactor's safety net | 37 (4 / 18 / 15) |
| [crabeloader.md](crabeloader.md) | What changed in CrabeLoader and what it breaks here | (compatibility review) |
| [python-tools.md](python-tools.md) | Every `.py`/`.ps1`: keep, fix, or remove | (relevance review) |

Each area file has a summary table, the full findings, and the verified inventories: functions with line ranges,
hooks, helpers and globals. Finding IDs are stable, so cite them in commits (`fix(GR-04): ...`).

## Every finding is tagged with a fix class

| Class | Meaning for the fix pass |
|---|---|
| **SAFE-REFACTOR** | Pure restructuring. It cannot change behaviour if done mechanically. Still compile it and run the golden checks. |
| **NEEDS-VERIFICATION** | Meant to keep behaviour, but subtle: timing, threads, byte output or log order. Prove it with the golden baseline and/or an in-game run. |
| **BEHAVIOUR-CHANGE** | The real fix changes behaviour: a latent bug fix or different failure handling. **Do not do it silently.** It is listed under "Decisions for you". |
| **DOC-ONLY** | An invariant to write down next to the code. No code change. |

---

## Facts established during the audit (they correct what the code or comments say)

1. **Pentane does not end the game on a handled fault.**
   - The comment at `di1compat.cpp:1225-1227` says "Pentane's [handler] ends the game". It doesn't.
   - The bundled `third_party/pentane/Pentane.dll` handler (function at 0x101b978e) was disassembled, and upstream
     `source/crash.cpp` (48e3fbb) was read. Both log `=== Exception Caught! ===` with a SymInitialize/StackWalk64 trace,
     then return `EXCEPTION_CONTINUE_SEARCH`, so a `__try/__except` still catches the fault and the game survives.
   - What a handled fault really costs:
     - a slow symbolised Pentane log entry (the bundled Pentane is a Debug build: `.textbss`, `.msvcjmc`, RTC checks);
     - one of CrabeLoader's 3 crash-report slots;
     - a hit in `harness.ps1`'s "Exception Caught" count.
   - The handler also fires on every **C++ throw** (code 0xE06D7363 >= 0x80000000), so each exception-based error path
     in the plugin pays that symbolised stack walk as well (core-part2).
   - The 12:01 crash fixed by 684539b was di1compat's own `avHandler` faulting **inside** the vectored handler. That
     gives the real invariant: `avHandler`, `watchHandler` and everything they call must never fault (core-part1 C13,
     A-36).
   - `Pentane_Main` runs inside Pentane.dll's DllMain, under the loader lock, before the game's entry point (B-N1).
     Install code must never wait on another thread.
2. **The engine's ×131 name hash sign-extends each byte.**
   - Checked by disassembly: 1.0 `FUN_0040bac0`, 3.0 `FUN_0040bb50` and `FUN_0040bb80` all use `movsx`.
   - `vfx.hpp:36` and `particles.py` match the engine.
   - `audio.hpp:921` and `vocheck.py` hash unsigned bytes, so they differ for any byte ≥ 0x80.
   - The two must not be merged blindly (AU-42, RE-33).
3. **The code defaults are not the recommended play config.**
   - With no cfg file the plugin runs `trace = 1` (26 probe hooks, a hardware watchpoint, a log line per open) and
     `serve10 = 0` (no 1.0 data).
   - `di1compat.cfg.example` sets `serve10 = 1`, `shaders10 = 1`, `trace = 0`.
   - `lockTrace10 = 1` stays on in both configs, which leaves 17 log-only probe hooks, some per frame (A-05, A-07).
   - Every "hot path" claim in these files states which config it applies to.
4. **`octtest` cannot gate a refactor today.**
   - 24 of its 70 modes always exit 0. Bad arguments crash with no output.
   - Several modes run a different pass order from the plugin.
   - It never compiles `di1compat.cpp`, so none of the plugin-only logic is reachable offline (OT-07, OT-16, OT-19,
     A-N1).
5. **CrabeLoader 1.1.0 landed today (dev branch).**
   - `crabe/crabeloader-no-guards.patch` no longer applies.
   - A new `.tbody` asset override hides archive content.
   - The crash reporter still dumps every handled access violation (capped at 3 per process).
   - The only hook shared with di1compat is `0x0068BBB0`, and di1compat hooks it only under `trace`.
   - Details in [crabeloader.md](crabeloader.md).

---

## Cross-cutting findings

These patterns span many files. Fix each one once, as a design decision, not 30 times locally. The per-area IDs give
the evidence.

### X-01 One translation unit with hidden dependencies (High · NEEDS-VERIFICATION)
- **How it is built:**
  - `di1compat.cpp` (4,361 lines) `#include`s 27 headers that hold all the code as `static`/`inline` functions.
  - `npcrt.hpp` is included at line 621 and `vfxrt/gprt/bldrt/caprt.hpp` at 4092-4095. Those five rely on statics
    defined earlier in the .cpp: `logf, hook, rt, g_cfg, read10s, read30x, g_open, g_sessLock, safeCopy, ...`
    (core-part1 C7, A-04).
- **Headers that are not self-contained:**
  - `cho.hpp` and `mission.hpp` use `<cctype>` functions they never include. `mernode.hpp` uses `sscanf_s` the same way
    (GR §5).
  - `gen30.hpp` and `sm3.hpp` depend on `render.hpp`'s `<cstdio>` (RE-25).
  - `windows.h`'s `min`/`max` macros are live in the whole TU (A-49).
- **Effects:**
  - No module can be tested or reasoned about alone. octtest cannot include the plugin logic (A-N1, OT-16).
  - Declaration order is not dependency order (A-01).
- **Direction:** `CLAUDE.md` § Modules, and "Shared code and 1.0-specific code" below.
  1. Move the shared services into small headers that every module includes: log, hooks, engine addresses, config,
     the zip reader. The 1.0 install layout goes in `games/di1/`.
  2. Make every header compile standalone (`cl /Zs`).
  3. Move the pure functions out of `di1compat.cpp`: `transcode*`, `missionGraph30`, `read10z` candidate building,
     config parsing. octtest can then test them.
  4. Only after that, optionally split into several TUs.
- **Risks:** static-initialisation order, ODR when `static` becomes `inline`, `min`/`max` macro reliance, and
  generated-code drift. Check with the golden baseline.

### X-02 Configuration is written in three places (Med · NEEDS-VERIFICATION)
- **Where it lives:**
  - 105 fields in an anonymous struct (`di1compat.cpp:131-237`).
  - A 105-branch `if/else if` chain in `loadConfig` (:250-354), with no `else`, so unknown keys are silently ignored.
  - Five log lines that cover 31 keys (+7 at :4317).
  - Hand-copied flag wiring in octtest (OT-12) and in `mer::Table` (GR-03).
- **Facts any replacement must keep:**
  - Keys equal the lower-cased field name except `hide_ps`, `vfx_universal10` and `vfx_trace`.
  - Kinds: 99 bool, 3 int, 3 strings of two sizes.
- **Fix:** one table `{ "key", kind, member }` with the 105 exact key strings and the same defaults, parsed by one loop
  (A-03). The logged config lines stay byte-identical. An "unknown key" warning would be new log text, so it is a
  decision for you.

### X-03 `Pentane_Main` hand-installs 149 hooks (Med · NEEDS-VERIFICATION)
- **The shape today:**
  - 149 `hook()` calls on 147 addresses: start lookup and start query appear twice, under the mutually exclusive
    `trace` and `spawntrace10 && !trace`.
  - Each address and prologue string sits up to 2,000 lines away from its detour (A-31).
  - `Pentane_Main` itself is 262 lines with about 12 responsibilities (B-01).
  - 85 hooks are installed with no cfg file and 82 with the recommended config.
- **Order matters:** each hook is live the moment it is enabled (C3).
- **Fix:**
  - Per-subsystem tables `{name, staticVa, prologueHex, detour, &original, gate}`, kept next to their detours and fed
    to the **existing** `hook()`.
  - Each subsystem gets an `install(const Config&)`.
  - Keep every name string and the install order; both reach the log. CrabeLoader does the same (`vfs_hook.cpp:169-199`).

### X-04 Diagnostics ship in the DLL and are partly on by default (Med · DOC-ONLY → your decision)
- **What runs today:**
  - Code default `trace = 1` installs 26 hooks. One of them arms a hardware write watchpoint on every thread, and
    every open logs a flushed line (A-05).
  - `lockTrace10 = 1`, the default in every config, installs 17 log-only hooks; `hkNavUpdate` runs every frame (A-07).
- **Behaviour filed under diagnostic flags.** Some feature code sits behind diagnostic flags, so turning a probe off
  also removes behaviour (A-06). One example: the only guard against the dead-root-handle crash, `hkInst`, is installed
  only under `trace`, so the recommended config runs without it (B-04).
- **Fix:** changing any default is a behaviour change (see "Decisions for you"). The refactor itself should move each
  probe into a `trace/` module and keep it compiled.

### X-05 God functions and god structs (High/Med · SAFE-REFACTOR or NEEDS-VERIFICATION)

| Function / struct | Lines | Where | ID |
|---|---|---|---|
| `octtest main` | 900 | octtest.cpp:2528-3427 | OT-01 |
| `audio::Host` (struct) | 453 | audio.hpp:377-829 | AU-01 |
| `mer::transcode` | 318 | mer.hpp:160-477 | GR-01 |
| `Pentane_Main` | 262 | di1compat.cpp:4098-4359 | B-01 |
| `transcode` (plugin dispatcher) | 260 | di1compat.cpp:853-1112 | A-02 |
| `cho::const30` | 258 | cho.hpp:553-810 | GR-02 |
| `render::transcodeMaterials` | 256 | render.hpp:499-754 | RE-03 |
| `audio::Vo` (struct) | 250 | audio.hpp:1295-1544 | AU-03 |
| `octx::realm30w` | 249 | octx.hpp:914-1162 | CO-03 |
| `dlgTranscode` | 229 | audio.hpp:1065-1293 | AU-02 |
| `sys::perceptionInfoLua` | 226 | sys.hpp:364-589 | SY-05 |
| `render::transcodeWorld` | 216 | render.hpp:765-980 | RE-04 |
| `hkOpen` (every asset open) | 208 | di1compat.cpp:3549-3756 | B-02, GP-01 |
| `sm3::toHlsl` | 195 | sm3.hpp:144-338 | RE-06 |
| `gameplay::activityGraph30` | 140 | gameplay.hpp:504-643 | GP-04 |
| `octx::addCounts` | 134 | octx.hpp:116-249 | CO-04 |
| `chan::roles` | 132 | chan.hpp:107-238 | RE-07 |

Each split must keep the exact order of side effects. Three examples:
- `hkOpen` dispatches in a fixed stage order, and two stages fall through (GP-01).
- Name-table order is part of the byte output (CO-02, GR rule 1).
- Writes that persist across a `continue` must stay (GR rule 4).

The area files give the safe split points.

### X-06 Duplicated helpers, some with different semantics (Med · SAFE-REFACTOR only where semantics match)

| Helper | Copies | Same semantics? |
|---|---|---|
| Jenkins `lookup2` | `dct.hpp:19`, `mer.hpp:36` | Yes. Merge them (GR-11). |
| ×131 name hash | `vfx.hpp:36` (signed), `audio.hpp:921` (unsigned) | **No.** See Facts #2 (AU-42, RE-33). |
| Lower-case / path normalise | `audio::lower` = `npc::lower`; `sx::lower` also maps `\`; `norm` also strips `assets/`; inline loops in cho, mission, bent, gen30, di1compat | Partly. Merge only identical ones (A-28). |
| Little-endian readers | `cho::rd`, `gen30::u32/u16` (bounds-checked); `btd::u32`, `dct::u32`, `vfx::rd32`, `sx::rd` (unchecked); `oct::u` | No. Different bounds behaviour. |
| Record builders | `mer::blob` = `octx::blobRec`; `cho::keyed` = `octx::keyedNode`; `cho::floatRec` | Yes (GR-10, CO-10). |
| Lua scanners / chunk walkers | `mission::beaconMap30`, `gameplay::luaTokens`, `audio::LuaParse`, five Lua 5.1 chunk walkers | **No** (GR-13, NP-01, AU-34). |
| Readability checks | `sys::readable` vs the plugin's own `readable` (the slow one) | No (SY-02). |
| Guarded string copy | `safeCopy` (first page only), `sys` `cstr` (every byte), `sx::readStr` (no check) | No (XF-02). |
| Prologue byte match | `hook()` loop vs `mernode::bytesMatch` | Yes (GR §3). |
| Shader-cache key | written three times (plugin and octtest) | Must stay identical (OT-10). |
| `.oct` transcode pipeline | 4 copies, drifted (plugin vs octtest modes) | **No** (CO-06, OT-07). |

**Rule for the fix pass:** merge only after proving the semantics are identical (same bytes, same edge cases).
Otherwise keep separate, well-named variants.

### X-07 32-bit wrap in bounds checks (Med · BEHAVIOUR-CHANGE on malformed input only)
- **The pattern:** `if (off + n <= size)` with `off` from the file. On Win32, `size_t` is 32-bit, so a huge `off`
  wraps and passes the check.
- **Where it occurs:**
  - `cho::rd` (GR-06), LinksBuffer (GR-17), `btd::convertBlock` (CO-07: a multi-GB memcpy), `oct::File::parse` (CO-15).
  - audio dialogue and FEV parsing (AU-25), `npc::luaStrings` (NP-02), `luachunk` register writes (CO-26, NP-N1).
  - Render readers (RE-19, RE-21, RE-22, RE-23).
- **Fix:** one bounds-checked reader in subtraction form (`n <= size && off <= size - n`). It is identical on valid
  input. Choosing what malformed input does afterwards is a decision (below).

### X-08 C++ exceptions can unwind into engine code (Med · NEEDS-VERIFICATION)
- **The rule exists but is applied once.** The code's own rule (`di1compat.cpp:3714`: "never unwind into engine code")
  is applied only around `hkOpen`'s 1.0 transcode.
- **Detours that allocate and have no catch:**
  - `hkOpen`'s 3.0 edit stages (GP-N1)
  - mernode thunks and `registerAll` (GR-09)
  - every audio detour and `onEvent` (AU-33)
  - `shader10` via the shader lookup (RE-N2)
- **Fix:** catch at each detour boundary.
  - **Do not** "catch → call the original" in detours that already called it; that runs the engine function twice
    (AU-33 correction).
  - **Do not** put a C++ `try` or objects with destructors into `__try` functions (C2712/C2713).

### X-09 Locks held across I/O and engine calls; no lock inventory (Med · NEEDS-VERIFICATION)
- **Inventory:**
  - About 40 mutexes or critical sections.
  - 18 are function-local `static std::mutex`, mostly guarding "log once" sets.
- **Locks held across slow work:**
  - `g_zipLock` across archive open and inflate, including the already-indexed fast path (A-11).
  - `g_dbLock` across zip I/O, DataMap parsing and engine opens (NR-02).
  - `g_sh10Lock` across HLSL generation, D3DCompile, disk I/O and engine registration (RE-39).
  - `g_recCs`: taken per draw on the render thread, while loader threads hold it across flushed log writes and input
    layout creation (RE-N1).
  - `g.mx` across FMOD loads and the FMOD update (AU-06/07).
  - `g_defsMx` across an engine read (A-38).
- **Lock order:** no inversion was found. The observed order is recorded in core-part1 C5 and audio's header.
- **Fix:**
  - Write the lock order down.
  - Add one `LogOnce` helper instead of the static-mutex-plus-set pattern.
  - Narrow lock scopes only where the area file shows identical results. Everything else here is timing-sensitive,
    so prove it in game.

### X-10 Faulting is used as control flow in a process that reports every fault (Med)
- **Today:**
  - `__try` blocks still read unchecked memory: `sx::readStr` (SX-05), `hdrName`, `hkAssetDef` (XF-02), `fallbackDef`
    under a lock (A-38).
  - `watchHandler` walks 400 stack words unchecked **inside** a vectored handler (A-36). That is the fatal class from
    Facts #1.
- **Design rule:** pre-check with one fast readability check (SY-02) and keep `__try` only as a second line.
- **Comment fix:** correct `di1compat.cpp:1225-1227` (A-N2).

### X-11 A refused hook leaves a null original that other code calls (Med · BEHAVIOUR-CHANGE on the failure path)
- **Why it happens:** `hook()` refuses when the prologue differs ("our exe carries other patches") and leaves
  `original == nullptr`. Inside its own detour that is safe, because a refused detour never runs. Outside it, it is not.
- **Unsafe calls found by a scan of every original called outside its detour:**
  - `g_open` reached through other detours (B-05):
    - `hkAssetDef` → `fallbackDef` → `read30` (:506)
    - `hkExpPkg` (:3304)
    - npcrt `hkInclude` → `prepare` (via `read30` at npcrt.hpp:250, and directly at :267)
  - `g_renderShut` in `caprt` `keep()` (:98, GP-41).
  - `audio::install` returns true even if every hook was refused (AU-38).
  - (`tips30` :3536 is safe: it is only reached from inside `hkOpen`.)
- **Fix:** guard each call, or make dependent features refuse to install when the hook they need was refused. Both
  change only the failure path. That is your decision; see the CrabeLoader file for why this can happen.

### X-12 Engine addresses and offsets are scattered (Med · SAFE-REFACTOR)
- **Scale:** 195 `rt(0x...)` sites, plus raw object offsets (`+0x60`, `+0xd4`, ...) inline in detours.
- **Positional binding:** mernode binds 24 engine functions by position, with every address written in three places
  (GR-08).
- **Unchecked addresses:** some called addresses get no signature check at all: `mission::kAliases` (GR-29), the ten
  audio call targets (AU-38), and mernode's data addresses (GR-08).
- **Fix:**
  - One `eng` namespace per subsystem, with named `constexpr` VAs and offsets and small typed accessors.
  - Keep every value verbatim, and diff the generated code.

### X-13 The safety net is not there yet (High · prerequisite for everything)
- **Gaps:**
  - octtest gates nothing by exit code (OT-19).
  - octtest re-implements the plugin pipeline in a different order (OT-07, CO-06).
  - octtest never runs the shipped `g_slack = 4096` path (CO-01, OT-08).
  - There are no golden byte baselines for cho, mer, render or sm3 (GR rule 6, RE-01, OT-27).
  - The plugin's `.cpp` logic is untestable offline (A-N1, OT-16).
- **Fix order:** see "Fix-pass plan", phase 0.

### X-14 Build and CI (Med · NEEDS-VERIFICATION)
- **Build settings (OT-28, OT-29):**
  - CMake's default Release flags: `/O2 /Ob2 /DNDEBUG`. That means `/W1`, no `/permissive-`, and no PDB, so plugin
    crash dumps have no symbols.
  - At `/W4` the code gives 65 warnings at 50 sites.
  - The DLL uses the static CRT (`/MT`), octtest the dynamic one (`/MD`).
- **CI:**
  - It builds no native code (OT-34).
  - The Python CI is red: 4 pyflakes warnings, plus 4 tools that crash on import and 15 that fail `--help` once lint
    is fixed ([python-tools.md](python-tools.md)).

---

## Most important findings per area

In total there are **420 verified findings: 11 High, 132 Med, 277 Low**. By fix class: 150 SAFE-REFACTOR, 120
NEEDS-VERIFICATION, 70 BEHAVIOUR-CHANGE, 80 DOC-ONLY. This table lists the High findings plus the Med findings with the
largest effect. Everything else is in the area files.

| Area | ID | Sev | Class | Finding |
|---|---|---|---|---|
| core | A-01 | High | NEEDS-VERIFICATION | `di1compat.cpp` 1–2210 is a god file mixing ≥14 concerns; declaration order ≠ dependency order |
| core | B-02 / GP-01 | High | NEEDS-VERIFICATION | `hkOpen` (every asset open) is a 208-line god detour. Its stage order is behaviour, and stream ownership is implicit per branch |
| core | A-36 | Med | NEEDS-VERIFICATION | `watchHandler` walks 400 stack words unchecked **inside a vectored handler**, the class that crashed at 12:01 (armed under `trace`) |
| core | B-04 | Med | BEHAVIOUR-CHANGE | The real crash guard `hkInst` is installed only under `trace`, so the recommended config runs without it |
| core | B-05 | Med | BEHAVIOUR-CHANGE | `g_open` is called outside its detour with no null check (X-11) |
| core | A-11 | Med | NEEDS-VERIFICATION | One global `g_zipLock` covers archive open, lookup and inflate, even on the already-indexed path |
| core | B-07 | Med | NEEDS-VERIFICATION | `linkShader` scans every shader on each lookup while holding the lock the render thread takes per draw |
| core | B-N1 | Med | DOC-ONLY | `Pentane_Main` runs inside Pentane's DllMain under the loader lock: nothing it calls may wait on another thread |
| core | A-03 | Med | NEEDS-VERIFICATION | Config is written in three places (105-field struct, 105-branch chain, partial log) (X-02) |
| graphs | GR-04 | High | SAFE-REFACTOR | `mer::mtc` re-scans the node pool in its loop condition: **132 ms -> 0.8 ms** per `in_city.mer` when hoisted, with identical bytes (measured) |
| graphs | GR-N1 | Med | BEHAVIOUR-CHANGE | `anim30` patches `HeaderStrings` through an index that its own edits can shift. It is safe only because of a record order that holds in all 3,017 shipped SubNetworks |
| graphs | GR-01, GR-02 | Med | NEEDS-VERIFICATION | `mer::transcode` (318 lines) and `cho::const30` (258 lines) god functions |
| codecs | CO-01 | High | NEEDS-VERIFICATION | Hidden globals (`octx::g_slack`, `g_variantGroups`) change the output, and the shipped value 4096 is never byte-tested |
| codecs | CO-02 | High | DOC-ONLY | Name-table order is part of the byte output. Reordering record construction changes bytes |
| codecs | CO-06 | Med | NEEDS-VERIFICATION | The `.oct` transcode pipeline exists 4 times (plugin + octtest modes), and the copies have drifted |
| codecs | CO-07 | Med | BEHAVIOUR-CHANGE | `btd::convertBlock`: a 32-bit wrap in validation leads to a multi-GB `memcpy` on malformed input |
| codecs | CO-13 | Med | NEEDS-VERIFICATION | `arc30::readMember` re-reads the index per call, makes one BCrypt call per 16 bytes, and holds a lock across I/O |
| render | RE-01 | High | SAFE-REFACTOR | No working byte-parity check for the shader and material translation. The Python twins drifted, and header comments claim checks that cannot pass |
| render | RE-N1 | Med | NEEDS-VERIFICATION | The render thread enters `g_recCs` per draw while loader threads hold it across flushed log writes and D3D input-layout creation |
| render | RE-39 | Med | NEEDS-VERIFICATION | `g_sh10Lock` is held across HLSL generation, D3DCompile, disk I/O and engine registration |
| render | RE-05 | Med | BEHAVIOUR-CHANGE | A failing `transcodeWorld` leaves a half-converted `.oct` that is still served |
| render | RE-33 | Med | BEHAVIOUR-CHANGE | Two `h131`s disagree on signedness; only `vfx.hpp`'s matches the engine (Facts #2) |
| audio | AU-08 | Med | NEEDS-VERIFICATION | `hkBankReq` reads `music.playset` (a `std::string`) without the lock its writers hold |
| audio | AU-30 | Med | BEHAVIOUR-CHANGE | An FMOD instance pointer can be shared by two plays, so a late stop of the old id stops the new sound |
| audio | AU-06, AU-18 | Med | NEEDS-VERIFICATION | The host tick holds `g.mx` across the FMOD update, and per-frame lipsync and stop hooks block on it for ids that are not ours |
| audio | AU-01 | Med | SAFE-REFACTOR | `audio::Host` is a 453-line god struct behind one mutex |
| scripts | NR-01 | Med | BEHAVIOUR-CHANGE | `prepare()` deletes `g_db` before building its replacement. A `bad_alloc` in between leaves it dangling |
| scripts | SX-N1 | Med | DOC-ONLY | The engine relink rewrites every Slang import slot before `bind()` restores the 1.0 natives (race window, UNVERIFIED in practice) |
| scripts | NR-02 | Med | NEEDS-VERIFICATION | `g_dbLock` is held across zip I/O, DataMap parsing and engine calls |
| scripts | NP-N1 / CO-26 | Med | BEHAVIOUR-CHANGE | `luachunk::eval` LOADNIL writes past its 306-register vector on malformed bytecode |
| scripts | SY-02 | Med | SAFE-REFACTOR | Two `readable()` implementations; the unqualified one is the slow one |
| gameplay | GP-07 | Med | NEEDS-VERIFICATION | `movie10`/`script10` on per-frame input delivery: a syscall, a heap allocation and the shared `g_sessLock` per call |
| gameplay | GP-06 | Med | NEEDS-VERIFICATION | `hkToolGate` redoes the full 1.0 lookup on every call for a refused tool |
| gameplay | GP-41 | Med | BEHAVIOUR-CHANGE | caprt `keep()` calls `g_renderShut` with no null check (X-11) |
| octtest | OT-07, OT-16 | High | NEEDS-VERIFICATION | octtest re-creates the transcode pipeline in a different order, and the plugin-only logic is unreachable from it |
| octtest | OT-19, OT-27 | High | DOC-ONLY / SAFE-REFACTOR | Exit codes cannot detect most regressions, and there is no golden baseline |
| build | OT-28, OT-29, OT-34 | Med | NEEDS-VERIFICATION / SAFE-REFACTOR | `/W1`, no PDB in Release, and no native build in CI |

---

## Decisions for you (BEHAVIOUR-CHANGE items)

The refactor must not make these changes on its own. Grouped by theme. The area files list every one.

1. **Defaults.**
   - Turn `trace` off by default, or keep it? (A-05, B-03)
   - Promote the `hkInst` crash guard out of `trace` into a real feature flag? (B-04)
   - `lockTrace10` likewise? (A-07)
   - Move behaviour that is filed under diagnostic flags into real feature flags? (A-06)
2. **Malformed 1.0 input.** One policy for every bounds and overflow bug (X-07): fail the transcode and serve nothing,
   serve the raw file, or log and continue.
   - Today the outcome varies: an access violation, a multi-GB memcpy, a write past a vector, a hang (RE-48), or a
     half-converted file that is still served (RE-05).
3. **Refused hooks (X-11).** Guard each null call, or make dependent features refuse to install.
4. **Hash signedness.** Make `audio::h131` sign-extend like the engine. This only matters for non-ASCII names. (AU-42)
5. **Audio.** Fix the shared FMOD instance window, where a late stop can stop a new sound (AU-30).
6. **Caching and errors.**
   - `zipFor` caches open failures forever (A-12).
   - `avHandler` logs only the first access violation (A-35).
   - `sprintf_s` overflow fail-fasts the game (A-37, C10).
   - `fallbackDef` builds under a lock (A-38).
7. **octtest behaviour.** Usage text, a top-level catch, failing exit codes (OT-02/03/04). Any CI that relies on
   today's exit codes will see different results.
8. **Python tools: decided 2026-10-16.**
   - Rule: keep what reveals information about 1.0/3.0 files; delete it when the C++ does the same job.
   - 11 tools removed. `arenapatch.py` was removed on the owner's call.
   - The 3 broken FMOD/VO oracles still need fixing.
   - Details: [python-tools.md](python-tools.md).
9. **CrabeLoader.** Move the harness to 1.1.0 with the regenerated patch and the `.tbody` override marker, or pin
   v1.0.0 ([crabeloader.md](crabeloader.md)).

---

## Shared code and 1.0-specific code

Goal: when the 1.0 work is done, a 2.0 port should reuse the shared code as it is, not dig it out of 1.0 files. So the
refactor keeps general mechanisms in shared folders and only 1.0 facts in `games/di1/`. The rules are `CLAUDE.md`
M8 to M11. The test for any piece of code is: is it correct for any source game, or only because of how 1.0 data
looks?

Three things already show the split is real. The 2.0 install on this machine uses the same zip container as 1.0,
ships the same FMOD Ex runtime, and its `.oct`/`.mer`/`.bent` files start with the same tuple header. So the zip
reader, the FMOD host and the tuple codec are shared code, even though today they sit next to 1.0 rules. Nothing else
about 2.0 was examined.

First cut per file, taken from the audit. Confirm each function with M8 when you split it.

| Current file | Likely shared (target folder) | 1.0-specific (`games/di1/`) |
|---|---|---|
| `di1compat.cpp` | log, config table, `hook()`/`installFeature`, `rt`, readability checks, `avHandler`, serving files to the engine, the zip reader mechanics → `core/`. 3.0 asset reads (`read30`, `read30x`), shader compile and cache, GameDB/zone hook mechanics → `engine/` | 1.0 install layout and archive search order, the `transcode()` dispatch table and pass order, 1.0 runtime features, the `...10` config keys |
| `oct.hpp` | everything → `formats/` | none |
| `octx.hpp` | record builders, `editPoolChildren`, `SnEdit`, association walks, the counts 3.0 expects (`addCounts`) → `convert/` | steps that encode 1.0 forms (realm-file rule, the skin/locator/camera/variant rewrites); decide per step |
| `bent.hpp`, `btd.hpp`, `chd.hpp`, `dct.hpp` | the codecs → `formats/` | the 1.0-layout rewrites (`bent` 4.1 rules, `btd` 1.0 block conversion, `dct` reseed/supplement) |
| `luachunk.hpp`, `datamap.hpp`, `arc30.hpp` | Lua 5.1 chunk reading and rewriting, DataMap parsing, the 3.0 archive reader → `formats/` | none expected |
| `cho.hpp`, `ports10.hpp`, `mission.hpp` | clip-block packing and record helpers → `convert/` | the choreography passes as 1.0 rules, the port tables, mission enums and aliases |
| `mer.hpp`, `mernode.hpp` | graph view, link decode and encode, `lookup2`, the `.mtc` builder → `formats/`. Native Meridian class registration → `engine/` | the 1.0 pin and parameter rules (`events10`, `gate10`, `pins10`, ...), the 1.0 node classes registered |
| `render.hpp`, `gen30.hpp`, `sm3.hpp` | SM3 bytecode decoder and HLSL emitter, `.shd` writer, vertex-format conversion, bundle parser, render-table loader → `formats/` and `convert/` | 1.0 material and shader rules, the generated 1.0 render table |
| `vfx.hpp`, `chan.hpp`, `vfxrt.hpp` | particle format writer, both `h131` variants (named for their signedness), the package-load hook → `formats/` and `engine/` | the 1.0 particle-layout conversion, the `universal10` alias |
| `audio.hpp` | FMOD Ex host, Wwise routing, MusicStack and dialogue-system bridges → `engine/` | 1.0 sound-folder layout, the `.dlg` v11 transcode, 1.0 mixer presets |
| `sx.hpp` | Slang container handling, the script-VM native binding machinery → `convert/` and `engine/` | the 1.0 natives and their semantics, behaviour-type maps |
| `sys.hpp`, `npc.hpp`, `npcrt.hpp` | `readable` → `core/`. GameDB and DataMap builders, Lua readers, the include-hook machinery → `formats/` and `engine/` | 1.0 locks, bitmasks, perceptions, chests, economy, skin remap, navmesh layout, playset records |
| `gameplay.hpp`, `ui.hpp`, `gprt.hpp`, `bldrt.hpp`, `caprt.hpp` | the Lua lexer → `base/`. Zone-list registration and UI hook machinery → `engine/` | 1.0 playsets, currency, challenges, tools, loading screens, menus, SelectMenu, buildings, expendables |

How to split without changing behaviour:
- Move first and change nothing. Then extract the mechanism into the shared folder, and pass the 1.0 facts in as a
  table or options struct. Check each step against the golden baseline.
- Keep stable names as they are, even when they say "10": config keys, hook names, log text (M11).
- Do not make shared code ask which game is running (M10). Where the two games differ, the game folder passes the
  difference in.

## Fix-pass plan (order matters)

0. **Build the safety net before touching code (X-13).**
   - Build both targets as they are and keep the binaries.
   - Make octtest fail on mismatches, or wrap it in a runner that compares output. Add golden dumps for `--cho-*`,
     `--mer`, `--render`, `--gen30` and `--counts` with `g_slack = 4096`. Mask timings and sort unordered lines (OT-22).
   - The owner records one in-game `di1compat.log` as the log-text baseline. Agents never run the game or `harness.ps1`.
1. **Shared utilities with exact semantics (X-06, X-07, X-09).**
   - Bounds-checked readers, the hash variants, `LogOnce`, `endsWith`/lowercase variants.
   - Named engine constants (X-12).
   - SAFE-REFACTOR only.
2. **Structure (X-01, X-02, X-03).**
   - Self-contained headers, shared-service headers.
   - Config table, hook tables, a per-subsystem `install()`.
   - Move each file into the `CLAUDE.md` layout and split it along the shared/1.0 line as it moves
     (see "Shared code and 1.0-specific code" below).
3. **God functions (X-05).** Split them along the seams the area files give, one function per commit, each checked
   against the golden baseline.
4. **Performance (NEEDS-VERIFICATION).**
   - Hoists, `reserve`, move instead of copy, `editPoolChildren` for the cho passes (GR-05), narrower lock scopes.
   - Measure before and after with `loadprof10`/`fpslog10`.
5. **Decisions.** Apply the BEHAVIOUR-CHANGE items you approve, each in its own commit.
6. **Separate from the native work:**
   - Python cleanup and CI ([python-tools.md](python-tools.md)).
   - The CrabeLoader patch update ([crabeloader.md](crabeloader.md)).

The rules for writing the new code are in the repository's `CLAUDE.md`.

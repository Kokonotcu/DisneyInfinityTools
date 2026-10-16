# Plugin core, part 1 (di1compat.cpp 1-2210: logging, hooks, config, 1.0 VFS, transcode dispatch, crash handling, trace probes) (`native/di1compat/di1compat.cpp`)

Scope: I re-read `di1compat.cpp` 1–2210 in full and checked every cited line. I also read:
- `hkOpen` 3549–3756 and `Pentane_Main` 4097–4359.
- `shader10`/`mergeVtxf` 4027–4090, the profiler helpers 2218–2372, `hkMissionTick` 2666–2676, `lockTraceRoom` 2746, `hkNavUpdate` 2952–2980, and `hkWatson`/`hkNormPath` 3408–3440.
- `sys.hpp:160–196`, `arc30.hpp:85–125`, `render.hpp:675–695`, `gprt.hpp:120–124/166–170/278–282/418–436`, `npcrt.hpp:226–302`, and `audio.hpp:41/2074–2086`.
- `minhook/hook.c` (EnableHook/Freeze/MH_CreateHook), `CMakeLists.txt`, `di1compat.cfg.example`, `README.md:118–144`, and `octtest.cpp` includes.
- `git show 684539b`.

I also ran these mechanical checks:
- An awk scan of every function range in 60–2210. All ranges in the inventory match.
- A script over the config struct and parser: 105 fields, 105 `strcmp` keys, no duplicate keys, every field read outside `loadConfig`, and the key-vs-field-name mapping.
- A grep of every `g_zipLock`/`g_sessLock`/`g_dbLock`/`g_sh10Lock` site, every `__try`, every `catch`, and every VEH registration.

Verification: 49 findings checked: 27 confirmed, 21 corrected, 1 unverified, 0 rejected; 2 new.

Fault-cost fact (supplied by the coordinator, verified there by disassembly of the bundled `third_party/pentane/Pentane.dll` and upstream `crash.cpp`): Pentane's vectored handler does **not** end the game. It logs `=== Exception Caught! ===` with a symbolised stack walk and returns `EXCEPTION_CONTINUE_SEARCH`. `harness.ps1:133` counts these lines.
- A fault that a `__try` handles costs a slow Pentane log entry plus one of CrabeLoader's 3 report slots. It is **not** fatal.
- What is fatal is a fault *inside a vectored handler*, which re-enters the handler chain. That was the 12:01 crash fixed by 684539b.
- So the real invariant is: **`avHandler`, `watchHandler` and everything they call must never fault.**
- No finding below treats a handled fault as fatal.

## Findings
| ID | Title | Sev | Class | Where |
|---|---|---|---|---|
| A-01 | God file mixing ≥14 concerns; declaration order ≠ dependency order | High | NEEDS-VERIFICATION | :1–2210 |
| A-N1 | Nothing in di1compat.cpp is testable offline (octtest compiles only the format headers) | Med | NEEDS-VERIFICATION | octtest.cpp:10–29; di1compat.cpp |
| A-02 | `transcode()` 260-line dispatcher with order-dependent fall-through | Med | NEEDS-VERIFICATION | :853–1112 |
| A-03 | Config kept in three places (struct, 105-branch chain, partial log) | Med | NEEDS-VERIFICATION | :131–368 |
| A-04 | Mid-file includes and hidden dependencies on file statics / shader10 | Med | NEEDS-VERIFICATION | :621, :641, :2019, :4092 |
| A-05 | `trace` defaults to on and installs 26 hooks, some behaviour-changing | Med | DOC-ONLY | :134, :4201–4228 |
| A-06 | Feature/behaviour code gated by or filed under diagnostic flags | Med | DOC-ONLY | :1388–1406, :1202, :1622–1632, :146 |
| A-07 | `lockTrace10` defaults to on: 17 probe hooks in the recommended config | Med | DOC-ONLY | :157, :4270–4291 |
| A-09 | `logf` holds the global log lock across formatting and a WriteFile per line | Med | NEEDS-VERIFICATION | :70–84 |
| A-11 | One global `g_zipLock` covers archive open, lookup, inflate (and the indexed fast path) | Med | NEEDS-VERIFICATION | :405–451, :590–606, hkOpen :3692/:3699 |
| A-12 | `zipFor` caches open failures forever, silently; unlocked `g_zips.size()` | Med | BEHAVIOUR-CHANGE | :376–389, :3666 |
| A-15 | `read30x` re-opens and re-decrypts the 3.0 archive index per call | Med | NEEDS-VERIFICATION | :519–527; arc30.hpp:93–125 |
| A-16 | `g_served` holds whole files, copied under `g_sessLock`, never evicted | Med | NEEDS-VERIFICATION | :559, :653–659, :816–817, :1036–1044, :3648–3650 |
| A-19 | `.tbody` serving copies and compares every texture to detect a rare header patch | Med | NEEDS-VERIFICATION | :610–619; hkOpen :3704–3707 |
| A-22 | `transcodeOct` pass order is load-bearing and implicit | Med | SAFE-REFACTOR | :734–758 |
| A-28 | Path normalisation / lower-casing hand-written in many places | Med | SAFE-REFACTOR | :397–399, :480–483, :602, :826–827 |
| A-31 | Hook VA/prologue live 2000+ lines from their detours | Med | NEEDS-VERIFICATION | 47 detours; :4183–4358 |
| A-35 | `avHandler` logs only the first AV of any kind; comment says "in the exe" | Med | BEHAVIOUR-CHANGE | :1221–1278 |
| A-36 | Watchpoint probe: unbounded stack walk inside a VEH, never disarmed | Med | NEEDS-VERIFICATION | :1280–1333, :1350–1354 |
| A-37 | `hkLayApply` can overflow `sprintf_s` and fail-fast the game | Med | BEHAVIOUR-CHANGE | :1647–1658, :1573–1579 |
| A-38 | `fallbackDef` holds `g_defsMx` across an engine file read inside `__try` | Med | BEHAVIOUR-CHANGE | :2017–2063 |
| A-08 | Config parsing limits, silent truncation, plugin-dir length fail-fast | Low | DOC-ONLY | :241–249 |
| A-10 | `logf` overloads the C library's `logf(float)` | Low | SAFE-REFACTOR | :70, :2719, :4307, :4333 |
| A-13 | `read10z` candidate generation is dense and churns strings | Low | SAFE-REFACTOR | :408–439 |
| A-14 | `read10s` double-buffers and builds a discarded provenance string | Low | NEEDS-VERIFICATION | :486–495, :437/:448 |
| A-17 | `g_vf30`/`g_tails10` written by hkOpen, read by transcodes, unlocked | Low | DOC-ONLY | :566–567, :4082–4090 |
| A-18 | CHD path is a single process-wide slot; `takeChdPath` does not take | Low | SAFE-REFACTOR | :1155–1173 |
| A-20 | `surfaceTable30()` rebuilt for every served `.oct` | Low | NEEDS-VERIFICATION | :718–732, :750 |
| A-21 | `has30` uncached, up to two filesystem syscalls per call | Low | NEEDS-VERIFICATION | :672–677, :685 |
| A-23 | Render IO setup and render-failure handling duplicated | Low | SAFE-REFACTOR | :636–650, :806–813, :1027–1034 |
| A-24 | Inconsistent error reporting and string-typed codec result | Low | NEEDS-VERIFICATION | :838–845, :880, :922, :3710 |
| A-25 | `missionGraph30` matches exact whitespace | Low | DOC-ONLY | :769–785 |
| A-26 | `.mtb` chunk walk can wrap a 32-bit offset | Low | BEHAVIOUR-CHANGE | :1008–1013 |
| A-27 | Four hand-rolled memoisation idioms with different lock policies | Low | NEEDS-VERIFICATION | :531–549, :572–586, :822–849, :2017–2049 |
| A-29 | Genie::File vtable ABI spelled out five times | Low | NEEDS-VERIFICATION | :455–469, :501–514, :1126–1138, :3559–3571, :3601–3608 |
| A-30 | Two different comma-list parsers re-parsed on every open | Low | NEEDS-VERIFICATION | :1114–1122; :3636–3643 |
| A-32 | Duplicated "log first N"/"log on change" patterns and identical typedefs | Low | SAFE-REFACTOR | many (see finding) |
| A-33 | Second `readable()` duplicates `sys::readable` with the slow algorithm | Low | NEEDS-VERIFICATION | :1225–1246 |
| A-34 | `safeCopy` checks only the first byte | Low | BEHAVIOUR-CHANGE | :87–102 |
| A-39 | C++ exceptions can escape several hooks into engine frames | Low | NEEDS-VERIFICATION | :1163–1173, :1337–1364, :1461–1470, :1542–1604 |
| A-40 | `hook()` results ignored; null trampoline still called | Low | BEHAVIOUR-CHANGE | :110–128, :506 |
| A-41 | Trace hooks dereference engine pointers unchecked; two alter engine calls | Low | DOC-ONLY | see finding |
| A-42 | Unsynchronised diagnostic state shared across hook calls | Low | NEEDS-VERIFICATION | :1339, :1359, :1393, :1473, :1484 |
| A-43 | `hkClipEval` takes a global mutex per clip evaluation; maps grow forever | Low | SAFE-REFACTOR | :1533–1584 |
| A-44 | Teardown probe: two VirtualQuery per object, a lock per node dtor, TLS under fibers | Low | SAFE-REFACTOR | :1515–1522, :1766–1962 |
| A-45 | Profiler state shared as plain doubles; duplicated QPC code | Low | NEEDS-VERIFICATION | :2098–2208 |
| A-46 | Always-on and trace per-call costs (inventory) | Low | DOC-ONLY | :1183–1191, :1461–1470, :2051–2063 |
| A-47 | Magic numbers; two different exe-image sizes | Low | SAFE-REFACTOR | see finding |
| A-48 | Stale or contradicting comments and metadata | Low | DOC-ONLY | see finding |
| A-49 | Missing direct includes; macro-order dependence | Low | SAFE-REFACTOR | :8–46 |
| A-N2 | `readable()` comment wrongly says Pentane's handler ends the game | Low | DOC-ONLY | :1225–1227 |

---

### A-01 The file is a god file mixing at least 14 unrelated concerns
- **Where:** `native/di1compat/di1compat.cpp:1–2210` (the whole file is 1–4362)
- **Severity / category / class:** High · Architecture / god file · NEEDS-VERIFICATION
- **Evidence:** Each concern and its lines:
  - Plugin ABI :48–64; logging :66–84; `safeCopy` :86–102; hook installer :104–128.
  - Config :130–368; 1.0 zip VFS :370–451.
  - Genie::File wrappers :453–469, :497–514, :1124–1147; transcode helpers and dispatcher :471–1112.
  - Feature hooks between probes: the CHD loader :1149–1173, the playset slot :1175–1191, cinesync `hkChoStart` :1622–1632 (inside the breaktrace block), and the asset-defs fallback :2008–2063.
  - Behaviour guard `hkInst` :1388–1406 among trace hooks.
  - Crash VEH :1221–1278 and hardware watchpoint :1280–1333.
  - Probe blocks :1193–1505, :1507–1962, :1964–2078, :2079–2208. Together they span roughly 1000 lines with features interleaved, so the report's "about 700 lines of probes" is a rough figure.
  - Forward declarations :394, :568, :1799, :2091, plus a redundant re-declaration :2019 of `read30` (defined :501).
  - Mid-file includes :621 and :4092–4095.
- **Why it matters:** Reading order is not dependency order. Anyone editing has to know which statics a mid-file header uses. A misplaced edit can silently change which flag gates a hook (A-06).
- **Fix (behaviour-preserving):** First extract each block into a header included at exactly the point where the block stands today. That is mechanical. Only afterwards regroup by concern, and keep the relative declaration order of namespace-scope objects.
- **Refactor risk:**
  - Mid-file include dependencies (constraint C7).
  - Regrouping changes the construction and destruction order of globals. Destruction runs at DLL unload, and `profThread` or the RenderDoc thread may still be alive.
  - The `min`/`max` macro environment (C11).
  - Log text.
  - octtest covers none of this code (A-N1).
- **Verification:** CONFIRMED.

### A-N1 Nothing in di1compat.cpp is testable offline (new)
- **Where:** `native/di1compat/octtest.cpp:10–29` (includes only the format headers); `native/di1compat/di1compat.cpp` (all functions in this slice)
- **Severity / category / class:** Med · Testability / refactor safety · NEEDS-VERIFICATION
- **Evidence:**
  - `octtest.cpp` includes octx/bent/btd/chd/mer/render/arc30/datamap/dct/luachunk/sx/sys/npc/cho/ui/audio/vfx/chan/gameplay. It never compiles `di1compat.cpp`.
  - So none of the following has an offline check:
    - `transcode()` dispatch order (A-02), `missionGraph30` (A-25), `padSmallDxt` (A-19), and the `.mtb` chunk walk (A-26).
    - `read10z` candidate generation (A-13), `loadConfig` parsing (A-03), and `transcodeOct` pass order (A-22).
  - octtest's `--readable` mode tests `sys::readable` only. The local `readable()` (:1228) is not tested.
- **Why it matters:** The fix pass must prove behaviour is unchanged. For every pure-logic refactor in this slice, today the only proof is an in-game run.
- **Fix (behaviour-preserving):** Move the pure, engine-free functions verbatim into a header that both TUs include. Candidates: `missionGraph30`, `padSmallDxt`, a `candidateArchives()` extracted from `read10z`, the config line parser, the `.mtb` MATP walk, and `endsWith`/`norm`. Then add octtest modes that pin today's outputs before any refactor.
- **Refactor risk:**
  - The moved code must not pick up a different `min`/`max` macro state. octtest includes no `<windows.h>` before its headers, so check `(std::min)` uses.
  - `static` linkage must become `inline`.
- **Verification:** NEW (verified: grep of `octtest.cpp` includes).

### A-02 `transcode()` is a 260-line god function with order-dependent fall-through across about 22 format branches
- **Where:** `native/di1compat/di1compat.cpp:853–1112`
- **Severity / category / class:** Med · God function, readability · NEEDS-VERIFICATION
- **Evidence:**
  - One function handles challenge types, activity tasks, `.ap` givers, `.dnax` gadgets, `__missiongraph.lua`, mission `.lua`, `.oct`, `.mer`/`.mtc`, `.dct` (+text10), `.btd`/`.btm`, `.bent`, `.banm`, `.mtb` (texture-only and material), `_ps.dnax`, `.particledata`, `.p3d`, expendables `.lua`, `datamap.lua`, `.sx`, and `.bmx`/`.btx`.
  - The order is semantic:
    - A bytecode `.dnax` whose gadgets convert returns `"dnagadgets"` at :887, so `psdna30` (:1048) never runs for it.
    - `actLua` carries the activityTasks result into the events/progress branch (:886, :901–914).
    - A texture-only `.mtb` returns before the render path (:1014–1024).
    - The oct catch *assigns* `note` (:922), where every other branch appends.
  - `norm(path)` has 14 call sites (:856, :902, :918, :932, :953, :976, :990, :997, :1016, :1026, :1049, :1059, :1074, :1096). Most sit in mutually exclusive branches, so one call evaluates 2–4 of them. Every file that reaches :1074 with `rewards10` on pays a `norm` there, because it is evaluated before `endsWith(".lua")`.
  - `endsWith` has about 27 call sites. About 20 are evaluated for a file that falls through every branch.
- **Hot path?:**
  - Code defaults: never runs (`serve10=0`).
  - Recommended config: once per served 1.0 file except `.tbody`. The repeated `norm`/`endsWith` cost is negligible next to inflate and transcode.
- **Why it matters:** The semantics hide in branch order and early returns. A reordering silently changes which codec wins.
- **Fix (behaviour-preserving):**
  - Compute `np = norm(path)` once.
  - Split each branch into `static const char* tcX(...)`.
  - Keep a top-level sequence in exactly today's order, with today's three outcomes: return a codec, return nullptr (stop), or fall through.
  - Keep `note =` in the oct catch.
- **Refactor risk:**
  - Fall-through and early-`nullptr` semantics.
  - The codec string is compared with `strcmp` in hkOpen (:3710 `"mer"`).
  - octtest does not exercise this function (A-N1).
- **Verification:** CORRECTED:
  - Severity High → Med. The rubric puts a large god function at Med.
  - "Recomputes norm up to 14 times" → 14 call sites, but at most about 4 evaluated per call.

### A-03 Config is kept in three places: struct field, if/else chain, and partial log lines
- **Where:** `native/di1compat/di1compat.cpp:131–237` (105 fields), `:250–354` (105-branch chain), `:357–367` (4 log lines); `:4317` (ui line)
- **Severity / category / class:** Med · Duplication, maintainability · NEEDS-VERIFICATION
- **Evidence (verified mechanically):**
  - 105 fields, 105 unique keys, no duplicates. Every field is read outside `loadConfig` (grep over di1compat.cpp and the runtime headers), so there are no dead keys.
  - Kinds:
    - 99 bools: `atoi(val) != 0`, so `true`/`yes` silently mean false.
    - 3 ints: `bufview10`, `renderdoc10`, `fadeprobe10` (`atoi`).
    - 3 strings via the template `strcpy_s`: `install10` `char[MAX_PATH]`, `memtest`/`block10` `char[600]`.
  - Every key equals the lower-cased field name except `hide_ps`→`hidePs`, `vfx_universal10`→`vfxUniversal10`, and `vfx_trace`→`vfxTrace`. Keys therefore cannot be generated from field names.
  - Logged: 31 keys at :357–367, plus 7 at :4317.
  - Unknown keys are silently ignored: the chain has no `else`.
- **Why it matters:**
  - Adding a key means three edits.
  - Typos go unnoticed.
  - The effective config cannot be rebuilt from the log. `shaders10`, `audio10`, `vfx10` and about 60 others are never logged.
- **Fix (behaviour-preserving):**
  - Write a table `{ "key", Kind, member pointer or offset+size }` with the 105 exact key strings. Do not derive keys from field names.
  - Use separate kinds for bool (`atoi != 0`), int (`atoi`), and strings of size 260 and of size 600.
  - Keep today's semantics:
    - Last occurrence wins.
    - Only column-0 `#` lines are comments.
    - The `sscanf_s(" %63[^= ] = %259[^\r\n]")` line format.
    - Exact-case `strcmp`.
  - Keep byte-identical: the "no %s: defaults (serve10 off)" line, the four config lines (emitted after `fclose`, only when the file opened), and the :4317 ui line.
  - An "unknown key" log line would be a log change.
- **Refactor risk:**
  - One wrong key or kind silently changes a default.
  - Verify with a test cfg that sets every key to a non-default value, then compare the resulting `g_cfg` bytes and the four log lines old vs new (possible only after A-N1).
  - `g_cfg` is an anonymous struct, so use `decltype(g_cfg)`. It is constant-initialised (NSDMIs); keep that.
- **Verification:** CONFIRMED (kinds, non-derivable keys and log-line rules added).

### A-04 Mid-file includes and implicit dependencies on file statics
- **Where:** `native/di1compat/di1compat.cpp:621` (`#include "npcrt.hpp"`), `:635`, `:641`, `:2019`, forward declarations `:394/:568/:1799/:2091`, `:4092–4095`
- **Severity / category / class:** Med · Hidden coupling · NEEDS-VERIFICATION
- **Evidence:**
  - `transcodeAssetRender` calls `npcrt::skin10` (:635).
  - `io.translatable` (:641) calls `shader10(..., true)` (defined :4027). That function holds `g_sh10Lock` while it:
    - runs `gen30::generate`, which reads through `read10s` and so takes `g_zipLock`;
    - runs D3D compiles (`compile10`);
    - registers programs with the engine (`registerProgram10`).
  - So a loader-thread world transcode performs engine registration calls under a plugin lock that the render thread's `hkShaderByName` also takes (:4074).
  - `read30` is re-declared at :2019 although it is defined at :501.
- **Why it matters:** The required declaration order and the cross-subsystem lock/engine-call coupling are invisible at the call sites.
- **Fix (behaviour-preserving):** Delete the redundant :2019 declaration. Comment the npcrt and shader10 dependencies at :635/:641. Moving the shared statics into a "core" header comes later (A-01).
- **Refactor risk:** Constraint C7; static-init order if definitions move.
- **Verification:** CONFIRMED (the shader10 engine-call-under-lock detail added).

### A-05 `trace` defaults to on in code and installs much more than "log every open"
- **Where:** `native/di1compat/di1compat.cpp:134`; installs `:4201–4228`; `README.md:128`; `di1compat.cfg.example` (`trace = 0`)
- **Severity / category / class:** Med · Config, misleading comment · DOC-ONLY
- **Evidence:**
  - The README says "`trace` | `1` | Log every file open (verbose). Set to `0` for play".
  - With no cfg, or a cfg without `trace`, 26 hooks are installed. 22 are defined in this slice; `hkHdrClone`, `hkMkStart`, `hkMkLocator` and `hkMkSpawnExp` come later.
  - Some of them change engine behaviour:
    - `hkInst` skips instancing on a dead root (:1398–1402).
    - `hkFigureOk` passes `&mine` for a null `code` (:1214).
    - `hkActorExists` applies `hide_ps` (:1202).
    - `hkIgpName` arms a hardware watchpoint in every thread (:1352, A-36).
  - Log volume under trace=1:
    - one line for every AssetOpen (:3754);
    - uncapped per-call lines from `hkAssetInst` (every SubNetwork instancing), `hkActorExists`, `hkFigureOk`, `hkFigurePlayset`, `hkAvFinal`, `hkAvRemove` (with a stack walk), `hkAvCreate` (2 lines), `hkAvSpawned`, `hkPlaceToy` and `hkAacExec` (avatar only, 2 lines).
    - Each line is a WriteFile under `g_logLock` (A-09).
- **Hot path?:**
  - Code defaults: yes, one log line per open and per trace-hook call.
  - Recommended config: none of this runs.
- **Why it matters:** Default behaviour, and its cost, depend on whether the user copied the example cfg.
- **Fix (behaviour-preserving):** Correct the :134 comment and document what `trace` installs. Changing the default needs the owner's decision.
- **Refactor risk:** None for a comment.
- **Verification:** CONFIRMED.

### A-06 Feature or behaviour code gated by, or filed under, diagnostic flags
- **Where:** `native/di1compat/di1compat.cpp:1388–1406`, `:1202`, `:1622–1632`, `:146`, `:2083`; `:2666–2676`, `:4236–4237`, `:4245–4246`, `:4296`
- **Severity / category / class:** Med · Hidden coupling, config · DOC-ONLY
- **Evidence:**
  - `hkInst` is a crash guard installed only under `trace`. With the recommended `trace=0`, the guard against the 2026-10-12 `FUN_004f5900` NULL+8 fault is off.
  - `hide_ps=1` has no effect without `trace`.
  - `hkChoStart` is a cinesync feature. It uses a `Bt`-prefixed typedef and sits in the breaktrace block.
  - `renderdoc10` capture starts only in `hkMissionTick` (:2673–2676). That hook is installed only under `missiontrace` (:4236–4237), and the capture fires only for a 1.0 playset mission (`playsetMission10`, :2671). The :146 comment and the README (:139) say neither. Only the :4127 log line mentions "(missiontrace)".
  - `hkFrameSync` is "test (fpslog10)" (:2083) but is also installed for `loadprof10` alone (:4296).
- **Why it matters:** Either the `FUN_004f5900` crash can return in play, or the guard is dead code. Two documented switches silently do nothing.
- **Fix (behaviour-preserving):**
  - Document the `renderdoc10 → missiontrace` and `hide_ps → trace` dependencies at :146/:152 and in the README.
  - Optionally move `hkChoStart` next to the audio code and rename its typedef. This is a safe move.
  - Do not change gating. Making `hkInst` unconditional is a behaviour change.
- **Refactor risk:** None for doc, move or rename.
- **Verification:** CONFIRMED. UNVERIFIED sub-point: whether the `FUN_004f5900` crash is still reachable with trace=0. Settle it by loading the 2026-10-12 scene (instancing a 1.0 prop's 'Anchor' in IN_City) with trace=0, or by grepping trace=1 logs for "dead root handle".

### A-07 `lockTrace10` defaults to true, so 17 probe hooks run in the recommended config
- **Where:** `native/di1compat/di1compat.cpp:157`; installs `:4270–4291`; `:2746`; `hkNavUpdate :2950–2980`; `README.md:130–141`
- **Severity / category / class:** Med · Config / undocumented default probes · DOC-ONLY
- **Evidence:**
  - 17 hooks (counted :4271–4290). The example cfg does not set `locktrace10`, and the README's "Diagnostics, all off by default and log-only" table does not list it.
  - Logging is budgeted by `lockTraceRoom` (≤4000 per counter; some hooks add caps of 200–400). Past the budget, a call costs one `InterlockedIncrement`.
  - `hkNavUpdate` ("compass draw", the avatar's MissionNavData update, likely per frame) does 2 `sys::readable` calls (QueryWorkingSetEx, about 0.4 µs each per sys.hpp:176–179) and an engine screen lookup `FUN_0154fed0` on every call, before its change check.
- **Hot path?:**
  - Code defaults: on.
  - Recommended config: on. Per frame it costs a few µs (`hkNavUpdate`), plus one interlocked op per message send/receive. Measurable cost is unlikely.
- **Why it matters:** 17 extra engine detours (prologue-dependent, run in play) contradict the README's "diagnostics off by default".
- **Fix (behaviour-preserving):** Document it in the README and at :157. Flipping the default is a behaviour change for the owner.
- **Refactor risk:** n/a
- **Verification:** CORRECTED: the performance part is negligible (µs per frame). The finding is about config and docs, not CPU.

### A-08 Config parsing limits and silent truncation
- **Where:** `native/di1compat/di1compat.cpp:241–249`
- **Severity / category / class:** Low · Correctness smell · DOC-ONLY
- **Evidence:**
  - `val[MAX_PATH]` with `%259[^\r\n]` silently cuts `memtest`/`block10` (`char[600]`) at 259 characters.
  - Lines over 599 characters are split by `fgets`, and the remainder is parsed as a new line.
  - Trailing spaces, tabs and inline `# comments` stay in the value:
    - `install10` with a trailing space gives a directory component "X " that does not exist, so it fails silently at `GetFileAttributesA` (:383).
    - Booleans are unaffected (`atoi`).
  - `sprintf_s(path, "%s\\di1compat.cfg", dir)` needs `strlen(dir) + 14 + 1 ≤ 260`. It fail-fasts (C10) when the plugin directory path is **246** characters or longer.
  - A missing cfg logs only "no …: defaults (serve10 off)". The effective `trace=1` is not logged.
- **Fix (behaviour-preserving):** Document it. Reading `val` into a 600-byte buffer changes behaviour only for values longer than 259 characters.
- **Refactor risk:** Low.
- **Verification:** CORRECTED: the fail-fast threshold is ≥246, not 245. Inline comments added.

### A-09 `logf` formats and flushes while holding the global log lock
- **Where:** `native/di1compat/di1compat.cpp:70–84`
- **Severity / category / class:** Med · Performance, lock held across I/O · NEEDS-VERIFICATION
- **Evidence:**
  - Inside `g_logLock` (a CRITICAL_SECTION) it calls `GetLocalTime`, then `fprintf`, `vfprintf`, `fputc` and `fflush`. Each of those four CRT calls takes the FILE's own lock, and `fflush` issues a WriteFile, so every line is one syscall under the global lock.
  - The file is opened with `_fsopen(path, "w", _SH_DENYNO)` (:4111). That is text mode, so `\n` becomes CRLF.
- **Lines per AssetOpen (hkOpen):**

  | hkOpen outcome | trace=0 (recommended) | trace=1 (code default) |
  |---|---|---|
  | 3.0 hit, ordinary path | 0 | 1 (`open … -> ok`, :3754) |
  | miss, not served (serve10 off, blocked, or not in 1.0) | 1 (`open … MISSING (why)`, :3754) | 1 |
  | served from session `g_served` | 1 (:3660) | 1 |
  | served from a 1.0 archive | 1 (:3743), plus transcode-internal lines (render problems :649/:1032, scene settings once :583, one per new 1.0 shader in `shader10`) | same |
  | `gamedb/*/bitmasks.lua` | 0 | +1 with a stack walk (:3620–3625) |
  | special rewrites (vtxf, tips, playset list, zone list/challengedata, memtest, CHD) | 1–2 | 1–2 |

  - Always on in both configs: `hkLoadChdCallback`, 1 line per CHD load. The lockTrace10 hooks log within their budgets.
  - trace=1 only: the uncapped trace-hook lines listed in A-05.
  - The code defaults have `serve10=0`, so no "served" lines appear there.
- **Hot path?:**
  - Code defaults: one WriteFile per open, plus per trace-hook call.
  - Recommended config: one per served or missing file, which is small next to inflate and transcode. 3.0 hits log nothing.
  - hkOpen runs on several threads (:1155 comment; `noteOpenThread` :2226–2235 tracks up to 8). Which threads is UNVERIFIED.
- **Why it matters:** All threads serialise on the disk write. The cost is noticeable under trace=1.
- **Fix (behaviour-preserving):**
  - `vsnprintf` the message into a stack buffer of at least 1 KB before the lock, with a heap fallback only for longer lines.
  - Under the lock, call `GetLocalTime` (keep it inside, so timestamps stay monotonic in file order), then one `fprintf("[..] %s\n")` or `fwrite`, then `fflush`.
  - Keep text mode, `fflush` per line (crash evidence) and the timestamp format.
- **Refactor risk:**
  - `logf` is called from the VEHs (:1276, :1298). The avHandler line can be about 270 bytes and its buffer is 900, so a stack buffer of at least 1 KB means the VEH path never allocates. Today vfprintf does not allocate after the first line either.
  - Nested logging on one thread works today because CRITICAL_SECTION is recursive. With pre-formatting, a nested VEH line appears whole instead of inside the outer partial line. That differs only in a crash.
  - `logf` must keep the type `void(const char*, ...)`: it is cast or assigned at :2719, :4307 and :4333.
- **Verification:** CORRECTED: "four FILE-locked calls plus one fflush" → four FILE-locked CRT calls, the last of which (`fflush`) does a WriteFile. Added the per-open table and the refactor constraints (`GetLocalTime` inside the lock, CRLF text mode, buffer size).

### A-10 The name `logf` overloads the C library's `float logf(float)`
- **Where:** `native/di1compat/di1compat.cpp:70`; address-of uses `:2719` (`static_cast<void (*)(const char*, ...)>(logf)`), `:4307`, `:4333`
- **Severity / category / class:** Low · Readability hazard · SAFE-REFACTOR
- **Evidence:** `<cmath>` reaches this TU through the format headers. The explicit `static_cast` at :2719 is needed only because of the overload. The other two uses resolve through the target type (`sx::g_log`, `audio::logfn`, both `void(*)(const char*, ...)`).
- **Fix (behaviour-preserving):** Rename it, e.g. `dlog`, in di1compat.cpp and the five runtime headers that call it (npcrt, vfxrt, gprt, bldrt, caprt). Log text is unaffected.
- **Refactor risk:** A missed call site in a header would bind to the CRT `logf(float)` and fail to compile. That is caught at build time.
- **Verification:** CONFIRMED (evidence :2719 added).

### A-11 One global `g_zipLock` covers path probing, archive opening, decompression, and the "already indexed" fast path
- **Where:** `native/di1compat/di1compat.cpp:405–451` (lock :407; open :383–385 via `zipFor`; inflate :436/:447); `:590–606` (`indexZip10`: zip lock, then sess lock); hkOpen `:3692–3695`, `:3699`; `gprt.hpp:426–434`
- **Severity / category / class:** Med · Performance, lock held across I/O · NEEDS-VERIFICATION
- **Evidence:**
  - Every 1.0 read in the process waits on one mutex while miniz opens a new archive (`GetFileAttributesA`, `mz_zip_reader_init_file` reads and sorts the central directory) and inflates the member. World `.oct`/`.vbuf` files can be MBs.
  - **Additionally:** hkOpen calls `indexZip10` for *every* served 1.0 file (:3699). `indexZip10` takes `g_zipLock`, then `zipFor`, then `g_sessLock`, *before* checking `g_indexed10`. So even an already-indexed archive makes the open wait behind another thread's inflate, and blocks session lookups while it holds `g_sessLock`.
  - `gprt.hpp:426` holds `g_zipLock` across an extract, a parse (`gameplay::toolLevels10`) and a `logf` (once).
  - `read10s` is also reached under `g_sh10Lock` (`shader10` → `gen30::generate`) and under the `sceneSettingsIO` mutex.
  - Lock nesting found: zip→sess (`indexZip10`), sh10→zip, sceneSettings.mx→zip, and dbLock→zip / dbLock→sess (npcrt `hkInclude` → `prepare`). No inversion.
- **Hot path?:**
  - Code defaults: off (`serve10=0`).
  - Recommended config: per 3.0-missing open (`read10`), per served file (`indexZip10`), and per transcode sub-read.
- **Why it matters:** Parallel loader threads serialise on 1.0 I/O and inflate.
- **Fix (behaviour-preserving):**
  1. In `indexZip10`, first test `g_indexed10.count(zipRel)` under `g_sessLock` alone and return if present. Then run today's zip → sess sequence. The result is identical (the set only grows) and no nesting is reversed.
  2. Later: keep a short global lock for the `g_zips` map only, plus a per-archive mutex around locate and extract. miniz reader state is per archive. Returned bytes and candidate order stay unchanged.
- **Refactor risk:**
  - Every direct `zipFor` + `mz_*` user (gprt.hpp:426, hkOpen :3692) must take the per-archive lock.
  - Two threads must not open the same archive twice; use a placeholder entry.
  - Keep zip → sess order.
- **Verification:** CORRECTED: added the per-served-file `indexZip10` lock (the cheapest fix), gprt's parse under the lock, and the full nesting list.

### A-12 `zipFor` caches open failures forever, logs nothing, and has an undocumented locking precondition
- **Where:** `native/di1compat/di1compat.cpp:374–389`; hkOpen `:3666`; Pentane_Main `:4137`
- **Severity / category / class:** Med · Error handling, latent bug · BEHAVIOUR-CHANGE
- **Evidence:**
  - `:385 if (!mz_zip_reader_init_file(...)) { delete z; z = nullptr; }`, then `:387 g_zips[rel] = z;`. A transient failure makes the archive invisible for the session, with no log line.
  - The caller must hold `g_zipLock`; this is not stated anywhere.
  - Data race: hkOpen :3666 reads `g_zips.size()` without the lock while other hkOpen threads insert through `zipFor` under `g_zipLock`.
    - Writers: whatever thread runs `read10z`, `indexZip10` or the annoSource read.
    - Reader: the MISSING path.
    - The race is formally UB but practically benign (an aligned 32-bit read).
    - Note: `size()` also counts cached failures, so the "N archives open" text in MISSING lines counts candidate names, not open archives.
  - Keys are case-sensitive. Boot indexing uses `fd.cFileName` (on-disk case, :4137) while `read10z` uses literal lower-case `"startup*.zip"` (:444). If the disk case differs, the same archive is opened twice (two FILE* and two central directories). Results are unaffected: `g_over10.emplace` is first-wins. The on-disk casing is UNVERIFIED.
- **Fix:**
  - Log once when init fails (adds log text).
  - Document the precondition.
  - Read `size()` under the lock or from an atomic counter.
  - Not caching failures is a behaviour change, and it also changes the MISSING-line count.
- **Refactor risk:** Any change to negative caching changes the "archives open" number in the log.
- **Verification:** CONFIRMED (threads named, log-text coupling added).

### A-13 `read10z` candidate generation is dense and churns strings
- **Where:** `native/di1compat/di1compat.cpp:408–439`
- **Severity / category / class:** Low · Readability / minor perf · SAFE-REFACTOR
- **Evidence:**
  - Per `/` split, it builds `dir`, `member`, `stem`, `asset`, `shard`, `bufAsset` and `zips[5]`, each looked up in a `std::map<std::string,…>` under the global lock.
  - `dir.substr(dir.rfind('/') + 1)` relies on `npos + 1 == 0`.
  - `top` (:417) is loop-invariant.
  - Every candidate name tried is cached in `g_zips` (nullptr entries). This is good for syscalls but grows the map, and it drives the A-12 count.
  - `g_zips` is never iterated (grep), so switching to `unordered_map` is safe.
- **Hot path?:**
  - Code defaults: off.
  - Recommended config: per 3.0-missing open and per transcode sub-read. The cost is small next to inflate.
- **Fix (behaviour-preserving):** Extract `candidateArchives(p, cut)` that returns the same five names in the same order, deepest split first. Make it unit-testable (A-N1).
- **Refactor risk:** Candidate order decides which archive wins.
- **Verification:** CONFIRMED.

### A-14 `read10s` double-buffers and builds a provenance string it throws away
- **Where:** `native/di1compat/di1compat.cpp:486–495`, `:437`, `:448`
- **Severity / category / class:** Low · Performance · NEEDS-VERIFICATION
- **Evidence:**
  - The data path is `mz_zip_reader_extract_to_heap`, then `out.assign`, then `mz_free`: two allocations and a full copy.
  - The `where` string is built and discarded on every `read10s` call.
  - The ownership contract ("mz_free it", :393) is a comment only.
- **Hot path?:**
  - Code defaults: off.
  - Recommended config: per transcode sub-read (`.mtb`/`.vbuf`/`.ibuf`/`.shd`…).
- **Fix (behaviour-preserving):** Thread an optional `std::string*` sink through `read10z` and extract with `mz_zip_reader_extract_to_mem` using the central-directory size. Skip `where` when the caller passes a sink. The bytes are identical.
- **Refactor risk:** Low. Keep the failure semantics: a failed extract tries the next candidate.
- **Verification:** CONFIRMED.

### A-15 `read30x` re-opens and re-decrypts the 3.0 archive index on every call, under a global lock
- **Where:** `native/di1compat/di1compat.cpp:519–527`; `arc30.hpp:93–125` (`readMember`); `render.hpp:686–689`
- **Severity / category / class:** Med · Load-time CPU waste, lock across I/O · NEEDS-VERIFICATION
- **Evidence:**
  - Each call does `CreateFileA`, reads the 8-byte header, takes the global `cipherLock()`, reads and decrypts the *whole* index (`8 × count` bytes), then reads the member.
  - render.hpp:686–689 reads a 3.0 and a 1.0 `.shd` per material, with no cache.
  - The world transcode's donor reads also go through this. arc30.hpp's own comment counts 92 donor reads for in_city.
- **Hot path?:**
  - Code defaults: off (`serve10=0`).
  - Recommended config: per material per served world or bundle (load time). The magnitude is UNVERIFIED; loadprof10's transcode ms would settle it.
- **Why it matters:** Repeated I/O and decryption at load, serialised on one lock.
- **Fix (behaviour-preserving):** Add a per-transcode memo in `render::IO`, or a process-wide decrypted-index cache per archive path in arc30. The archives are immutable during a session.
- **Refactor risk:** Memory in a 32-bit process. arc30.hpp is also used by octtest, so a cache must not change its outputs.
- **Verification:** CORRECTED: severity Low–Med → Med (rubric: load-time CPU waste, plus a lock held across I/O).

### A-16 `g_served` stores whole files, copied under `g_sessLock`, and never evicts
- **Where:** `native/di1compat/di1compat.cpp:559`; writes `:655–658`, `:817`, `:1038`, hkOpen `:3712`, `npcrt.hpp:276`; read hkOpen `:3649–3650`
- **Severity / category / class:** Med · Performance, memory · NEEDS-VERIFICATION
- **Evidence:**
  - :655–658 copy `r.mtb`/`r.vbuf`/`r.ibuf`/`r.ibuf2` under the lock. Those members are dead afterwards; only `r.tiers`, `r.textures10`, `r.skinned` and `r.sceneSettings` are used at :661–664.
  - :817 `emplace(mp, r.mtb)`: `r.mtb` is dead.
  - The `.mtb` branch copies `r.mtb` twice (:1038, :1044).
  - hkOpen :3650 `hit = it->second` copies the whole file under the lock, and `memFile` then copies it again.
  - Never pruned; this is intentional (:3650).
- **Hot path?:**
  - Code defaults: off.
  - Recommended config: per world transcode (writes) and per session hit (read copy under the lock).
- **Fix (behaviour-preserving):**
  - `std::move` the dead members into the map.
  - In the `.mtb` branch, assign `out` before the lock, then move into the map.
  - Optionally store `std::shared_ptr<const std::string>` so hkOpen copies outside the lock. All five writers must change.
- **Refactor risk:**
  - Preserve overwrite (`operator[]` at :655–658, :1038, :3712, npcrt:276) versus first-wins (`emplace` at :817). With `emplace(std::move(...))` the source is moved-from even when the key exists; that is harmless because it is dead.
  - The map is never iterated (grep), so `unordered_map` is safe.
- **Verification:** CONFIRMED.

### A-17 `g_vf30` and `g_tails10` are written from `hkOpen` and read by transcodes without a lock
- **Where:** `native/di1compat/di1compat.cpp:566–567`; written in `mergeVtxf :4083–4090` (called from hkOpen `:3558–3567` when `shaders/vformat.vtxf` opens); read at `:642–645`, in `shader10 :4037`, and through `io.tails10`
- **Severity / category / class:** Low · Concurrency assumption · DOC-ONLY
- **Evidence:**
  - Writer: the thread that opens `vformat.vtxf` at renderer init (shader package load at boot, :3759).
  - Readers: loader-thread transcodes, and the render thread via `hkShaderByName` → `shader10`.
  - Safety rests on the comment "Read once at boot, before any asset transcode" (:4082). Nothing enforces it.
- **Fix:** Document it. A log line on a second `mergeVtxf` call would add log text; `call_once` would change behaviour on a reopen.
- **Refactor risk:** Low.
- **Verification:** CORRECTED: severity Low–Med → Low (the boot ordering holds by engine design), threads named.

### A-18 The CHD path is a single process-wide slot, and `takeChdPath` does not take
- **Where:** `native/di1compat/di1compat.cpp:1155–1173`; caller hkOpen `:3626`
- **Severity / category / class:** Low · Concurrency, naming · SAFE-REFACTOR
- **Evidence:**
  - `hkLoadChdCallback` (always installed, :4186) sets `g_chdPath` (:1168) and clears it after the original returns (:1170). Two concurrent CHD loads would overwrite each other; whether that happens is UNVERIFIED.
  - `takeChdPath` only compares. hkOpen calls it for every non-write `.lua` open, outside its try block. That costs one uncontended mutex, plus `norm` only while the slot is non-empty.
- **Hot path?:**
  - Code defaults: per `.lua` open.
  - Recommended config: per `.lua` open. The cost is negligible.
- **Fix (behaviour-preserving):** Rename it to `isChdPath` and document the single-slot assumption.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### A-19 `.tbody` serving copies and compares every texture to detect a rare header patch
- **Where:** `native/di1compat/di1compat.cpp:610–619`; hkOpen `:3704–3707`
- **Severity / category / class:** Med · Performance · NEEDS-VERIFICATION
- **Evidence:**
  - `out.assign(data, n); padSmallDxt(out); codec = out.size() == n && memcmp(out.data(), data, n) ? "tbody" : nullptr;`
  - For an unpatched texture (almost all), `memcmp` scans all `n` bytes, so each served texture costs a full copy plus a full compare before `memFile` copies it again.
- **Hot path?:**
  - Code defaults: off.
  - Recommended config: once per served `.tbody`, about one extra memcpy+memcmp of the texture. That is small but avoidable next to inflate.
- **Fix (behaviour-preserving):** Add `static bool needsPadDxt(const void* d, size_t n)` with exactly :612/:615's conditions. The patch changes the header precisely when the condition holds (at least one of w/h is below 4 and is raised to 4), so `codec == "tbody"` ⇔ `needsPadDxt`. Copy and patch only then; otherwise leave `codec = nullptr` and `out` empty.
- **Refactor risk:** The conditions must match exactly (`size >= 128`, `"DDS "`, `"DXT"` at 84, `w<4||h<4`, `mips<=1`).
- **Verification:** CONFIRMED (equivalence proven from the code).

### A-20 `surfaceTable30()` is rebuilt for every served `.oct`
- **Where:** `native/di1compat/di1compat.cpp:718–732`, called `:750`
- **Severity / category / class:** Low · Performance · NEEDS-VERIFICATION
- **Evidence:** With `surfaces10` (default on), each `.oct` transcode reads up to 1024 engine strings. Each one is a `safeCopy` (one `sys::readable`, i.e. QueryWorkingSetEx per page, or VirtualQuery if the page is not resident) plus a `std::string`. This happens even when the file has no collision shapes.
- **Hot path?:**
  - Code defaults: off.
  - Recommended config: per served `.oct`.
- **Fix:** Build the table lazily, only when `surfaces30` needs it (needs an API change; same results). Caching it across `.oct`s is a behaviour change if the engine table changes after boot (UNVERIFIED across playset switches).
- **Refactor risk:** Low.
- **Verification:** CONFIRMED.

### A-21 `has30` is uncached and makes up to two filesystem syscalls per call
- **Where:** `native/di1compat/di1compat.cpp:672–677`; used `:685` (per motion-clip template in `cho::clips30`)
- **Severity / category / class:** Low · Performance · NEEDS-VERIFICATION
- **Hot path?:**
  - Code defaults: off.
  - Recommended config: per motion-clip template in served choreographies.
- **Fix (behaviour-preserving):** Memoise per directory in a mutex-guarded set. The 3.0 install does not change during a session.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### A-22 The pass order in `transcodeOct` is load-bearing and implicit
- **Where:** `native/di1compat/di1compat.cpp:734–758`
- **Severity / category / class:** Med · Readability, refactor hazard · SAFE-REFACTOR
- **Evidence:**
  - Fixed order: render (mutates `f` via `npcrt::skin10` :635) → navmesh/drop → AssetPool → skin → locators → cameras → collision → realm → cho → curves → surfaces → `const30` → `addAllCounts`.
  - :751–752 states that `const30` must be the last SubNetwork edit before the counts.
- **Fix (behaviour-preserving):** Write it as an explicit, commented pipeline in today's order. Never reorder.
- **Refactor risk:**
  - Any reorder changes output bytes.
  - The second `norm` at :753 (`norm(norm(path))`) is **not** a no-op for paths starting with `assets/assets/`, because `norm` strips one `assets/` per call. Keep it, or prove the double prefix never occurs.
- **Verification:** CORRECTED: added the `norm` non-idempotence caveat. The report implied the call at :753 was redundant.

### A-23 Render IO setup and render-failure handling are duplicated
- **Where:** `native/di1compat/di1compat.cpp:636–650` (world), `:806–813` (`particleShaders`), `:1027–1034` (`.mtb`); `!g_renderOk` at `:627`, `:797`, `:1025`
- **Severity / category / class:** Low · Duplication · SAFE-REFACTOR
- **Evidence:**
  - The two bundle IO setups are identical (`read30x`/`read10s` only), as are their donor prefixes.
  - Failure handling differs:
    - World and `.mtb` log each `r.problems` line.
    - `particleShaders` logs nothing and appends "channels: … did not transcode".
    - `!g_renderOk` gives a note plus false (world), a note plus nullptr (`.mtb`), or a silent `{}` (particles).
- **Fix (behaviour-preserving):** Add `static render::IO bundleIO()` for the two bundle paths. A shared `logRenderProblems` must **not** be applied to `particleShaders`, because that would add log lines.
- **Refactor risk:** Keep the world-only IO fields (translate, skinned, tails10, scene settings) in the world path.
- **Verification:** CORRECTED: severity Low–Med → Low. Flagged that unifying failure logging is a log change.

### A-24 Error reporting and the result type of `transcode` are inconsistent
- **Where:** `native/di1compat/di1compat.cpp:922`, `:880`, `:838–845`, `:936`, `:988`, `:996`; hkOpen `:3710`, `:3714–3720`
- **Severity / category / class:** Low · Error handling · NEEDS-VERIFICATION
- **Evidence:**
  - :922 assigns `note`.
  - `dnaGadgets30` and `isSpark10` exceptions are swallowed silently.
  - `.mer`/`.bent`/`.banm` parse exceptions propagate to hkOpen's catch ("TRANSCODE EXCEPTION:").
  - The codec is a `const char*` literal compared with `strcmp` (:3710).
- **Fix (behaviour-preserving):** An internal enum plus a name table that gives the same strings. Keep each branch's catch scope so the log text stays identical.
- **Refactor risk:** Log text.
- **Verification:** CORRECTED: severity Low–Med → Low.

### A-25 `missionGraph30` matches exact whitespace
- **Where:** `native/di1compat/di1compat.cpp:769`, `:779`, `:783`
- **Severity / category / class:** Low · Brittleness · DOC-ONLY
- **Evidence:** CRLF or any reformatting of the 1.0 Lua makes it return 0 silently. The insertion logic (insert before `AvatarReady`, `pos += add.size()`) is correct.
- **Fix:** Document it. Do not loosen the matching; that would change behaviour.
- **Refactor risk:** n/a
- **Verification:** CONFIRMED.

### A-26 The `.mtb` chunk walk can wrap a 32-bit size
- **Where:** `native/di1compat/di1compat.cpp:1008–1013`
- **Severity / category / class:** Low · Malformed input · BEHAVIOUR-CHANGE
- **Evidence:**
  - `o += cs` with a 32-bit `size_t` can wrap. When `o` lands in `[2^32−16, 2^32)`, `o + 16 <= n` also wraps and passes, so `memcmp` reads up to 16 bytes *before* `data` (heap, readable).
  - The loop is bounded only by the file's `nc`.
  - Only malformed 1.0 data reaches this, and 1.0 data is shipped and trusted.
- **Fix:** `if (cs > n - o) break;` after the MATP test. For valid data this is identical: the next iteration's `o + 16 <= n` test would already stop.
- **Refactor risk:** None for valid data.
- **Verification:** CONFIRMED (the out-of-bounds read detail added).

### A-27 Four hand-rolled memoisation idioms with different locking
- **Where:**
  - `lang30` `:531–549`: computes under its mutex, across a directory scan and `read30x` calls.
  - `sceneSettingsIO` `:572–586`: `done=true` before parsing, never retried; holds its mutex across `read10s` and `read30x`.
  - `isSpark10` `:822–849`: computes outside the lock.
  - `fallbackDef` `:2017–2049`: leaked `new` map, built under the lock (A-38).
- **Severity / category / class:** Low · Duplication · NEEDS-VERIFICATION
- **Fix:** A small `Memo<K,V>` with an explicit per-site policy ("compute under the lock" or "compute outside").
- **Refactor risk:** Keep each site's policy. It decides blocking and duplicate work.
- **Verification:** CONFIRMED.

### A-28 Path normalisation and lower-casing are hand-written in many places
- **Where:**
  - `native/di1compat/di1compat.cpp:397–399` (`read10`), identical to `norm` `:480–482`.
  - `:602` (`indexZip10`: lower-case plus `\`→`/`, no `assets/` strip).
  - `:826–827` (`isSpark10`: no strip).
  - Lower-case-only loops `:775`, `:872`, `:2039`, `:2046`.
  - Also `sys.hpp:165–166`, `cho::Aliases::add`, and `npc::lower`.
- **Severity / category / class:** Med · Duplication with drift risk · SAFE-REFACTOR
- **Evidence:**
  - `g_served` keys come from `norm` (hkOpen `np`, `transcodeAssetRender`'s `dir/stem`).
  - `g_over10` keys come from `indexZip10`'s own loop.
  - `read10` normalises separately for archive lookup.
  - If one variant changes, the lookups silently stop matching.
- **Fix (behaviour-preserving):** Have `read10` call `norm` (identical code). Name the other variants explicitly (`normPathNoStrip`, `lowerAscii`) and map each call site to the exact variant it uses today.
- **Refactor risk:** The variants really differ.
- **Verification:** CORRECTED: severity Low–Med → Med (rubric: real duplication with drift risk; session keys depend on it).

### A-29 The Genie::File vtable ABI is spelled out several times
- **Where:** `native/di1compat/di1compat.cpp:455–469` (slots 2/3, data word 6), `:501–514`, `:1126–1138`, hkOpen `:3559–3571`, `:3601–3608`
- **Severity / category / class:** Low · Duplication, readability · NEEDS-VERIFICATION
- **Evidence:** The copies differ:
  - `read30` skips the read when `n==0` and does not resize on a short read.
  - `readStream` always reads and resizes to `got`.
  - hkOpen's copies read only when the size is non-zero.
- **Fix:** A `genie` namespace with named slot constants and `__fastcall` wrappers. Keep each caller's exact sequence.
- **Refactor risk:** Keep the dummy `edx` (C2).
- **Verification:** CORRECTED: severity Low–Med → Low (the engine ABI is fixed, so drift risk is low).

### A-30 Two different comma-list parsers for config lists, re-parsed on every open
- **Where:** `native/di1compat/di1compat.cpp:1114–1122` (`memtest`: substring of the raw path); hkOpen `:3636–3643` (`block10`: prefix of the normalised path)
- **Severity / category / class:** Low · Duplication (cold: both empty by default) · NEEDS-VERIFICATION
- **Fix:** Pre-split once in `loadConfig`. Keep substring-vs-prefix and raw-vs-normalised semantics.
- **Refactor risk:** Those semantic differences.
- **Verification:** CONFIRMED.

### A-31 Hook boilerplate is spread across three places per hook
- **Where:** 47 detours defined in `:1149–2208` (counted); `hook(...)` lines in `Pentane_Main :4183–4358`
- **Severity / category / class:** Med · Maintainability · NEEDS-VERIFICATION
- **Evidence:**
  - Each detour has a typedef, a `static XFn g_x` and an `hkX`. The VA and prologue live 2000–3000 lines away.
  - `hkStartLookup`/`hkStartQuery` have two install sites with mutually exclusive conditions (`trace` vs `spawntrace10 && !trace`).
- **Fix:** Put `constexpr HookSpec {name, va, prologue}` next to each detour. `Pentane_Main` keeps the same calls, order and conditions, referencing the spec.
- **Refactor risk:** Install order (each `hook()` enables immediately; C3), conditions, log text ("hook %s @ %08X ok (0)"), and hook-name strings.
- **Verification:** CONFIRMED.

### A-32 Duplicated "log first N"/"log on change" patterns and identical typedefs
- **Where:**
  - Budget counters :1283 (10), :1412 (6), :1431 (20), :1444 (12), :1640 (80), :1666 (80), :1678 (400), :1731 (300), :1739 (40), :1751 (40), :1800 (3000/30000/200), :1969 (6), :2050 (20), :2066 (20), plus `g_postSeen` (3 per name) and `g_btFirst.size() <= 40`.
  - Not budgets: `g_avLogged` :1224 (one-shot) and `g_btEvals` :1538 (modulo 20000).
  - Change detectors :1339–1343, :1359–1362, :1473–1480, :1484–1492, :1598–1602.
  - Identical typedefs: `ThisFn` :1471 ≡ `BtThisFn` :1533; `OneArgFn` :1495 ≡ `BtOneFn` :1749 ≡ `EntryRelFn` :1804.
- **Severity / category / class:** Low · Duplication · SAFE-REFACTOR
- **Fix:** `static bool logBudget(volatile LONG& c, LONG cap)`, which is `InterlockedIncrement` then compare. Keep every cap. Merge the identical typedefs.
- **Refactor risk:** Keep the short-circuit order. Some sites increment first (`hkLayApply` :1643); others increment only after a match (`hkChoHas` :1975, `hkChoMake` :2072, `hkHdrMake` :1417). Swapping the order changes when budgets run out.
- **Verification:** CORRECTED: the cap list had a spurious "5" (the watch comment says 5; the code caps at 10) and missed 3. Two counters are not budgets.

### A-33 A second `readable()` duplicates `sys::readable` with the slow algorithm and different semantics
- **Where:** `native/di1compat/di1compat.cpp:1225–1241` (added by 684539b), used by `afterCall :1246`, which `stackOf` (:1264) and the profiler's `afterCallSafe` (:2247–2250, sampler :2369) call. `tdReadable :1515` wraps `sys::readable`.
- **Severity / category / class:** Low · Duplication, diagnostic perf · NEEDS-VERIFICATION
- **Evidence:**

  | Case | local `readable` (:1228) | `sys::readable` (sys.hpp:180) |
  |---|---|---|
  | `n==0` | true without querying | checks 1 byte |
  | `PAGE_EXECUTE`-only page | false | true |
  | address < 0x10000 | queried (MEM_FREE, so false) | false without querying |

  - Perf: sys.hpp:173–179 documents that VirtualQuery walks the region (0.19–5.1 ms per call on large regions) and that this was once 85% of the main thread.
  - The local version runs one VirtualQuery per candidate exe-range stack word:
    - the trace stack logs (`hkHdrMake`, `hkAvRemove`, hkOpen bitmasks), `hkNodeDtor` (teardowntrace), and avHandler;
    - the loadprof10 sampler, for up to 384 words per thread per ~1 ms sample.
  - The sampler does this after resuming the thread, so the cost falls on the sampler thread and lowers its effective rate. The magnitude is UNVERIFIED.
- **Hot path?:**
  - Code defaults: only the trace stack logs.
  - Recommended config: none, unless loadprof10 is set.
- **Fix:** Rename it (`vqReadable`) and explain why it is separate. Or switch `afterCall` to `sys::readable` after confirming that no exe page in `[g_base, g_base+0x2100000)` is execute-only.
- **Refactor risk:** This is the 12:01 boot-crash fix. Any change needs a boot test with `loadprof10=1`.
- **Verification:** CORRECTED: added the perf aspect (the slow path sys.hpp warns about, run per stack word).

### A-34 `safeCopy` checks only the first byte
- **Where:** `native/di1compat/di1compat.cpp:87–102`
- **Severity / category / class:** Low · Correctness vs stated intent · BEHAVIOUR-CHANGE
- **Evidence:**
  - :91 checks 1 byte. A string that runs into an unreadable page still faults first-chance. The `__except` handles it and the game survives, but the fault costs:
    - a symbolised Pentane "Exception Caught" entry;
    - a CrabeLoader report slot (one of 3);
    - avHandler's one-shot (A-35).
  - :98 writes `out[0]` even when `cap==0`; no caller passes 0.
  - `strcpy_s(out, cap, "<unreadable>")` fail-fasts if `cap < 13`. The smallest cap in the slice is 40 (:1653).
  - In the recommended config, callers pass engine-owned strings (hkOpen path :3557, playset key, cho name), so the edge case needs probe-supplied non-pointers.
- **Hot path?:**
  - Code defaults: per open, plus probes.
  - Recommended config: per open (one QueryWorkingSetEx), per frame in `hkPlaysetChar`, and per cho start.
- **Fix:** Re-check readability at each 4 KB crossing. Valid strings are unchanged. A string crossing into unreadable memory is then truncated (no AV, no dump, no "<unreadable>"); that difference is a behaviour change.
- **Refactor risk:** That edge case's log text changes.
- **Verification:** CORRECTED: severity Low–Med → Low; class BEHAVIOUR-CHANGE.

### A-35 `avHandler` logs only the first AV process-wide, including handled ones, and its comment says "in the exe"
- **Where:** `native/di1compat/di1compat.cpp:1221–1224`, `:1254–1278`; registered first in the VEH chain at `:4178`
- **Severity / category / class:** Med · Diagnostics · BEHAVIOUR-CHANGE
- **Evidence:**
  - `code != AV || InterlockedExchange(&g_avLogged, 1)` arms on the first AV of any kind and any thread. That includes AVs the plugin's `__try` paths handle and the engine's own handled AVs. A later real crash then goes unlogged.
  - There is no EIP filter, although :1221 says "in the exe".
  - `ExceptionInformation[0] == 8` (DEP) prints as "write".
  - **Can avHandler itself fault? Not in practice with the current code.** This matters most, because a fault *inside* a VEH re-enters the handler chain. That is the fatal case: the 12:01 crash fixed by 684539b. A handled fault elsewhere only costs a Pentane log entry and a CrabeLoader slot. With the current code:
    - It reads only `e->ExceptionRecord`/`ContextRecord`.
    - `stackOf` bounds the walk to the committed region containing ESP (:1256–1258).
    - `afterCall` checks its 6 bytes with VirtualQuery before reading (:1246).
    - Residual risks are a TOCTOU protection or unmap change by another thread, and `stackOf` checking only `State == MEM_COMMIT`, not `Protect`. A committed PAGE_GUARD or NOACCESS page at ESP is implausible for an AV on a live stack.
    - Its line is at most about 270/900 bytes.
- **Fix (log-only behaviour change; needs sign-off):** Skip AVs whose EIP is inside di1compat.dll, or log the first N.
  - Note: `g_selfLo`/`g_selfHi` (:2251) are set **only inside `profThread`** (:2304–2305, loadprof10). They are 0 otherwise, so the DLL range must be initialised at boot first. Pentane_Main already has the module handle at :4117.
  - Optionally add the Protect check in `stackOf`. That is identical for normal stacks.
- **Refactor risk:** Changes which line appears. Log consumers are UNVERIFIED.
- **Verification:** CONFIRMED. Added: avHandler self-fault analysis on current code, and the fix's dependency on initialising `g_selfLo`.

### A-36 Hardware-watchpoint probe: unbounded stack walk inside a VEH, never disarmed
- **Where:** `native/di1compat/di1compat.cpp:1280–1333`, `:1350–1354`; installed under `trace` (`:4207`)
- **Severity / category / class:** Med · Diagnostics side effects, latent crash · NEEDS-VERIFICATION
- **Evidence:**
  - `hkIgpName` calls `watch()` (a no-op after the first arm).
  - `armWatch` suspends every other thread and sets DR0 and DR7 (L0, RW0=write, LEN0=4) (:1315–1316).
  - The caller blocks for up to 5 s (:1332).
  - `watchHandler` is registered first (:1330), swallows every single-step with DR6 bit 0, and is never removed. The watchpoint is never disarmed, so every later write to that address costs a kernel trap plus a VEH dispatch, forever. That applies even after the slot's memory is freed and reused, on every thread armed at the time.
  - The `sp[0..399]` walk (:1294–1297) reads 1600 bytes above ESP with **no bounds or readability check**. That is the pattern 684539b removed from `stackOf`.
    - If the writer's ESP is within 1600 bytes of its stack base, this faults *inside a vectored handler*. The fault re-enters the handler chain: a Pentane entry, a CrabeLoader report, and avHandler if not yet spent.
    - `watchHandler` has no `__try`, so the nested AV is left to the interrupted engine frames' handlers.
    - This is the same class as the 12:01 crash, and it violates the "a VEH must never fault" invariant.
  - The `*(int*)g_watchAddr` read (:1291) is safe. A data breakpoint fires after the write succeeded, on the writing thread.
  - The comment says "first 5 hits" (:1281); the code logs 10 (:1288).
  - Check-then-set race on `g_watchAddr` (:1328–1329).
- **Hot path?:**
  - Code defaults: armed once the playset flow runs.
  - Recommended config: not installed.
- **Fix (behaviour-preserving):** Bound the existing loop by the committed stack region (`VirtualQuery(sp)`, as `stackOf` does) and keep its "any exe-range word" logic and caps. Output is identical whenever today's walk does not fault. Do **not** replace it with `stackOf`: `stackOf` validates call sites with `afterCall`, so it would log different addresses. Correct the comment. Disarming, or a separate flag, is a behaviour change.
- **Refactor risk:** Low.
- **Verification:** CORRECTED:
  - The `g_watchAddr` read is not a fault risk; only the stack walk is.
  - "Reuse stackOf" would change the log.
  - Added the never-disarmed trap cost.

### A-37 `sprintf_s` appends can overflow, which fail-fasts the game; `hkLayApply` can overflow
- **Where:** `native/di1compat/di1compat.cpp:1647–1658` (`char line[900]`, guard `if (at > sizeof(line) - 80) break;` checked *after* the write); `:1573–1579` (`hkClipEval`); `stackOf :1265` (no capacity guard)
- **Severity / category / class:** Med · Latent crash (opt-in `breaktrace`) · BEHAVIOUR-CHANGE
- **Evidence:**
  - One entry `" %u:%u%s'%s'/%u%s'%s'"` is at most 109 characters (k ≤ 23, two u16, two "(null)", two 39-character names).
  - Example: 8 entries of 100 characters give `at = 800`, which passes the guard (≤ 820). A ninth 100-character entry needs 101 bytes while 100 remain. `sprintf_s` then calls the invalid-parameter handler, which with the static CRT terminates the process (C10).
  - Every other fixed buffer in the slice fits its worst case (re-checked): :660 179/200, :943 ≈257/300, :981 25/64, :1041 ≈99/160, `avHandler` ≈270/900, `watchHandler` ≈171/700, `hkHdrMake` ≈341/700, `hkAvRemove` ≈160/400, `tdObj` ≈160/200, `hkNodeDtor` ≈485/600, hkOpen :3623 ≈543/600.
  - `hkClipEval` overflows only with absurd float clocks.
- **Hot path?:** Off in both configs (breaktrace).
- **Fix:** Check `sizeof(line) - at > 110` *before* each append, or use `_snprintf_s(..., _TRUNCATE)`. Add a capacity check in `stackOf`. Output is identical whenever today's code survives; only the case that today kills the process changes.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### A-38 `fallbackDef` holds `g_defsMx` across an engine file read, inside `hkAssetDef`'s `__try`
- **Where:** `native/di1compat/di1compat.cpp:2017–2049` (lock :2022, `read30` → `g_open` :2026, key read :2045), `:2051–2063` (`__try { f = fallbackDef(key); } __except (EXCEPTION_EXECUTE_HANDLER)`); installed always (`:4304`)
- **Severity / category / class:** Med · Lock across an engine call, latent hang · BEHAVIOUR-CHANGE
- **Evidence:**
  1. **Lock across an engine call (fact).** `read30` calls the unhooked engine open and read while `g_defsMx` is held, on the first miss, during the build.
  2. **Asynchronous exceptions.** An AV under the lock (a bad `key`, or inside the engine open) unwinds through `fallbackDef` with `/EHsc` (C1).
     - Microsoft guarantees destruction of locals only under `/EHa`.
     - In practice both fault sites are *inside calls*: `strlen` in `std::string`'s constructor, and the engine open. `/EHsc` keeps accurate unwind state at calls, so the `lock_guard` is most likely released.
     - A permanently locked `g_defsMx` is therefore possible per the docs but unlikely. UNVERIFIED without a test.
  3. **Fibers.** If the engine open yields fibers on the frame thread (`FUN_0041fc10`, :2133–2134) and another fiber on the same thread reaches `fallbackDef`, it re-locks a non-recursive `std::mutex` from the same thread. That is UB per the standard; the outcome depends on the STL (block, throw, or silent re-entry that sees the half-built map). Whether this open yields, and whether `hkAssetDef` runs on fibers, are UNVERIFIED.
  4. `EXCEPTION_EXECUTE_HANDLER` also swallows C++ exceptions such as `bad_alloc`, silently.
- **Fix:**
  - Safe part (behaviour-preserving for readable keys): in `hkAssetDef`, `safeCopy` the key into a local buffer and pass that.
    - The buffer must be larger than any DefID, or fall back to today's path for longer keys.
    - An unreadable key then gives "" and a miss, which is today's result without the AV.
    - It adds no destructor objects to `hkAssetDef` (C2712).
  - Lock part (BEHAVIOUR-CHANGE):
    - Moving the build outside the lock changes what concurrent callers see during the build (a miss instead of waiting), or needs a wait primitive, which is fiber-unsafe in the same way.
    - It must still emit the "asset defs fallback: …" line exactly once.
    - Needs the owner's decision.
- **Refactor risk:**
  - `hkAssetDef` returns `it->second.c_str()` to the engine, so map nodes must never move or be erased (`std::map` keeps them stable).
  - The map is deliberately leaked.
- **Verification:** CORRECTED:
  - Point 1 (lock left held) is tempered to "documented risk, unlikely in practice".
  - Point 2's outcome is implementation-defined, not "UB that deadlocks".
  - The fix is reclassified: only the key copy is behaviour-preserving.

### A-39 C++ exceptions can escape several hooks into engine frames
- **Where:**
  - Always installed: `hkLoadChdCallback :1168` (`norm`), and `takeChdPath` via hkOpen :3626 before hkOpen's try.
  - Default on: `hkChoStart :1631` → `audio::choreographyStarted`, which takes a `recursive_mutex` and plays.
  - trace: `hkReaderList :1341–1343`, `hkReaderItemOk :1361–1362`, `hkPost :1467`.
  - breaktrace: `hkClipEval :1549–1580`, `hkChoReady :1598–1600`.
- **Severity / category / class:** Low · Robustness · NEEDS-VERIFICATION
- **Evidence:**
  - Hook-boundary catches do exist: hkOpen `:3714/:3717`, `hkAssetDef`'s `__except` (:2056), `npcrt.hpp:302` (`hkInclude`), and di1compat.cpp `:2640`, `:3206`.
  - The hooks listed above have none. `bad_alloc` in a 32-bit process with growing caches (A-16) is plausible.
- **Fix:** `try { … } catch (...) {}` around each hook's own allocating code, then call the original as today. Nothing changes when nothing throws.
- **Refactor risk:** MSVC C2713: `try` and `__try` cannot be in the same function. Keep these blocks out of `__try` functions (`hkHdrMake`, `hkAssetDef`, `safeCopy`, `stackOf`).
- **Verification:** CORRECTED: the claim "only hkOpen's transcode block catches" is wrong. Five other boundaries exist, and the list now includes the default-on `hkChoStart`.

### A-40 `hook()` results are ignored, and each hook freezes all threads
- **Where:** `native/di1compat/di1compat.cpp:110–128`; `:506`; callers `:4183–4358`
- **Severity / category / class:** Low · Error handling, startup cost · BEHAVIOUR-CHANGE
- **Evidence:**
  - If "AssetOpen" is refused, `hook()` returns before `MH_CreateHook`. MinHook writes `*ppOriginal` only on success (hook.c:633–634), so `g_open` stays null.
  - `read30` still calls it:
    - `fallbackDef` (always installed) gets an AV at 0 under `hkAssetDef`'s `__try`. That fault is handled and the game survives, at the cost of a Pentane entry and a CrabeLoader slot.
    - npcrt's `prepare` is protected only by `catch (std::exception&)`, which under `/EHsc` does not catch SEH, so its AV is unhandled.
  - `MH_EnableHook` → `EnableHook` → `Freeze` per hook (hook.c:722–742) means one all-thread suspend per hook at startup.
  - `target[i]` is read unchecked, but it is the exe image at a known VA.
- **Fix:** `if (!g_open) return false;` in `read30`. This changes only the failure case. Keep per-hook enabling (C3).
- **Refactor risk:** Low.
- **Verification:** CONFIRMED.

### A-41 Trace hooks dereference engine pointers unchecked, and two of them alter engine calls
- **Where:**
  - `hkIgpName :1352`; `hkLevelTick :1476/:1479`; `hkLoadDone :1490`; `hkStartLookup :1437`; `hkStartQuery :1450`.
  - `hkAacExec :1992–1993`; `hkAvSpawned :2003–2005`; `hkBtPlay :1612–1618`; `hkExplode :1755–1762`.
  - `hkChoStart :1627–1628` (default on); `hkActorExists :1201`.
  - Behaviour-altering: `hkFigureOk :1214`, `hkInst :1398–1402`.
- **Severity / category / class:** Low · Consistency with the "check first" policy (:89–90, :1514) · DOC-ONLY
- **Fix:** Document that these are engine-owned objects, or wrap the reads with `sys::readable`, which adds per-call cost. Keep `hkFigureOk`'s argument substitution.
- **Refactor risk:** Low.
- **Verification:** CONFIRMED.

### A-42 Unsynchronised diagnostic state shared across hook calls
- **Where:** `native/di1compat/di1compat.cpp:1339`, `:1359` (function-static `std::string last`); `:1473` `g_levelState`; `:1484` `g_loadDoneLast`; `:1393` `g_instHeaders` (written `hkInst :1404`, read `hkHdrClone :3189`)
- **Severity / category / class:** Low · Concurrency · NEEDS-VERIFICATION
- **Evidence:**
  - All are trace-only.
  - If the reader-list or reader-item hooks run on more than one thread, the `std::string` assignment is a data race (UB).
  - If two loader threads instance templates concurrently, `g_instHeaders` attributes the header count to the wrong template (log only).
- **Fix:** Thread-local or mutex-guarded state. Same output in the single-threaded case.
- **Refactor risk:** Low.
- **Verification:** UNVERIFIED. The race exists only if these engine functions run on more than one thread. Settle it by logging `GetCurrentThreadId()` in each hook under trace=1 for one IN_City load and one playset switch.

### A-43 The `hkClipEval` probe takes a global mutex on every clip evaluation, and its maps grow without bound
- **Where:** `native/di1compat/di1compat.cpp:1533–1584`
- **Severity / category / class:** Low · Performance, diagnostics accuracy · SAFE-REFACTOR
- **Evidence:**
  - Per evaluation it takes `g_btMx`, calls `GetTickCount64`, does about 3 hash operations, and occasionally calls `logf` under the mutex (:1554, :1560, :1580).
  - Every 2 s it scans `g_btClock` and sorts the fresh entries.
  - Maps keyed by raw pointer are never erased, so a reused processor address merges with stale data.
  - It calls the engine clock `rt(0x00528A70)` an extra time before the original. That is a float-returning `__fastcall`; side effects are UNVERIFIED.
  - Shadowing: `auto c` (:1558) vs `const BtClock& c` (:1576).
- **Hot path?:** Off in both configs (breaktrace). When on, it runs per frame per processor.
- **Fix:** Rename the shadowed variable (safe). Logging outside the lock would change line order under contention, so that part needs verification.
- **Refactor risk:** Low.
- **Verification:** CONFIRMED.

### A-44 Teardown probe: two VirtualQuery per object, a lock per node destruction, and thread-local state under fibers
- **Where:** `native/di1compat/di1compat.cpp:1517–1521` (`tdProt`), `:1766–1776` (`tdObj`), `:1795–1798`, `:1834–1842`, `:1849–1864`, `:1865–1891`
- **Severity / category / class:** Low · Diagnostics · SAFE-REFACTOR
- **Evidence:**
  - `tdObj` does its readability check through `tdReadable`/`tdRd` (`sys::readable`).
  - Its two VirtualQuery calls (:1773 for `AllocationBase`, and `tdProt` for `Protect`) produce *logged data*, not checks.
  - `hkNodeDtor` locks `g_tdWatchLock` on every node dtor. `hkPlBind` locks per placement inside its loop.
  - `t_td*` are `__declspec(thread)`, although :2122 notes fibers share threads.
  - :1835 resets `t_tdFrees` unconditionally (even when `on` is false) and never restores it, so a nested Layout dtor resets the outer one's count.
  - The `tdEntry` bit math is unnamed.
- **Hot path?:** Off in both configs (teardowntrace). When on, it runs on every engine node dtor and every free (`hkHeapFree`); the early-outs read TLS first.
- **Fix (behaviour-preserving):** Name the constants. One VirtualQuery can feed both `AllocationBase` and `Protect` (same output). Do not replace those queries with `sys::readable`: it does not return the logged values.
- **Refactor risk:** Low.
- **Verification:** CORRECTED: the proposed "use `sys::readable` for tdObj's check" was wrong. The check already uses it; the VirtualQuery calls supply logged fields.

### A-45 Profiler state shared across threads as plain doubles; duplicated QPC code
- **Where:** `native/di1compat/di1compat.cpp:2099–2121` (`g_fiberRole`, `g_fiberMs[5]`, `g_fiberSince`), `:2108–2111`, `:2125–2132`, `:2178–2208`
- **Severity / category / class:** Low · Concurrency, duplication · NEEDS-VERIFICATION
- **Evidence:**
  - `hkFiberSwitch` writes; `fiberMsNow` reads from `hkOctLoad` (only when on the frame thread) and from `hkFrameSync`.
  - This is race-free only if the scheduler thread that calls `FUN_0041e480` is the frame thread (:2097 says "only the scheduler's thread calls it"; that it equals the frame thread is UNVERIFIED).
  - `fiberProf` falls back to `s[0]` (:2131).
  - `hkFrameSync` uses raw QPC ticks with its own frequency static.
- **Hot path?:** Off in both configs (loadprof10/fpslog10). When on, it runs per frame and per fiber switch.
- **Fix:** Document the assumption. Reusing `qpcMs()` in `hkFrameSync` is **not** arithmetic-identical: today it takes differences of integer ticks divided by frequency, while `qpcMs` differences absolute-ms doubles. The `%.2f` fps line can differ in the last digit, so that change needs verification.
- **Refactor risk:** Low.
- **Verification:** CORRECTED: the "keeping the arithmetic identical" claim for reusing `qpcMs` is false.

### A-46 Always-on and trace per-call costs (inventory; no action needed)
- **Where:**
  - `hkPlaysetChar :1183–1191`: always on. One `safeCopy` (one QueryWorkingSetEx) plus up to 4 `_stricmp`. Per frame on non-3.0 playset worlds per the :1177–1178 comment (UNVERIFIED).
  - `hkPost :1461–1470`: trace. `safeCopy` plus 6 `strstr` per event post; frequency UNVERIFIED.
  - `hkAssetDef :2051–2063`: always on. No extra cost on a hit; on a miss, a lock plus a lower-cased `std::string` copy.
  - `hkChoStart :1624–1632`: default on. `safeCopy` plus `audio::choreographyStarted`, which returns early unless `ready()` (audio.hpp:2076).
- **Severity / category / class:** Low · Performance · DOC-ONLY
- **Hot path?:** Per frame (`hkPlaysetChar`) in both configs, at about 1 µs.
- **Verification:** CONFIRMED (`hkChoStart`'s safety without audio resolved).

### A-47 Magic numbers and inconsistent exe-image constants
- **Where:**
  - Base `0x400000`: :106, :1265, :1273, :1292, :1296, :1522.
  - Image end `0x2100000`: :1244, :1247, :1273, :1292, :1296 (also :2365, :3420). But `0x0209B000` in `tdStatic` :1522.
  - Duplicated static-VA conversion :1273, :1292, :1296.
  - :1401 `t[100 / 4]` (offset 0x64, printed between 0x5c and 0x60).
  - Suffix lengths :630 (−4), :798 (−13), :962 (−11), :1050 (−8).
  - `0x1b`/`"\x1bLua"` at :864, :865, :878, :890, :902, :1074, :1084, :1103.
  - DR7 bits :1316.
- **Severity / category / class:** Low · Readability · SAFE-REFACTOR
- **Evidence:** If the real `SizeOfImage` is `0x209B000`, then `afterCall`, avHandler and watchHandler accept 0x65000 bytes *past* the image as "exe". That range is a plausible source of the "unreadable exe page" that 684539b guards against (UNVERIFIED). Settle it by logging `SizeOfImage` from the exe's PE header, as :2305 does for the DLL.
- **Fix:** Add `kExeBase`, `kExeImageEnd` (keep both values until consciously unified), `toStaticVa()`, `sizeof(".particledata") - 1`, and `isLuaChunk()`.
- **Refactor risk:** Keep values identical. Unifying the two image ends changes which addresses log as static and which stack words are tested.
- **Verification:** CONFIRMED (the image-size note added).

### A-48 Stale or contradicting comments and metadata
- **Where and evidence:**
  - :6–7 lists 4 of 105 keys.
  - :134 "log every open" (A-05).
  - :145 documents `bufview10` modes 1–3; the code uses 1–8 (:3790–3828).
  - :1218–1220 describes hooks that are defined 115 lines later (:1335–1364).
  - :1221 "in the exe" (A-35).
  - :1281 "first 5 hits" vs 10.
  - :2083 "test (fpslog10)" (also loadprof10).
  - :2 cites `D:\DI-Extract\_research\PLAN.md` (not in the repo; plan IDs unresolvable).
  - :63 `version = {0, 1, 0}` vs the log line "di1compat 0.3.0" (:4114).
  - **`README.md:143`** calls the config "the `Cfg` structure"; it is an anonymous struct.
  - **`mernode.hpp:3`** says "Included by di1compat.cpp after rt()/logf()", but it is included at :29, before both. It actually receives them as parameters (:2719).
- **Severity / category / class:** Low · Readability · DOC-ONLY
- **Fix:** Comment and doc corrections. The version field affects what Pentane displays; check with the owner.
- **Refactor risk:** None.
- **Verification:** CORRECTED: README line 143 (not 149). Added the stale mernode.hpp comment.

### A-49 Missing direct includes and macro-order dependence
- **Where:** `native/di1compat/di1compat.cpp:8–46`; uses `std::set` :561/:868, `std::function` :683/:1076, `std::unordered_map` :1536
- **Severity / category / class:** Low · Build fragility · SAFE-REFACTOR
- **Evidence:**
  - `<set>`, `<functional>` and `<unordered_map>` arrive only through other headers.
  - `<windows.h>` is first (:8) without `NOMINMAX`, so the `#ifndef NOMINMAX / #define NOMINMAX` in sys.hpp:4–6 and audio.hpp:15–17 has no effect in this TU. That is why the code writes `(std::min)`/`(std::max)` (:1258, :2192).
- **Fix:** Add the direct includes. Leave the `windows.h` order alone.
- **Refactor risk:** Splitting the file without the direct includes breaks the build.
- **Verification:** CONFIRMED.

### A-N2 `readable()` comment wrongly says Pentane's handler ends the game (new)
- **Where:** `native/di1compat/di1compat.cpp:1225–1227`
- **Severity / category / class:** Low · Stale / misleading comment · DOC-ONLY
- **Evidence:**
  - The comment reads: "__try is not enough: a first-chance AV reaches the vectored handlers before any __except, and Pentane's ends the game (12:01 boot crash …)". The 684539b commit message makes the same claim.
  - Per the coordinator's disassembly of the bundled `third_party/pentane/Pentane.dll` (handler at 0x101b978e) and upstream `crash.cpp` (commit 48e3fbb), Pentane's VEH logs `=== Exception Caught! ===` and returns `EXCEPTION_CONTINUE_SEARCH` on every path, with no Terminate/ExitProcess. `harness.ps1:133` counts those lines.
  - The 12:01 crash was avHandler faulting again *inside* the vectored handler (684539b's message: `afterCall+0x21 <- avHandler <- afterCallSafe <- profThread`).
- **Why it matters:** The comment teaches the wrong invariant: "never fault, even under `__try`". The real rules are these:
  - A handled fault is survivable, but costs a Pentane log entry and a CrabeLoader report slot.
  - A fault inside a VEH, or anything it calls, is fatal.
  - A later maintainer could mis-prioritise. For example, they might treat `safeCopy`'s page-crossing edge (A-34) as a crash risk, or treat the VEH paths (A-36) as merely costly.
- **Fix (behaviour-preserving):** Comment-only. Say that a handled first-chance AV costs a Pentane entry and a CrabeLoader report, so pre-check readability. Say that avHandler and everything it calls must never fault, because a fault inside a VEH re-enters the handlers (the 12:01 crash).
- **Refactor risk:** None.
- **Verification:** NEW. The Pentane behaviour comes from the coordinator's disassembly, not re-done here. I did confirm the comment text, the commit message, and `harness.ps1:133`.

---

## Verified context for the fix pass

### Which detours in this slice are live (checked against Pentane_Main 4097–4359)
| Activation | Detours defined in 1–2210 | Code defaults | Recommended cfg | Evidence |
|---|---|---|---|---|
| Always installed | `hkLoadChdCallback`, `hkPlaysetChar`, `hkAssetDef`; VEH `avHandler` (first in chain) | on | on | :4186, :4200, :4304, :4178 |
| `breakTrace \|\| (audio10 && cineSync10)` | `hkChoStart` | on | on | :4245–4246 |
| `trace` | `hkActorExists, hkFigureOk, hkFigurePlayset, hkReaderList, hkReaderItemOk, hkIgpName, hkAssetInst, hkInst, hkHdrMake, hkStartLookup, hkStartQuery, hkPost, hkLevelTick, hkLoadDone, hkAvFinal, hkAvRemove, hkChoHas, hkAvCreate, hkAacExec, hkAvSpawned, hkChoMake, hkPlaceToy` (22 of the block's 26; `hkHdrClone`, `hkMkStart`, `hkMkLocator` and `hkMkSpawnExp` are defined later); `watchHandler` is registered lazily | on | off | :4201–4228, :1330 |
| `spawnTrace10 && !trace` | `hkStartLookup`, `hkStartQuery` | off | off | :4355–4358 |
| `breakTrace` | `hkClipEval, hkMunHit, hkExplode, hkChoReady, hkBtPlay, hkLayApply, hkLayGate, hkLayBind, hkClipSet, hkMusicPush` | off | off | :4247–4258 |
| `teardownTrace` | `hkLayoutDtor, hkNodeDtor, hkEntryRel, hkEntryFree, hkPlBind, hkTrackFree, hkHeapFree` | off | off | :4259–4267 |
| `fpsLog10 \|\| loadProf10` | `hkFrameSync` | off | off | :4296 |
| `loadProf10` | `hkFiberSwitch, hkFileWait, hkOctLoad` (+ `profThread`) | off | off | :4297–4303 |
| `missionTrace` | `hkMissionTick` (typedef :2081 here, detour :2667) | off | off | :4236–4237 |

The recommended config's hot paths in this slice are:
- **Every AssetOpen:** `safeCopy`, about 6 `endsWith` and 2 `_stricmp`, and `takeChdPath` for `.lua`.
- **3.0 misses with `serve10`:** `norm`, a `g_sessLock` lookup, `read10`/`read10z`/`zipFor` under `g_zipLock`, and one `logf`.
- **Served files:** `indexZip10` (both locks), `transcode*`, `memFile`, and one `logf`.
- **Per frame:** `hkPlaysetChar` (UNVERIFIED frequency).
- **Per choreography start:** `hkChoStart`.

### Constraints any behaviour-preserving refactor must respect (verified; C-numbers are cited above)
- **C1 SEH.**
  - Functions with `__try`: `safeCopy` :95, `stackOf` :1261/:1264, `hkHdrMake` :1416, `hkAssetDef` :2056, plus `afterCallSafe` :2249 and `ourRetSafe` :2256 outside the slice.
  - None of them may get objects with destructors (C2712), or a C++ `try` (C2713).
  - The build is CMake defaults: `/EHsc`, `MSVC_RUNTIME_LIBRARY MultiThreaded`, no `/EHa` (CMakeLists.txt).
- **C2 Calling conventions.**
  - thiscall is emulated as `__fastcall(self, void* edx, …)`.
  - cdecl detours: `hkMunHit, hkHeapFree, hkAvSpawned, hkPlaceToy, hkFiberSwitch, hkOctLoad, hkFrameSync`.
  - `rt(0x00528A70)` is cast to a float-returning fastcall (:1544).
  - Keep return widths.
- **C3 Install order and timing.** Each `hook()` enables immediately. Code relies on that (:4341 "AssetOpen is live already").
- **C4 Reverse-engineered VAs and offsets.** Keep them verbatim, including :1401's `100 / 4`.
- **C5 Lock nesting observed (no inversion found):**
  - `g_zipLock → g_sessLock` (`indexZip10`).
  - `g_sh10Lock → g_zipLock` (`shader10` → `gen30::generate` → `read10s`). `g_sh10Lock` is also held across D3DCompile and engine program registration.
  - `sceneSettingsIO.mx → g_zipLock`; `lang30.mx → arc30 cipherLock` (I/O under it).
  - `g_dbLock → g_zipLock`, `g_dbLock → g_sessLock`, and `g_dbLock` held across engine opens (npcrt `hkInclude :298` → `prepare`, :250/:265/:267/:275).
  - `g_defsMx` held across an engine open (A-38).
  - `g_logLock` is a leaf, taken under `g_btMx`, `g_postMx`, `g_tdWatchLock`, `g_defsMx`, `sceneSettingsIO.mx`, `g_sh10Lock`, `g_zipLock` (gprt :434) and `g_dbLock`.
- **C6 Fibers.** Resource Loader fibers run on the frame thread (:2093–2097). `std::mutex` and `thread_local` are per thread. Two plugin locks are held across engine calls: `fallbackDef` and npcrt `prepare`. Whether those calls yield is UNVERIFIED.
- **C7 One TU, mid-file includes.**
  - `npcrt.hpp` (:621) uses `logf, hook, rt, g_cfg, read10s, read30x, read30, g_open, g_sessLock, g_served`.
  - `vfxrt/gprt/bldrt/caprt.hpp` (:4092–4095) use `logf, hook, rt, g_cfg, safeCopy`. `gprt` also uses `g_sessLock, g_zipLock, zipFor, g_screens10, g_movies10`.
- **C8 Log text.**
  - Nothing in the repo parses `di1compat.log`; only README.md:114 mentions it. The external research tree is absent, so external consumers are UNVERIFIED.
  - Keep lines byte-identical, CRLF (text-mode FILE), and the per-line flush.
- **C9 Exceptions.** The hook-boundary catches are listed in A-39. No exception may unwind into engine frames.
- **C10 Fatal `*_s` overflow.** With the static CRT there is no `_set_invalid_parameter_handler` anywhere in the plugin (grep). `hkWatson` (:3412, :4305) hooks only the exe's `__invoke_watson`, so a plugin `sprintf_s`/`strcpy_s` overflow terminates the process.
- **C11 `min`/`max` macros** are live in this TU (A-49).
- **C12 Offline coverage.** octtest does not compile this file (A-N1).
- **C13 Fault cost and the VEH invariant.**
  - A first-chance AV that a `__try` handles is not fatal, because Pentane's VEH logs and continues the search. It does cost a symbolised Pentane entry (counted by `harness.ps1:133`) plus one of CrabeLoader's 3 reports. That is why the plugin pre-checks with `sys::readable`.
  - A fault inside `avHandler`/`watchHandler`, or anything they call, re-enters the handler chain and is the fatal case. Any refactor of `stackOf`/`afterCall`/`readable`/`logf` must keep them fault-free on every input a VEH can pass.

---

## Inventory

### Functions in lines 1–2210 (all ranges checked mechanically with an awk scan; every row matched)
| Function | Lines | LOC | What it does | Hot/cold |
|---|---|---|---|---|
| logf | 70–84 | 15 | Timestamped, locked, flushed log line | Per served/missing open (recommended); every open plus trace hooks (code defaults) |
| safeCopy | 87–102 | 16 | Copy an engine C string after a 1-byte readability check | Every AssetOpen (:3557); per frame via `hkPlaysetChar`; probes |
| rt | 106 | 1 | Rebase a static VA | Trivial |
| hook | 110–128 | 19 | Verify prologue, create and enable a MinHook detour | Startup |
| loadConfig | 239–368 | 130 | Parse cfg (105-key chain); log 31 keys | Startup |
| zipFor | 376–389 | 14 | Lazily open and cache a 1.0 zip (caller holds `g_zipLock`) | Per candidate in read10z; per served file via indexZip10 |
| read10 | 395–403 | 9 | Normalise, read10z, Cho-alias retry | Per 3.0 miss (serve10) plus transcode sub-reads |
| read10z | 405–451 | 47 | Candidate archive search plus extract, under `g_zipLock` | Same as read10 |
| memFile | 455–469 | 15 | Build an engine Genie::MemFile copy | Every served or rewritten open |
| endsWith | 472–476 | 5 | Case-insensitive suffix | About 8 per open; about 20 per transcode fall-through |
| norm | 478–484 | 7 | `\`→`/`, lower-case, strip one `assets/` (not idempotent) | Per miss; 2–4 per transcode call |
| read10s | 486–495 | 10 | read10 into a std::string | Per transcode sub-read |
| read30 | 501–514 | 14 | 3.0 asset through the engine's open | fallbackDef (first miss), psdna30, npcrt |
| read30x | 519–527 | 9 | 3.0 archive member via arc30 (re-reads the index) | Per render donor/.shd read; lang30 |
| lang30 | 531–549 | 19 | Cached 3.0 key hashes per language | Once per language |
| sceneSettingsIO | 572–586 | 15 | Parse both `.ssf` once; attach to IO | Per world render (cached) |
| indexZip10 | 590–606 | 17 | Index full-path members of a 1.0 archive | Per served file (takes both locks even when already indexed) |
| padSmallDxt | 610–619 | 10 | DDS tiny-DXT header fix | Per served `.tbody` |
| transcodeAssetRender | 625–667 | 43 | World materials/buffers → 3.0; store in session | Per served `.oct` |
| has30 | 672–677 | 6 | 3.0 dir or zip exists (≤2 syscalls) | Per motion clip template |
| transcodeCho | 679–708 | 30 | Choreography passes | Per served `.oct` |
| surfaceTable30 | 718–732 | 15 | Copy the engine surface table | Per served `.oct` (surfaces10) |
| transcodeOct | 734–758 | 25 | Ordered `.oct` pipeline | Per served `.oct` |
| missionGraph30 | 767–789 | 23 | ForceActivateFirstTime on story openers | Per served `__missiongraph.lua` |
| particleShaders | 795–819 | 25 | Transcode a particle `.mtb`; return shader pairs | Per served `.particledata` |
| isSpark10 | 822–849 | 28 | Memoised FastCoin.IsSpark test | Per expendable group (memoised) |
| transcode | 853–1112 | 260 | Format dispatcher | Per served 1.0 file except `.tbody` |
| memtestMatch | 1114–1122 | 9 | Comma-list substring match | Only with memtest |
| readStream | 1126–1138 | 13 | Read and destroy an engine stream | Cold |
| copyToMemFile | 1141–1147 | 7 | readStream → memFile | Cold (memtest) |
| takeChdPath | 1158–1162 | 5 | Is this the pending CHD path? | Every non-write `.lua` open |
| hkLoadChdCallback | 1163–1173 | 11 | Mark a `.lua` for CHD compile | Always; per CHD load |
| hkPlaysetChar | 1183–1191 | 9 | Non-3.0 playsets keep the selected character | Always; likely per frame |
| hkActorExists | 1197–1205 | 9 | Log actor-exists queries; hide_ps | trace |
| hkFigureOk | 1211–1217 | 7 | Log figure check (substitutes a null `code`) | trace |
| readable | 1228–1241 | 14 | VirtualQuery-only readability | Stack walks (diagnostics) |
| afterCall | 1242–1252 | 11 | Is a stack word an exe return address? | Diagnostics |
| stackOf | 1254–1267 | 14 | Append validated exe return addresses | Diagnostics |
| avHandler | 1268–1278 | 11 | VEH: log the first AV with a stack | Every first-chance exception (early exit) |
| watchHandler | 1284–1301 | 18 | VEH: log DR0 write hits (unbounded walk) | Every exception once armed |
| armWatch | 1302–1325 | 24 | Set DR0/DR7 in all threads | Once (trace) |
| watch | 1326–1333 | 8 | Register VEH, arm, wait ≤5 s | Once (trace) |
| hkReaderList | 1337–1345 | 9 | Log reader item list on change | trace |
| hkIgpName | 1350–1354 | 5 | Arm watchpoint on the figure slot | trace |
| hkReaderItemOk | 1357–1364 | 8 | Log reader item gate on change | trace |
| hkFigurePlayset | 1367–1374 | 8 | Log `Gateway[sku].playset` | trace |
| hkAssetInst | 1380–1386 | 7 | Log SubNetwork instancing (uncapped) | trace |
| hkInst | 1394–1406 | 13 | Skip instancing on a dead root (behaviour guard) | trace |
| hkHdrMake | 1413–1424 | 12 | Log runtime `As*` headers with stack | trace |
| hkStartLookup | 1432–1439 | 8 | Log start lookup (20) | trace / spawntrace10 |
| hkStartQuery | 1445–1452 | 8 | Log start query (12) | trace / spawntrace10 |
| hkPost | 1461–1470 | 10 | Log matching event posts (3 each) | trace |
| hkLevelTick | 1474–1481 | 8 | Log level loader state changes | trace |
| hkLoadDone | 1485–1494 | 10 | Log load-done result changes | trace |
| hkAvFinal | 1497 | 1 | Log AvatarSpawning finalize | trace |
| hkAvRemove | 1498–1505 | 8 | Log avatar removal with stack | trace |
| tdReadable / tdRd / tdProt / tdStatic | 1515 / 1516 / 1517–1521 / 1522 | 1/1/5/1 | Probe helpers | Probes |
| hkClipEval | 1542–1584 | 43 | Clip clock statistics | breaktrace (per frame per processor) |
| hkChoReady | 1591–1604 | 14 | Log audio-sync wait changes | breaktrace |
| hkBtPlay | 1610–1621 | 12 | Log cho play (audio-synced) | breaktrace |
| hkChoStart | 1624–1632 | 9 | Start the 1.0 cinesync sound; optional log | Default on |
| hkLayApply | 1641–1663 | 23 | Log layout attachments (can overflow, A-37) | breaktrace |
| hkLayGate | 1667–1672 | 6 | Log layout gate | breaktrace |
| btClipBound | 1682–1705 | 24 | Count clip outputs reaching nodes | breaktrace |
| hkClipSet | 1706–1716 | 11 | Log clip setup | breaktrace |
| hkMusicPush | 1720–1728 | 9 | Log music cue pushes (uncapped) | breaktrace |
| hkLayBind | 1732–1736 | 5 | Log attachment binds | breaktrace |
| hkMunHit | 1740–1748 | 9 | Log exploder hits | breaktrace |
| hkExplode | 1752–1764 | 13 | Log explode data | breaktrace |
| tdObj | 1766–1776 | 11 | Describe an object | teardowntrace |
| tdEntry | 1778–1785 | 8 | Node handle → hierarchy entry | teardowntrace |
| tdEntryLog | 1786–1794 | 9 | Log a hierarchy entry | teardowntrace |
| hkLayoutDtor | 1807–1844 | 38 | Log Layout dtor; TLS context | teardowntrace |
| hkPlBind | 1849–1864 | 16 | Record bound placement nodes | teardowntrace |
| hkNodeDtor | 1865–1891 | 27 | Log node dtors / early frees | teardowntrace (every node dtor) |
| hkEntryRel | 1892–1902 | 11 | Log entry release in a Layout dtor | teardowntrace |
| tdBlock | 1910–1923 | 14 | Allocation-tracker lookup | teardowntrace |
| hkEntryFree | 1924–1935 | 12 | Log entry free in a Layout dtor | teardowntrace |
| hkTrackFree | 1939–1950 | 12 | Log tracker deletes | teardowntrace |
| hkHeapFree | 1954–1962 | 9 | Log heap frees | teardowntrace (every engine free) |
| hkChoHas | 1970–1977 | 8 | Log 'Cho Controls' queries (6) | trace |
| hkAvCreate | 1980–1985 | 6 | Log AvatarSpawning create | trace |
| hkAacExec | 1990–1996 | 7 | Log avatar AsyncActorCreator execute | trace |
| hkAvSpawned | 2001–2007 | 7 | Log avatar spawn callback | trace |
| fallbackDef | 2020–2049 | 30 | Lazy Cho DefID → FileName map (lock across engine read) | First asset-def miss builds; each miss looks up |
| hkAssetDef | 2051–2063 | 13 | Answer asset-def misses from the fallback | Always |
| hkChoMake | 2067–2075 | 9 | Log toy/spawn cho creation (20) | trace |
| hkPlaceToy | 2078 | 1 | Log PlaceToy callback | trace |
| fiberRole | 2101–2107 | 7 | Fiber → scheduler role | loadprof10 |
| fiberMsNow | 2108–2111 | 4 | Cumulative ms per role | loadprof10 |
| hkFiberSwitch | 2114–2121 | 8 | Accumulate per-fiber time | loadprof10 |
| fiberProf | 2125–2132 | 8 | Per-fiber counters (thread_local[8]) | loadprof10 |
| hkFileWait | 2137–2145 | 9 | Time file waits | loadprof10 |
| hkOctLoad | 2151–2176 | 26 | Log per-`.oct` load timing | loadprof10 |
| hkFrameSync | 2178–2208 | 31 | Frame timing, fps line | fpslog10 \|\| loadprof10 (per frame) |

Types: `SemVer` :49, `PluginInformation` :50, the anonymous `g_cfg` struct :131, `BtClock` :1539, `FiberProf` :2124. `qpcMs` is declared :2091 and defined :2218 (outside the slice).

### Generic helpers (for cross-file duplicate matching)
| Name | Signature | Line | Notes |
|---|---|---|---|
| logf | `static void logf(const char* fmt, ...)` | 70 | Overloads C `logf(float)`; cast at :2719 |
| safeCopy | `static void safeCopy(char* out, size_t cap, const char* src)` | 87 | Checks first byte only; `cap ≥ 13` |
| rt | `static uintptr_t rt(uint32_t staticVa)` | 106 | Base 0x400000 |
| hook | `static bool hook(const char*, uint32_t, const char*, void*, void**)` | 110 | Passed to `audio::install` (:4338) |
| zipFor | `static mz_zip_archive* zipFor(const std::string& rel)` | 376 | Caller holds `g_zipLock`; caches nullptr |
| read10 / read10z | `static void* read10(const char*, size_t*, std::string*)` / `read10z(const std::string&, …)` | 395 / 405 | Caller `mz_free`s |
| memFile | `static int* memFile(const void* data, size_t n)` | 455 | Engine MemFile |
| endsWith | `static bool endsWith(const char*, const char*)` | 472 | Case-insensitive |
| norm | `static std::string norm(const char* path)` | 478 | Not idempotent for `assets/assets/` |
| read10s | `static bool read10s(const std::string&, std::string&)` | 486 | |
| read30 | `static bool read30(const std::string&, std::string&)` | 501 (re-declared 2019) | Engine open via `g_open` |
| read30x | `static bool read30x(const std::string&, std::string&)` | 519 | `"3:dir/name/member"` |
| lang30 | `static const std::unordered_set<uint32_t>& lang30(const std::string&)` | 531 | |
| indexZip10 | `static void indexZip10(const std::string&)` | 590 | zip → sess |
| padSmallDxt | `static void padSmallDxt(std::string&)` | 610 | |
| has30 | `static bool has30(std::string)` | 672 | |
| surfaceTable30 | `static std::vector<std::string> surfaceTable30()` | 718 | |
| memtestMatch | `static bool memtestMatch(const char*)` | 1114 | |
| readStream / copyToMemFile | `static bool readStream(int*, std::string&)` / `static int* copyToMemFile(int*, size_t*)` | 1126 / 1141 | |
| readable | `static bool readable(const void*, size_t)` | 1228 | VirtualQuery-only (≠ `sys::readable`) |
| afterCall | `static bool afterCall(uintptr_t)` | 1242 | Image end 0x2100000 |
| stackOf | `static void stackOf(const uint32_t*, char*, size_t, int, int max = 24)` | 1254 | No capacity guard |
| watch | `static void watch(uintptr_t)` | 1326 | |
| tdReadable / tdRd / tdProt / tdStatic | — | 1515–1522 | tdStatic image end 0x209B000 |
| tdObj / tdBlock | `static void tdX(char*, size_t, uintptr_t)` | 1766 / 1910 | |
| fiberRole / fiberMsNow / fiberProf | — | 2101 / 2108 / 2125 | |

### Globals and statics in 1–2210
Thread notes: hkOpen helpers run on whichever engine threads call AssetOpen (:1155; `noteOpenThread`); the exact set is UNVERIFIED. Probe-hook threads are UNVERIFIED unless a comment states them.

| Name | Line | Type | Writers | Readers / threads | Guarded? |
|---|---|---|---|---|---|
| Pentane_PluginInformation / DependencyCount | 60 / 64 | extern "C" data | none | Pentane | n/a |
| g_logLock | 67 | CRITICAL_SECTION | init :4100 | all | is the lock (recursive) |
| g_log | 68 | FILE* | :4111 | all via logf | write-once before hooks |
| g_base | 105 | uintptr_t | :4112 | all | write-once |
| g_cfg | 131–237 | anonymous struct, 105 fields | `loadConfig` only | everywhere | write-once at startup |
| g_zipLock | 373 | std::mutex | — | VFS users, gprt | is the lock |
| g_zips | 374 | `std::map<std::string, mz_zip_archive*>` | `zipFor` :387 | hkOpen threads | `g_zipLock`; **unlocked `size()`** at :3666 (counts failures too) |
| g_choAlias | 391 | `cho::Aliases` | `.add` from the datamap lambda :1086 | read10 | internal mutex (cho.hpp:877) |
| g_open | 498 | trampoline | `hook()` :4183 (MH_CreateHook, success only) | read30, hkOpen, npcrt | write-once |
| g_gameDir | 518 | std::string | :4113 | lang30, read30x, has30 | write-once |
| lang30 `mx`, `cache` | 533–534 | mutex, map | lang30 | transcodes | own mutex (held across I/O) |
| g_sessLock | 558 | std::mutex | — | hkOpen, transcodes, gprt, npcrt | is the lock |
| g_served | 559 | `std::map<std::string,std::string>` | :655–658, :817, :1038, :3712, npcrt.hpp:276 | hkOpen | `g_sessLock` |
| g_over10 | 560 | map | indexZip10 :603–604, :4343 | hkOpen, particleShaders | `g_sessLock` (:4139 `size()` is before hooks) |
| g_indexed10 | 561 | std::set | indexZip10 :596 | — | `g_sessLock` inside `g_zipLock` |
| g_movies10 / g_screens10 | 562 / 563 | std::set | hkOpen :3722 / :3725 | gprt :122, :168, :280 | `g_sessLock` |
| g_render | 564 | render::Table | :4152 | transcodes, shader10 | write-once before hooks |
| g_vf10 | 565 | VFormats | :4173 | transcodes, shader10 | write-once before hooks |
| g_vf30 / g_tails10 | 566 / 567 | VFormats / map | `mergeVtxf` :4086–4087 (from hkOpen) | transcodes, shader10 (render thread) | **No** (boot-order assumption, A-17) |
| g_renderOk | 569 | bool | :4152 | transcodes | write-once |
| sceneSettingsIO `mx, done, ok, f10, f30` | 574–576 | mutex, bools, Ssf×2 | sceneSettingsIO | transcodes | own mutex |
| g_mer / g_merOk | 587 / 588 | mer::Table / bool | :4158–4164 | transcode | write-once |
| missionGraph30 `emptyUnlocks`, `add` | 769, 783 | const std::string | magic statics | transcodes | init-once |
| isSpark10 `m`, `memo` | 824–825 | mutex, map | isSpark10 | transcodes | own mutex |
| g_chdLock / g_chdPath | 1156 / 1157 | mutex / string | hkLoadChdCallback :1168/:1170 | hkOpen (takeChdPath) | `g_chdLock` |
| g_avLogged | 1224 | volatile LONG | avHandler (Interlocked) | any (VEH) | atomic one-shot |
| g_watchAddr / g_watchHits | 1282 / 1283 | volatile uintptr_t / LONG | watch :1329 (check-then-set) / watchHandler | VEH, armWatch | No / atomic |
| hkReaderList / hkReaderItemOk `last` | 1339 / 1359 | std::string | the hook | UNVERIFIED | **No** |
| g_instHeaders | 1393 | uint32_t | hkInst :1404 | hkHdrClone :3189 | **No** |
| g_postMx / g_postSeen | 1459 / 1460 | mutex / map | hkPost | UNVERIFIED | `g_postMx` |
| g_levelState / g_loadDoneLast | 1473 / 1484 | int | their hooks | UNVERIFIED | No |
| g_btMx, g_btFirst, g_btMoving, g_btClock, g_btReport, g_btReady | 1535–1541, 1590 | mutex, unordered maps/set, ULONGLONG | hkClipEval, hkChoReady | UNVERIFIED | `g_btMx` |
| t_tdDepth, t_tdNode, t_tdOuter, t_tdFrees | 1795–1798 | `__declspec(thread)` | teardown hooks | per thread, not per fiber | TLS |
| g_tdWatchLock / g_tdWatch | 1801 / 1802 | mutex / map | hkPlBind, hkNodeDtor | UNVERIFIED | `g_tdWatchLock` |
| g_defs / g_defsMx | 2017 / 2018 | leaked map* / mutex | fallbackDef | asset-def callers (fibers possible) | `g_defsMx` (held across an engine open) |
| g_frameTid / g_frameNo | 2089 / 2090 | volatile DWORD / LONG | hkFrameSync | hkOctLoad, hkOpen, sampler | volatile / atomic increment |
| kFiberRole | 2098 | const char* const[5] | — | — | const |
| g_fiberRole / g_fiberMs[5], g_fiberSince | 2099 / 2100 | volatile LONG / double | hkFiberSwitch | frame thread, sampler | volatile / **No** (A-45) |
| fiberProf `s` | 2127 | thread_local FiberProf[8] | fiberProf users | per thread | TLS |
| hkFrameSync statics | 2182–2184, 2197 | LARGE_INTEGER, double, int | hkFrameSync | main thread | single thread |
| Log-budget counters | 1283, 1412, 1431, 1444, 1640, 1666, 1678, 1731, 1739, 1751, 1800, 1969, 2050, 2066 (+ `g_btEvals` 1538) | volatile LONG | Interlocked | any | atomic |
| Trampolines (47 in the slice plus `g_missionTick` 2082) | 1154 … 2150 | function pointers | `hook()` at startup | their detours | write-once before enable |

---

## Outside-slice notes (for the verifiers of those ranges)
- hkOpen :3666 reads `g_zips.size()` unlocked (A-12).
- hkOpen :3650 copies `g_served` entries under the lock (A-16).
- hkOpen :3704–3707 copies and compares every `.tbody` (A-19).
- hkOpen :3699 takes both VFS locks per served file (A-11).
- `shader10` (:4027–4071) holds `g_sh10Lock` across `gen30::generate`, D3DCompile and engine program registration. Both the loader-thread world transcode and the render thread's `hkShaderByName` take that lock.
- npcrt `hkInclude :298` holds `g_dbLock` across `prepare`, which does engine opens (`read30`, `g_open` :267) and takes `g_zipLock` and `g_sessLock`.
- `arc30::readMember` re-reads and decrypts the index per call under the global `cipherLock` (A-15).
- `g_selfLo`/`g_selfHi` are set only in `profThread` (:2304–2305).
- The `watchHandler`-style unbounded read pattern also exists in `ourRetSafe` :2256, but under `__try` and on the DLL's own image, so it is effectively safe.
- Resolved from the report's list: with `audio10 && cineSync10` but no audio install, `hkChoStart` is safe. `audio::choreographyStarted` returns unless `ready()` (audio.hpp:2076).
- README.md:143 ("`Cfg` structure"); README.md:130 diagnostics list omits `locktrace10` (default on), `teardowntrace`, `vfx_trace`, `opacitylog10` and `fadeprobe10`. `fadeprobe10=2` is not log-only.

## Rejected on verification
- None. All 49 findings hold in substance. 21 needed corrections to line numbers, severity, claims or fixes, as noted in each finding.

# Plugin core, part 2 (di1compat.cpp 2210-4361: profiler, trace detours, GameDB hooks, hkOpen, shaders10 runtime, Pentane_Main) (`native/di1compat/di1compat.cpp`, `gprt.hpp`, `caprt.hpp`, `bldrt.hpp`, `npcrt.hpp`, `audio.hpp`, `sys.hpp`, `minhook/hook.c`, `third_party/pentane/Pentane.dll`)

Scope: I re-read di1compat.cpp 2020-4361 in full. I also read the out-of-slice code the findings depend on: `logf`/`safeCopy`/`hook`/`rt` 66-128, `g_cfg` + `loadConfig` 131-368, `zipFor`/`read10`/`read10z`/`memFile`/`endsWith`/`norm`/`read10s`/`g_open`/`read30` 370-514, session globals 551-606, `readStream` 1124-1147, the trace detours 1149-1505 (`hkInst`, `watch`/`armWatch`/`watchHandler`, `readable`/`afterCall`/`stackOf`/`avHandler`), and `hkChoStart` 1622-1632. Cross-checked against `gprt.hpp` (25-50, 460-489), `caprt.hpp` 52 and 202-218, `bldrt.hpp` 105-113, `npcrt.hpp` 250-285 and 411-422, `audio.hpp` 2074-2086 and 2134-2239, `sys.hpp` 151-197 and 664-672, `gen30.hpp` 543 and 565-631, `octx.hpp` 315-330, `minhook/hook.c` 263-300, 579-665 and 717-775, `CMakeLists.txt`, `di1compat.cfg.example`, README "Configuration", `harness.ps1`, and commits 684539b and 34cab5b. I disassembled the vectored exception handler in the bundled `third_party/pentane/Pentane.dll` (capstone, read-only; see B-N3).

The hook table was rebuilt mechanically: every `hook("` line was extracted and each detour was resolved to its definition line. I recounted the installed hooks per configuration by hand from the gates.

Verification: 49 findings checked: 24 confirmed, 23 corrected, 1 unverified, 1 rejected; 3 new.

Two facts that change several of the original claims:
- **`Pentane_Main` runs inside Pentane.dll's `DllMain`, under the loader lock, before the game's entry point.** This is stated in `audio.hpp:2137-2139` and commit 34cab5b. I confirmed it in Pentane.dll: the plugin-entry loop `0x101df000` is called at `0x101c2f6a`. Consequence: no engine code can run, and no file can open, while hooks are being installed (B-N1). This removes B-22 and changes B-01 and B-21.
- **Faults are NOT fatal via Pentane.** This corrects the original report's framing "Faults are fatal", which was based on the comment at 1225-1227 and commit 684539b.
  - The bundled Pentane.dll's vectored handler logs `=== Exception Caught! ===` with a SymInitialize/StackWalk64/SymFromAddr trace, then returns `EXCEPTION_CONTINUE_SEARCH` on every path. It never calls TerminateProcess or ExitProcess. Confirmed by my disassembly and independently by the coordinator (upstream Pentane `crash.cpp`, commit 48e3fbb).
  - A fault inside `__try` is therefore handled by its `__except`, and the game survives.
  - The real cost of a handled fault is a slow, symbolised Pentane log entry plus a CrabeLoader crash report, which uses one of CrabeLoader's 3 slots.
  - The 12:01 crash was di1compat's own `avHandler` faulting again inside the vectored handler (commit 684539b).
  - The stale comment is filed as B-N3, Low. The check-before-read policy stays justified by those costs.

## Findings
| ID | Title | Sev | Class | Where |
|---|---|---|---|---|
| B-02 | `hkOpen` is a 208-line god detour with implicit stream ownership | High | NEEDS-VERIFICATION | di1compat.cpp:3549-3756 |
| B-01 | `Pentane_Main` is a 262-line installer with implicit ordering | Med | SAFE-REFACTOR | di1compat.cpp:4098-4359 |
| B-N1 | `Pentane_Main` runs under the loader lock (undocumented constraint) | Med | DOC-ONLY | di1compat.cpp:4098-4359; audio.hpp:2137-2139 |
| B-03 | Diagnostic hook sets are on by default (trace without a cfg; lockTrace10 always) | Med | BEHAVIOUR-CHANGE | di1compat.cpp:134, 157, 4201-4228, 4270-4291, 3754 |
| B-04 | A real crash guard (`hkInst`) exists only under `trace` | Med | BEHAVIOUR-CHANGE | di1compat.cpp:1388-1406, 4209 |
| B-05 | `g_open` used outside its detour with no null check; AssetOpen `hook()` result ignored | Med | BEHAVIOUR-CHANGE | di1compat.cpp:4183, 3304, 506; npcrt.hpp:267 |
| B-06 | Session cache `g_served` never evicted; every hit copied under `g_sessLock` | Med | NEEDS-VERIFICATION | di1compat.cpp:3646-3661, 559 |
| B-07 | `linkShader` scans the whole shader array on every lookup, inside the render thread's critical section | Med | NEEDS-VERIFICATION | di1compat.cpp:3937-4006, 4077 |
| B-08 | Long lock scopes (`g_sh10Lock` across D3DCompile and disk I/O) and an undocumented lock hierarchy | Med | DOC-ONLY | di1compat.cpp:4029-4063, 3939-4005 |
| B-09 | Plugin locks versus engine fibers (constraint) | Med | DOC-ONLY | di1compat.cpp:2093-2097, 2122-2134 |
| B-10 | `thread_local` state shared by fibers (`sys::g_pkg10`, `g_fpScope`) | Med | DOC-ONLY | di1compat.cpp:3298-3310, 3362, 3083 |
| B-14 | Four copies of "drain an engine stream", already drifted | Med | NEEDS-VERIFICATION | di1compat.cpp:1126-1138, 501-514, 3559-3572, 3601-3608 |
| B-15 | Engine prototypes and VAs declared ad hoc at call sites | Med | SAFE-REFACTOR | 69 `rt(0x…)` literals in 2210-4361 |
| B-19 | Mid-file runtime includes depend on main-file statics; `caprt::g_base` shadows `::g_base` | Med | NEEDS-VERIFICATION | di1compat.cpp:4092-4095, 621; caprt.hpp:52 |
| B-11 | C++ exceptions can unwind into engine frames from `hkShaderByName` | Low | BEHAVIOUR-CHANGE | di1compat.cpp:4068-4079, 4052; gen30.hpp:543, 631 |
| B-12 | `compile10`: 64 KB stack buffer, out-of-bounds index on a negative `bufview10`, non-atomic cache write | Low | BEHAVIOUR-CHANGE | di1compat.cpp:3834, 3828, 3848 |
| B-13 | Engine reads in hooks are inconsistent with "check before read" | Low | DOC-ONLY | see entry |
| B-16 | Hidden re-entrancy through other plugin hooks | Low | DOC-ONLY | di1compat.cpp:2565, 3502, 3241 |
| B-17 | A GameDB load runs inside a `logf` argument list | Low | SAFE-REFACTOR | di1compat.cpp:3322, 3154 |
| B-18 | `logf` overloads C's `logf(float)` | Low | SAFE-REFACTOR | di1compat.cpp:70, 2719, 4307, 4333 |
| B-20 | Config fan-out and gating are spread out; two gates are inconsistent | Low | SAFE-REFACTOR | di1compat.cpp:4156-4164, 4200, 4245, 4307-4337 |
| B-21 | One MinHook thread freeze per hook (boot cost) | Low | NEEDS-VERIFICATION | di1compat.cpp:124-125; minhook/hook.c:263-267, 717-770 |
| B-23 | `g_recCs` is initialised only under shaders10 | Low | SAFE-REFACTOR | di1compat.cpp:4230, 3920 |
| B-24 | `missionTrace` (a log flag) changes the gameplay data path | Low | DOC-ONLY | di1compat.cpp:3599 |
| B-25 | renderdoc10: coupling, hard-coded path, detached thread | Low | DOC-ONLY | di1compat.cpp:4122-4129, 2671-2676 |
| B-26 | `hkZoneStr` returns a pointer into a static `std::string` after unlocking | Low | NEEDS-VERIFICATION | di1compat.cpp:3456-3462 |
| B-27 | Unsynchronised `g_zips.size()`, a misleading "archives open" count, `missWhy` built eagerly | Low | SAFE-REFACTOR | di1compat.cpp:3666 |
| B-28 | Logging work done regardless of budget or state | Low | NEEDS-VERIFICATION | di1compat.cpp:2687-2694, 2850, 2906-2912, 2955-2966, 3037-3050 |
| B-29 | `hkNavUpdate` change detection is global, not per component | Low | DOC-ONLY | di1compat.cpp:2967-2973 |
| B-30 | `lockTraceRoom` counters never saturate | Low | SAFE-REFACTOR | di1compat.cpp:2746 |
| B-31 | currency10 allocates per call; `hkHudCurrency` makes several engine calls per update | Low | SAFE-REFACTOR | di1compat.cpp:2547-2618 |
| B-32 | `profThread` (145 lines; loadprof10) | Low | NEEDS-VERIFICATION | di1compat.cpp:2295-2439 |
| B-33 | Two `readable()` implementations | Low | DOC-ONLY | di1compat.cpp:1228-1241; sys.hpp:173-197 |
| B-34 | Utility duplication | Low | SAFE-REFACTOR | see entry |
| B-35 | Dead locals and dead stores | Low | SAFE-REFACTOR | di1compat.cpp:4041-4046, 4009 |
| B-36 | Resource handling in `registerProgram10` and `shader10` | Low | BEHAVIOUR-CHANGE | di1compat.cpp:3865-3871, 4054-4056 |
| B-37 | `linkShader`'s return value ignored; "records full" logs on every lookup | Low | DOC-ONLY | di1compat.cpp:3966, 4019, 4077 |
| B-38 | `Pentane_Main` does not check Win32 results | Low | BEHAVIOUR-CHANGE | di1compat.cpp:4102-4130 |
| B-39 | Version string mismatch | Low | BEHAVIOUR-CHANGE | di1compat.cpp:4114, 63 |
| B-40 | `_setmaxstdio(8192)` and `nullptr` cached permanently in `zipFor` | Low | DOC-ONLY | di1compat.cpp:4130, 376-389 |
| B-41 | `hkNormPath` underflows when `size == 0` | Low | BEHAVIOUR-CHANGE | di1compat.cpp:3437 |
| B-42 | Short-read handling differs between `chdForLua` and `tips30` | Low | DOC-ONLY | di1compat.cpp:3200, 3536 |
| B-43 | Readability: cryptic names, magic numbers, name reuse | Low | SAFE-REFACTOR | see entry |
| B-44 | Inline-assembly thunks for custom calling conventions | Low | DOC-ONLY | di1compat.cpp:3878-3907 |
| B-45 | `hkZoneCurrency` CString out-parameter semantics | Low | DOC-ONLY | di1compat.cpp:2573 |
| B-46 | Fixed per-open costs in `hkOpen` | Low | SAFE-REFACTOR | di1compat.cpp:3557-3626 |
| B-47 | 1.0 inflate under the global `g_zipLock` | Low | NEEDS-VERIFICATION | di1compat.cpp:3692-3695, 405-451 |
| B-48 | Unlocked function statics in hooks whose thread is unverified | Low | DOC-ONLY | di1compat.cpp:2672, 2692, 2967-2968 |
| B-49 | Three different "is this a 1.0 zone" tests | Low | DOC-ONLY | di1compat.cpp:2659-2664, 2559-2566; gprt.hpp:32-36 |
| B-N2 | The trace watchpoint machinery (DR0) traps every write for the session and scans the stack unchecked inside a VEH | Low | NEEDS-VERIFICATION | di1compat.cpp:1280-1333, 1350-1354, 4207 |
| B-N3 | Stale comment: "Pentane's [handler] ends the game" on a first-chance AV | Low | DOC-ONLY | di1compat.cpp:1225-1227 |

---

### B-02 `hkOpen` is a 208-line god detour with implicit stream ownership
- **Where:** `native/di1compat/di1compat.cpp:3549-3756`
- **Severity / category / class:** High · God function, error handling · NEEDS-VERIFICATION
- **Evidence:** About 15 interceptors are chained in a fixed order:
  - vtxf 3558-3573
  - tips 3575-3576
  - Play Sets 3578-3594
  - zone list / challenge / beacon 3597-3619
  - bitmasks stack 3620-3625
  - CHD 3626
  - `di1sys/` 3627
  - memtest 3628-3633
  - block10 3634-3644
  - session cache 3645-3662
  - overlay / UI scope / challenge path 3666-3674
  - `_pc_con.dct` 3678-3681
  - `_holo.mtb` 3686-3689
  - lipsync 3690-3696
  - transcode / `.tbody` / `.mtc` 3698-3720
  - movie and screen tracking 3722-3726
  - prof stacks 3734-3741
  - logs 3743, 3754

  Mechanical counts: 12 `return`, 8 `flags & 1`, **13** `endsWith(`. Some branches destroy `r` (`vt[11]` at 3568/3571/3608, `readStream` in `tips30`/`chdForLua`/Play Sets) and then reopen with the same arguments (3572, 3593, 3618, `tips30` 3536). Others return `r`, a `MemFile`, or `copyToMemFile(r)` (3630).
- **Hot path?:** code defaults: every file open (serve branches skipped, `serve10` off); recommended config: every file open, plus the serve path for every miss.
- **Why it matters:** Who owns `r` (destroyed, returned, or reopened) is implicit per branch, so a split can double-destroy or leak an engine stream, or change which branch wins.
- **Fix (behaviour-preserving):**
  - Split into `static int* tryX(const OpenCtx&, int*& r)` interceptors called in today's order.
  - Each one documents "consumes r", "returns r" or "reopens".
  - Give `flags & 1` a name (`kOpenWrite`).
- **Refactor risk:**
  - Keep the branch order: `zones10()` before `zonelist30`/`beaconMap30` (3611), zone list before CHD, CHD before `di1sys/`.
  - Keep the second `g_open` with the same `a5`.
  - Keep the `served …`, `open … -> MISSING`, `memtest` and `prof` log text byte-identical.
  - Keep the try/catch around transcode (3703-3720): nothing may unwind into the engine.
  - Keep the `stackOf` calls bounded.
  - Also covered by the gameplay-ui verified area.
- **Verification:** CORRECTED: 13 `endsWith` calls, not 10. Everything else confirmed.

### B-01 `Pentane_Main` is a 262-line installer with implicit ordering
- **Where:** `native/di1compat/di1compat.cpp:4098-4359`
- **Severity / category / class:** Med · God function, ordering · SAFE-REFACTOR
- **Evidence:** One function does all of the following:
  - log rotation and open 4101-4111
  - base and directory discovery 4112-4120
  - `loadConfig` 4121
  - RenderDoc 4122-4129
  - `_setmaxstdio` 4130
  - resident zip index 4134-4140
  - `slurpFile` lambda and table loads 4141-4166
  - shaders10 init 4167-4176
  - VEH 4178 and MinHook init 4179
  - 104 inline `hook()` calls (4183-4357) under about 28 gate expressions, counting "unconditional"
  - config fan-out 4156-4164, 4307-4310, 4333-4337
  - subsystem installers 4316, 4338, 4352-4354
  - `g_over10` seeding 4340-4344

  The body is long but almost entirely linear: a sequence of gated calls.
- **Why it matters:** The ordering invariants are implicit, the function can't be tested piecemeal, and the gate expressions are hard to audit. It is long but not complex, so it does not block other refactors. Downgraded from High.
- **Fix (behaviour-preserving):**
  - Extract phases into named functions called in the same order: `openLogRotating`, `resolveDirs`, `initRenderDoc`, `indexResident10`, `loadTables`, `initShaders10`, `installCoreHooks`, `installTraceHooks`, …
  - Optionally use per-group static tables `{name, va, prologue, detour, &orig}` iterated in the current order, each with its gate expression kept verbatim.
- **Refactor risk (order to keep):**
  - `InitializeCriticalSection(&g_recCs)` before the shader hooks (4230).
  - `g_merOk` set (4158) before the registry hook (4268-4269).
  - `g_d3dCompile`, `g_shaderCache`, `g_vf10` (4167-4176) before the shader hooks.
  - `g_profPath` before `CreateThread` (4301-4302).
  - `sx::g_exe` and `sx::init` before the Slang hooks (4309-4311).
  - `audio::logfn` and `audio::cfg` before `audio::install` (4333-4338).
  - `g_over10` seeding before the vfx package hook (4341-4345).
  - The tutorial10 byte check before its hook (4325-4327).
  - `avHandler` registered before any hook (4178).
  - Hook name strings verbatim (logged as `hook %s @ %08X ok (0)`).
  - The function runs under the loader lock (B-N1), so no extracted phase may wait on another thread.
  - "AssetOpen hooked first" matters only for log line order: no engine thread exists yet (B-N1).
- **Verification:** CORRECTED:
  - Severity High → Med (linear code, does not block other refactors).
  - The reason for "AssetOpen first" corrected: not a concurrency need.
  - Gate count about 28.

### B-N1 `Pentane_Main` runs under the loader lock (undocumented constraint) — NEW
- **Where:** `native/di1compat/di1compat.cpp:4098-4359`; `native/di1compat/audio.hpp:2137-2139`; commit 34cab5b
- **Severity / category / class:** Med · Hidden constraint, boot latency · DOC-ONLY
- **Evidence:**
  - `audio.hpp:2137-2139`: "Pentane_Main runs inside Pentane.dll's DllMain … i.e. under the loader lock, where a new thread cannot start until DllMain returns". Commit 34cab5b fixed a 20 s stall this caused.
  - In the bundled Pentane.dll, its init registers its VEH at `0x101c2f17` and then calls the plugin-entry loop `0x101df000` at `0x101c2f6a`.
  - Pentane.dll is a static import of the exe (`pentane_install.py`), so this runs before the game's entry point.
  - Work done inside the lock today:
    - `LoadLibraryExA` of renderdoc.dll (4123)
    - `LoadLibraryA("d3dcompiler_47.dll")` (4168)
    - `indexZip10` over every `startup*.zip`, which opens the archives and indexes every member (4134-4140)
    - two table parses (4152, 4158)
    - a zip read of `shaders/vformat.vtxf` (4173)
    - MinHook's snapshot and freeze for each of about 82-85 hooks
  - `CreateThread(profThread)` (4302) and audio's host thread can start only after `DllMain` returns.
- **Why it matters:**
  - Any wait on another thread added to `Pentane_Main` deadlocks or times out, as audio10 did before 34cab5b. `watch()` (5 s wait, 1332) must never become reachable from boot.
  - Loading DLLs inside `DllMain` is against Microsoft's `DllMain` guidance. It works for these two DLLs today, but a refactor that adds a DLL whose init creates or waits on threads can hang the launch.
  - All of this I/O delays process start.
  - It also settles a question other findings raised: no engine code runs during installation, so hook enable order and seeding order are not observable by the game (B-21, B-22).
- **Fix (behaviour-preserving):**
  - Document it at the top of `Pentane_Main` and in the refactor rules.
  - Keep the work where it is: moving it out of `DllMain` changes timing and what early opens see.
- **Refactor risk:** No `std::thread::join`, `std::async().get()`, or `WaitForSingleObject` on a new thread anywhere reachable from `Pentane_Main`.
- **Verification:** NEW: verified from audio.hpp:2137-2139, commit 34cab5b and the Pentane.dll call sequence.

### B-03 Diagnostic hook sets are on by default (trace without a cfg; lockTrace10 always)
- **Where:** `native/di1compat/di1compat.cpp:134` (`bool trace = true;`), `:157` (`bool lockTrace10 = true;`), installs 4201-4228 (26) and 4270-4291 (17), the per-open log 3754, `logf`'s `fflush` 82; gprt.hpp:462-464
- **Severity / category / class:** Med · SE practice, perf, config · BEHAVIOUR-CHANGE (any fix changes defaults)
- **Evidence:**
  - **No cfg:** 85 hooks are installed:
    - `Pentane_Main` 63 (62 if `di1compat_mer.txt` is missing), npcrt 5, gprt 12, bldrt 1, caprt 4;
    - audio 0, because its gate needs `install10`.
  - **No cfg, trace probes:**
    - All 26 trace probes are installed.
    - `if (g_cfg.trace || !r) logf(...)` (3754) writes one flushed line per open: `GetLocalTime`, `vfprintf`, `fflush` under `g_logLock`.
    - `hkActorExists` (1203), `hkFigureOk` (1215), `hkFigurePlayset` (1372), `hkAssetInst` (1384) and `hkMkStart` (3154) log **every** call with no budget.
    - `hkPost` (1461) does a `safeCopy` and up to 6 `strstr` per event post.
    - `hkHdrClone` (3169) calls the engine resolver `FUN_004B46D0` once more per header clone (3175).
    - "playset figure slot (trace)" (4207) arms a hardware watchpoint (B-N2).
  - **Recommended config:** 82 hooks are installed:
    - `Pentane_Main` 44, npcrt 5, audio 16, gprt 12, bldrt 1, caprt 4;
    - 18 of them are log-only lockTrace10 probes (17, plus gprt's "vehicle control takeover (vehicle10 probe)").
- **Which lockTrace10 hooks run per frame (recommended config):**
  - `hkNavUpdate` (MissionNavData update, an avatar component; most likely per frame per avatar). Cost per call: 2 `sys::readable` (QueryWorkingSetEx, about 0.4 µs each per sys.hpp:176-179) plus one engine screen lookup `FUN_0154FED0` (2963). That work continues after the 300-line budget is spent.
  - `hkLoadTick` (LoadScreenActive node update: mutex plus `std::set` lookup per call, 2850).
  - Possibly `hkBeaconLookup` (2 `safeCopy`, mutex, `std::string` per call; callers are focus-point, giver and mission-target updates, rate UNVERIFIED).
  - The other 14 are event-driven: lock/unlock, contexts, cho play/event, messages, monitor fire, net-sync Link_In, colvis load/apply, compass track, mission progress.
  - Total cost is a few µs per frame. It is not measurable at the README's ~150 fps, so this is not a High CPU finding.
- **Hot path?:**
  - Code defaults: per open (a flushed log line), per event post, per header clone, per SubNetwork instance. Unbounded logging on instance-heavy loads.
  - Recommended config: only the small per-frame probes above, plus misses still logged (3754).
- **Why it matters:**
  - Debug probes ship enabled.
  - Without a cfg the log grows with every open and instance.
  - `hkNetSync` calls engine virtual getters to log (2864-2865), but only while the 4000-line `g_gateLines` budget lasts. It walks the vbtable the same way `FUN_006ec4f0` itself does (2863).
  - The watchpoint writes DR0 only. DR7 changes only L0/RW0/LEN0 (1316), so a debugger loses its slot 0, not every breakpoint.
- **Fix:**
  - Defaults are behaviour, so document this and raise it as a product decision. The README already says `trace` default 1, "Set to 0 for play".
  - A compile-time `DI1_DIAGNOSTICS` switch defaulting ON keeps the binary identical.
- **Refactor risk:** Changing defaults changes the installed hooks and the log output.
- **Verification:** CORRECTED:
  - Severity High → Med: the per-frame cost under the recommended config is microseconds.
  - DR0 scope corrected (slot 0 only; only threads alive at arm time).
  - `hkNetSync`'s virtual calls are bounded by the budget.
  - Hook counts confirmed: 85 and 82.

### B-04 A real crash guard (`hkInst`) exists only under `trace`
- **Where:** `native/di1compat/di1compat.cpp:1388-1406` (detour 1394), install 4209 inside `if (g_cfg.trace)` (4201); `hidePs` at 1202 (installed 4202)
- **Severity / category / class:** Med · Hidden coupling, latent crash · BEHAVIOUR-CHANGE
- **Evidence:**
  - `hkInst` really guards; it does more than log. It resolves `self + 0xc` through `FUN_004B46D0` (1397). If that is null, it logs `instancing template %p with a dead root handle … : skipped` and **returns 0 without calling the original** (1398-1402). The caller `FUN_007734f0` handles a failed instance (comment 1390).
  - Otherwise it records `g_instHeaders` (used only by `hkHdrClone`'s log) and calls through.
  - The guard was added for an in-game AV in `FUN_004f5900` (NULL+8, 2026-10-12, a 1.0 prop's 'Anchor'). That trigger was fixed at the data level: octx.hpp:320-323 no longer turns SubNetworkRef -1 pieces into assets.
  - With `trace = 0` (recommended) the hook is not installed, so a template with a dead root that reaches instancing faults instead of being skipped.
  - README "Known issues" lists "A rare crash has been seen while the vault props are instanced". Whether that is this AV is UNVERIFIED. To settle it, reproduce with `trace = 1` and grep di1compat.log for "dead root handle".
  - `hidePs` is an experiment documented as trace-only (cfg comment at 152).
- **Why it matters:** A behaviour-changing safety net hides behind a log flag. The recommended config runs without it.
- **Fix:**
  - Behaviour-preserving step: add a key (for example `instguard10`, default 0) and gate the hook on `g_cfg.trace || g_cfg.instGuard10`. That is identical for every existing cfg.
  - Making the guard default-on in the recommended config is the user's decision. It would add one resolver call per SubNetwork instance and skip dead-root instances instead of crashing.
- **Refactor risk:**
  - Keep the hook name `"SubNetwork instance guard (trace)"` verbatim when installed under `trace`.
  - `g_instHeaders` must still be set before `hkHdrClone` logs.
- **Verification:** CORRECTED:
  - Confirmed that it guards and that it exists only under trace.
  - Added that the original trigger is fixed in octx.hpp.
  - Rewrote the fix: "a default equal to the effective trace value" cannot be expressed as a struct default.

### B-05 `g_open` used outside its detour with no null check; AssetOpen `hook()` result ignored
- **Where:** `native/di1compat/di1compat.cpp:4183` (result unused). Users of `g_open`:
  - `hkExpPkg` 3304 (installed at 4192 under `popin10 || deliv10`, both default on)
  - `read30` 506, via `fallbackDef` 2026 ← `hkAssetDef` (installed unconditionally, 4304)
  - npcrt.hpp:267, and `read30` at npcrt.hpp:250
- **Severity / category / class:** Med · Error handling (latent crash) · BEHAVIOUR-CHANGE
- **Evidence:**
  - `hook()` refuses on a prologue mismatch (120-123) and then never reaches `MH_CreateHook`. MinHook also writes `*ppOriginal` only on success (hook.c:633-634). Either way `g_open` stays `nullptr`.
  - `hkExpPkg` then calls `g_open(p, 0, 0, 0, nullptr)` at the first expendables package load, which is a call to address 0.
  - Through `hkAssetDef` the null call happens under `__try` (2056) with `g_defsMx` held (see the cross-slice notes).
- **Why it matters:** If another mod patched `0x0041CB10` first, the plugin would crash instead of degrading.
- **Fix:** Gate the dependent hooks on the AssetOpen result, or null-check `g_open` at the 4 call sites. This changes behaviour only in the failure case (crash → no-op).
- **Refactor risk:** None on the success path. Keep the `hook AssetOpen …` log line.
- **Verification:** CORRECTED: the `read30` call in `fallbackDef` is at 2026 (not 2027); added the npcrt.hpp:250 `read30` path.

### B-06 Session cache `g_served` never evicted; every hit copied under `g_sessLock`
- **Where:** `native/di1compat/di1compat.cpp:3646-3661`; declaration :559; writers :655-658, :817 (emplace), :1038, :3712, npcrt.hpp:276; no `erase` anywhere (grep)
- **Severity / category / class:** Med · Memory, perf, concurrency · NEEDS-VERIFICATION
- **Evidence:**
  - `if (it != g_served.end()) hit = it->second;  // kept: the engine reopens files` (3650) copies a whole `.mtb`/`.vbuf`/`.ibuf` under the lock.
  - `memFile(hit.data(), hit.size())` (3657) copies it again.
- **Hot path?:** code defaults: unreachable (`serve10` off); recommended config: every reopen of a session product during loads.
- **Why it matters:** Every transcoded world stays resident for the session in a 32-bit process. Each reopen pays two copies, and the first copy blocks every other open's cache lookup.
- **Fix (behaviour-preserving):** Store `std::shared_ptr<const std::string>`, copy the pointer under the lock, and call `memFile` after unlocking. Eviction would change behaviour, because the engine reopens files; document the growth instead.
- **Refactor risk:**
  - All 6 writer sites across two files change together.
  - `:817` uses `emplace` (first wins); the others overwrite. Keep that difference.
- **Verification:** CONFIRMED.

### B-07 `linkShader` scans the whole shader array on every lookup, inside the render thread's critical section
- **Where:** `native/di1compat/di1compat.cpp:3937-4006` (scan 3947-3950); caller 4077 `if (s && (ours || g_cfg.lazyLink10)) linkShader(s);`
- **Severity / category / class:** Med · CPU, concurrency · NEEDS-VERIFICATION
- **Evidence:**
  - Inside `RecLock lock;` (3939), before knowing whether any pass is fresh, the code loops `for (uint32_t k = 0; k < nsh; ++k) if (*(uint32_t*)rt(0x0215CFC8 + 8 * k) == …)`. That is up to the 836 3.0 shaders (comment 3911).
  - `g_recCs` is also taken by `hkCloneRec` on the render thread, "mid-draw" (3915-3917, 3926).
  - `shader10` builds a `std::string` key for `g_sh10.find(name)` (4030) and takes a mutex. It does so only for names starting with 'x'; hash names are 33 chars, so the key is a heap allocation.
- **Hot path?:**
  - Code defaults: not installed (`shaders10` off).
  - Recommended config: every `FUN_0045c820` lookup (`lazyLink10` defaults on). The comment says this is on loader threads; per-frame is UNVERIFIED. Counting calls per frame would settle it.
- **Why it matters:** About 836 compares plus a critical-section enter per lookup, with the render thread blocked meanwhile.
- **Fix (behaviour-preserving):** Compute `shaderIndex` on the first fresh pass, still inside `RecLock`. It is read only in `rec[0]` (3975) and the `ours:` log (3989-3991).
- **Refactor risk:**
  - Keep the scan inside `RecLock`.
  - Keep the record publication order: fill `rec`, then `*(uint16_t*)(p + 0x16) = n` (3998), then `count = n + 1` (3999).
  - Also covered by the render verified area.
- **Verification:** CONFIRMED.

### B-08 Long lock scopes (`g_sh10Lock` across D3DCompile and disk I/O) and an undocumented lock hierarchy
- **Where:** `native/di1compat/di1compat.cpp:4029-4063` (`g_sh10Lock`); 3939-4005 (`RecLock`, including `inputLayout10` 3984)
- **Severity / category / class:** Med · Concurrency · DOC-ONLY
- **Evidence:**
  - `g_sh10Lock` is held across all of the following:
    - `gen30::generate` → `compile10` → D3DCompile
    - the cache `fopen`/`fread`/`fwrite` (3833-3848)
    - `registerProgram10` (engine CreateVertexShader/CreatePixelShader, 3868)
    - `finalizeShd10` (3878-3890)
  - `RecLock` is held across `FUN_0044D430` (CreateInputLayout, per comment 3985). The render thread's `hkCloneRec` waits for it.
  - Observed lock order:
    - `g_zipLock` → `g_sessLock` (`indexZip10` 592-595)
    - `g_sh10Lock` → `g_zipLock` (`shader10` → `read10s` → `read10z` 407)
    - every lock → `g_logLock` (leaf)
  - No inversion found. Every `g_sessLock` scope (654, 800, 816, 1037, 3648, 3670, 3711, 3722-3725) is map-only.
  - Deadlock path UNVERIFIED: if `FUN_0045CF40` or the program registration (called under `g_sh10Lock`) reached `FUN_0045C820` for an 'x' name, `shader10` would re-lock a non-recursive `std::mutex` on the same thread. Disassembling both callees would settle it.
  - Per comment 2093-2097, zone loads run on loader fibers of the frame thread. A cache-miss D3DCompile therefore blocks that thread for its whole duration.
- **Why it matters:** Hidden stalls of the render and frame threads, and a hierarchy that a refactor could invert.
- **Fix:** Document the hierarchy in a header comment. No code change.
- **Refactor risk:** Don't widen any lock scope or take `g_sessLock` before `g_zipLock`.
- **Verification:** CONFIRMED.

### B-09 Plugin locks versus engine fibers (constraint)
- **Where:** `native/di1compat/di1compat.cpp:2093-2097, 2122-2123, 2133-2134`; every `std::mutex` and `RecLock` scope in the slice
- **Severity / category / class:** Med · Concurrency constraint · DOC-ONLY
- **Evidence:**
  - The engine runs loads on scheduler fibers that share the frame thread.
  - `std::mutex` (an SRW lock): if a fiber yields while holding it and another fiber on the same thread locks it, the thread self-deadlocks.
  - `CRITICAL_SECTION` (`RecLock`, `g_logLock`) is recursive per thread, so in the same situation it silently stops excluding.
  - The file-done wait yields (2133-2134).
  - In this slice no plugin lock is held across `g_open`, `readStream` or engine loaders: 3580-3583 copies then releases; `registerDelivery10` 3288-3296 makes only a registry call; `zones10` and `tips30` hold none.
  - Whether the engine registration and D3D calls made under `g_sh10Lock`/`RecLock` can yield is UNVERIFIED.
- **Why it matters:** This is the rule any refactor must keep.
- **Fix:** Write it down: never call `g_open`, `read30`, `readStream` or engine loaders while holding a plugin lock.
- **Refactor risk:** Moving code into a lock scope can freeze the game.
- **Verification:** CONFIRMED.

### B-10 `thread_local` state shared by fibers (`sys::g_pkg10`, `g_fpScope`)
- **Where:** `native/di1compat/di1compat.cpp:3306-3309`, 3362; sys.hpp:672 (`inline thread_local int g_pkg10 = -1;`); `g_fpScope` 3083, 3111-3115, 3132-3138
- **Severity / category / class:** Med (latent) · Concurrency · DOC-ONLY
- **Evidence:**
  - `sys::g_pkg10` stays set across the whole `g_expPkg` package load.
  - Package loads open files, and the engine's file-done wait yields to other fibers (2133-2134). If another fiber on the same thread runs `hkExpDef` or a nested `hkExpPkg` meanwhile, two things can happen:
    - (a) the first package's later definitions see -1 after the nested load resets it, so they get no 1.0 pop-in;
    - (b) definitions of another (3.0) package loading on that fiber see `>= 0`. `applyPopin10` (sys.hpp:664-671) then stamps the 1.0 pop-in on any definition whose two distances are 0.
  - The plugin's own comment at 2122 notes that `thread_local` mixes fibers.
  - Whether `FUN_0160aa30` yields is UNVERIFIED. To settle it: log `GetCurrentFiber()` at the start and end of `hkExpPkg` (or watch `hkFiberSwitch` under loadprof10).
- **Why it matters:** Pop-in can be applied to the wrong package or skipped, depending on fiber timing.
- **Fix:** Document it. A fix keyed by `GetCurrentFiber()` (as `fiberProf` does, 2125-2132) changes behaviour only in the interleaved case.
- **Refactor risk:** Low.
- **Verification:** CORRECTED: added direction (b), a 3.0 package receiving the 1.0 pop-in.

### B-14 Four copies of "drain an engine stream", already drifted
- **Where:**
  - `readStream` 1126-1138
  - `read30` 501-514
  - `hkOpen` 3559-3572 and 3601-3608 (each re-declares SizeFn/ReadFn/DtorFn and uses `vt[4]`/`vt[5]`/`vt[11]`)
  - destroy-only at 3305 and npcrt.hpp:268
- **Severity / category / class:** Med · Duplication · NEEDS-VERIFICATION
- **Evidence:**
  - `readStream` calls Read even when the size is 0, resizes to `got`, and destroys the stream.
  - `read30` and the two `hkOpen` copies skip Read on size 0 (`t30.empty() ? 0 : …`) and keep the full size on a short read.
- **Fix (behaviour-preserving):** One `EngineStream` helper with an explicit `skipReadIfEmpty` / `keepFullSizeOnShortRead` option, so both behaviours stay.
- **Refactor risk:** The size-0 Read difference; who calls `vt[11]`, and when (before or after `mergeVtxf` at 3567-3568).
- **Verification:** CONFIRMED.

### B-15 Engine prototypes and VAs declared ad hoc at call sites
- **Where:** 69 literal `rt(0x…)` casts in 2210-4361 (grep)
- **Severity / category / class:** Med · Readability, maintainability · SAFE-REFACTOR
- **Evidence:**
  - `FUN_0041A250` is typed `void(__fastcall*)(void*,void*,const char*)` at 2573 but `void*(__fastcall*)(…)` at 3521.
  - `FUN_0139E0B0` is typed both as `StrFn` (2564) and as `ZoneStrFn` (3449).
  - Pass-state records `0x02173A66`/`0x02173A68` appear 7 times; the shader array `0x0215CFC0`/`C4`/`C8` 7 times.
  - Program-table VAs appear both in `registerProgram10` (3857) and in `linkShader` (3971-3997).
  - Unnamed constants: `.shd` layout `0xE0`, `0x50`, `0x19`, `+0x28`, `+0x30`; `strlen(name) == 33`; `0xFFE17B81u`; `0x9e3779b97f4a7c15ull`; `0xF0000000u`; the `4095` and `2047` caps.
- **Fix (behaviour-preserving):** An `engine.hpp` with named `constexpr` VAs and typed inline wrappers that make the exact same calls.
- **Refactor risk:**
  - Keep the `__fastcall`-with-dummy-`edx` emulation of thiscall.
  - Keep the return types.
  - Keep whether a call goes through a hooked function (B-16).
- **Verification:** CORRECTED: the shader array appears 7 times (including `CFC4`), not 6.

### B-19 Mid-file runtime includes depend on main-file statics; `caprt::g_base` shadows `::g_base`
- **Where:** `native/di1compat/di1compat.cpp:4092-4095` (and `npcrt.hpp` at 621); caprt.hpp:52 `static SpawnFn g_cust, g_base;`
- **Severity / category / class:** Med · Coupling · NEEDS-VERIFICATION
- **Evidence:** Approximate grep counts of main-file statics used:
  - gprt.hpp: `rt(` ~36, `logf(` 15, `g_cfg.` 14, `hook(` 12, `safeCopy(` 9, plus `g_sessLock`, `g_screens10`, `g_movies10`, `zipFor`, `g_zipLock`
  - vfxrt.hpp: `logf`, `safeCopy`, `rt`, `hook`, `g_cfg`, `g_over10`, `g_gameDir`, `g_base`
  - caprt.hpp and bldrt.hpp: `hook`, `rt`, `logf`, `safeCopy`, `g_cfg`

  These headers compile only at that point in the file. Inside `namespace caprt`, `g_base` is a trampoline (184, 211). `rt()` is unaffected because it binds to `::g_base` where it is defined. `vfxrt::retVa` (vfxrt.hpp:10) duplicates `callerVa` (2742).
- **Fix:** A `di1compat_internal.hpp` declaring the shared statics; move the runtime headers into their own translation units; rename `caprt::g_base`.
- **Refactor risk:**
  - The statics have internal linkage today.
  - Splitting into several translation units introduces cross-TU initialisation order.
  - Code in caprt that means the exe base must say `::g_base`.
- **Verification:** CORRECTED: vfxrt also uses `logf`, `safeCopy`, `rt`, `hook` and `g_cfg`; the counts are approximate.

### B-11 C++ exceptions can unwind into engine frames from `hkShaderByName`
- **Where:** `native/di1compat/di1compat.cpp:4074` → `shader10` 4027-4064 (`engineId.at(pid)` 4052); gen30.hpp:631 → `buildShd` → `L.B.at(p.hash)` (gen30.hpp:543); `buildShd` lies outside `generate`'s try (gen30.hpp ~604-616)
- **Severity / category / class:** Low · Error handling · BEHAVIOUR-CHANGE
- **Evidence:**
  - `create = (strlen(name) == 33)` (4074): a hash-named lookup that the transcode has not already made runs generation with no try block.
  - `hkOpen`'s transcode has a catch (3703-3720), but `hkShaderByName` does not.
  - The build uses CMake's default MSVC flags (`/EHsc`; `CMakeLists.txt` sets none).
- **Why it matters:** An exception here ends in `std::terminate`.
- **Fix:** Wrap the body in try/catch and return the engine's lookup result. This is a behaviour change, and not necessarily better: comment 4071-4073 says a name the engine could not resolve made a loader task wait forever (hang instead of crash). Needs the user's decision.
- **Refactor risk:** Keep the cached `nullptr` semantics of `g_sh10[name]`.
- **Verification:** CORRECTED: severity Low–Med → Low (needs a gen30 bug); added that the catch path may hang instead of crash.

### B-12 `compile10`: 64 KB stack buffer, out-of-bounds index on a negative `bufview10`, non-atomic cache write
- **Where:** `native/di1compat/di1compat.cpp:3834` (`char buf[65536];`), 3828 (`bufview[(g_cfg.bufview10 - 1) % 5]`), 3848
- **Severity / category / class:** Low · Robustness · BEHAVIOUR-CHANGE (clamp and temp-rename touch only invalid inputs or crash windows)
- **Evidence:**
  - **Stack:** the 64 KB frame sits on the callers' stacks: `hkOpen` transcode → `shader10` (via `io.translatable` at 641) on loader fibers, or `hkShaderByName`. shaders10 is the recommended config and runs in-game (README), so today's stacks have the headroom. The fiber stack size is UNVERIFIED.
  - **Out of bounds:** `bufview10 = -1` gives `(-2) % 5 = -2`, which is undefined behaviour. This is a test-only key.
  - **Cache write:** the cache file is written in place. A truncated `DXBC` file longer than 32 bytes would be accepted (3839). D3D's checksum then refuses it at create time, so the shader fails to register (logged).
- **Fix:**
  - A heap read buffer (same bytes).
  - Clamp `bufview10` (changes only invalid configs).
  - Write to a temp file and rename (same content).
- **Refactor risk:** The cache file name is FNV-64 of `target + "\n" + hlsl` (3830). Keep the HLSL text and the hash exactly.
- **Verification:** CORRECTED: severity Med → Low; compile10 already runs in the recommended config without overflow.

### B-13 Engine reads in hooks are inconsistent with "check before read"
- **Where:**
  - Derived pointers, unchecked:
    - `hkTrack` 2932-2933 (`mission + 0x140` → `ctx + 0x10`)
    - `hkMissionTick` 2685 (`st + 8`) and 2689 (`m + 0x3d9`)
    - `hkFpScan` 3135 (list walk ≤1000)
    - `hkOpacityMask` 2453-2456, 2466-2472, 2479-2483
    - `hkHdrClone` log arguments 3189-3191
  - Engine `this` fields or arguments the original also reads:
    - `hkLoadTick` 2845
    - `hkColVisApply` 2891
    - `hkFpUpdate` 3030-3045 (after the vtable check at 3029)
    - `hkFpApply` 3109-3114
    - `hkNetSync` 2863 (mirrors `FUN_006ec4f0`)
  - `__try` only: `hdrName` 3167; `ourRetSafe` 2256-2260
  - Checked: `hkNavUpdate` 2955-2957, `hkMisProgress` 2991-3005, `hkFpNodeUpdate` 3062-3063, `hkMsgRecv` 2812
- **Severity / category / class:** Low · Correctness consistency · DOC-ONLY
- **Evidence:**
  - Installed with the recommended config: `hkTrack` (rare), `hkNetSync`, `hkLoadTick`, `hkColVisApply`. Only `hkTrack` reads derived pointers.
  - `ourRetSafe` reads inside di1compat.dll's own image (bounded by `SizeOfImage`, 2305), so it cannot fault in practice.
  - `avHandler` logs only the first AV (`InterlockedExchange(&g_avLogged, 1)`, 1270), so a handled AV in `hdrName` would use up that single crash log.
  - The original's claim that "a first-chance AV ends the game; `__try` does not help" is wrong (B-N3).
    - A fault under `__try` is handled by its `__except`, and the game survives.
    - It still costs a Pentane symbolised log entry and a CrabeLoader crash report (one of 3 slots).
    - Only a fault raised inside a vectored handler (avHandler at 12:01) has ended the game.
- **Why it matters:** The pointer policy is uneven. Most cited reads are safe by construction; the derived ones are diagnostic-only.
- **Fix:** Document which reads are engine `this` fields and which are derived. Adding checks changes only timing; removing them adds risk.
- **Refactor risk:** Don't "simplify" by dropping `sys::readable` calls.
- **Verification:** CORRECTED:
  - The first-chance premise (B-N3).
  - Reclassified `hkFpUpdate`, `hkFpApply`, `hkLoadTick`, `hkColVisApply` and `hkNetSync` as `this` or argument reads.
  - Severity Med → Low.

### B-16 Hidden re-entrancy through other plugin hooks
- **Where:** `native/di1compat/di1compat.cpp:2565` (`zoneCurrency10`), 3502 (`hkFindScreen`), 3241 (`cbLoad`)
- **Severity / category / class:** Low · Coupling · DOC-ONLY
- **Evidence:**
  - **`zoneCurrency10`** calls `rt(0x0139E0B0)`, which `hkZoneStr` hooks when `serve10 && loadScreen10` (4320). Its key `"inventoryFilter"` passes straight through.
  - **`hkFindScreen`** calls `rt(0x00B42540)`, which `gprt::hkRibbonName` hooks (gprt.hpp:38-42, 470; `selectMenu10` is default on). That hook returns `"SelectMenu"` when the zone has `worldIsPlayset`, so `!_stricmp(rib, name)` (3503) makes tutorial10's redirect a no-op exactly where it applies. In those zones selectMenu10 supersedes it.
  - **`cbLoad`** calls loader `vtable[1]` = `FUN_018DB270` (hooked by `hkCbLoad`).
    - Only `kBits10` (`di1sys/raw10/gamedb/core/bitmasks.lua`) ends in `/bitmasks.lua`. Recursion into the bitmask branch is avoided only because it is never in `dbTriggers10` (sys.hpp:163-171).
    - `kPerc10` and `kChest10` re-enter `hkCbLoad` too, but exit at the `bitmasks.lua` check.
- **Fix:** Comment each call site with whether it intends the hooked path or the original.
- **Refactor risk:** Swapping `rt()` calls for trampoline calls changes behaviour.
- **Verification:** CORRECTED: only `kBits10` can reach the bitmask branch.

### B-17 A GameDB load runs inside a `logf` argument list
- **Where:** `native/di1compat/di1compat.cpp:3322`, 3154
- **Severity / category / class:** Low · Refactor hazard · SAFE-REFACTOR
- **Evidence:**
  - 3322: `if (g_cfg.bits10) logf("bitMask loader given %s after %s -> %u", sys::kBits10, p, cbLoad(self, sys::kBits10, a2, a3));`. The 1.0 bitmask load happens only as a log argument.
  - 3154: `InterlockedIncrement(&g_nStart)` is a log argument.
- **Why it matters:** A future `LOG(...)` macro that skips argument evaluation when logging is off would silently drop the bitmask load.
- **Fix (behaviour-preserving):** `uint8_t ok = cbLoad(...); logf(..., ok);`. Same for 3154.
- **Refactor risk:** None.
- **Verification:** CORRECTED: 2462 and 2554 are side effects in the `if` condition, not in `logf` arguments. A macro that skips arguments would not affect them, so they are dropped from the finding.

### B-18 `logf` overloads C's `logf(float)`
- **Where:** `native/di1compat/di1compat.cpp:70`; cast at 2719; assignments at 4307 and 4333
- **Severity / category / class:** Low · Readability hazard · SAFE-REFACTOR
- **Evidence:** `<cmath>` is included (audio.hpp:23, luachunk.hpp:8, octx.hpp:4, render.hpp:8, sm3.hpp:5). `logf(someFloat)` compiles and calls the math function, and taking the address needs the `static_cast` at 2719.
- **Fix (behaviour-preserving):** Pure rename (for example `dlog`).
- **Refactor risk:** Large diff, no runtime change. `sx::g_log`, `audio::logfn` and `mernode::registerAll` take the pointer.
- **Verification:** CORRECTED: severity Low–Med → Low.

### B-20 Config fan-out and gating are spread out; two gates are inconsistent
- **Where:** `native/di1compat/di1compat.cpp:4156-4164`, 4307-4310, 4333-4337; 4200; 4245; 4355
- **Severity / category / class:** Low · Design · SAFE-REFACTOR
- **Evidence:**
  - Compound gates:
    - `spawnTrace10 && !trace` (4355)
    - `(aliases10 || native10) && g_merOk` (4268)
    - `serve10 && tutorial10` plus a byte check (4323-4328)
    - `breakTrace || (audio10 && cineSync10)` (4245)
  - Detours re-check flags at runtime (`hkExpPkg` 3300-3301, `hkChoStart` 1629/1631).
  - Inconsistencies:
    - **4200 "VR playset character slot"** is a behaviour-changing hook with no config key at all.
    - **4245** lacks the `install10[0]` that the audio install has (4332). Without a cfg, `hkChoStart` is installed and calls `audio::choreographyStarted` (audio.hpp:2074-2076). Its `ready()` fails at once (no host started), so this is harmless.
- **Fix:** One installer table per subsystem, keeping each gate expression verbatim. Adding a key for 4200 is a behaviour decision.
- **Refactor risk:** Order; boot-time copies must equal `g_cfg` (never changed after `loadConfig`).
- **Verification:** CORRECTED:
  - Severity Med → Low (a facet of B-01).
  - Added the 4200 and 4245 inconsistencies.

### B-21 One MinHook thread freeze per hook (boot cost)
- **Where:** `native/di1compat/di1compat.cpp:124-125`; minhook/hook.c:717-770 (`EnableHook` → `Freeze`), 263-267 (`CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD)` of all system threads)
- **Severity / category / class:** Low · Perf (once) · NEEDS-VERIFICATION
- **Evidence:** Of 149 call sites, 85 hooks are installed with no cfg and 82 with the example cfg. Each does a system-wide thread snapshot and suspends this process's other threads. Under the loader lock there are few such threads.
- **Fix:** `MH_CreateHook` for all, then `MH_QueueEnableHook` and one `MH_ApplyQueued`.
- **Refactor risk:**
  - The engine cannot run during `Pentane_Main` (B-N1), so enabling AssetOpen later is not observable.
  - What changes: the status logged per hook (must then come from create/queue), and a failed apply leaves all hooks off instead of some. Measure first.
- **Verification:** CORRECTED: the "AssetOpen live first" caveat is moot (loader lock); the remaining differences are logging and failure semantics.

### B-23 `g_recCs` is initialised only under shaders10
- **Where:** `native/di1compat/di1compat.cpp:4230` vs `RecLock` 3920
- **Severity / category / class:** Low · Trap · SAFE-REFACTOR
- **Evidence:** Safe today: `linkShader` and `hkCloneRec` are reachable only through the shader hooks. A new caller with `shaders10 = 0` would enter a zero-filled `CRITICAL_SECTION`.
- **Fix (behaviour-preserving):** Initialise it unconditionally at the top of `Pentane_Main`.
- **Verification:** CONFIRMED.

### B-24 `missionTrace` (a log flag) changes the gameplay data path
- **Where:** `native/di1compat/di1compat.cpp:3599` → `zones10()` 2619-2658 (fills `g_currency10` 2638, `g_playsets10`, `g_missionPrefix10`)
- **Severity / category / class:** Low · Hidden coupling · DOC-ONLY
- **Evidence:** With `zones10 = 0` and `playsetMenu10 = 0`, turning on `missiontrace` fills `g_currency10`. That activates the currency10 redirects (2547-2618). `playsets10(…, g_cfg.zones10, …)` still does not add records.
- **Fix:** Document it; keep the expression.
- **Verification:** CONFIRMED.

### B-25 renderdoc10: coupling, hard-coded path, detached thread
- **Where:** `native/di1compat/di1compat.cpp:4122-4129`; trigger 2673-2676
- **Severity / category / class:** Low · Coupling, portability · DOC-ONLY
- **Evidence:**
  - The DLL is loaded from `"C:\\Program Files\\RenderDoc\\x86\\renderdoc.dll"`, inside `DllMain` (B-N1).
  - The capture fires only from `hkMissionTick`, which is installed only under `missionTrace` (4236-4237), and only for 1.0 mission prefixes.
  - `std::thread(...).detach()` (2675) can throw into engine code, and `1000 * renderdoc10` can overflow.
- **Fix:** Document it; optionally make the path configurable with the same default.
- **Verification:** CONFIRMED.

### B-26 `hkZoneStr` returns a pointer into a static `std::string` after unlocking
- **Where:** `native/di1compat/di1compat.cpp:3456-3462`
- **Severity / category / class:** Low · Concurrency · NEEDS-VERIFICATION
- **Evidence:** A concurrent "LoadingScreens" query reassigns `pick` while the first caller reads it. Comment 3446 says the only reader is `FUN_00b78c70`, so this is unlikely.
- **Fix:** Intern the strings in a static `std::set<std::string>`. Same text, stable pointers.
- **Verification:** CONFIRMED.

### B-27 Unsynchronised `g_zips.size()`, a misleading "archives open" count, `missWhy` built eagerly
- **Where:** `native/di1compat/di1compat.cpp:3666`; `zipFor` 376-389
- **Severity / category / class:** Low · Concurrency, log accuracy · SAFE-REFACTOR
- **Evidence:**
  - `std::to_string(g_zips.size())` reads a map guarded by `g_zipLock` without taking it.
  - `zipFor` caches `nullptr` for every probed path that doesn't exist. `read10z` tries up to 5 candidates per split, so the number counts probed paths, not "archives open".
  - The string is built before the read, even when the file is then served.
- **Fix:** Read the size under `g_zipLock` and build the message lazily. Keep the log text, and document what the number means.
- **Verification:** CORRECTED: added that the count includes cached `nullptr` probes.

### B-28 Logging work done regardless of budget or state
- **Where:**
  - `hkFpUpdate` 3037-3050: 4 `cstr` plus `readable` checks and a 512-byte `sprintf` before the `last.size() > 400` check
  - `hkFpApply` 3113-3121 and `hkFpScan` 3136-3144: `sprintf` before the size check
  - `hkMissionTick` 2683-2694: a 17-field `sprintf`, 2 `safeCopy` and one 14-global scan per tick, per context, with no budget
  - `hkLoadTick` 2850: a mutex even when the set is full
  - `hkBeaconLookup` 2906-2912
  - `hkNavUpdate` 2955-2966
- **Severity / category / class:** Low · Perf · NEEDS-VERIFICATION
- **Hot path?:** code defaults and recommended config: `hkNavUpdate`, `hkLoadTick` and `hkBeaconLookup` are installed (lockTrace10); the rest only under `missionTrace` (off in both). `hkMissionTick` runs per frame per context (comment 2079).
- **Fix:** Do the size check under the lock before formatting, where that is equivalent. In `hkFpUpdate`, once full the only effect is `return`.
- **Refactor risk:** Keep each `lockTraceRoom` increment where it is; it decides which lines are logged.
- **Verification:** CORRECTED: `hkMissionTick` scans the 14 state globals once (not 2×14); `hkFpUpdate` makes 4 `cstr` calls.

### B-29 `hkNavUpdate` change detection is global, not per component
- **Where:** `native/di1compat/di1compat.cpp:2967-2973`
- **Severity / category / class:** Low · Diagnostic logic · DOC-ONLY
- **Evidence:** `static int last[6]; static float lastT[3];` are shared by every MissionNavData. With two players each call compares against the other's state, logging until the 300-line budget is gone.
- **Fix:** Keying by `nav` changes the log (fewer lines), so document only.
- **Verification:** CONFIRMED.

### B-30 `lockTraceRoom` counters never saturate
- **Where:** `native/di1compat/di1compat.cpp:2746`; 2975, 3001, 3056, 3067, 3124, 3147
- **Severity / category / class:** Low · Correctness, readability · SAFE-REFACTOR
- **Evidence:** `InterlockedIncrement` keeps counting after the budget. A `LONG` wraps after 2^31 calls (`hkMsgSend`, `hkMsgRecv`, `hkChoEvent` count per message), which reopens logging. This is unreachable in practice.
- **Fix (behaviour-preserving):** A compare-exchange loop that increments only while under budget. It is identical below 2^31, including under concurrency. A plain read-then-increment is not.
- **Verification:** CORRECTED: the fix must be a CAS loop.

### B-31 currency10 allocates per call; `hkHudCurrency` makes several engine calls per update
- **Where:** `native/di1compat/di1compat.cpp:2551`, 2554, 2559-2566, 2596-2617
- **Severity / category / class:** Low · Perf · SAFE-REFACTOR
- **Evidence:**
  - `g_currency10.find(filter)` builds a temporary `std::string`.
  - `told.insert(std::string(who) + filter)` allocates on every hit.
  - `zoneCurrency10` calls the zone-string getter (through `hkZoneStr` when installed).
  - In a 1.0 zone `hkHudCurrency` replaces the original with the same engine sequence: inventory amount, Lua table get, tointeger, release.
- **Hot path?:** code defaults and recommended config: installed (`currency10`); the rate is per HUD update (UNVERIFIED, plausibly per frame); the early-out path in 3.0 zones costs a getter call, a mutex and a map lookup.
- **Fix:** Transparent comparators (`std::less<>`); a `told` key without concatenation. Same log text.
- **Verification:** CONFIRMED.

### B-32 `profThread` (145 lines; loadprof10)
- **Where:** `native/di1compat/di1compat.cpp:2295-2439`
- **Severity / category / class:** Low · Diagnostic quality · NEEDS-VERIFICATION
- **Evidence:**
  - (a) Each candidate stack word inside the exe range goes through `afterCallSafe` → `afterCall` → `::readable` (VirtualQuery). The cost for image regions is UNVERIFIED; sys.hpp:176-179's 0.19-5.1 ms figures were measured on heap regions.
  - (b) Thread handles from `OpenThread` are never closed, and a reused TID samples the dead thread's handle.
  - (c) It takes a toolhelp snapshot every second.
  - (d) The key is a positional `std::array<uint32_t, 9>`.
  - (e) It never exits and never closes its timer handle.
  - (f) The comment at 2215-2217 says "caller, caller's caller" and "first two" return addresses; the code stores four (k[3..6], 2368), as the 2309 comment and the TSV header say.
- **Fix:** A struct key, closing handles that drop out of the snapshot (changes sampling of reused TIDs), and the comment fix.
- **Refactor risk:** Commit 684539b: keep "check before read" in the stack walk.
- **Verification:** CONFIRMED. Item (a)'s magnitude is UNVERIFIED.

### B-33 Two `readable()` implementations
- **Where:** `native/di1compat/di1compat.cpp:1228-1241` (VirtualQuery; requires a read-type protection bit); sys.hpp:173-197 (QueryWorkingSetEx fast path; rejects only NOACCESS/GUARD/0)
- **Severity / category / class:** Low · Duplication · DOC-ONLY
- **Evidence:** In the slice, `profThread` (via `afterCall`) and `stackOf` (3623, 3740) use the global one, both diagnostic-only; the hooks use `sys::`. They differ on `PAGE_EXECUTE`-only pages.
- **Fix:** Merge only after confirming the semantics for each call site.
- **Refactor risk:** The commit 684539b regression area.
- **Verification:** CORRECTED: severity Med → Low (in-slice uses are diagnostic-only).

### B-34 Utility duplication
- **Where / evidence:**
  - `slurpFile` 4141-4151, `compile10`'s read loop 3833-3838, `audio::slurp` (audio.hpp:928)
  - `callerVa` 2742, `vfxrt::retVa`, and inline `- g_base + 0x400000` (2366, 2369, 3420)
  - The image-size literal `0x2100000` (2365, 3420; :1244, :1247, :1273, :1292, :1296) vs `0x0209B000` (:1522)
  - The own-module lookup at 2302-2305 and 4117-4118
  - `copyStack` vs `stackOf`'s region logic (1256-1258)
- **Severity / category / class:** Low · Duplication · SAFE-REFACTOR
- **Fix (behaviour-preserving):** Shared helpers that keep the literal values. Don't replace `0x2100000` with `SizeOfImage`; that changes classification.
- **Verification:** CORRECTED: `fnv64` (3779, 64-bit FNV-1a) is **not** a duplicate of `audio::fnv1` (32-bit FNV-1 over lower case). `fnv64` names the on-disk shader cache files (3830) and must stay exactly as is.

### B-35 Dead locals and dead stores
- **Where:** `native/di1compat/di1compat.cpp:4041`, 4046 (`vsIndex`/`psIndex` written, never read), hence `registerProgram10`'s `index` and `g_prog10`'s `.second` (3856, 3873-3874); `g_linkAll` (4009) never called
- **Severity / category / class:** Low · Dead code · SAFE-REFACTOR
- **Fix (behaviour-preserving):** Remove the dead maps and the unused index. Keep `g_linkAll` as MinHook's out-parameter.
- **Verification:** CORRECTED:
  - The `bufview10`/`magenta10`/idcolor paths in `compile10` (3790-3828) are live, config-gated test features, not dead code.
  - `DI1NativeProbe` (2703-2718) is a deliberate smoke test of the registration path. Removing it changes the class registry and the `meridian native: %d of %d` log line.

### B-36 Resource handling in `registerProgram10` and `shader10`
- **Where:** `native/di1compat/di1compat.cpp:3865` and 4054 (`_aligned_malloc` unchecked), 4056 (`_strdup` unchecked), 3869-3871 (`data` leaked when the engine refuses)
- **Severity / category / class:** Low · Failure paths · BEHAVIOUR-CHANGE
- **Evidence:**
  - Programs registered before a later failure (4044) keep their slots toward the 2047 cap.
  - The Blob COM handling (3842-3847) is correct.
- **Fix:** Null checks; free on refusal. Changes only OOM and refusal paths.
- **Verification:** CONFIRMED.

### B-37 `linkShader`'s return value ignored; "records full" logs on every lookup
- **Where:** `native/di1compat/di1compat.cpp:3966`; callers 4019, 4077
- **Severity / category / class:** Low · Logging · DOC-ONLY
- **Evidence:** Once 4095 records are used, every lookup of a shader that still has a fresh pass writes a flushed line.
- **Fix:** Document it; changing the logging is a behaviour change.
- **Verification:** CONFIRMED.

### B-38 `Pentane_Main` does not check Win32 results
- **Where:** `native/di1compat/di1compat.cpp:4102-4111`, 4117-4119, 4130, 4171
- **Severity / category / class:** Low · Failure paths · BEHAVIOUR-CHANGE
- **Evidence:**
  - With no `\` in the path (impossible in practice), the rotation would rename the exe and `_fsopen` would create a file in its place.
  - A failed `_fsopen` silently disables all logging (72).
  - A failed `GetModuleHandleExA` reads the cfg from the game directory instead.
- **Fix:** Early-return guards; success paths unchanged.
- **Verification:** CONFIRMED.

### B-39 Version string mismatch
- **Where:** `native/di1compat/di1compat.cpp:4114` (`"di1compat 0.3.0 loaded"`) vs :63 `{0, 1, 0}`
- **Severity / category / class:** Low · Readability · BEHAVIOUR-CHANGE (unifying changes one of the two outputs)
- **Verification:** CONFIRMED.

### B-40 `_setmaxstdio(8192)` and `nullptr` cached permanently in `zipFor`
- **Where:** `native/di1compat/di1compat.cpp:4130`; 376-389
- **Severity / category / class:** Low · Robustness · DOC-ONLY
- **Evidence:** Archives are never closed. If the FILE limit is reached, later archives are permanently "missing", and the miss line then misleads (B-27).
- **Verification:** CONFIRMED.

### B-41 `hkNormPath` underflows when `size == 0`
- **Where:** `native/di1compat/di1compat.cpp:3437`
- **Severity / category / class:** Low · Robustness · BEHAVIOUR-CHANGE (only for `size == 0`, which the engine never passes; comment 3428 says 0x80)
- **Evidence:** `std::string cut(src, size - 1)` with `size == 0` asks for 0xFFFFFFFF characters, so `std::length_error` is thrown into the engine.
- **Fix:** `if (!size) return g_normPath(dst, size, src);`
- **Verification:** CONFIRMED.

### B-42 Short-read handling differs between `chdForLua` and `tips30`
- **Where:** `native/di1compat/di1compat.cpp:3200` vs 3536
- **Severity / category / class:** Low · Error handling · DOC-ONLY
- **Evidence:** `chdForLua` silently serves truncated bytes; `tips30` reopens the file.
- **Fix:** Document it; adding a log changes log output.
- **Verification:** CONFIRMED.

### B-43 Readability: cryptic names, magic numbers, name reuse
- **Where / evidence:**
  - `hkOpen` locals `zl, cd, zl10, k, kb, ka, kz` (3597-3618).
  - Bit packing in `linkShader` (3975-3981).
  - Lambda buffer sizes 48 and 96 that must match their callers (2685, 2931-2933).
  - `zones10()` runs for three flags.
  - The file-scope `mergeVtxf` (4083) reuses `gen30::mergeVtxf`'s name. It does not shadow it (the call is qualified), but it has side effects (`g_vf30`; `g_tails10` via the out-parameter).
  - Hook names: "MissionContext update (trace)" is installed under `missionTrace` (4237); "frame sync (fps log)" is also used for loadprof10 (4296).
- **Severity / category / class:** Low · Readability · SAFE-REFACTOR
- **Fix:** Rename locals; named constants.
- **Refactor risk:** Never touch logged hook names.
- **Verification:** CORRECTED: `mergeVtxf` is name reuse, not shadowing.

### B-44 Inline-assembly thunks for custom calling conventions
- **Where:** `native/di1compat/di1compat.cpp:3878-3890` (EDI = shader, ECX = &cursor); 3892-3907 (ECX = entry, EAX = vsIndex, 3 stack arguments, caller cleans `add esp, 12`)
- **Severity / category / class:** Low · Constraint · DOC-ONLY
- **Evidence:** The compiler cannot see what `call fn` clobbers. Whether the callees preserve EBX/ESI/EBP is UNVERIFIED (assumed MSVC-conventional).
- **Fix:** Keep them byte-for-byte as separate functions.
- **Verification:** CONFIRMED.

### B-45 `hkZoneCurrency` CString out-parameter semantics
- **Where:** `native/di1compat/di1compat.cpp:2573`
- **Severity / category / class:** Low · Correctness · DOC-ONLY
- **Evidence:** It assigns into `out` via `FUN_0041A250`, which is correct only if callers pass a constructed CString.
- **Verification:** UNVERIFIED: disassembling `FUN_00e21850` (does it construct `out` with `FUN_00418310`?) or its callers would settle it.

### B-46 Fixed per-open costs in `hkOpen`
- **Where:** `native/di1compat/di1compat.cpp:3557-3626`, 3754
- **Severity / category / class:** Low · Perf · SAFE-REFACTOR
- **Evidence:** Each open pays:
  - one `safeCopy` (a QueryWorkingSetEx syscall);
  - up to 3 `_stricmp` and about 7 `endsWith` on the common path (each `strlen`s both strings);
  - a mutex for every `.lua` (`takeChdPath` 1158-1161);
  - with `trace = 1`, a flushed log line.
- **Hot path?:** code defaults: per open, plus the per-open log; recommended config: per open, no log on hits.
- **Fix:** `strlen(p)` once and a suffix helper. Same results.
- **Verification:** CONFIRMED.

### B-47 1.0 inflate under the global `g_zipLock`
- **Where:** `native/di1compat/di1compat.cpp:3692-3695`; `read10z` 405-451
- **Severity / category / class:** Low · Concurrency · NEEDS-VERIFICATION
- **Evidence:** All 1.0 reads in the process, including decompression, are serialised on one mutex.
- **Fix:** Per-archive locks, keeping `g_zips` itself guarded. miniz is not safe within one archive.
- **Verification:** CONFIRMED.

### B-48 Unlocked function statics in hooks whose thread is unverified
- **Where:** `native/di1compat/di1compat.cpp:2672`, 2692 (`hkMissionTick`); 2967-2968 (`hkNavUpdate`)
- **Severity / category / class:** Low · Concurrency · DOC-ONLY
- **Evidence:** Sibling hooks with the same pattern take a mutex (3046-3054). The thread affinity is UNVERIFIED.
- **Verification:** CONFIRMED (the statics are unguarded); thread affinity UNVERIFIED.

### B-49 Three different "is this a 1.0 zone" tests
- **Where:** `playsetMission10` 2659-2664 (name prefixes); gprt.hpp:32-36 (`worldIsPlayset`); `zoneCurrency10` 2559-2566 (`inventoryFilter` in `g_currency10`)
- **Severity / category / class:** Low · Duplication · DOC-ONLY
- **Fix:** Don't unify; the semantics differ. Name and document them.
- **Verification:** CONFIRMED.

### B-N2 The trace watchpoint machinery (DR0) traps every write for the session and scans the stack unchecked inside a VEH — NEW
- **Where:** `native/di1compat/di1compat.cpp:1280-1333` (`watchHandler`, `armWatch`, `watch`), 1350-1354 (caller `hkIgpName`), install 4207
- **Severity / category / class:** Low · Diagnostics risk and cost · NEEDS-VERIFICATION
- **Evidence:**
  - **Trigger:** `hkIgpName` calls `watch(*(self+0xc) + 0xc)` on every call (1352). `watch` returns at once once `g_watchAddr` is set. The guard is a non-atomic check-then-set (1328-1329), so two threads making the first call together both add a VEH and spawn an arming thread.
  - **Arming:**
    - `watch` adds `watchHandler` with First=1 (1330), so it runs before `avHandler` and Pentane's handler.
    - It starts `armWatch` and waits up to 5 s (1331-1332).
    - `armWatch` suspends every thread of the process **alive at that moment** except itself, and sets DR0 plus DR7 (L0, write, 4 bytes; other DR7 bits kept, 1316). Threads created later are unarmed.
  - **After arming:**
    - Every 4-byte write to the slot by an armed thread raises `EXCEPTION_SINGLE_STEP` for the rest of the session.
    - `watchHandler` returns `CONTINUE_EXECUTION` for every hit and logs the first 10 (the comment at 1281 says 5).
    - For each logged hit it reads `*(int*)g_watchAddr` and `sp[0..399]` (1291, 1294-1296) without the stack-region bound that commit 684539b added to `stackOf`. A fault there is a nested AV inside a vectored handler, the one failure mode observed to end the game (B-N3).
    - The watched address is a field of a heap object. If that object is freed and its memory reused, unrelated writes keep trapping.
- **Hot path?:** code defaults: armed on the first playset-figure-slot read (trace on). Every later write to the slot costs an exception dispatch; the write rate is UNVERIFIED. Recommended config: not installed.
- **Why it matters:** A diagnostic left on by default (B-03) has a session-long CPU cost and a small fatal-fault surface.
- **Fix (behaviour-preserving in intent):**
  - Bound the scan to the committed stack region as `stackOf` does. This changes output only where the read would fault.
  - Use `InterlockedCompareExchangePointer` for the one-shot guard (differs only in the race).
  - Correct the comment to "first 10 hits".
- **Refactor risk:** Keep `watchHandler` first in the VEH list (registered last); otherwise Pentane's handler (B-N3) would log and stack-walk every single-step.
- **Verification:** NEW: verified in source. The write rate is UNVERIFIED.

### B-N3 Stale comment: "Pentane's [handler] ends the game" on a first-chance AV — NEW
- **Where:** `native/di1compat/di1compat.cpp:1225-1227`. This is outside the 2210-4361 line range, but the original slice report (its "Faults are fatal" framing, B-13 and the constraints) relied on it.
- **Severity / category / class:** Low · Stale comment · DOC-ONLY (comment fix only)
- **Evidence:**
  - The comment says: "__try is not enough: a first-chance AV reaches the vectored handlers before any __except, and Pentane's ends the game (12:01 boot crash …)".
  - The bundled `third_party/pentane/Pentane.dll` contradicts it (`pentane_install.py` deploys this exact file). I checked it by disassembly; the coordinator checked it independently in the binary and in upstream `crash.cpp`, commit 48e3fbb.
    - Pentane registers its handler with First=1 (`AddVectoredExceptionHandler` call at `0x101b8a0b`; handler entered via thunk `0x1011214f` → `0x101b9760`) during its init (`0x101c2f17`), before the plugin entries run (`0x101c2f6a`). di1compat's `avHandler` is registered later, also First=1 (4178), so it runs first.
    - For codes at or above `0x80000000` (AVs, single-step, and MSVC C++ throws `0xE06D7363`) the handler logs `=== Exception Caught! ===`, the code, the fault address and the registers. It then calls `SymInitialize(process, NULL, TRUE)` and `StackWalk64`/`SymFromAddr` for each frame.
    - It returns 0 (`EXCEPTION_CONTINUE_SEARCH`) on every path (`0x101b9b47`) and references neither `TerminateProcess` nor `ExitProcess`.
  - `harness.ps1:133-134` counts a run's `Exception Caught` lines and carries on, which fits non-fatal logging.
  - So a fault inside `__try` is handled by its `__except` and the game survives.
  - Per commit 684539b, the 12:01 crash was di1compat's own `avHandler` faulting again while handling a first AV, i.e. inside the vectored handler: `afterCall+0x21 <- avHandler <- afterCallSafe <- profThread`.
- **Why it matters:**
  - The later pass must not treat `__try` sites as "fatal anyway", and must not treat `__try` as free either.
  - The real cost of a handled fault is a slow, symbolised Pentane log entry (also paid by every C++ throw) plus a CrabeLoader crash report that uses one of its 3 slots. That is why "check before read" (`sys::readable`) stays right.
  - The case that actually ended the game was a fault inside a vectored handler (`avHandler`; also see `watchHandler`, B-N2).
- **Fix (behaviour-preserving):** Reword 1225-1227 to: a handled first-chance AV costs a Pentane symbolised log entry and a CrabeLoader report; the 12:01 crash was `avHandler` faulting again inside the vectored handler. No code change.
- **Refactor risk:** None (comment only). Do not use this correction to relax "check before read".
- **Verification:** NEW: verified by disassembly of Pentane.dll (`0x101b8a0b`, `0x101b9760`-`0x101b9b6c`); matches the coordinator's check of the binary and upstream `crash.cpp` (48e3fbb); commit 684539b.

## Inventory

### 1. Top-level functions and structs, lines 2210-4361
All line ranges were checked against the source; every row matched. Hot/cold notes: "rec" = recommended config.

| Name | Lines | Count | Purpose | Hot/cold |
|---|---|---|---|---|
| `qpcMs` | 2218-2225 | 8 | QPC milliseconds | loadprof10 only |
| `noteOpenThread` | 2228-2235 | 8 | Remember up to 8 open-calling threads | Per open, loadprof10 only |
| `copyStack` | 2238-2246 | 9 | Region-bounded stack copy of a suspended thread | Sampler ~1 kHz |
| `afterCallSafe` | 2247-2250 | 4 | `__try` around `afterCall` | Sampler |
| `ourRetSafe` | 2253-2261 | 9 | Return address into di1compat.dll? (`__try`) | Sampler |
| `symbolAt` | 2264-2293 | 30 | Code address → `module!export+off` | Sampler, cached |
| `profThread` | 2295-2439 | 145 | Sampling profiler thread, TSV dump | loadprof10 only |
| `hkOpacityMask` | 2444-2491 | 48 | Log primitives the opacity test drops | opacityLog10 (off) |
| `hkVisTest` | 2502-2527 | 26 | Log fade inputs; optionally unfade | fadeProbe10 (off) |
| `currency10` | 2547-2556 | 10 | inventoryFilter → 1.0 currency | Per currency hook call |
| `zoneCurrency10` | 2559-2566 | 8 | The zone's currency via the engine getter | Per currency hook call |
| `hkZoneCurrency` | 2569-2574 | 6 | Redirect the filter-currency answer | currency10 (on) |
| `hkPlaysetCurrency` | 2577-2583 | 7 | Redirect `UI_GetPlaysetCurrency` | Per Lua call |
| `struct ScriptRef` | 2591 | 1 | Lua ref `{L, idx}` | — |
| `hkHudCurrency` | 2594-2618 | 25 | The HUD shows the 1.0 currency | Per HUD update (UNVERIFIED) |
| `zones10` | 2619-2658 | 40 | Register 1.0 playsets, currencies, mission prefixes | Once per zone-list open |
| `playsetMission10` | 2659-2664 | 6 | 1.0 mission-prefix test | missionTrace hooks |
| `hkMissionTick` | 2667-2695 | 29 | Mission state log; RenderDoc arm | Per frame per context (missionTrace, off) |
| `probeImpulse` | 2704 | 1 | Native probe impulse | Never fired |
| `hkMerRegistry` | 2705-2722 | 18 | 1.0 aliases + native Meridian classes | Once |
| `cstr` | 2735-2741 | 7 | Checked copy of a CString member | Per probe log |
| `callerVa` | 2742 | 1 | Return address → static VA | Per probe log |
| `lockTraceRoom` | 2746 | 1 | Line budget (4000) | Per probe call |
| `hkCtlLock`/`hkCtlUnlock` | 2749-2760 | 12 | Control lock log | Rare |
| `hkCtxOn`/`hkCtxOff` | 2763-2774 | 12 | Controller context log | Event |
| `hkChoPlay` | 2777-2782 | 6 | Cho node play log | Event |
| `hkChoEvent` | 2785-2793 | 9 | Cho events 0-3 log | Event |
| `hkMsgSend` | 2796-2802 | 7 | Message send log | Per message |
| `hkMsgRecv` | 2807-2817 | 11 | Message delivery log | Per delivery |
| `hkMonPin` | 2831-2840 | 10 | Mission monitor fire log | Event |
| `hkLoadTick` | 2843-2855 | 13 | LoadScreenActive log | Per node update (likely per frame) |
| `hkNetSync` | 2858-2870 | 13 | MasterNetSync log (virtual calls within budget) | Per Link_In |
| `colVisMode` | 2876-2879 | 4 | Bits → text | Log only |
| `hkColVisLoad`/`hkColVisApply` | 2882-2895 | 14 | ColVisToggle log | Event |
| `hkBeaconLookup` | 2903-2920 | 18 | BeaconMap lookup log once per name | Per lookup (UNVERIFIED) |
| `hkTrack` | 2926-2943 | 18 | Compass track log | Rare |
| `hkNavUpdate` | 2952-2979 | 28 | Compass draw-state log | Likely per frame per avatar (lockTrace10, on) |
| `hkMisProgress` | 2988-3012 | 25 | Mission progress entries log | Per log write |
| `hkFpUpdate` | 3026-3057 | 32 | FocusPoint record log | missionTrace (off) |
| `hkFpNodeUpdate` | 3060-3072 | 13 | FocusPoint Update pin log | missionTrace (off) |
| `struct FpScope` + `g_fpScope` | 3082-3083 | 2 | Per-thread scope counters | — |
| `hkActorOf` | 3086-3096 | 11 | Count handle→actor in scope | missionTrace (off), very hot when on |
| `fpScopeText` | 3097-3103 | 7 | Format FpScope | Log only |
| `hkFpApply`/`hkFpScan` | 3106-3148 | 43 | Tag apply/scan log | missionTrace (off) |
| `hkMkStart`/`hkMkLocator`/`hkMkSpawnExp` | 3154-3156 | 3 | Meridian node construction counts | trace |
| `hdrName` | 3165-3168 | 4 | Header name under `__try` | Log only (≤6) |
| `hkHdrClone` | 3169-3194 | 26 | Log header clones with a dead slot | Per header clone (trace) |
| `chdForLua` | 3197-3210 | 14 | `.lua` → CHD MemFile | Per CHD-designated open |
| `gameDbLoad` | 3220-3233 | 14 | Run a CHD loader in Lua mode | Rare |
| `cbLoad` | 3237-3244 | 8 | DB callback load with an engine CString | Rare |
| `singletonLoader` | 3246-3251 | 6 | Checked singleton → loader | Rare |
| `hkLoadLocks` | 3252-3272 | 21 | Locks + 1.0 supplements + ChestInfo | Rare |
| `registerDelivery10` | 3286-3297 | 12 | RewardType Delivery once | Per package load (cheap after the first) |
| `hkExpPkg` | 3298-3310 | 13 | Package load with 1.0 pop-in stamping | Per package load |
| `hkCbLoad` | 3316-3332 | 17 | 1.0 bitmasks and perceptions | Rare |
| `hkEconOnDeath` | 3341-3353 | 13 | 1.0 SpawnEconomyOnDeath read first | Per getter until decided |
| `hkExpDef` | 3359-3364 | 6 | Stamp 1.0 pop-in | Per definition |
| `sysServe` | 3367-3402 | 36 | Serve `di1sys/` supplements | Per `di1sys` open |
| `hkLoadInventory` | 3403-3406 | 4 | `gameDbLoad` for inventory | Rare |
| `hkWatson` | 3412-3426 | 15 | Log the CRT fail-fast stack | Fail-fast only |
| `hkNormPath` | 3432-3442 | 11 | Truncate over-long resident paths | Per resident lookup |
| `hkZoneStr` | 3451-3466 | 16 | 1.0 loading-screen choice | Per zone-string read |
| `hkFmvPath` | 3475-3486 | 12 | 1.0 FMV fallback | Per FMV |
| `hkFindScreen` | 3497-3506 | 10 | Tutorial element 61 ribbon stand-in | Per screen lookup |
| `hkCtxParse` | 3515-3527 | 13 | 1.0 controller-context list rewrite | Per node parse |
| `tips30` | 3533-3546 | 14 | Merge 1.0 tips | Once |
| `hkOpen` | 3549-3756 | 208 | AssetOpen: every interception and the 1.0 serve | **Per file open** |
| `struct Blob` | 3768-3771 | 4 | ID3DBlob interface | — |
| `fnv64` | 3779-3784 | 6 | FNV-1a 64 (shader cache file names) | Per compile |
| `compile10` | 3787-3850 | 64 | HLSL → DXBC with a disk cache and test overrides | Per program generation |
| `registerProgram10` | 3853-3876 | 24 | Add a program to the VS/PS table | Per program |
| `finalizeShd10` | 3878-3890 | 13 | Asm thunk: `.shd` finalize | Per generated shader |
| `inputLayout10` | 3892-3907 | 16 | Asm thunk: input layout | Per linked pass |
| `struct RecLock` | 3920 | 1 | RAII guard for `g_recCs` | — |
| `hkCloneRec` | 3924-3935 | 12 | Serialise pass-state clones | Render thread, per clone |
| `linkShader` | 3937-4006 | 70 | Lazily build pass-state records | Per shader lookup (rec) |
| `hkLinkAll` | 4010-4023 | 14 | Replace the boot linker | Once |
| `shader10` | 4027-4064 | 38 | Get or generate an 'x' 1.0 shader | Per 'x' lookup; generation cold |
| `hkShaderByName` | 4068-4079 | 12 | Shader by name, with 1.0 shaders and lazy link | Per lookup (rec) |
| `mergeVtxf` | 4083-4090 | 8 | Merge vformat.vtxf; sets `g_vf30`/`g_tails10` | Once |
| `Pentane_Main` | 4098-4359 | 262 | Plugin entry (in Pentane's `DllMain`) | Once, under the loader lock |
| `DllMain` | 4361 | 1 | No-op | — |

### 2. Hooks installed by `Pentane_Main` (exact name strings; verified row by row)
Columns:
- "No cfg" = code defaults: `trace` 1, `serve10` 0, `shaders10` 0, `install10` empty.
- "Rec." = recommended config: `serve10` 1, `shaders10` 1, `trace` 0, `install10` set.
- Detour line numbers are in di1compat.cpp unless stated.

| Line | Name string | Static VA | Detour | Gate | No cfg | Rec. | Frequency |
|---|---|---|---|---|---|---|---|
| 4183 | AssetOpen | 0x0041CB10 | hkOpen 3549 | — | on | on | Per file open |
| 4184 | GameDB locks load | 0x01612FA0 | hkLoadLocks 3252 | — | on | on | Rare |
| 4185 | GameDB inventory load | 0x014C08D0 | hkLoadInventory 3403 | — | on | on | Rare |
| 4186 | GameDB CHD callback load | 0x018DB450 | hkLoadChdCallback 1163 | — | on | on | Rare |
| 4188 | GameDB callback load | 0x018DB270 | hkCbLoad 3316 | bits10 ‖ perc10 | on | on | Rare; re-entered via cbLoad |
| 4190 | combat SpawnEconomyOnDeath get | 0x00FF69C0 | hkEconOnDeath 3341 | econ10 | on | on | Per getter until decided |
| 4192 | expendables package load | 0x0160AA30 | hkExpPkg 3298 | popin10 ‖ deliv10 | on | on | Per package |
| 4194 | zone currency (currency10) | 0x00E21850 | hkZoneCurrency 2569 | currency10 | on | on | Per filter-currency query |
| 4195 | UI_GetPlaysetCurrency (currency10) | 0x00B54B50 | hkPlaysetCurrency 2577 | currency10 | on | on | Per Lua call |
| 4196 | HUD currency (currency10) | 0x00FBB420 | hkHudCurrency 2594 | currency10 | on | on | Per HUD update (UNVERIFIED) |
| 4199 | expendable definition ctor | 0x016022C0 | hkExpDef 3359 | popin10 | on | on | Per definition |
| 4200 | VR playset character slot | 0x00B7F910 | hkPlaysetChar 1183 | — (no key; behaviour-changing) | on | on | Likely per frame (comment 1177, UNVERIFIED) |
| 4202 | actor exists (trace) | 0x00751750 | hkActorExists 1197 | trace | on | off | Per query; logs every call |
| 4203 | figure check (trace) | 0x00DDAE30 | hkFigureOk 1211 | trace | on | off | Logs every call |
| 4204 | figure playset (trace) | 0x00435080 | hkFigurePlayset 1367 | trace | on | off | Logs every call |
| 4205 | reader list (trace) | 0x00427330 | hkReaderList 1337 | trace | on | off | Builds a string per call |
| 4206 | reader item gate (trace) | 0x00DEB610 | hkReaderItemOk 1357 | trace | on | off | Builds a string per call |
| 4207 | playset figure slot (trace) | 0x00DF9FD0 | hkIgpName 1350 | trace | on | off | First call arms DR0 (B-N2) |
| 4208 | asset instance (trace) | 0x007734F0 | hkAssetInst 1380 | trace | on | off | Per SubNetwork instance; logs every call |
| 4209 | SubNetwork instance guard (trace) | 0x004FF0E0 | hkInst 1394 | trace | on | off | Per instance; **behaviour-changing guard** (B-04) |
| 4210 | header clone (trace) | 0x004F5900 | hkHdrClone 3169 | trace | on | off | Per header clone (+1 resolver call) |
| 4211 | start lookup (trace) | 0x013A85D0 | hkStartLookup 1432 | trace | on | off | Rare |
| 4212 | AsyncActorCreator execute (trace) | 0x0075A290 | hkAacExec 1990 | trace | on | off | Per async create |
| 4213 | avatar spawn callback (trace) | 0x00A9B3E0 | hkAvSpawned 2001 | trace | on | off | Rare |
| 4214 | cho make (trace) | 0x014523B0 | hkChoMake 2067 | trace | on | off | Per cho make |
| 4215 | AvatarSpawning create (trace) | 0x00A9F080 | hkAvCreate 1980 | trace | on | off | Rare |
| 4216 | cho has (trace) | 0x014835C0 | hkChoHas 1970 | trace | on | off | Per query |
| 4217 | PlaceToy callback (trace) | 0x00A8B670 | hkPlaceToy 2078 | trace | on | off | Rare |
| 4218 | event post (trace) | 0x0068BBB0 | hkPost 1461 | trace | on | off | Every event post |
| 4219 | level loader tick (trace) | 0x00D00530 | hkLevelTick 1474 | trace | on | off | Per tick |
| 4220 | level load-done check (trace) | 0x00CCC450 | hkLoadDone 1485 | trace | on | off | Per poll |
| 4221 | AvatarSpawning finalize (trace) | 0x00AA36F0 | hkAvFinal 1497 | trace | on | off | Rare |
| 4222 | AvatarSpawning remove (trace) | 0x00A9EC70 | hkAvRemove 1498 | trace | on | off | Rare (stack walk) |
| 4223 | start query (trace) | 0x0145F720 | hkStartQuery 1445 | trace | on | off | Per query |
| 4224 | MNode PlayerStartPosNode (trace) | 0x0148A790 | hkMkStart 3154 | trace | on | off | Per node; logs every call |
| 4225 | MNode LocatorNode (trace) | 0x01480090 | hkMkLocator 3155 | trace | on | off | Per node |
| 4226 | MNode SpawnExpendable (trace) | 0x017BF020 | hkMkSpawnExp 3156 | trace | on | off | Per node |
| 4227 | header make (trace) | 0x004FAE90 | hkHdrMake 1413 | trace | on | off | Per header creation |
| 4231 | shader by name | 0x0045C820 | hkShaderByName 4068 | shaders10 | off | on | Per lookup |
| 4233 | shader linker (deferred) | 0x00472050 | hkLinkAll 4010 | shaders10 && lazyLink10 | off | on | Once |
| 4234 | pass-state clone (locked) | 0x0045C5C0 | hkCloneRec 3924 | shaders10 | off | on | Render thread, per clone |
| 4237 | MissionContext update (trace) | 0x0166FFB0 | hkMissionTick 2667 | missionTrace | off | off | Per frame per context |
| 4238 | FocusPoint_Actor update (missiontrace) | 0x015DF390 | hkFpUpdate 3026 | missionTrace | off | off | Per record update |
| 4239 | FocusPointActor Update pin (missiontrace) | 0x01730E80 | hkFpNodeUpdate 3060 | missionTrace | off | off | Per pin |
| 4241 | FocusPointActor tag apply (missiontrace) | 0x0173A3F0 | hkFpApply 3106 | missionTrace | off | off | Per apply |
| 4242 | FocusPoint_Actor tag scan (missiontrace) | 0x015D5F80 | hkFpScan 3126 | missionTrace | off | off | Per scan |
| 4243 | handle to actor (missiontrace) | 0x0074B200 | hkActorOf 3086 | missionTrace | off | off | Very frequent (UNVERIFIED) |
| 4246 | choreography start (cinesync10 / breaktrace) | 0x01449390 | hkChoStart 1624 | breakTrace ‖ (audio10 && cineSync10) | on | on | Per cho start (B-20: lacks install10) |
| 4248 | ClipProcessor evaluate (breaktrace) | 0x00543F40 | hkClipEval 1542 | breakTrace | off | off | Per clip evaluation |
| 4249 | exploder munition impact (breaktrace) | 0x0151E420 | hkMunHit 1740 | breakTrace | off | off | Event |
| 4250 | exploder explode (breaktrace) | 0x0151E130 | hkExplode 1752 | breakTrace | off | off | Event |
| 4251 | choreography audio-sync wait (breaktrace) | 0x01442EE0 | hkChoReady 1591 | breakTrace | off | off | Event |
| 4252 | choreography play (breaktrace) | 0x01445E70 | hkBtPlay 1610 | breakTrace | off | off | Event |
| 4253 | layout attachments apply (breaktrace) | 0x00788280 | hkLayApply 1641 | breakTrace | off | off | Event |
| 4254 | layout attachments gate (breaktrace) | 0x00785B40 | hkLayGate 1667 | breakTrace | off | off | Event |
| 4255 | layout attachment bind (breaktrace) | 0x00787830 | hkLayBind 1732 | breakTrace | off | off | Event |
| 4256 | clip setup (breaktrace) | 0x00552FC0 | hkClipSet 1706 | breakTrace | off | off | Event |
| 4257 | music cue push (breaktrace) | 0x0073C5C0 | hkMusicPush 1720 | breakTrace | off | off | Event |
| 4260 | Zoo::Layout dtor (teardowntrace) | 0x00787BC0 | hkLayoutDtor 1807 | teardownTrace | off | off | Per dtor |
| 4261 | Samurai::Node dtor (teardowntrace) | 0x004EE5F0 | hkNodeDtor 1865 | teardownTrace | off | off | Per dtor |
| 4262 | hierarchy entry release (teardowntrace) | 0x004B2780 | hkEntryRel 1892 | teardownTrace | off | off | Per release |
| 4263 | hierarchy entry free (teardowntrace) | 0x004E1A40 | hkEntryFree 1924 | teardownTrace | off | off | Per free |
| 4264 | Layout placement bind (teardowntrace) | 0x0077D9C0 | hkPlBind 1849 | teardownTrace | off | off | Per bind |
| 4265 | block tracker delete (teardowntrace) | 0x00416840 | hkTrackFree 1939 | teardownTrace | off | off | Per delete |
| 4266 | heap free (teardowntrace) | 0x00409E10 | hkHeapFree 1954 | teardownTrace | off | off | Every engine free |
| 4269 | Meridian class registry | 0x0178E330 | hkMerRegistry 2705 | (aliases10 ‖ native10) && g_merOk | on* | on* | Once (*if di1compat_mer.txt loads) |
| 4271 | GamePlayers lock controls (locktrace10) | 0x0139B8D0 | hkCtlLock 2749 | lockTrace10 | on | on | Rare |
| 4272 | GamePlayers unlock controls (locktrace10) | 0x0139B960 | hkCtlUnlock 2755 | lockTrace10 | on | on | Rare |
| 4273 | controller context apply (locktrace10) | 0x0121D5C0 | hkCtxOn 2763 | lockTrace10 | on | on | Event |
| 4274 | controller context remove (locktrace10) | 0x0121D8C0 | hkCtxOff 2769 | lockTrace10 | on | on | Event |
| 4275 | choreography node play (locktrace10) | 0x017E91B0 | hkChoPlay 2777 | lockTrace10 | on | on | Event |
| 4276 | choreography node event (locktrace10) | 0x017E5890 | hkChoEvent 2785 | lockTrace10 | on | on | Per event |
| 4277 | message sender send (locktrace10) | 0x018120B0 | hkMsgSend 2796 | lockTrace10 | on | on | Per message |
| 4278 | message handler delivery (locktrace10) | 0x018049F0 | hkMsgRecv 2807 | lockTrace10 | on | on | Per delivery |
| 4279 | mission monitor fire (locktrace10) | 0x01730490 | hkMonPin 2831 | lockTrace10 | on | on | Event |
| 4280 | load screen active update (locktrace10) | 0x0145B9A0 | hkLoadTick 2843 | lockTrace10 | on | on | Per node update (likely per frame) |
| 4281 | master net sync Link_In (locktrace10) | 0x006EC4F0 | hkNetSync 2858 | lockTrace10 | on | on | Per Link_In |
| 4283 | colvis toggle parameters (locktrace10) | 0x01808FE0 | hkColVisLoad 2882 | lockTrace10 | on | on | Per load |
| 4285 | colvis toggle apply (locktrace10) | 0x01803550 | hkColVisApply 2888 | lockTrace10 | on | on | Per apply |
| 4286 | beacon map lookup (locktrace10) | 0x01591570 | hkBeaconLookup 2903 | lockTrace10 | on | on | Per lookup (UNVERIFIED rate) |
| 4287 | compass mission track (locktrace10) | 0x015BA4D0 | hkTrack 2926 | lockTrace10 | on | on | Rare |
| 4288 | compass draw (locktrace10) | 0x00C13C50 | hkNavUpdate 2952 | lockTrace10 | on | on | Likely per frame per avatar |
| 4289 | mission progress entries (locktrace10) | 0x0157CD10 | hkMisProgress 2988 | lockTrace10 | on | on | Per log write |
| 4293 | opacity mask (log) | 0x0044AD80 | hkOpacityMask 2444 | opacityLog10 | off | off | Per object per frame |
| 4295 | visibility test (fadeprobe10) | 0x0044B8F0 | hkVisTest 2502 | fadeProbe10 | off | off | Per node per frame |
| 4296 | frame sync (fps log) | 0x0047AAB0 | hkFrameSync 2178 | fpsLog10 ‖ loadProf10 | off | off | Per frame |
| 4298 | fiber switch (loadprof10) | 0x0041E480 | hkFiberSwitch 2114 | loadProf10 | off | off | Every fiber switch |
| 4299 | file-done wait (loadprof10) | 0x0041FC10 | hkFileWait 2137 | loadProf10 | off | off | Per wait |
| 4300 | .oct asset load (loadprof10) | 0x00786D00 | hkOctLoad 2151 | loadProf10 | off | off | Per .oct load |
| 4304 | asset defs lookup | 0x00703340 | hkAssetDef 2051 | — | on | on | Per DefID lookup |
| 4305 | CRT __invoke_watson | 0x012CA1D1 | hkWatson 3412 | — | on | on | Fail-fast only |
| 4306 | resident path normaliser | 0x0041FEF0 | hkNormPath 3432 | — | on | on | Per resident lookup |
| 4311 | Slang linker (1.0 natives) | 0x00736DA0 | sx::hkLink (sx.hpp:669) | sx10 && sxNatives10 | on | on | Per script link |
| 4313 | Slang call by name (1.0 behavior type) | 0x00739090 | sx::hkCallByName (sx.hpp:699) | + sxPorts10 && sxBeh10 | on | on | Per call (UNVERIFIED) |
| 4320 | zone string field (ui: 1.0 loading screen) | 0x0139E0B0 | hkZoneStr 3451 | serve10 && loadScreen10 | off | on | Per zone-string read (also via zoneCurrency10) |
| 4322 | FMV path (ui: 1.0 clips) | 0x0067E460 | hkFmvPath 3475 | serve10 && fmv10 | off | on | Per FMV |
| 4327 | screen by name (ui: tutorial element 61) | 0x0154FF90 | hkFindScreen 3497 | serve10 && tutorial10 && bytes at 0x00DE4C5A | off | on | Per screen lookup |
| 4331 | controller context list (ui: 1.0 tutorial contexts) | 0x01224880 | hkCtxParse 3515 | serve10 && context10 | off | on | Per parse |
| 4345 | particle package load (vfx) | 0x00672620 | vfxrt::hkPkg (vfxrt.hpp:38) | vfx10 && vfxUniversal10 | on | on | Per package |
| 4348 | effect create (vfx trace) | 0x0065E3A0 | vfxrt::hkEffect (vfxrt.hpp:89) | vfxTrace | off | off | Per spawn |
| 4349 | emitter create (vfx trace) | 0x0066A1C0 | vfxrt::hkEmitter (vfxrt.hpp:98) | vfxTrace | off | off | Per spawn |
| 4350 | slaved emitter create (vfx trace) | 0x0066E7F0 | vfxrt::hkAttached (vfxrt.hpp:112) | vfxTrace | off | off | Per spawn |
| 4356 | start lookup (spawntrace10) | 0x013A85D0 | hkStartLookup 1432 | spawnTrace10 && !trace | off | off | Rare |
| 4357 | start query (spawntrace10) | 0x0145F720 | hkStartQuery 1445 | spawnTrace10 && !trace | off | off | Rare |

**Installed by subsystem `install()` calls in `Pentane_Main`** (gates inside each header; verified):
- **npcrt::install** (4316; npcrt.hpp:411-422):
  - spawner10: `0x010C2F70`
  - gamedb30: `0x0070D730`, `0x0070DF70`
  - gamedb30 && avperc10: `0x0152AE80`, `0x0152AFA0`
  - All default on: 5 in both configs.
- **audio::install** (4338, gate `audio10 && install10[0]`; audio.hpp:2193-2239):
  - 9 unconditional: `0x0182DB30`, `0x01948A10`, `0x01947A50`, `0x01947150`, `0x019478D0`, `0x019475A0`, `0x01947640`, `0x01948C20`, `0x0182C460`
  - music: `0x007397B0`, `0x01947BD0`, `0x0073EDE0`, `0x0073ED10`
  - vo: `0x01948D80`, `0x0182F570`, `0x0073BF50`
  - audioTrace10: `0x01830270`
  - 0 with no cfg; 16 with the recommended config (returns early with no hooks only if the host thread cannot be created).
- **gprt::install** (4352; gprt.hpp:460-489):
  - lockTrace10: `0x00D612A0` (vehicle10 probe)
  - accept10: `0x015BCC10`
  - tools10: `0x0150F7D0`
  - selectMenu10: `0x00B42540`, `0x00B68F60`, `0x0156DDE0`, `0x0156A360`
  - actionMap10 ‖ lockTrace10: `0x01564790`
  - focus10: `0x01559480`, `0x01554590`
  - lockTrace10 ‖ keys10: `0x01565330`, `0x00B48250`
  - 12 in both configs.
- **bldrt::install** (4353; bldrt.hpp:105-113):
  - building10 ‖ spawnTrace10: `0x00CEF120`
  - spawnTrace10: `0x00CEFE60`, `0x011854B0`
  - 1 in both configs.
- **caprt::install** (4354; caprt.hpp:202-218):
  - shutdown10 && popin10: `0x007534C0`, `0x01860B40`
  - that ‖ capsuleTrace10: `0x0160A3E0`, `0x0160A270`
  - capsuleTrace10: `0x017BF050`, `0x0160EBD0`, `0x00CBC5C0`, `0x0160F370`
  - 4 in both configs.

**Totals** (mechanical grep of `hook("`):
- 149 call sites (`Pentane_Main` 104; audio 17, gprt 12, caprt 8, npcrt 5, bldrt 3) and 147 distinct VAs.
- Installed with no cfg: **85** (`Pentane_Main` 63, or 62 without the mer table; npcrt 5; gprt 12; bldrt 1; caprt 4).
- Installed with the recommended config: **82** (`Pentane_Main` 44; npcrt 5; audio 16; gprt 12; bldrt 1; caprt 4). 18 of them are log-only lockTrace10 probes.

**VAs hooked twice:** exactly two.
- `0x013A85D0`: 4211 (trace) and 4356 (spawntrace10)
- `0x0145F720`: 4223 and 4357

The gate `spawnTrace10 && !trace` (4355) makes the pairs mutually exclusive. If both ever ran:
- The second `hook()` would fail its prologue check (MinHook has already written `E9 rel32`), log `hook … refused: prologue differs`, and never reach MinHook.
- Even if it did reach MinHook, `MH_CreateHook` returns `MH_ERROR_ALREADY_CREATED` (minhook/hook.c:658) without touching `*ppOriginal` (written only at 633-634 on success).
- The trampolines stay valid either way. Harmless, but the log line would mislead.

**Overlaps:** the closest hooked VAs are `0x017BF020` (4226) and `0x017BF050` (caprt.hpp:215), 48 bytes apart; the next closest pair is 144 bytes apart. MinHook patches 5 bytes, so no patched ranges overlap.

### 3. Generic helpers in this range (verified)
| Name | Signature | Line | Purpose |
|---|---|---|---|
| qpcMs | `static double qpcMs()` | 2218 | QPC milliseconds |
| noteOpenThread | `static void noteOpenThread()` | 2228 | Record the calling thread ID (≤8) |
| copyStack | `static int copyStack(const uint32_t*, uint32_t*, int)` | 2238 | Region-bounded stack copy |
| afterCallSafe | `static bool afterCallSafe(uint32_t)` | 2247 | `__try` + `afterCall` |
| ourRetSafe | `static bool ourRetSafe(uint32_t)` | 2253 | Return address into own DLL |
| symbolAt | `static std::string symbolAt(uintptr_t)` | 2264 | Nearest export symbol |
| cstr | `static const char* cstr(const void*, char*, size_t)` | 2735 | Checked CString member copy |
| callerVa | `static uint32_t callerVa(void*)` | 2742 | Runtime → static VA (dup of `vfxrt::retVa`) |
| lockTraceRoom | `static bool lockTraceRoom(volatile LONG& = g_traceLines)` | 2746 | Line budget |
| colVisMode | `static const char* colVisMode(uint8_t)` | 2876 | Bits → text |
| fpScopeText | `static const char* fpScopeText(char*, size_t)` | 3097 | Format FpScope |
| hdrName | `static void hdrName(const uint32_t*, char*, size_t)` | 3165 | Header name under `__try` |
| gameDbLoad | `static uint32_t gameDbLoad(GameDbLoadFn, const char*, void*, const char*, uint32_t, uint32_t)` | 3220 | CHD/Lua mode toggle |
| cbLoad | `static uint8_t cbLoad(void*, const char*, uint32_t, uint32_t)` | 3237 | Engine CString around vtable[1] |
| singletonLoader | `static void* singletonLoader(uint32_t)` | 3246 | Checked singleton → loader |
| fnv64 | `static uint64_t fnv64(const std::string&)` | 3779 | FNV-1a 64 (cache names; not interchangeable with `audio::fnv1`) |
| finalizeShd10 | `static void finalizeShd10(uint8_t*)` | 3878 | Asm thunk |
| inputLayout10 | `static uint32_t inputLayout10(uint32_t, uint32_t)` | 3892 | Asm thunk |
| RecLock | `struct RecLock` | 3920 | RAII `EnterCriticalSection` |
| slurpFile (lambda) | `std::string(const char*)` | 4141 | Whole-file read (64 KB stack buffer) |
| (inline) log rotation | — | 4105-4110 | Rotate `.1`..`.5` logs |

### 4. Globals and statics declared in 2210-4361 (verified)
| Name | Line | Type | Writers | Guard |
|---|---|---|---|---|
| g_openTidN / g_openTids[8] | 2226-2227 | volatile LONG / DWORD[8] | noteOpenThread | Interlocked index; benign publication race |
| g_selfLo / g_selfHi | 2251 | uintptr_t | profThread start | Sampler only |
| g_profPath | 2294 | std::string | Pentane_Main 4301 | Before CreateThread |
| g_playsets10 / g_missionPrefix10 | 2532-2533 | vector | zones10 | g_playsets10Lock (2534) |
| g_currency10 | 2545 | map\<string,string> | zones10 2638 | g_currency10Lock (2546) |
| g_rdoc | 2666 | RENDERDOC_API_1_1_2* | Pentane_Main 4125 | Write-before-hooks |
| g_probeClass | 2703 | int | hkMerRegistry | Write-once |
| g_lockLines, g_traceLines | 2745 | volatile LONG | Probe hooks | Interlocked |
| g_gateLines | 2828 | volatile LONG | hkLoadTick, hkNetSync | Interlocked |
| g_fpScope | 3083 | thread_local FpScope | hkFpApply/Scan/ActorOf | Per thread (fibers share; B-10) |
| g_nStart/g_nLocator/g_nSpawnExp | 3153 | volatile LONG | MNode hooks | Interlocked |
| g_hdrLogged | 3164 | volatile LONG | hkHdrClone | Interlocked |
| g_d3dCompile / g_shaderCache | 3773-3774 | fn ptr / string | Pentane_Main (boot) | Read-only after boot |
| g_sh10Lock / g_sh10 / g_prog10 | 3775-3777 | mutex / maps | shader10, registerProgram10 | g_sh10Lock |
| g_compiling10 | 3786 | thread_local string | shader10 | Same call chain |
| g_recCs / g_linked | 3918-3919 | CRITICAL_SECTION / set | linkShader, hkCloneRec | g_recCs (init 4230, shaders10 only; B-23) |
| Hook trampolines (~65; e.g. g_opacityMask 2443 … g_shaderByName 4067; g_linkAll 4009 never called) | — | fn ptrs | MinHook create | Written before enable |

Function-local statics:

| Owner | Lines | Statics | Guard |
|---|---|---|---|
| qpcMs | 2220 | `f` | Benign init race |
| noteOpenThread | 2230 | `seen` (thread_local) | — |
| hkOpacityMask | 2447-2448, 2463 | `m`, `seen`, `dumped` | Guarded by `m` |
| hkVisTest | 2507-2509 | `m`, `last`, `lines` | Guarded by `m` |
| currency10 | 2553 | `told` | g_currency10Lock |
| hkMissionTick | 2672, 2677, 2692 | `armed`, `stateVa`, `last` | **Unguarded** (B-48) |
| hkMonPin | 2833, 2836 | `kPin`, `lines` | Atomic |
| hkLoadTick | 2847-2848 | `m`, `seen` | Guarded |
| hkColVisLoad / hkColVisApply | 2885, 2890 | `lines` | Atomic |
| hkBeaconLookup | 2909-2913 | `m`, `seen`, `lines` | Guarded |
| hkTrack | 2936 | `lines` | Atomic |
| hkNavUpdate | 2967-2968, 2974 | `last[6]`, `lastT[3]`, `lines` | **Unguarded, shared** (B-29) |
| hkMisProgress | 2997-3000 | `m`, `seen`, `lines` | Guarded |
| hkFpUpdate / Apply / Scan | 3046-3055, 3116-3123, 3139-3146 | `m`, `last`, `lines` | Guarded |
| hkFpNodeUpdate | 3066 | `lines` | Atomic |
| registerDelivery10 | 3288-3289 | `m`, `done` | Guarded |
| hkZoneStr | 3456-3458 | `mx`, `counter`, `pick` | Guarded; `pick` escapes (B-26) |
| hkOpen | 3734-3736 | `stacks`, `kinds`, `perKind` | Interlocked |
| compile10 | 3813-3814 | `magenta`, `bufview[5]` | Magic statics |
| registerProgram10 | 3860 | `next[2]` | g_sh10Lock |
| hkCloneRec | 3931 | `told` | g_recCs |
| linkShader | 3986 | `told` | g_recCs |

### Constraints the refactor must respect (amended)
- **Loader lock.** `Pentane_Main` runs inside Pentane.dll's `DllMain` (B-N1). Nothing reachable from it may wait on another thread.
- **SEH vs C++ unwinding.**
  - `__try` functions (`afterCallSafe` 2249, `ourRetSafe` 2256, `hdrName` 3167, plus `hkAssetDef` 2056 and `hkHdrMake` 1416 outside the slice) must not gain objects with destructors (C2712).
  - RAII hooks (`std::lock_guard`; `hkVisTest`'s `Unfade` 2506, whose destructor writes `out[1] = 1` on every return after 2506 when `fadeProbe10 == 2`) cannot gain `__try`.
  - The build uses `/EHsc` (CMake default).
- **First-chance faults.** These are costly, not fatal. A fault under `__try` is handled by its `__except`, and the game survives (B-N3).
  - Each AV costs a CrabeLoader crash report (one of 3 slots).
  - Each AV and each C++ throw costs a slow, symbolised Pentane log entry.
  - The one fatal case is a fault raised inside a vectored handler (`avHandler` at 12:01; `watchHandler` B-N2).
  - Keep "check before read".
- **Calling conventions.** thiscall as `__fastcall(self, edx-dummy, …)`; the asm thunks byte-identical (B-44); `__stdcall Pentane_Main` exported via `.def`.
- **Order.** See B-01. Keep `linkShader`'s record publication order (3969-3999).
- **Log text and names.** Keep hook names, `served …`, `open … -> MISSING`, `prof …`, `config …` lines byte-identical. Keep `fnv64` and the HLSL text, which key the shader cache.
- **Side effects inside `logf` arguments** (B-17).

### Cross-slice notes (outside 2210-4361)
- `fallbackDef` (2020-2049) holds `g_defsMx` across `read30` → `g_open`, an engine open that may yield to another fiber (B-09). `hkAssetDef` (2056) wraps it in `__try`.
  - Whether an AV inside `g_open` would leave `g_defsMx` locked under `/EHsc` is UNVERIFIED. A fault inside a called function is usually at a tracked call-site state, so the `lock_guard` destructor would likely run during the SEH unwind.
  - The generated unwind map, or a test, would settle it.
- `tdStatic` (1522) uses image size `0x0209B000`; the rest of the file uses `0x2100000`.
- `logf` (82) flushes every line. `zipFor` (387) caches `nullptr` permanently. `read10z` (405-451) inflates while holding `g_zipLock`.
- The comment at 1225-1227 needs the correction in B-N3.

## Rejected on verification
- **B-22 Install-order windows after AssetOpen is live:** not reachable.
  - `Pentane_Main` runs inside Pentane.dll's `DllMain`, under the loader lock, before the game's entry point (audio.hpp:2137-2139; Pentane.dll calls the plugin entries at `0x101c2f6a`).
  - No engine code exists yet to open files while the later hooks, `sx::g_log`, `audio::cfg` and the `g_over10` alias are set up.
  - The comment "AssetOpen is live already" (4341) is defensive. The ordering invariants themselves are kept as refactor rules in B-01.

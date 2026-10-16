# Scripts, system data and NPCs (`sx.hpp`, `sys.hpp`, `npc.hpp`, `npcrt.hpp`)

Scope: all four files read in full (sx 899, sys 731, npc 732, npcrt 425 lines), plus every caller and dependency a finding
relies on: `luachunk.hpp` (whole), `di1compat.cpp` (config 131-237, log/safeCopy/hook 66-128, read10/read30/read30x/memFile
370-527, indexZip10 590-606, the transcode dispatch 1073-1112, `::readable`/stack walk 1220-1280, the lockTrace10 probes
2735-3010, the GameDB hooks 3234-3400, hkOpen 3549-3740, Pentane_Main 4170-4361), the `readable` users in `gprt.hpp`,
`caprt.hpp`, `bldrt.hpp`, `vfxrt.hpp`, `oct.hpp`/`gen30.hpp`/`audio.hpp` where cited, and the octtest handlers
(octtest.cpp 2530-2937). Engine claims about return values and callee-pop sizes were checked by disassembling the
installed `DisneyInfinity3.exe` (capstone, read-only). Nothing in the repository was edited, built or run.

Verification: 60 findings checked: 39 confirmed, 18 corrected, 2 unverified, 1 rejected; 2 new.

**Config facts used for every hot-path statement.** Code defaults: `serve10 = 0`, `trace = 1`, `lockTrace10 = 1`; the
recommended play config (`di1compat.cfg.example`) is `serve10 = 1`, `shaders10 = 1`, `trace = 0`, the rest at code
default. Every 1.0 asset (scripts, .bmx/.btx, DataMaps) is served only with `serve10` (hkOpen 3627, 3645), so under code
defaults no DI1 script is ever linked, `prepare()` never runs and the sx natives are never called. Hooks that are installed
and active in BOTH configs: the npcrt include/unload/perception hooks (`gamedb30`, `avperc10`), hkCallByName
(`sx10 && sxNatives10 && sxPorts10 && sxBeh10`), and the probes gated by `lockTrace10`, `keys10`, `actionMap10`.

## Findings
| ID | Title | Sev | Class | Where |
|---|---|---|---|---|
| NR-01 | Exception between `delete g_db` and the new GameDb leaves `g_db` dangling (bad_alloc only) | Med | BEHAVIOUR-CHANGE | npcrt.hpp:240-241 |
| SX-N1 | Engine relink rewrites every import slot before `bind()` restores it (race window) | Med | DOC-ONLY | sx.hpp:629-676; exe 0x00736E26 |
| NR-02 | `g_dbLock` held across zip/archive I/O, DataMap parsing and engine calls | Med | NEEDS-VERIFICATION | npcrt.hpp:297-326, 150-171 |
| NP-03 | Mixed error channels; a throw in `skinRemap` serves the whole .oct raw | Med | BEHAVIOUR-CHANGE | npc.hpp:228, 337-349 |
| NP-02 | 32-bit size_t wrap in the Lua header checks; `luaStrings` walks past the buffer | Med | BEHAVIOUR-CHANGE | npc.hpp:302, 306, 569 |
| NP-04 | Negative index read in `skinRemap` | Med | BEHAVIOUR-CHANGE | npc.hpp:239-252 |
| NP-N1 | `luachunk::eval` indexes its 306 registers unchecked (LOADNIL writes past the vector) | Med | BEHAVIOUR-CHANGE | luachunk.hpp:127-151 (via sys.hpp:690) |
| SX-05 | `readStr` relies on `__try` alone; every fault reaches the vectored handlers first | Low | BEHAVIOUR-CHANGE | sx.hpp:129-137, 688-696 |
| XF-02 | `safeCopy` checks one page only; inconsistent guarded copies | Med | BEHAVIOUR-CHANGE | di1compat.cpp:87-102; sys.hpp:612-619; sx.hpp:129 |
| SX-02 | `keyId` builds a heap string per call; unpaired keys lock a mutex per call | Med | SAFE-REFACTOR | sx.hpp:249-272 |
| SY-02 | Two `readable()` implementations; the unqualified one is the slow one | Med | SAFE-REFACTOR | sys.hpp:180-196; di1compat.cpp:1228-1240 |
| NP-01 | Five Lua 5.1 chunk walkers with diverging semantics | Med | NEEDS-VERIFICATION | npc.hpp:293-354, 562-601; sx.hpp:725-786; luachunk.hpp:32-159 |
| SX-09 | Stack-balance adjustments are bare numbers; port signatures duplicated | Med | NEEDS-VERIFICATION | sx.hpp:281-344, 584-601 |
| SX-01 | sx.hpp holds three subsystems plus generated tables | Med | SAFE-REFACTOR | sx.hpp:41-897 |
| SY-01 | sys.hpp header comment is stale; engine glue mixed with pure helpers | Med | SAFE-REFACTOR | sys.hpp:1-2, 180-226, 595-672 |
| SY-05 | `perceptionInfoLua` god function with a shared scratch string | Med | NEEDS-VERIFICATION | sys.hpp:364-589 |
| NR-03 | Long multi-purpose functions (`prepare`, `hkInclude`, `hkPercApply`) | Med | NEEDS-VERIFICATION | npcrt.hpp:144-204, 226-280, 293-344 |
| SX-03 | Behaviour-type renumbering calls the engine up to 30 times per conversion | Low | NEEDS-VERIFICATION | sx.hpp:370-386 |
| SX-04 | Locked increment on every stub call | Low | SAFE-REFACTOR | sx.hpp:125 |
| SX-06 | `bind()` allocates and locks on every relink | Low | SAFE-REFACTOR | sx.hpp:631-664 |
| SX-07 | `bind`'s `names` map never shrinks | Low | DOC-ONLY | sx.hpp:637 |
| SX-08 | First-link detection relies on DLL addresses > script sizes | Low | DOC-ONLY | sx.hpp:638-645, 654 |
| SX-10 | Near-identical native wrappers | Low | SAFE-REFACTOR | sx.hpp:274-278, 389-396 |
| SX-11 | `provide()` is dead; `stub`'s `n->fn` branch unreachable | Low | SAFE-REFACTOR | sx.hpp:121, 617-627 |
| SX-12 | Static return buffer in `reactionInfoFlipMotionName` | Low | DOC-ONLY | sx.hpp:331-340 |
| SX-13 | Unchecked constant count in `ai::eval` | Low | BEHAVIOUR-CHANGE | sx.hpp:738-739 |
| SX-14 | Quadratic table stores and 256-register setup in `ai::eval` | Low | SAFE-REFACTOR | sx.hpp:749-758 |
| SX-15 | Method existence test inflates whole 1.0 members | Low | NEEDS-VERIFICATION | sx.hpp:805-810; di1compat.cpp:1105 |
| SX-16 | `sx::ai::lower` also maps `\` to `/` | Low | SAFE-REFACTOR | sx.hpp:799-803 |
| SX-17 | Private exe-rebasing helpers duplicate `rt()` | Low | SAFE-REFACTOR | sx.hpp:97-100, 370 |
| SX-18 | `hkCallByName` typed `void` (engine value is incidental) | Low | SAFE-REFACTOR | sx.hpp:697-703 |
| SX-19 | `sx::g_log` set after AssetOpen is hooked; early unpaired keys never logged | Low | NEEDS-VERIFICATION | sx.hpp:259-262; di1compat.cpp:4183, 4307 |
| SX-20 | Unnamed script-header/context/import-entry offsets | Low | SAFE-REFACTOR | sx.hpp:53-81, 89-93, 110-115, 633-655, 685-686 |
| SY-03 | One `readable` syscall per character in `snapshotPerc30` | Low | SAFE-REFACTOR | sys.hpp:612-619 |
| SY-04 | Whole-set copies of lock UIDs | Low | SAFE-REFACTOR | sys.hpp:201-226; di1compat.cpp:3260, 3396 |
| SY-06 | `chestInfoLua` string/list bounds 4 bytes looser than `perceptionInfoLua` | Low | DOC-ONLY | sys.hpp:272-282 vs 374, 389 |
| SY-07 | 32-bit wrap in the list-word bound | Low | BEHAVIOUR-CHANGE | sys.hpp:393-396 |
| SY-08 | Three identical Lua quoting helpers | Low | SAFE-REFACTOR | sys.hpp:245; npc.hpp:492; sx.hpp:787 |
| SY-09 | Repeated path normalisation and "GameDB\|" parsing | Low | SAFE-REFACTOR | sys.hpp:151-171 |
| SY-10 | Mutation through `const Value*` in `groups30` | Low | SAFE-REFACTOR | sys.hpp:695-721 |
| SY-11 | Long comments / long strings not recognised by the text scanners | Low | DOC-ONLY | sys.hpp:43-46, 66-69, 116-117 |
| SY-12 | Unnamed engine offsets and caps; implicit pop-in stamp protocol | Low | SAFE-REFACTOR | sys.hpp:201-216, 608-631, 663-671 |
| NP-05 | Quadratic stores; immutable 3.0 DataMaps re-parsed per playset | Low | NEEDS-VERIFICATION | npc.hpp:328-332; npcrt.hpp:236, 261 |
| NP-06 | Assorted duplicates and the magic `12` | Low | SAFE-REFACTOR | npc.hpp:361, 405, 492, 514, 523; npcrt.hpp:34, 267, 272 |
| NP-07 | `playsetText` rescans everything per group | Low | SAFE-REFACTOR | npc.hpp:539-550 |
| NP-08 | Navmesh size products can wrap (no reachable over-read) | Low | SAFE-REFACTOR | npc.hpp:62, 65 |
| NP-09 | Hand-built oct record type; no guard for blobs >= 16 MiB | Low | SAFE-REFACTOR | npc.hpp:148-149 |
| NP-10 | One namespace for six domains | Low | SAFE-REFACTOR | npc.hpp |
| NP-11 | Full DataMap copy into `GameDb::m10` | Low | SAFE-REFACTOR | npc.hpp:445 |
| NR-05 | Per-call engine work and locking in the perception hooks, in every playset | Low | NEEDS-VERIFICATION | npcrt.hpp:144-223 |
| NR-06 | Stale apply/remove pairing in `g_percOn` | Low | DOC-ONLY | npcrt.hpp:102, 202, 215-219 |
| NR-07 | Unnamed engine globals, offsets and vtable slots | Low | SAFE-REFACTOR | npcrt.hpp (see text) |
| NR-08 | Side effects inside `&&` chains; IIFE in `logf` args | Low | SAFE-REFACTOR | npcrt.hpp:133-134, 163-164, 272 |
| NR-09 | Hidden include-order dependency; stale header comment | Low | SAFE-REFACTOR | npcrt.hpp:1; di1compat.cpp:621 |
| NR-10 | String building and router calls on every include | Low | SAFE-REFACTOR | npcrt.hpp:328, 337 |
| NR-11 | Most npcrt detours have no exception guard | Low | BEHAVIOUR-CHANGE | npcrt.hpp:144-223, 305-371 |
| NR-12 | `remove_if` predicate with side effects | Low | SAFE-REFACTOR | npcrt.hpp:324 |
| NR-13 | Overlapping 1.0 playsets: first playset's tables released at the second's unload | Low | DOC-ONLY | npcrt.hpp:298-331, 346-371 |
| XF-01 | Always-on probes do `readable` + engine work every frame | Low | SAFE-REFACTOR | di1compat.cpp:2952-2979; gprt.hpp:307-345 |
| XF-03 | Duplicated engine CString handling | Low | SAFE-REFACTOR | npcrt.hpp:42-48; di1compat.cpp:3237-3244; bldrt.hpp:60 |
| XF-04 | Two scanners for vformat.vtxf | Low | NEEDS-VERIFICATION | npc.hpp:168-202; gen30.hpp:157-181 |

---

### NR-01 Exception between `delete g_db` and the new `GameDb` leaves `g_db` dangling
- **Where:** `native/di1compat/npcrt.hpp:240-241` (the window); dereferences at 305-307, 312-318; double delete at 240
- **Severity / category / class:** Med · exception safety (use-after-free, double free) · BEHAVIOUR-CHANGE (failure path only)
- **Evidence:** `delete g_db;` then `g_db = new npc::GameDb(npc::gamedb30(m10, m30));`. Every throw that malformed data can
  cause (luaEval's `.at()` inside `npc::dataMap`, 234-236) happens before the delete. Inside the window the only possible
  exception is `std::bad_alloc`: the whole-DataMap copy at npc.hpp:445, the sets/maps/vectors in `gamedb30`, `operator new`,
  and MSVC's node-container move constructor (it allocates a sentinel). `m30.at(rk)` (npc.hpp:468) cannot throw, because
  `reach` inserts only keys it found (npc.hpp:416). After the throw, hkInclude's catch (302-304) carries on with the old
  pointer and the old `g_dbPlayset`:
  1. the same include, if the previous 1.0 playset is still active: `npc::reach(g_db->m10, …)` (312-314);
  2. any later Playset include of the previous playset's name: `prepare` returns at 231 (`g_db && g_dbPlayset == n`
     compares only), then 305-307 run `todo = g_db->playset`;
  3. the next `prepare` that reaches 240 deletes the same pointer again.
  hkPercApply/hkPercRemove never touch `g_db`; hkPlaysetUnload only tests it for null (353).
- **Why it matters:** heap corruption after an allocation failure. In practice it needs an out-of-memory condition in the
  32-bit process, so it is rare.
- **Fix (behaviour-preserving):** build first, then swap:
  `npc::GameDb* db = new npc::GameDb(npc::gamedb30(m10, m30)); delete g_db; g_db = db;`. The success path is identical (same
  object, same log lines, same order). On failure the previous supplement stays valid (old `g_db`/`g_dbPlayset`) instead of
  UB: that is the behaviour change, and it is the only sane choice. Do not make `g_db` a namespace-scope `unique_ptr`: that
  adds a destructor run at DLL detach (an exit-time behaviour change).
- **Refactor risk:** keep the log at 244 after the assignment and `g_psPath.clear()` where it is. If combined with NP-11
  (move m10 in), read `m10.size()` before the move.
- **Verification:** CORRECTED: the trigger is `std::bad_alloc` only (no data-driven throw exists inside the window); the
  three dereference paths are listed; the fix avoids `unique_ptr` because of the exit-time destructor.

### SX-N1 The engine relink rewrites every import slot before `bind()` restores it (NEW)
- **Where:** `native/di1compat/sx.hpp:629-676` (`bind`, `hkLink`); engine linker FUN_00736DA0
- **Severity / category / class:** Med · thread safety / hidden coupling · DOC-ONLY (UNVERIFIED reachability; see Fix)
- **Evidence:** disassembly of FUN_00736DA0: after the one-time fix-up (FUN_007366F0, skipped once a header carries
  0xCAFEBEEF), the loop at 0x00736DD2-0x00736E30 looks up every import in the registry and stores the result with
  `mov [edi+8], ecx` (0x00736E26). It does this unconditionally, on every call. sx.hpp:630 says the engine relinks cached
  scripts on every request. For a DI1 script the engine therefore first writes its own value into each slot, and only
  then does `hkLink` → `bind()` write the DLL native back (sx.hpp:654-655). In between, the slot holds null for the
  natives 3.0 lacks (the CALL handler skips a null slot without popping its arguments: sx.hpp:9-10), the 3.0 native for
  the 29 registered with another signature, and the 3.0 Hash*/AiBehavior natives for the ported names (they read a 1.0
  string key as 0).
- **Hot path?:** code defaults: never (no DI1 scripts). Recommended config: every request of a cached DI1 script.
- **Why it matters:** any thread that runs the same cached script during that window gets a VM stack off by the native's
  signature or a wrong hash lookup. `bind()`'s own allocations and mutex (SX-06) make the window longer. This only matters
  if the Slang VM (FUN_00737010) can run a script on one thread while another thread requests it.
- **Fix (behaviour-preserving):** none removes the window without a design change. Shortening `bind()` (SX-06) is
  behaviour-preserving and narrows it. The real fix (bind DI1 imports inside the linker, e.g. a detour of the registry
  lookup so the engine itself writes the DLL native) changes which code writes the slot: a design change that needs the
  user's decision and an in-game test.
- **Refactor risk:** any change that adds work to `bind()` between the engine's write and ours widens the window.
- **Verification:** NEW (verified by disassembly that the slot write is unconditional). Concurrency is UNVERIFIED; settle
  it by logging `GetCurrentThreadId()` once per thread in `hkLink` and in `stub`/a port.

### NR-02 `g_dbLock` held across zip/archive I/O, DataMap parsing and engine calls
- **Where:** `native/di1compat/npcrt.hpp:297-326` (lock scope; `prepare` 226-280 runs inside it), `150-171` (hkPercApply)
- **Severity / category / class:** Med · locking · NEEDS-VERIFICATION
- **Evidence:** under `g_dbLock`, `prepare` makes these calls:
  - the engine DataMap lookup FUN_00705860 (234);
  - `read10s`, which inflates under `g_zipLock` and falls back to `g_choAlias.m` (di1compat.cpp:401, 407);
  - `read30x` → `arc30::readMember`, which reads a file and decrypts under arc30's `cipherLock`;
  - two or three whole DataMap evaluations (O(n²) stores, NP-05);
  - `read30` → `g_open`, the unhooked engine AssetOpen (250), plus one `g_open` per core expendables package (267);
  - `g_sessLock` (275) and `logf` (`g_logLock`).

  hkPercApply holds the lock across the engine calls `percId` (FUN_01496720) and `zoneName` (FUN_01391040), across
  `npc::percAdds`, and across `logf`.

  Correction to the report's lock order: these plugin locks are each taken and released inside `g_dbLock`. They are not
  nested in one another: `read10z` releases `g_zipLock` before `prepare` takes `g_sessLock`.

  I checked every other holder of `g_zipLock`, `g_sessLock`, `cipherLock` and `g_choAlias.m`: hkOpen 3645-3729,
  indexZip10 590-606, `sceneSettingsIO`, `lang30`. None of them calls engine code or a `g_dbLock` hook while holding its
  lock, so there is no lock inversion inside the plugin.

  Four hooks take `g_dbLock`: hkInclude (FUN_0070D730), hkPlaysetUnload (FUN_0070DF70, whose only caller is
  0x01440F1C), and hkPercApply/hkPercRemove (perception modifiers on the AI update). Their threads are not documented.
- **Hot path?:** code defaults: `prepare` never runs (`serve10` off), but the lock is still taken on every include and every
  perception apply/remove. Recommended config: `prepare` runs once per 1.0 playset load.
- **Why it matters:** a deadlock needs an engine lock held by a thread that then enters one of the four hooks, while the
  `prepare` thread waits on that engine lock inside `g_open` or the lookup. That is UNVERIFIED. The report's "long stalls
  are certain" overstates it: only a thread that contends for `g_dbLock` during a `prepare` waits, for that one build.
- **Fix (behaviour-preserving):** in hkInclude, decide under the lock whether a build is needed (the "core" and
  already-built checks). Build into locals outside the lock: reads, parses and engine opens. Then re-lock, re-check
  `g_dbPlayset`, and publish `g_db`, `g_pl10`/`g_pl30`, `g_psPath` and the `g_served` entry. hkPercApply is harder to fix,
  because `percList` needs `g_percMod`.
- **Refactor risk:** two concurrent Playset includes of the same playset would both build and log "gamedb30 %s: 1.0
  DataMap…", "avperc10…", "rewards10…" and "playset30…" twice. That is a log change, so keep a "building" marker. The
  lookup at 234 must keep running on the including thread with `self`. Publish `g_served[g_psPath]` before
  `dataMapRoute(true, psMap)` (337) runs.
- **Verification:** CORRECTED: corrected the lock-nesting description and narrowed the stall claim. Deadlock UNVERIFIED;
  log the thread id once in each of the four hooks to settle it.

### NP-03 Mixed error channels; a throw in `skinRemap` serves the whole .oct raw
- **Where:** `native/di1compat/npc.hpp:228` (`f.ints(…).at(0)`), `337, 339, 348, 349` (luaEval `.at()`); callers
  npcrt.hpp:300-304, di1compat.cpp:635; catch at di1compat.cpp:3714-3720
- **Severity / category / class:** Med · error handling / robustness · BEHAVIOUR-CHANGE
- **Evidence:** `luaEval` returns `false` with a reason, but its `K.at`/`R.at` calls throw `std::out_of_range`;
  npcrt.hpp:302 acknowledges this. In `prepare` those throws are caught by hkInclude ("supplement failed"), which is
  benign.

  `skinRemap`'s `.at(0)` on an empty `vformatCRC` record is different. It runs inside the .oct transcode
  (`transcodeAssetRender` → `npcrt::skin10`, di1compat.cpp:635). hkOpen's catch then sets `codec = nullptr` and
  serves `memFile(data, n)`, the raw 1.0 .oct (3729). That drops the NavMeshPool conversion, and a raw 1.0 navmesh faults
  3.0's parser FUN_01909fb0 (di1compat.cpp:710-712).
- **Why it matters:** one malformed record turns into a game crash for any .oct that also carries a navmesh. The trigger is
  malformed input only (skin10 was checked against 419 shipped models).
- **Fix (behaviour-preserving):** none. A consistent channel (`return false` or a guarded `ints`) changes which outcome
  and log line happens ("TRANSCODE EXCEPTION" versus "+skinremap(…)"), so it needs the user's decision.
- **Refactor risk:** keep hkOpen's catch as the last line of defence, whatever is chosen.
- **Verification:** CORRECTED: raised the severity from Low to Med (malformed input, per the rubric) and spelled out the
  crash path.

### NP-02 32-bit size_t wrap in the Lua header checks; `luaStrings` walks past the buffer
- **Where:** `native/di1compat/npc.hpp:302, 306` (`luaEval`), `569` (`luaStrings`)
- **Severity / category / class:** Med · robustness / out-of-bounds read · BEHAVIOUR-CHANGE (malformed input only)
- **Evidence:** with a 32-bit `size_t`, `at + n + 12` wraps for n ≥ 2^32-28 and `(size_t)ncode * 4` wraps for
  ncode ≥ 2^30, so both checks pass.
  - `luaEval`'s code loop (333-352) stops at the first word that is not a constructor opcode, so it usually reads only a
    few words past the end.
  - `luaStrings`' loop (590-599) accepts every opcode and runs `pc < ncode` with ncode ≥ 2^30. It is certain to read
    into unmapped memory: an access violation, a CrabeLoader dump and the end of the game.

  Inputs: `luaEval` reads the 1.0 and 3.0 DataMaps (`prepare`) and avatar.btx (`percLists`). `luaStrings` reads 1.0
  `*_ps.dnax` and the 3.0 base DNA (`psDna30`, served in hkOpen). `sx::ai::eval` does it correctly (`len > n - at`,
  `4ull * ncode`, sx.hpp:733-735).
- **Why it matters:** a malformed install file becomes a hard crash, not a "not converted" log line.
- **Fix (behaviour-preserving):** use sx's form of the checks. Valid files are unchanged. Malformed ones fail with the
  existing reasons ("header"/"code") or `false`, instead of UB.
- **Refactor risk:** keep the reason strings; octtest `--dmeval`, `--psdna` and `--avperc` must give identical results.
- **Verification:** CORRECTED: the `luaStrings` case is a deterministic walk past the buffer, not just a possible
  over-read. Severity raised from Low-Med to Med.

### NP-04 Negative index read in `skinRemap`
- **Where:** `native/di1compat/npc.hpp:239` (validation), `247-248` (bound), `252` (read)
- **Severity / category / class:** Med · out-of-bounds read on malformed data · BEHAVIOUR-CHANGE (malformed input only)
- **Evidence:** only `id[0]` and `id[2]` are validated. With `id[1] < 0`, `(uint64_t)id[1] + id[3]` wraps to a small
  value, so `2 * (…) > ibuf.size()` passes. Line 252 then reads `ibuf[2 * (id[1] + q)]` up to 2·|id[1]| bytes before the
  buffer. The values read are range-checked (`v >= count`), so no out-of-bounds write follows.
- **Why it matters:** UB on malformed 1.0 data. It rarely faults, because the bytes before a heap block are usually
  mapped.
- **Fix (behaviour-preserving):** also reject `id[1] < 0 || id[3] < 0`, counted as skipped. Valid files are unchanged. A
  malformed one changes the `why` count "… remapped primitives skipped".
- **Refactor risk:** none beyond octtest `--skin`.
- **Verification:** CORRECTED: severity raised from Low to Med (rubric: latent bug, malformed input); claim confirmed.

### NP-N1 `luachunk::eval` indexes its 306 registers unchecked (NEW)
- **Where:** `native/di1compat/luachunk.hpp:127` (`R(256 + 50)`), `140` (MOVE `R[b]`), `143` (LOADNIL `R[k] = Value()` for
  k = a..b), `151` (SETLIST `R[a + k]`); in-scope caller `sys::groups30` (sys.hpp:690; served by di1compat.cpp:1076)
- **Severity / category / class:** Med · out-of-bounds write on malformed data · BEHAVIOUR-CHANGE (malformed input only)
- **Evidence:** `b = i >> 23 & 0x1ff` goes up to 511 and `a + k` up to 766, while `R` has 306 cells and `operator[]` is
  unchecked. A LOADNIL with b ≥ 306 writes `Value()` past the vector (heap corruption). MOVE and SETLIST read past it.
  Valid Lua 5.1 code never names a register at or above maxstacksize (≤ 250). This file is outside the four audited ones;
  it is reported here because `sys::groups30` (1.0 expendables packages) and the zonelist path feed it 1.0 install data.
- **Why it matters:** heap corruption on a malformed package. No exception is thrown, so hkOpen's catch does not help.
- **Fix (behaviour-preserving):** bound checks that throw `std::runtime_error` (luachunk's own channel). Malformed chunks
  then end as "TRANSCODE EXCEPTION" with the raw file served. Valid chunks are unchanged.
- **Refactor risk:** keep luachunk throwing (its callers rely on exceptions); octtest `--groups30` as the golden test.
- **Verification:** NEW, verified by reading the code.

### SX-05 `readStr` relies on `__try` alone; every fault reaches the vectored handlers first
- **Where:** `native/di1compat/sx.hpp:129-137` (`readStr`, `__except (1)`), `688-696` (`behInitParams`)
- **Severity / category / class:** Low (parent-corrected from Med, see below) · robustness · BEHAVIOUR-CHANGE (failure path / diagnostics only)
- **Evidence:** di1compat.cpp:1225-1227: "__try is not enough: a first-chance AV reaches the vectored handlers before any
  __except, and Pentane's ends the game". `safeCopy` (di1compat.cpp:89-91) pre-checks for that reason; `readStr` does not.
  **Parent correction (2026-10-16):** the "ends the game" part is not what Pentane does.
  - The bundled `third_party/pentane/Pentane.dll` vectored handler (disassembled at 0x101b978e) logs
    `=== Exception Caught! ===` with a SymInitialize/StackWalk64/SymFromAddr stack trace, then returns 0
    (`EXCEPTION_CONTINUE_SEARCH`) on both paths. It never calls TerminateProcess or ExitProcess.
  - Upstream Pentane `source/crash.cpp` (48e3fbb) is the same.
  - So the `__except` here **does** run.
  - The 12:01 crash in commit 684539b was di1compat's own `avHandler` faulting again inside the vectored handler.
  - What a handled fault really costs: one slow symbolised Pentane log entry (the bundled Pentane is a Debug build), plus a
    CrabeLoader crash report that uses one of its 3 slots.
  `readStr` is called on every key cell (`keyCell`), in `hashFindInRange` and in `reactionInfoFlipMotionName`. Type-2 cells
  can be object pointers (header line 12). `behInitParams` only reads engine-owned VM state (ctx+4, header, cells) and the
  engine's own `name` argument, so a fault there needs an already corrupt VM.
- **Hot path?:** code defaults: never (no DI1 scripts). Recommended config: every key-taking native call from a 1.0 script.
- **Why it matters:** the `__except` does return `false`, but only after the vectored handlers have run. A garbage type-2
  cell therefore produces a slow Pentane "Exception Caught" entry, which `harness.ps1` counts as a failure signal, and a
  CrabeLoader crash report that uses one of its 3 slots. The game survives.
- **Fix (behaviour-preserving):** in `keyCell` and `hashFindInRange`, accept without a syscall a pointer inside the
  calling script's buffer `[hdr, hdr + rd(hdr + 0xc))` (`transcode` writes the size at 0xc). Check anything else with
  `sys::readable` page by page, and keep `__try` as a second line. Valid input is unchanged. On failure the result stays
  the same (`false`; the cell passes through and 3.0's pop reads it as 0), but the fault, the Pentane log entry and the
  CrabeLoader report go away. Leave
  `behInitParams` as it is and document why: a pre-check there would tax every call-by-name of every script.
- **Refactor risk:**
  - `keyCell` needs the ctx/header, so its signature changes.
  - sx.hpp is included before sys.hpp (di1compat.cpp:35-36), so `readable` must move to a shared header (SY-01).
  - A plain syscall on this path would undo SX-02's gain.
  - `readStr`/`behInitParams` must stay free of C++ objects (C2712).
- **Verification:** CONFIRMED.

### XF-02 `safeCopy` checks one page only; inconsistent guarded copies
- **Where:** `native/di1compat/di1compat.cpp:87-102` (`safeCopy`); `sys.hpp:612-619` (`cstr` lambda); `sx.hpp:129-137`
- **Severity / category / class:** Med · robustness / duplication · BEHAVIOUR-CHANGE (failure path only)
- **Evidence:** the three guarded copies use three different strategies:
  - `safeCopy` checks only the first byte's page (`sys::readable(src, 1)`), then copies under `__try`, so a string that
    runs into an unreadable page still faults;
  - `snapshotPerc30`'s `cstr` checks every byte;
  - `readStr` checks nothing.

  `safeCopy` has 58 call sites (di1compat.cpp 39, gprt 9, caprt 4, vfxrt 4, bldrt 2), and its own comment says the
  probes pass "values that may not be pointers".

  Other `__try` blocks that read unchecked memory, all in di1compat.cpp (another area): `hdrName` (3167) reads `h[0]`
  unchecked; `hkAssetDef` (2056) calls an engine function under `__try`. `ourRetSafe` (2256) reads only the DLL's own
  image.
- **Why it matters:** each fault is a CrabeLoader crash report (one of its 3 slots) plus a slow symbolised Pentane log
  entry. The comment at di1compat.cpp:1225 says it also ends the game. Disassembly of the bundled Pentane handler shows it
  does not (see SX-05's parent correction), so the cost is diagnostics noise and a hitch, not a crash.
- **Fix (behaviour-preserving):** one page-aware guarded copy (check the first page, then each new page as `src + i`
  crosses a 4 KiB boundary). On valid strings the output is identical. A string that crosses into an unreadable page ends
  without a fault (today: "<unreadable>" if the game survives). Decide which text to produce then.
- **Refactor risk:** safeCopy's output strings appear in log lines; keep "<unreadable>" for the failure case if possible.
- **Verification:** CORRECTED: raised the severity from Low to Med (same failure class as SX-05), corrected the call-site
  count (58, not 59), set the class, and listed the out-of-area `__try` reads.

### SX-02 `keyId` builds a heap string per call; unpaired keys lock a mutex per call
- **Where:** `native/di1compat/sx.hpp:249-264` (`keyId`), `266-272` (`keyCell`); callers 274, 276, 278, 288-291, 391, 395;
  load-time caller `ai::keyed` 817
- **Severity / category / class:** Med · CPU · SAFE-REFACTOR
- **Evidence:** `static const std::unordered_map<std::string, int> ids … ids.find(name)` with `const char* name`. C++17
  unordered containers have no heterogeneous lookup, so every call builds a `std::string`. 111 of the 429 KEYS30 names are
  longer than MSVC's 15-character SSO buffer, so those calls allocate (re-counted). A miss runs
  `std::lock_guard<std::mutex> lock(mx); told.insert(id)` on every call.
- **Hot path?:** code defaults: never at runtime (no DI1 scripts are served). Recommended config: every call of the 10
  key-taking port names (HashFind/Int/Str, HashAdd/Int/Str, HashRemove, HashFindInRange ×3 lookups, AiBehaviorGet{String,
  Float}Property) from 1.0 AI scripts. The rate is UNVERIFIED.
- **Why it matters:** a heap allocation per script hash access, on a path that can run per frame per AI actor.
- **Fix (behaviour-preserving):** `std::unordered_map<std::string_view, int>` built from KEYS30 (string literals with
  static storage), looked up with `std::string_view(name)`. Keep the mutex on the miss path, or test `told` first. KEYS30
  is sorted case-insensitively with strictly increasing ids and has no case-only duplicates (re-checked), so a binary
  search plus an exact compare would also work.
- **Refactor risk:** keep case-sensitive exact matching and the once-per-id log line
  `sx key '%s': not in the 3.0 enum, id %#x`. octtest `--sxkeys` (octtest.cpp:2581) is the golden test.
- **Verification:** CORRECTED: the key-taking ports are 10 names (5 functions), not "the 21 ported natives"; they are
  reachable only with `serve10`.

### SY-02 Two `readable()` implementations; the unqualified one is the slow one
- **Where:** `native/di1compat/sys.hpp:180-196` (`sys::readable`); `di1compat.cpp:1228-1240` (`::readable`), only caller
  `afterCall` 1246
- **Severity / category / class:** Med · duplication / shadowing hazard · SAFE-REFACTOR (rename); unifying is
  NEEDS-VERIFICATION
- **Evidence:**
  - `sys::readable` does one `K32QueryWorkingSetEx` per page. It accepts a resident page whose protection has no NOACCESS
    or GUARD bit (so `PAGE_EXECUTE` is accepted). Only for a non-resident page does it call `VirtualQuery`, and it then
    skips to the end of that region. So: zero `VirtualQuery` calls when every page is resident, and at most one per region
    that contains a non-resident page. It also rejects addresses below 0x10000.
  - `::readable` calls `VirtualQuery` only, requires an explicit read flag, and walks regions.
  - The sys.hpp:176-179 comment measures `VirtualQuery` at 0.19-5.1 ms in large touched regions, against 0.4 µs for
    `QueryWorkingSetEx`.
  - After line 1228, an unqualified `readable(` in di1compat.cpp resolves to `::readable`. vfxrt.hpp:63
    (`using sys::readable;` inside `namespace vfxrt`) is what keeps vfxrt on the fast one.
  - The comment "The one pointer check of the plugin" (sys.hpp:173-174) is stale.
- **Why it matters:** the next unqualified use in di1compat.cpp silently picks the slow probe, which is the one that once
  cost 85% of the main thread.
- **Fix (behaviour-preserving):** rename `::readable` (e.g. `readableVq`) and fix the stale comment.
- **Refactor risk:** unifying the two changes results for execute-only pages and for addresses below 0x10000; check that
  against `afterCall`/`stackOf`.
- **Verification:** CONFIRMED.

### NP-01 Five Lua 5.1 chunk walkers with diverging semantics
- **Where:** `npc::luaEval` npc.hpp:293-354, `npc::luaStrings` 562-601, `sx::ai::eval` sx.hpp:725-786,
  `luachunk::eval` luachunk.hpp:100-159, `luachunk::rewrite` 32-78; HDR array at npc.hpp:297, 564, sx.hpp:727,
  luachunk.hpp:34, 102
- **Severity / category / class:** Med · duplication · NEEDS-VERIFICATION
- **Evidence (all differences confirmed):**
  - Nil-value stores: luachunk erases the key (luachunk.hpp:133); npc and sx keep it.
  - Key kinds: sx rejects nil and table keys (sx.hpp:766); luachunk rejects nil keys (throws); npc accepts anything.
  - Register count: npc uses `maxstack + 1` (checked with `.at`); sx uses 256 (bounds-checked); luachunk uses 306,
    unchecked (NP-N1).
  - SETLIST base: npc computes it in uint32, sx and luachunk in double. These differ only when C > 85,899,346, which only
    an extra-word C can reach, so there is no difference on real data.
  - Byte order: only luachunk accepts big-endian chunks.
  - Error channel: luachunk throws, sx returns `false`, npc does both (NP-03).
- **Why it matters:** any header or bounds fix (NP-02, NP-N1) has to be made 3-5 times, and the copies will drift.
- **Fix (behaviour-preserving):** share one bounds-checked header and constants parser (sx's checks), and keep each opcode
  loop's semantics, or use an explicit policy struct. Golden tests: octtest `--dmeval`, `--avperc`, `--gamedb`,
  `--psdna`, `--ai`, `--groups30`.
- **Refactor risk:** high for a naive merge: nil stores, key acceptance and the error channel all change output or
  control flow.
- **Verification:** CONFIRMED.

### SX-09 Stack-balance adjustments are bare numbers; port signatures duplicated
- **Where:** `native/di1compat/sx.hpp:281-295, 299, 302, 305-310, 313, 315, 344`; `g_ports` 584-601
- **Severity / category / class:** Med · readability / refactor risk · NEEDS-VERIFICATION
- **Evidence:** I re-derived every port against its 1.0 row and the 3.0 signature given in its comment. Every adjustment
  is consistent with the "last argument on top" order that `Vm::at` uses:

  | Port | 1.0 (args→res) | 3.0 native | Adjustment |
  |---|---|---|---|
  | HashFindInRange | 3→2 | 0x1345F60 5→2 | pop 3, push 5 |
  | PauseAtEndWhenDoneChoreography | 1→0 | 0x1383520 2→0 | push 1 before |
  | ActorGetNamedLocatorPosition | 2→3 | 0x759470 2→6 | `top() -= 3` after |
  | Matrix3x3BuildFromVector | 3→9 | 0x733BC0 6→9 | push 2,1,1 before |
  | ReactionInfo_GetReactionScript | 3→1 | 0x137C4A0 2→1 | `top() -= 1` before (variant is the top cell) |
  | Combat_AttackStart | 2→0 | 0xEB2020 2→1 | `top() -= 1` after |
  | SlaveActorToVehicle | 5→0 | 0xAA2B30 5→1 | `top() -= 1` after |
  | FlipMotionName, BehaviorStringFromType | 1→1 | none (DLL only) | pop 1, push 1 |
  | Hash*, AiBehaviorGet*, CurrentBehaviorType, TriggerEvent, GetSliderFloat | same | same | in-place cell rewrite only |

  The 9 ports whose names are also in `g_natives` have equal `(args, results)` (re-checked by script). `g_ports`'
  args/results are never read at runtime: ports always have `fn`, so `bind` never binds them to `stub` (655). Only
  octtest's natives.tsv reads them (octtest.cpp:2624).
- **Why it matters:** an off-by-one silently corrupts the VM stack (sx.hpp:9-10).
- **Fix (behaviour-preserving):** one constexpr descriptor per port (`args10, res10, args30, res30, va30`), with
  `static_assert`s that each push or pop count equals the difference. Derive `g_ports`' signature from `g_natives`.
- **Refactor risk:** high if any number changes; the 3.0 signatures come from comments, not re-disassembly. natives.tsv
  (sxcheck.py) must stay byte-identical. The functions must stay `__cdecl`.
- **Verification:** CONFIRMED.

### SX-01 sx.hpp holds three subsystems plus generated tables
- **Where:** `native/di1compat/sx.hpp`: container 41-83; VM natives, linker and call-by-name 85-703; `sx::ai` 705-897;
  KEYS30 146-240 (429 rows), `g_natives` 413-583 (465 rows)
- **Severity / category / class:** Med · god file / coupling · SAFE-REFACTOR
- **Evidence:** octtest.cpp:20 includes sx.hpp, so the engine-calling natives, the SEH code and the hooks compile into
  the offline exe. octtest uses `keyId`, `ai::bmx`/`btx`, `transcode`, `g_natives`, `g_ports`, KEYS30, BEH10, `eventTo30`
  and `flipMotionName` (octtest.cpp:2581-2638).
- **Why it matters:** runtime ABI code and load-time translation change for different reasons and follow different
  safety rules (SEH, VM stack balance).
- **Fix (behaviour-preserving):** split into a container header, a keys header (KEYS30, `keyFallback`, `keyId`), an ai
  header and a runtime header with an `install(hook, rt, cfg)` modelled on `audio::install` (di1compat.cpp:4338). Move
  the generated tables into `.inc` files that name their generator (gentab.py, keymerge.py).
- **Refactor risk:** keep namespaces and names for octtest, keep the arrays `inline`, keep the include order (sx.hpp before
  sys.hpp, di1compat.cpp:35-36), and keep `__try` functions free of objects (C2712).
- **Verification:** CONFIRMED.

### SY-01 sys.hpp header comment is stale; engine glue mixed with pure helpers
- **Where:** `native/di1compat/sys.hpp:1-2`; `readable` 180-196; `loaderUids` 201-220; `snapshotPerc30` 608-638;
  `applyPopin10` 664-671; shared state 141-171, 223-226, 595-597, 672
- **Severity / category / class:** Med · design / stale comment · SAFE-REFACTOR
- **Evidence:** the comment says "Pure text/data helpers here; the engine glue … stays in di1compat.cpp". The file also
  holds the plugin-wide memory probe (used by six other files: di1compat.cpp, gprt, caprt, bldrt, vfxrt, octtest), engine
  memory readers and a writer, and state shared with hooks.
- **Why it matters:** the comment misdirects anyone deciding where engine-touching code belongs.
- **Fix (behaviour-preserving):** move `readable` to a small shared header that sx, sys and the *rt files include, keeping
  `sys::readable` as the name (octtest.cpp:2466, 2518). Move the engine glue into a runtime header. Fix the comment.
- **Refactor risk:** include order; octtest names.
- **Verification:** CORRECTED: six other files, not seven.

### SY-05 `perceptionInfoLua` god function with a shared scratch string
- **Where:** `native/di1compat/sys.hpp:364-589` (226 lines); `chestInfoLua` 256-342 (87 lines)
- **Severity / category / class:** Med · god function / readability · NEEDS-VERIFICATION
- **Evidence:** about 20 lambdas and 10 output sections. `num`, `flag`, `inum` and `kv` all write the captured `s`
  through `str` (403-406, 441). 475-477 copies `key` from `s` before `flag()`/`num()` overwrite it.
- **Why it matters:** correct output depends on the call order of side-effecting helpers.
- **Fix (behaviour-preserving):** separate parsing, reachability (`use[]`) and per-section emitters, and make the helpers
  return values.
- **Refactor risk:** the output must stay byte-identical. Golden tests: octtest `--perc` (octtest.cpp:2653) and `--chest`
  (2642).
- **Verification:** CORRECTED: the octtest handler lines are 2642/2653 (the report cited 2644, 2659-2663, which are
  inside them).

### NR-03 Long multi-purpose functions
- **Where:** `native/di1compat/npcrt.hpp:226-280` (`prepare`, 55 lines), `293-344` (`hkInclude`, 52), `144-204`
  (`hkPercApply`, 61)
- **Severity / category / class:** Med · god functions · NEEDS-VERIFICATION
- **Evidence:** each function mixes several jobs:
  - `prepare`: the 1.0 DataMap, ToyBox plus `gamedb30`, the avperc10 lists, the playset30 text, and the rewards10
    probe plus `g_served` publication;
  - `hkInclude`: preparation, actor-table collection, routing, the CarryStyleInfo diagnostic, DataMap routing and
    verification;
  - `hkPercApply`: list matching, the zone cache, logging, host detection and refcount repair.
- **Why it matters:** these functions also hold the lock (NR-02) and the exception window (NR-01), so restructuring them
  is where those bugs get fixed or made worse.
- **Fix (behaviour-preserving):** extract `buildSupplement()`, `routeTables()` and `applyAdditions()`.
- **Refactor risk:** the order around `g_include` carries behaviour: route first (328), then `dataMapRoute` (337), then
  the original include (338), then verification (339-342). Lock scopes must stay as they are, or change only as NR-02
  describes.
- **Verification:** CONFIRMED.

### SX-03 Behaviour-type renumbering calls the engine up to 30 times per conversion
- **Where:** `native/di1compat/sx.hpp:370-386`; callers 391, 395, 410, 694
- **Severity / category / class:** Low · CPU · NEEDS-VERIFICATION
- **Evidence:** `behTo30` calls `beh30Name(i)` (FUN_01523C30) up to 30 times, each followed by a `_stricmp`. `behTo10`
  makes one engine call and up to 36 `_stricmp`.
- **Hot path?:** code defaults: never. Recommended config: every AiBehaviorGet{String,Float}Property /
  GetCurrentBehaviorType call and every SetInitialParams of a DI1 script. The rate is UNVERIFIED; about 1 µs per call.
- **Fix (behaviour-preserving):** build `beh10to30[36]`/`beh30to10[30]` once, on first use (magic static), and replace
  the literal 36/30 (373, 374, 380, 382, 403) with `std::size(BEH10)` and a named 3.0 count.
- **Refactor risk:** assumes the engine table at 0x210455c is immutable (comment at 359) and FUN_01523C30 is pure.
- **Verification:** CORRECTED: severity Low (the report said Low-Med); added the `serve10` gating.

### SX-04 Locked increment on every stub call
- **Where:** `native/di1compat/sx.hpp:125`
- **Severity / category / class:** Low · CPU / contention · SAFE-REFACTOR
- **Evidence:** `_InterlockedIncrement(&n->hits) == 1` on every call. `hits` is read nowhere else (grep, octtest
  included). `Native` is 16 bytes on x86, so four share a cache line.
- **Hot path?:** code defaults: never. Recommended config: every call of an unported 1.0 native.
- **Fix (behaviour-preserving):** `if (n->hits == 0 && _InterlockedCompareExchange(&n->hits, 1, 0) == 0) log`.
- **Refactor risk:** keep the log text "sx stub %s first called (pops %d, pushes %d zero)".
- **Verification:** CONFIRMED.

### SX-06 `bind()` allocates and locks on every relink
- **Where:** `native/di1compat/sx.hpp:631-664`
- **Severity / category / class:** Low · CPU (warm) · SAFE-REFACTOR
- **Evidence:** `bind()` does avoidable work on every call:
  - it takes the mutex on every call;
  - `name = names[hdr]` copies a string (644) that is used only when `first`;
  - `table().find(nm)` builds a `std::string` per import (651); 432 of the 465 stub names are longer than 15 characters
    (re-counted);
  - `unbound +=` concatenates per unbound import (659).

  `hkLink`'s `catch (...) {}` (673) silently swallows a `bad_alloc` from these allocations. That leaves the rest of that
  request's imports on the engine's binding until the next relink.
- **Hot path?:** code defaults: early return for every non-DI1 script (three word reads). Recommended config: every
  request of a DI1 script. This work also lengthens the SX-N1 window.
- **Fix (behaviour-preserving):** key the registry by `string_view`; copy the name and build `unbound` only when
  `first && g_log`.
- **Refactor risk:** keep the log format `sx link %s: %d imports: …`. Dropping the `names[hdr]` insert-on-miss only
  changes map growth (not observable).
- **Verification:** CORRECTED: severity Low; added the swallowed-exception consequence.

### SX-07 `bind`'s `names` map never shrinks
- **Where:** `native/di1compat/sx.hpp:637`
- **Severity / category / class:** Low · unbounded growth · DOC-ONLY
- **Evidence:** one entry per distinct header address ever linked. A reused address is overwritten on its first link.
- **Fix:** document it (there is no unload hook).
- **Verification:** CONFIRMED.

### SX-08 First-link detection relies on DLL addresses > script sizes
- **Where:** `native/di1compat/sx.hpp:638-645, 654`
- **Severity / category / class:** Low · fragile invariant · DOC-ONLY
- **Evidence:** entry 0's word 3 holds the path offset (`transcode`, 77), then 0, or a `Native*` if entry 0 is DLL-bound
  (654). `w3 && w3 < size` works because DLL addresses are larger than script sizes. `w3` is read outside the lock, so two
  threads can both log "first" (benign).
- **Fix:** document the invariant at 638 and 654.
- **Refactor risk:** storing an index in word 3 breaks the detection.
- **Verification:** CONFIRMED.

### SX-10 Near-identical native wrappers
- **Where:** `native/di1compat/sx.hpp:274-278, 389-396`
- **Severity / category / class:** Low · duplication · SAFE-REFACTOR
- **Evidence:** the two AiBehaviorGet*Property wrappers differ only in VA; hashFind/hashAdd/hashRemove differ in VA and
  key position.
- **Fix (behaviour-preserving):** `template<int keyPos, uint32_t va> void __cdecl keyedCall(uint8_t*)`.
- **Refactor risk:** keep `__cdecl` and one function per row (addresses are stored in import slots).
- **Verification:** CONFIRMED.

### SX-11 `provide()` is dead; `stub`'s `n->fn` branch unreachable
- **Where:** `native/di1compat/sx.hpp:617-627, 121`
- **Severity / category / class:** Low · dead code · SAFE-REFACTOR
- **Evidence:** no callers in the tree (grep, octtest included). `g_natives[].fn` stays null, and ports are bound
  directly, so `stub` never sees a non-null `fn`.
- **Fix:** delete `provide` and the branch, or keep them and document them as the cross-track API.
- **Verification:** CONFIRMED.

### SX-12 Static return buffer in `reactionInfoFlipMotionName`
- **Where:** `native/di1compat/sx.hpp:331-340` (`static char buf[512]`)
- **Severity / category / class:** Low · thread safety · DOC-ONLY
- **Evidence:** this mirrors 1.0's single static buffer (comment 319-320). Concurrent calls would race.
- **Fix:** document it; consider `thread_local` only after confirming the VM's threading.
- **Verification:** UNVERIFIED: settle by checking whether natives run on more than one thread (the same thread-id log as
  SX-N1).

### SX-13 Unchecked constant count in `ai::eval`
- **Where:** `native/di1compat/sx.hpp:738-739`
- **Severity / category / class:** Low · robustness · BEHAVIOUR-CHANGE (log text on malformed input)
- **Evidence:** `std::vector<Val> K(nk)` with `nk` from the file. A huge value throws (`length_error` or `bad_alloc`).
  hkOpen's catch (di1compat.cpp:3714-3720) then serves the raw bytes, the same result as the `-1` path, with
  "TRANSCODE EXCEPTION" instead of "bmx: truncated". A mid-size bogus `nk` (millions) succeeds and builds millions of empty
  `Val`s first.
- **Fix:** `if (nk > n - at) return fail("truncated");`. The note text changes for such files.
- **Verification:** CONFIRMED.

### SX-14 Quadratic table stores and 256-register setup in `ai::eval`
- **Where:** `native/di1compat/sx.hpp:749-758`
- **Severity / category / class:** Low · CPU (cold) · SAFE-REFACTOR
- **Evidence:** a linear `find_if` per store; `R(256)` constructs 256 `Val` per eval. The tables are small and this runs
  per .bmx/.btx serve.
- **Hot path?:** code defaults: never; recommended config: load time only.
- **Fix:** a note only; an index map would have to keep insertion order.
- **Verification:** CONFIRMED.

### SX-15 Method existence test inflates whole 1.0 members
- **Where:** `native/di1compat/sx.hpp:805-810`; `exists` = `read10s` at di1compat.cpp:1105
- **Severity / category / class:** Low · CPU (load) · NEEDS-VERIFICATION
- **Evidence:** every Method / Method_Custom calls `exists("scripts/" + s)`, which locates and inflates a whole member.
- **Hot path?:** code defaults: never. Recommended config: each .bmx/.btx serve (55 + 58 files), so a few hundred small
  inflations per session.
- **Fix:** a locate-only probe that follows `read10` → `read10z` exactly (5 candidate archives per split, the root
  archives, the choAlias fallback; di1compat.cpp:395-451).
- **Refactor risk:** `read10s` also returns false when extraction fails, while a locate-only probe would say true and
  pick `scripts/`.
- **Verification:** CORRECTED: severity Low (the report said Low-Med).

### SX-16 `sx::ai::lower` also maps `\` to `/`
- **Where:** `native/di1compat/sx.hpp:799-803` versus `npc::lower` (npc.hpp:361) and `audio::lower` (audio.hpp:53)
- **Severity / category / class:** Low · naming hazard · SAFE-REFACTOR
- **Fix:** rename it (`normPath`). Never merge it with the plain `lower`s.
- **Verification:** CONFIRMED.

### SX-17 Private exe-rebasing helpers duplicate `rt()`
- **Where:** `native/di1compat/sx.hpp:97-100, 370`
- **Severity / category / class:** Low · duplication · SAFE-REFACTOR
- **Evidence:** `g_exe + va - 0x400000` duplicates `rt()` (di1compat.cpp:106). `g_exe` is set only when
  `sx10 && sxNatives10` (4308-4309), and every user is reachable only then.
- **Fix:** pass `rt` in, as `audio::install` does.
- **Verification:** CONFIRMED.

### SX-18 `hkCallByName` typed `void` (engine value is incidental)
- **Where:** `native/di1compat/sx.hpp:697-703`
- **Severity / category / class:** Low · ABI · SAFE-REFACTOR
- **Evidence (disassembly):** FUN_00739090 is `push esi; push edi; …; call 0x738BC0; pop edi; pop esi; ret 8`. Its EAX is
  whatever the VM-run chain leaves (0x738BC0 → 0x738BA0 → tail-call 0x737010), never set on purpose. Of its 28 direct
  call sites, 24 overwrite EAX before reading it. The other 4 are tail positions in functions whose other exits leave an
  unrelated EAX: 0x014C2590, 0x015245E0, 0x01540E30 and 0x015F3EE0. Two of these (0x015245E0, 0x01540E30) are vtable
  methods whose virtual callers were not traced. FUN_00739090 has no absolute references.
- **Why it matters:** today EAX passes through `hkCallByName` because the original call is its last statement. Code added
  after it would change the incidental value.
- **Fix (behaviour-preserving):** retype the detour as `uint32_t` and return the original's value, or document "nothing
  after the original call".
- **Verification:** CORRECTED: severity Low (the report said Low-Med); the return value is effectively void (evidence
  above).

### SX-19 `sx::g_log` set after AssetOpen is hooked; early unpaired keys never logged
- **Where:** `native/di1compat/sx.hpp:259-262`; di1compat.cpp:4183 (AssetOpen hooked), 4307 (`sx::g_log = logf`)
- **Severity / category / class:** Low · init order · NEEDS-VERIFICATION
- **Evidence:** `told.insert(id).second && g_log` inserts first, so a key seen while `g_log` is null is never logged
  later. The unsynchronised pointer write is benign on x86.
- **Fix:** assign `sx::g_log` before the first `hook()`.
- **Refactor risk:** log lines could appear that do not today, but only if the engine opens a .bmx/.btx during
  Pentane_Main (UNVERIFIED, unlikely).
- **Verification:** CONFIRMED.

### SX-20 Unnamed script-header/context/import-entry offsets
- **Where:** `native/di1compat/sx.hpp:53-81, 89-93, 110-115, 633-638, 649-655, 685-686`
- **Severity / category / class:** Low · magic numbers · SAFE-REFACTOR
- **Evidence:** header +0x2c (import count), +0x54 (import table), +0xc (size), +0x40 (code); context +0x1c/+0x20/+0xc/+4;
  24-byte import entries with words at 8/12/20.
- **Fix:** named constexpr constants next to the header comment (5-12).
- **Verification:** CONFIRMED.

### SY-03 One `readable` syscall per character in `snapshotPerc30`
- **Where:** `native/di1compat/sys.hpp:612-619`
- **Severity / category / class:** Low · CPU (cold) · SAFE-REFACTOR
- **Evidence:** `while (k < 256 && readable(c + k, 1) && c[k]) ++k;` makes 1 + (length + 1) `QueryWorkingSetEx` calls per
  name, at about 0.4 µs each (sys.hpp:177-178), for every 3.0 perception and group name, a few ms per call.
- **Hot path?:** code defaults: never (needs a 1.0 DataMap's bitmasks, so `serve10`). Recommended config: once per 1.0
  bitmasks load (hkCbLoad, di1compat.cpp:3325).
- **Fix (behaviour-preserving):** call `readable` at k = 0 and whenever `c + k` enters a new 4 KiB page. The result is
  identical, since readability is per page.
- **Verification:** CONFIRMED.

### SY-04 Whole-set copies of lock UIDs
- **Where:** `native/di1compat/sys.hpp:201-226`; di1compat.cpp:3258-3261, 3396
- **Severity / category / class:** Low · CPU (cold) · SAFE-REFACTOR
- **Evidence:** `lockUids()` returns a full `std::set` copy under the lock. `loaderUids` builds a node set (caps 64 sets
  × 200,000 ids), twice per locks load (2-iteration loop at 3258-3261).
- **Hot path?:** cold in both configs (`serve10` only).
- **Fix:** a sorted vector published as `shared_ptr<const>`. Only membership is used.
- **Verification:** CONFIRMED.

### SY-06 `chestInfoLua` string/list bounds 4 bytes looser than `perceptionInfoLua`
- **Where:** `native/di1compat/sys.hpp:272-273, 282` versus `374, 389`
- **Severity / category / class:** Low · robustness (inconsistent bounds) · DOC-ONLY
- **Evidence:** the second block header is read at `s1` (261), so the pool is `[4, s1)`. chest accepts strings up to
  `s1 + 4` and list words up to `s1 + s2 + 4`, 4 bytes into the next block's size word. `u32` is bounds-checked, so there
  is no over-read.
- **Fix:** document it. Tightening it changes which malformed files are accepted.
- **Verification:** CONFIRMED.

### SY-07 32-bit wrap in the list-word bound
- **Where:** `native/di1compat/sys.hpp:393-396`
- **Severity / category / class:** Low · robustness · BEHAVIOUR-CHANGE (malformed input only)
- **Evidence:** `lw + 4 * i + 4 > lwEnd` wraps for a file-provided i ≥ 2^30. `u32` is bounds-checked, so the result is a
  wrong word that is still in bounds.
- **Fix:** 64-bit arithmetic. Valid data is unchanged; such a file then fails with "offset out of range" instead of using
  a wrong word.
- **Verification:** CORRECTED: class (the report implied no behaviour change).

### SY-08 Three identical Lua quoting helpers
- **Where:** `sys::luaQuote` sys.hpp:245-254, `npc::luaQuote` npc.hpp:492-501, `sx::ai::luaStr` sx.hpp:787-796 (append form)
- **Severity / category / class:** Low · duplication · SAFE-REFACTOR
- **Evidence:** all three escape `"` and `\` and write `\ddd` for bytes < 32 or > 126. `luachunk::quote`
  (luachunk.hpp:170-175) escapes only `\`, `"` and `\n`.
- **Fix:** merge the three.
- **Refactor risk:** never merge them with `luachunk::quote`; served text would change.
- **Verification:** CONFIRMED.

### SY-09 Repeated path normalisation and "GameDB|" parsing
- **Where:** `native/di1compat/sys.hpp:151-171`; same loop at sx.hpp:801, di1compat.cpp:398, 481, 602, gprt.hpp:167
- **Severity / category / class:** Low · duplication · SAFE-REFACTOR
- **Evidence:** `dbTrigger10` allocates, lowercases and locks on every GameDB locks/callback load (hkLoadLocks 3256,
  hkCbLoad 3321). That is cold, but it happens in both configs.
- **Fix:** one normalisation helper.
- **Refactor risk:** the two "GameDB|" parsers are not equivalent. `dbTrigger10Add` accepts an entry without a second
  `|` and normalises; `npc::gamedbEntry` requires both bars and returns (type, path) unnormalised. Share only the loop.
- **Verification:** CONFIRMED.

### SY-10 Mutation through `const Value*` in `groups30`
- **Where:** `native/di1compat/sys.hpp:695-721`
- **Severity / category / class:** Low · const-correctness · SAFE-REFACTOR
- **Evidence:** this works because `Value::tab` is a `shared_ptr`. The copy at 711 survives `g.tab` reallocation.
- **Fix:** non-const accessors.
- **Refactor risk:** keep the copy and the `std::map` output order (727).
- **Verification:** CONFIRMED.

### SY-11 Long comments / long strings not recognised by the text scanners
- **Where:** `native/di1compat/sys.hpp:43-46, 66-69, 116-117`
- **Severity / category / class:** Low · robustness · DOC-ONLY
- **Evidence:** `--` is treated as a line comment only; `--[[ ]]` and `[[ ]]` are not recognised. Whether any 1.0 locks
  table uses them is UNVERIFIED.
- **Fix:** document it.
- **Verification:** CONFIRMED.

### SY-12 Unnamed engine offsets and caps; implicit pop-in stamp protocol
- **Where:** `native/di1compat/sys.hpp:201-216, 608-631, 663-671`; di1compat.cpp:3323-3329; caprt.hpp:63
- **Severity / category / class:** Low · magic numbers / hidden coupling · SAFE-REFACTOR
- **Evidence:** `caprt::def10` recognises 1.0 definitions by `== sys::kPopin10 * sys::kPopin10`, which is what
  `applyPopin10` writes.
- **Fix:** named offsets, plus `sys::isPopin10Stamp()` next to `applyPopin10`.
- **Refactor risk:** both sides must compute the float in exactly the same way.
- **Verification:** CONFIRMED.

### NP-05 Quadratic stores; immutable 3.0 DataMaps re-parsed per playset
- **Where:** `native/di1compat/npc.hpp:328-332`; npcrt.hpp:236, 261
- **Severity / category / class:** Low · CPU (load) · NEEDS-VERIFICATION
- **Evidence:** a linear scan per store. `prepare` re-reads and re-evaluates the full ToyBox and Dory DataMaps for every
  newly prepared 1.0 playset, while holding `g_dbLock` (NR-02).
- **Hot path?:** code defaults: never; recommended config: once per 1.0 playset load.
- **Fix:** a per-table key index, plus caching the parsed 3.0 maps.
- **Refactor risk:** keep first-insertion order and last-value-wins (357). A cache must not cache a failed read.
- **Verification:** CONFIRMED.

### NP-06 Assorted duplicates and the magic `12`
- **Where:** `npc.hpp:361` (= `audio::lower`), `405`, `492` (= `sys::luaQuote`), `514`, `523-524` versus npcrt.hpp:34-35;
  npcrt.hpp:267, 272
- **Severity / category / class:** Low · duplication · SAFE-REFACTOR
- **Evidence:** `GROUPS` and `KINDS` are the same 13 kinds in different case. `12` is `strlen("Expendables|")`.
- **Fix:** shared constants.
- **Verification:** CONFIRMED.

### NP-07 `playsetText` rescans everything per group
- **Where:** `native/di1compat/npc.hpp:539-550`
- **Severity / category / class:** Low · CPU (cold) · SAFE-REFACTOR
- **Evidence:** `lower(g)` is recomputed per record × group (542), and all of `out` is scanned 13 times.
- **Fix:** iterate each group's contiguous range (`lower_bound({lower(g), ""})`). The output order (GROUPS order, then map
  order) is unchanged.
- **Verification:** CONFIRMED.

### NP-08 Navmesh size products can wrap (no reachable over-read)
- **Where:** `native/di1compat/npc.hpp:62, 65`
- **Severity / category / class:** Low · robustness · SAFE-REFACTOR
- **Evidence:** `(size_t)ne * 0x50` / `nv * 0x40` can wrap. However, each element's `body()` check fails at the first
  element beyond `n`, before the index arithmetic can wrap (v grows monotonically from `ep < n`), so the second pass
  (114-117) is never reached with a wrapped array.
- **Fix:** 64-bit products, for clarity (no behaviour change, since the wrap is unreachable).
- **Verification:** CONFIRMED.

### NP-09 Hand-built oct record type; no guard for blobs >= 16 MiB
- **Where:** `native/di1compat/npc.hpp:148-149`
- **Severity / category / class:** Low · duplication / encoding · SAFE-REFACTOR
- **Evidence:** `(uint16_t)(oct::SCALAR | oct::BYTE << 3 | (cw - 1) << 6)` hand-inverts `oct::spec` (oct.hpp:21-29). The
  other hand-built types are at oct.hpp:197, 209, 227 and 234. `cw` tops out at 3 bytes, although the format allows 4
  (`spec`: `((t >> 6) & 3) + 1`). A converted blob of 16 MiB or more would get a truncated size.
- **Fix:** an `oct::makeType()` helper. Allowing cw = 4 is a behaviour change only for ≥ 16 MiB blobs, which no shipped
  navmesh reaches.
- **Verification:** CORRECTED: added the other oct.hpp sites and the fact that the format supports a 4-byte count.

### NP-10 One namespace for six domains
- **Where:** `native/di1compat/npc.hpp` (navmesh, skin remap, Lua evaluator, DataMap/GameDB, playset text, DNA rewrite,
  perception lists)
- **Severity / category / class:** Low · god file · SAFE-REFACTOR
- **Fix:** split by domain.
- **Refactor risk:** octtest uses `npc::*` names (octtest.cpp:2826-2937).
- **Verification:** CONFIRMED.

### NP-11 Full DataMap copy into `GameDb::m10`
- **Where:** `native/di1compat/npc.hpp:445`
- **Severity / category / class:** Low · copy · SAFE-REFACTOR
- **Evidence:** this copy sits inside NR-01's window, and is the likeliest `bad_alloc` source there.
- **Fix:** take m10 by value and move it in.
- **Refactor risk:** `prepare` logs `m10.size()` afterwards (npcrt.hpp:244), so capture it first. octtest `--gamedb`
  calls `gamedb30` too.
- **Verification:** CONFIRMED.

### NR-05 Per-call engine work and locking in the perception hooks, in every playset
- **Where:** `native/di1compat/npcrt.hpp:144-223`
- **Severity / category / class:** Low · CPU (warm) · NEEDS-VERIFICATION
- **Evidence:** per call, the two hooks do the following:
  - hkPercApply takes `g_dbLock` on every successful apply. In a 1.0 playset it also runs `zoneName()` (engine call, a
    string, `lower`) and `percId` per addition.
  - hkPercRemove calls the component getter FUN_0069ECD0 and takes `g_dbLock` before finding out that `g_percOn` is
    empty.

  Both are installed whenever `gamedb30 && avperc10` (both on in both configs). Under code defaults `g_dbActive` can never
  become true, so they are pure overhead there.
- **Hot path?:** code defaults: active in every playset, doing nothing useful. Recommended config: active in every
  playset. The rate is UNVERIFIED (modifier apply/remove, not per frame as far as known).
- **Fix:** check `g_percOn.empty()` under the lock before the getter. That is safe if FUN_0069ECD0 is pure (it is the
  engine's component getter, also called at caprt.hpp:97 and di1compat.cpp:3344).
- **Refactor risk:** do not cache perception ids. Re-resolving them is deliberate (a -1 parsed before the name existed,
  92-93). Not installing these hooks when `serve10 = 0` would remove two "hook … ok" log lines.
- **Verification:** CONFIRMED.

### NR-06 Stale apply/remove pairing in `g_percOn`
- **Where:** `native/di1compat/npcrt.hpp:102, 202, 215-219`, cleared at 356-358
- **Severity / category / class:** Low · stale state · DOC-ONLY
- **Evidence:** keyed by (component, modifier) pointers and popped LIFO. If an actor dies without a remove and its memory
  is reused before the playset unload, a later remove disables ids enabled for another object (refcount underflow through
  FUN_013C9E50).
- **Fix:** document it.
- **Verification:** UNVERIFIED: settle by checking whether the engine always calls the remove (FUN_0152AFA0) before
  destroying an actor that holds an applied modifier.

### NR-07 Unnamed engine globals, offsets and vtable slots
- **Where:** `native/di1compat/npcrt.hpp`. The repeated values:
  - router `[0x022A6E68]`: 71, 285, 363;
  - lookup 0x00705860: 232, 340;
  - component getter 0x0069ECD0 with `[0x02260044]`: 174, 211 (also caprt.hpp:97, di1compat.cpp:3344);
  - `[0x02289700]`: 115 (also di1compat.cpp:3323-3324);
  - vtable slots `[1]`/`[2]`: 73, 288-289, 365; `vt[8]`/`vt[9]`: 180;
  - stream close `[11](s, 1)`: 268 (also di1compat.cpp:512, 1135, 3305, 3568, 3571, 3608);
  - zone record: bytes 8/9, stride 12 (193-195).
- **Severity / category / class:** Low · magic numbers · SAFE-REFACTOR
- **Fix:** named constants, plus `engine::closeStream()` and `engine::component()` helpers.
- **Verification:** CONFIRMED (all lines re-checked).

### NR-08 Side effects inside `&&` chains; IIFE in `logf` args
- **Where:** `native/di1compat/npcrt.hpp:133-134` (`plain[p++]`, `&& ++z`), `163-164`, `272`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Fix:** plain loops and a named local.
- **Refactor risk:** `++z` runs only when the earlier conditions hold, and the loop breaks on `!ok`; keep both.
- **Verification:** CONFIRMED.

### NR-09 Hidden include-order dependency; stale header comment
- **Where:** `native/di1compat/npcrt.hpp:1`; included at di1compat.cpp:621
- **Severity / category / class:** Low · coupling · SAFE-REFACTOR
- **Evidence:** the comment names `logf`, `hook`, `g_cfg`, `read10s` and `read30x`. The file also uses `rt`, `read30`,
  `g_open`, `g_sessLock`, `g_served` and `oct::File`. `g_open` is null if the AssetOpen hook was refused, and `prepare`
  calls it unconditionally (250 via `read30`, 267).
- **Fix:** correct the comment, or pass the dependencies in as `audio::install` does.
- **Verification:** CONFIRMED.

### NR-10 String building and router calls on every include
- **Where:** `native/di1compat/npcrt.hpp:328, 337`
- **Severity / category / class:** Low · CPU · SAFE-REFACTOR
- **Evidence:** `what + KINDS[…] + " " + name` is built for every include. `route()` returns at 72 for empty `todo`
  without logging, and `dataMapRoute` returns at 286 for an empty path.
- **Hot path?:** both configs: every DataMap record include (load time).
- **Fix:** guard with `if (!todo.empty())`; the result is identical.
- **Verification:** CONFIRMED.

### NR-11 Most npcrt detours have no exception guard
- **Where:** `native/di1compat/npcrt.hpp:144-223, 305-371`
- **Severity / category / class:** Low · exception safety · BEHAVIOUR-CHANGE (failure path)
- **Evidence:** only `prepare` is in a try/catch (300-304). The rest of hkInclude, and all of hkPercApply, hkPercRemove
  and hkPlaysetUnload, allocate without a catch, which breaks the code's own rule "never unwind into engine code" (302).
  The only possible throw is `bad_alloc`.
- **Fix:** a shared guard that logs and still calls the original exactly once. That changes a crash into a logged
  continue.
- **Verification:** CONFIRMED.

### NR-12 `remove_if` predicate with side effects
- **Where:** `native/di1compat/npcrt.hpp:324`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Fix:** an explicit loop with the same first-wins semantics.
- **Verification:** CONFIRMED.

### NR-13 Overlapping 1.0 playsets: first playset's tables released at the second's unload
- **Where:** `native/di1compat/npcrt.hpp:298-331, 346-371`
- **Severity / category / class:** Low · state · DOC-ONLY
- **Evidence:** if a second 1.0 playset's Playset include comes before the first's unload, the bookkeeping goes wrong:
  - `prepare` replaces `g_db` while `g_dbActive` is still true;
  - tables already in `g_dbRouted` are not re-routed, and `g_dbOwned` accumulates both playsets' tables;
  - unload(first) returns at 353 (the name differs), and unload(second) releases both sets.

  So the first's tables are released late, or early if the second unloads first. They are not "never released", as the
  report says. Actor includes of the first playset in between resolve against the second's `m10`.
- **Fix:** document it, pending confirmation.
- **Verification:** CORRECTED: the release behaviour is described above. Whether the engine can include two playsets
  without an unload in between is still UNVERIFIED.

### XF-01 Always-on probes do `readable` + engine work every frame
- **Where:** `native/di1compat/di1compat.cpp:2952-2979` (hkNavUpdate, hooked at 4288 under `lockTrace10`);
  gprt.hpp:307-345 (hkDeliver), 193-211 (hkFlashCall), 161-180 (`movie10`, `gfxString`); di1compat.cpp:2988-3012
  (hkMisProgress)
- **Severity / category / class:** Low · CPU (per frame) · SAFE-REFACTOR
- **Evidence:** per call, the probes do the following:
  - hkNavUpdate runs 2-3 `QueryWorkingSetEx` calls (`readable(nav, 0xa8)`, `readable(m0, 0x28)`) and the screen lookup
    FUN_0154FED0 every frame. It keeps doing so after its 300-line budget is spent, because the budget is tested last
    (2975).
  - hkDeliver's probe block does a `safeCopy` of the screen name on every call.
  - hkMisProgress makes 3-4 `readable` calls, a `std::string`, a mutex and a set lookup before its dedupe, on every
    progress write.

  Correction: hkDeliver's `movie10` call and hkFlashCall's `screenOfCall` + `movie10` are not probe work. Their result
  (`ten`) drives keys10 and actionMap10, so they cannot be skipped after the log budget.
- **Hot path?:** identical in both configs (`lockTrace10`, `keys10` and `actionMap10` are on in both): hkNavUpdate every
  frame; hkDeliver per action delivery (rate UNVERIFIED); hkFlashCall per frame only while a list key is held. At about
  0.4 µs per check, this is a few µs per frame, not measurable against a 16 ms frame.
- **Fix (behaviour-preserving):**
  - hkNavUpdate: `if (lines > 300) return;` right after `g_navUpdate(nav)`. Logs stay identical, because `lines` only
    grows.
  - hkDeliver: move the name `safeCopy` inside the `inputRoom()` branch.
  - hkMisProgress: no change; its dedupe needs the name.
- **Refactor risk:** do not cache `movie10` per screen without invalidation.
- **Verification:** CORRECTED: severity Low (the report said Low-Med); separated the functional work from the probe work.

### XF-03 Duplicated engine CString handling
- **Where:** `native/di1compat/npcrt.hpp:42-48` (`ECStr`); di1compat.cpp:3237-3244 (`cbLoad`); bldrt.hpp:60
- **Severity / category / class:** Low · duplication · SAFE-REFACTOR
- **Evidence:** all three use ctor 0x00418310; npcrt and cbLoad also release with 0x00418230. bldrt.hpp:60 builds a
  CString passed by value that the callee releases, so it uses only the ctor.
- **Fix:** one shared `ECStr` for the first two; bldrt keeps its by-value transfer.
- **Verification:** CORRECTED: bldrt uses only the ctor half.

### XF-04 Two scanners for vformat.vtxf
- **Where:** `native/di1compat/npc.hpp:168-202` (`skinLayouts`, its own brace walker on top of `gen30::ventries`);
  gen30.hpp:157-181 (`vformats`)
- **Severity / category / class:** Low · duplication · NEEDS-VERIFICATION
- **Fix:** let `gen30::vformats` also return stream/offset data.
- **Refactor risk:** octtest `--skin` and the shaders10 vertex formats must both stay identical.
- **Verification:** CONFIRMED.

---

## Inventory

All line ranges below were re-checked mechanically (grep and scan). Rows that changed are marked "(fixed)".

### Functions and structs: sx.hpp
| Name | Lines | Purpose | Hot/cold (recommended config; code defaults: no DI1 script runs) |
|---|---|---|---|
| `rd` / `wr` / `is10` | 38 / 39 / 41 | LE u32; 1.0 SLNG check | `rd` per bind/isDi1 |
| `transcode` | 48-83 | 1.0 .sx container to 3.0 (24-B imports appended) | per .sx serve |
| `Cell`, `Vm` | 86, 87-95 | VM cell and stack accessors | hot |
| `g_exe`, `g_log`, `NativeFn`, `call30` | 97-100 | exe base, log, rebased call | hot |
| `Native` | 102-107 | {name, args, results, fn, hits}, 16 B | data |
| `callingEntry` | 110-115 | import entry of the running CALL | hot |
| `stub` | 118-126 | stack-balanced stub | hot |
| `readStr` | 129-137 | `__try` string copy (SX-05) | hot |
| `Key`, `KEYS30` | 145-240 | 429 key name→id rows | data |
| `keyFallback`, `keyId`, `keyCell` | 243-248, 249-264, 266-272 | key translation | hot (natives), cold (bmx/btx) |
| `hashFind`/`hashAdd`/`hashRemove` | 274/276/278 | key-translating forwards | hot |
| `hashFindInRange` | 281-295 | pop 3, push 5 | hot |
| other ports | 299, 302, 305-310, 313, 315, 318, 321-340, 344, 349-356 | see SX-09 | hot |
| `BEH10`, `beh30Name`, `behTo30`, `behTo10`, `behCell30` | 364-369, 370, 371-377, 378-385, 386 | behaviour renumbering | hot |
| AiBehavior ports | 389-392, 393-396, 398-404, 406-411 | | hot |
| `g_natives` | 413-583 | 465 stub rows | data |
| `g_ports` | 584-601 | 21 ports | data |
| `table`, `init`, `provide` | 603-607, 610-616, 621-627 | registry; `provide` dead | once / dead |
| `bind` | 631-664 | rebind DI1 imports | per relink |
| `LinkFn`/`g_link`/`hkLink` | 667, 668, 669-676 | linker detour | per relink |
| `isDi1`, `behInitParams` | 683-687, 688-696 | `__try` | every call-by-name (both configs) |
| `CallByNameFn`/`g_callByName`/`hkCallByName` | 697, 698, 699-703 | call-by-name detour | every call-by-name (both configs) |
| `ai::Val`, `ai::Table`, `ai::eval` | 723, 724, 725-786 | constructor evaluator | cold |
| `ai::luaStr`, `luaNum`, `Exists`, `lower`, `method` | 787-796, 797, 798, 799-803, 805-810 | text helpers | cold |
| `ai::keyed`, `ai::value`, `ai::bmx`, `ai::btx` | 812-823, 827-848, 850-878, 880-896 | 3.0 Lua text | cold |

### Functions and structs: sys.hpp
| Name | Lines | Purpose | Hot/cold |
|---|---|---|---|
| `npos`; `skipString`, `matchBrace`, `findKey` | 23; 26-31, 34-52, 55-80 | Lua text scanning | cold |
| `keepRecords` | 85-123 | filter GameDB records by UID | cold |
| `kLocks10Prefix`, `kLocks10Core` | 135-136 | served paths | — |
| `dbTriggers10`, `dbTriggers10Lock`, `dbTrigger10Add`, `dbTrigger10` | 141-145, 146-150, 151-161, 163-171 | 1.0 GameDB path registry | per DataMap serve / per GameDB load (both configs) |
| `readable` | 180-196 | page probe (QWSEx, VirtualQuery fallback) | see readable table |
| `loaderUids`; `g_lockMx`, `g_lockUids`, `setLockUids`, `lockUids` | 201-220; 223-226 | Locks DB UIDs | cold |
| `luaQuote` | 245-254 | Lua literal | cold |
| `chestInfoLua` | 256-342 (87) | ChestInfo.bin to Lua | cold |
| `Perc30`; `perceptionInfoLua` | 359-362; 364-589 (226) | perceptions to Lua | cold |
| `kPerc10Prefix`, `kPerc10`, `g_percMx`, `g_perc30`, `g_group30`, `perc30` | 593-597, 598-605 | perception snapshot | cold |
| `snapshotPerc30` | 608-638 | read engine names/groups | cold (SY-03) |
| `kChest10*`, `kRaw10Prefix`, `kBits10`, `kPopin10`; `applyPopin10`; `g_pkg10` | 642-663; 664-671; 672 | pop-in stamp | per expendable def in a 1.0 package |
| `groups30` | 686-729 | expendable groups to 3.0 keys | cold |

### Functions and structs: npc.hpp
| Name | Lines | Purpose | Hot/cold |
|---|---|---|---|
| `NAV_MASK30`; `nav30`; `navmesh30` | 39; 41-125 (85); 129-155 | navmesh 1.0 to 3.0 | per served .oct |
| `SkinLayouts`, `skinLayouts`; `skinRemap` | 167, 168-202; 210-284 (75) | skin palettes | once; per served .oct |
| `LVal`, `LTab`; `luaEval` | 290-291; 293-354 (62) | constructor evaluator | cold |
| `DmRec`, `DataMap`, `lower`; `dataMap` | 358, 359, 361; 363-388 | DataMap model | cold |
| `gamedbEntry`, `dnaOf`, `reach` | 391-398, 400-410, 413-421 | GameDB entries, closure | `reach` per include (recommended) |
| `GameDb`, `gamedb30` | 436-440, 442-472 | 3.0 tables a 1.0 map lacks | cold |
| `luaQuote`, `coreExpendables10`, `playsetText` | 492-501, 508-516, 519-552 | Playset record text | cold |
| `luaStrings`, `psDna30` | 562-601, 603-621 | DNA rewrite | per served _ps.dnax |
| `PercEntry`, `PercList`, `PercAdds`; `percLists`, `percActive`, `percAdds` | 636-638; 641-680, 683-698, 701-730 | perception diff | cold / per zone |

### Functions and structs: npcrt.hpp
| Name | Lines | Purpose | Hot/cold |
|---|---|---|---|
| typedefs, `g_include`, `g_playsetUnload` | 19-25 | | — |
| `g_dbLock` … `g_psPath`, `KINDS` | 27-35 | gamedb30 state | — |
| `ECStr`, `loaded`, `route` | 42-48, 54-65, 68-84 | engine CString, router | per include |
| perc typedefs and globals | 94-102 | avperc10 state | — |
| `zoneName`, `percId`, `percList` | 104-111, 113-117, 120-142 | | per apply |
| `hkPercApply`, `hkPercRemove` | 144-204 (61), 206-223 | perception detours | per modifier apply/remove (both configs) |
| `prepare` | 226-280 (55) | 1.0 playset supplement | once per 1.0 playset (recommended only) |
| `dataMapRoute`, `hkInclude`, `hkPlaysetUnload` | 283-291, 293-344 (52), 346-371 | include/unload detours | per include/unload (both configs) |
| `QbFactoryFn`, `g_qbFactory`, `hkQbFactory` | 383, 384, 385-391 | QB type 10 | per QB creation |
| `skin10`, `install` | 396-409, 411-423 | | per .oct / once |

### Hooks (prologues re-compared with source; callee-pop sizes and return use checked by disassembly)
| Name (log text) | Static VA | Prologue | Detour → original | Installed at | Gate (code defaults / recommended) | Engine ret | Frequency |
|---|---|---|---|---|---|---|---|
| Slang linker (1.0 natives) | 0x00736DA0 | `51538B5C240C568BF18BCBE8` | `sx::hkLink` → `sx::g_link` | di1compat.cpp:4311 | `sx10 && sxNatives10` (on/on); `sx::init` first (4310) | `ret 4`, AL = ok | every script request (relink, SX-N1) |
| Slang call by name (1.0 behavior type) | 0x00739090 | `56578B7C240C8BF18B4E04` | `sx::hkCallByName` → `sx::g_callByName` | 4313 | + `sxPorts10 && sxBeh10` (on/on) | `ret 8`, EAX incidental (SX-18) | every call-by-name of every script |
| quarterback factory (spawner10) | 0x010C2F70 | `64A1000000006AFF68????????50` | `npcrt::hkQbFactory` | npcrt.hpp:414 | `spawner10` (on/on) | cdecl | per QB creation |
| DataMap record include (gamedb30) | 0x0070D730 | `6AFF68????????64A100000000` | `hkInclude` → `g_include` | npcrt.hpp:416 | `gamedb30` (on/on); `prepare` needs `serve10` (off/on) | `ret 0x10` | per DataMap record include |
| DataMap playset unload (gamedb30) | 0x0070DF70 | `566A0068????????8BF1` | `hkPlaysetUnload` → `g_playsetUnload` | npcrt.hpp:417 | `gamedb30` | `ret 4`; one caller (0x01440F1C), EAX unused | per playset unload |
| perception mode apply (avperc10) | 0x0152AE80 | `51837C240CF9578BF9` | `hkPercApply` → `g_percApply` | npcrt.hpp:419 | `gamedb30 && avperc10` (on/on) | `ret 0xc` | per modifier apply |
| perception mode remove (avperc10) | 0x0152AFA0 | `837C2408F9568BF1` | `hkPercRemove` → `g_percRemove` | npcrt.hpp:420 | same | `ret 0xc` | per modifier remove |

Script import-slot bindings (not MinHook): `sx::bind` writes `stub` or a port at entry+8 and the `Native*` at entry+12
(sx.hpp:654-655): 465 stub rows and 21 ports, called as `void __cdecl fn(ctx)`. The engine rewrites +8 on every relink
(SX-N1).

### Generic helpers (cross-file duplicate matching)
| Helper | File:line | Duplicates / notes |
|---|---|---|
| `sx::rd` / `sx::wr` | sx.hpp:38 / 39 | `oct::u` (oct.hpp:31) (fixed), u32 lambdas sys.hpp:259, 371, npc.hpp:44, 300, 567; `oct::File::put` (oct.hpp:187), nav30 `put` (npc.hpp:85) |
| `sx::call30`, `sx::beh30Name` | sx.hpp:100, 370 | `rt()` (di1compat.cpp:106) |
| `sx::readStr` | sx.hpp:129 | `safeCopy` (di1compat.cpp:87), `cstr` (di1compat.cpp:2735), sys.hpp:612 lambda (XF-02) |
| `sx::keyFallback` | sx.hpp:243 | FNV-1a; differs from `audio::fnv1` (FNV-1, lower-cased, audio.hpp:45) |
| `sx::ai::eval`, `npc::luaEval`, `npc::luaStrings`, `luachunk::eval`, `luachunk::rewrite` | see NP-01 | five walkers |
| `sx::ai::luaStr`, `sys::luaQuote`, `npc::luaQuote` | sx.hpp:787, sys.hpp:245, npc.hpp:492 | identical; NOT `luachunk::quote` |
| `sx::ai::luaNum` | sx.hpp:797 | `%.17g`; sys `%.9g` (312, 404); `luachunk::numText` |
| `sx::ai::lower` (lower + `\`→`/`) | sx.hpp:799 | `norm` (di1compat.cpp:478), sys.hpp:158, 166, gprt.hpp:167, di1compat.cpp:398, 602 |
| `npc::lower`, `audio::lower` | npc.hpp:361, audio.hpp:53 | identical (lowercase only) |
| `sys::readable` / `::readable` | sys.hpp:180 / di1compat.cpp:1228 | SY-02 |
| `sys::dbTrigger10Add` / `npc::gamedbEntry` | sys.hpp:151 / npc.hpp:391 | different parse rules (SY-09) |
| `fail` lambda | sx.hpp:51, 728; sys.hpp:258, 370; npc.hpp:43, 296, 605 | repeated idiom |
| `npcrt::ECStr` | npcrt.hpp:42 | di1compat.cpp:3237-3244; bldrt.hpp:60 is ctor-only (fixed) |
| stream close `(*(void***)s)[11](s, nullptr, 1)` | npcrt.hpp:268 | di1compat.cpp:512, 1135, 3305, 3568, 3571, 3608 |
| component getter `rt(0x0069ECD0)` | npcrt.hpp:174, 211 | caprt.hpp:97, di1compat.cpp:3344 |
| Lua 5.1 header `HDR[12]` | sx.hpp:727; npc.hpp:297, 564 | luachunk.hpp:34, 102 |

### Namespace-level globals and statics
| Name | File:line | Type | Writers | Readers / threads | Guard |
|---|---|---|---|---|---|
| `sx::g_exe` | sx.hpp:97 | `uintptr_t` | Pentane_Main (di1compat.cpp:4309) | natives | none; written before the hooks |
| `sx::g_log` | sx.hpp:98 | fn ptr | Pentane_Main (4307), after AssetOpen is live (4183) | `keyId`, `stub`, `bind` | none (SX-19) |
| `sx::KEYS30`, `BEH10` | sx.hpp:146, 364 | const arrays | — | any | n/a |
| `sx::g_natives`, `g_ports` | sx.hpp:413, 584 | `Native[]` | `fn`: `provide` only (never called); `hits`: `stub` | script threads | `hits` interlocked |
| `sx::table()` static | sx.hpp:605 | `unordered_map<string, Native*>` | `init` (before hkLink) | `bind` | none; safe by ordering |
| `keyId` statics `ids`/`mx`/`told` | sx.hpp:251, 259, 260 | map / mutex / `set<int>` | magic static; `told` on misses | asset-open, script threads | `told` under `mx` |
| `bind` statics `mx`/`names` | sx.hpp:636-637 | mutex / map | `bind` | linker threads | `mx`; grows without bound |
| `reactionInfoFlipMotionName::buf` | sx.hpp:333 | `char[512]` | native | scripts | none (1.0 semantics) |
| `sx::g_link`, `g_callByName` | sx.hpp:668, 698 | trampolines | `hook()` | detours | n/a |
| `sys::dbTriggers10()`/`dbTriggers10Lock()` | sys.hpp:143, 148 | `set<string>` / mutex | `dbTrigger10Add` (DataMap transcode, di1compat.cpp:1087) | `dbTrigger10` (hkLoadLocks, hkCbLoad) | locked; never cleared |
| `sys::g_lockMx`/`g_lockUids` | sys.hpp:223-224 | mutex / set | hkLoadLocks (3260) | `sysServe` (3396) | locked; copied |
| `sys::g_percMx`/`g_perc30`/`g_group30` | sys.hpp:595-597 | mutex / set / map | `snapshotPerc30` (3325) | `perc30()` (3388) | locked; copied |
| `sys::g_pkg10` | sys.hpp:672 | `thread_local int` | hkExpPkg (3306-3309) | hkExpDef (3362) | thread-local |
| `npcrt::g_dbLock` | npcrt.hpp:27 | `std::mutex` | — | hkInclude, hkPlaysetUnload, hkPercApply, hkPercRemove | — |
| `g_db` (raw owner), `g_dbPlayset`, `g_dbActive`, `g_dbRouted`, `g_dbOwned`, `g_psPath` | npcrt.hpp:28-33 | various | `prepare`, hkInclude, hkPlaysetUnload | same | all under `g_dbLock` (every access re-checked) |
| `g_pl10`, `g_pl30`, `g_percMod`, `g_percZone`, `g_percOn` | npcrt.hpp:98, 100-102 | vectors / maps | `prepare`, perception hooks, unload | engine threads (unknown) | `g_dbLock` |
| `skin10` statics `once`/`L` | npcrt.hpp:398-399 | `once_flag`, `SkinLayouts` | `call_once` | transcode threads | `call_once` |

Lock facts (corrected): under `g_dbLock`, `prepare` takes and releases `g_zipLock`, `g_choAlias.m`, arc30's `cipherLock`,
`g_sessLock` and `g_logLock` one at a time (none nested). `indexZip10` nests `g_zipLock` → `g_sessLock`. No plugin code
holds any of these while calling engine code or a `g_dbLock` hook, so there is no inversion in the plugin. Engine calls
under `g_dbLock`: FUN_00705860, AssetOpen (`g_open`), FUN_01496720, FUN_01391040 (NR-02).

### `sys::readable` cost model and every call site
Implementation (sys.hpp:180-196): it rejects addresses below 0x10000 and wrap-around. It makes one `K32QueryWorkingSetEx`
per 4 KiB page (about 0.4 µs). A page that is resident and has no NOACCESS/GUARD bit is accepted. Otherwise it makes one
`VirtualQuery` (which can cost 0.19-5.1 ms in a large touched region, per the comment) and jumps to that region's end. So
the cost is zero `VirtualQuery` calls when every page is resident, and at most one per region that holds a non-resident
page. A range of k pages costs k calls (`readable(arr, cnt * 4)` with cnt up to 100,000 is about 98 calls; `loaderUids`'
`readable(ids, n * 4)` is up to 196).

| File:line | Function | Code defaults | Recommended config |
|---|---|---|---|
| di1compat.cpp:91 | `safeCopy` (58 call sites: di1compat.cpp 39, gprt 9, caprt 4, vfxrt 4, bldrt 2) (fixed) | per asset open (hkOpen 3557), per GameDB load, plus the per-frame users below | same |
| di1compat.cpp:722, 725 | `surfaceTable30` (+ one `safeCopy` per surface name, up to 1024) | never (`serve10`) | per served .oct (`surfaces10`) |
| di1compat.cpp:1515 | `tdReadable` (47 lines of `tdReadable`/`tdRd` uses) | `teardownTrace` off | off |
| di1compat.cpp:2738 | probe `cstr` (19 call expressions) (fixed) | inside the `lockTrace10` budgets; missionTrace sites off | same |
| di1compat.cpp:2812 | hkMsgRecv | inside the 4000-line budget | same |
| di1compat.cpp:2955, 2957 | hkNavUpdate | **every frame**, even after the budget (XF-01) | same |
| di1compat.cpp:2991 (×2), 2994 | hkMisProgress | every progress write, before the dedupe | same |
| di1compat.cpp:3003, 3005 | hkMisProgress loop | after dedupe and budget | same |
| di1compat.cpp:3032, 3037, 3043, 3062, 3063, 3130 | hkFpUpdate / hkFpNodeUpdate / hkFpScan | `missionTrace` off | off |
| di1compat.cpp:3249, 3250 | `singletonLoader` | only after `dbTrigger10` matches a 1.0 table: never | per 1.0 locks/bitmasks load |
| di1compat.cpp:3293 | `registerDelivery10` | until the first success (then a mutex per package load) | same |
| gprt.hpp:174, 178 | `gfxString` | per movie ExternalInterface call (`actionMap10 \|\| lockTrace10`); per frame while a list key is held | same |
| caprt.hpp:63 | `def10` | per expendable base spawn / async completion (`shutdown10 && popin10`), 3.0 playsets included | same |
| caprt.hpp:68, 72, 152, 153, 160 | `defText`, `mode`, hkNode, hkReq | `capsuleTrace10` off | off |
| caprt.hpp:122, 125 | hkDone | per async expendable completion | same |
| bldrt.hpp:99, 100 | hkNode | `spawnTrace10` off | off |
| vfxrt.hpp:72, 117, 119 | `trace`, hkAttached (`using sys::readable`, vfxrt.hpp:63) | `vfxTrace` off | off |
| sys.hpp:205, 208, 211, 213, 216 | `loaderUids` | never | twice per 1.0 locks load |
| sys.hpp:613, 616 | `snapshotPerc30`'s `cstr` | never | once per character per name per 1.0 bitmasks load (SY-03) |
| sys.hpp:622, 625, 629 | `snapshotPerc30` | never | per 1.0 bitmasks load |
| octtest.cpp:2466, 2518 | `--readable` test/benchmark | offline | offline |
| *(not sys)* di1compat.cpp:1246 | `afterCall` → `::readable` (VirtualQuery) | stack walks: first AV, hkAvRemove, the bitmasks-open stack (`trace`), the profiler (`loadProf10` off) | the bitmasks-open stack is off (`trace = 0`); others the same |

### Invariants a behaviour-preserving refactor must keep (verified)
- **Prologues, VAs and offsets.** Every `hook()` prologue (table above) and every `rt(0x…)`/`call30(0x…)` address is
  reverse-engineered. Never "fix" one.
- **Callee-pop sizes**, verified by disassembly: hkLink `ret 4`, hkCallByName `ret 8`, hkInclude `ret 0x10`,
  hkPlaysetUnload `ret 4`, hkPercApply/Remove `ret 0xc`, hkQbFactory cdecl.
- **Return values.** 0x00736DA0 returns AL (keep `uint8_t`); 0x0152AE80/AFA0 return values are used (`r & 0xff`).
  0x00739090 and 0x0070DF70 are effectively void (SX-18; NR-04 rejected).
- **SEH.** `readStr`, `behInitParams` (sx) and `safeCopy`, `stackOf`, `hdrName`, `afterCallSafe`, `ourRetSafe`,
  hkAssetDef (di1compat.cpp) use `__try`. They must not acquire objects with destructors (C2712). `__try` does not
  prevent a CrabeLoader dump or Pentane's game end.
- **VM stack balance.** The SX-09 table must hold exactly.
- **Order.** `sx::init` before the hkLink hook. In hkInclude: route (328), then `dataMapRoute` (337), then the original
  include (338), then verification (339-342). `g_served[g_psPath]` is published before `dataMapRoute(true, …)`.
- **Exceptions.** None may escape a detour (hkOpen's catch at di1compat.cpp:3714; npcrt.hpp:302; hkLink's `catch (...)`).
- **Byte-identical output.** bmx/btx, chest, perception, playset, groups30 and natives.tsv text: quoting (`luaQuote`
  versus `luachunk::quote`), number formats (`%.17g`, `%.9g`, `numText`) and iteration order (insertion versus
  `std::map`). Golden tests: octtest `--ai`, `--sx`, `--sxkeys`, `--chest`, `--perc`, `--groups30`, `--dmeval`, `--gamedb`,
  `--playset`, `--avperc`, `--psdna`, `--nav`, `--skin`.
- **Log text.** Keep it verbatim ("sx stub %s first called", "sx link %s:", "gamedb30 …", "STILL MISSING", "NOT FOUND",
  "playset30: …"). The only tracked file outside native/ that mentions the log is README.md:114, which names the file.

## Rejected on verification
- **NR-04 `hkPlaysetUnload` returns `void` and runs code after the original:** disassembly shows FUN_0070DF70 ends with
  `call 0x70D940; pop esi; ret 4`. Its only direct caller (0x01440F1C) overwrites EAX immediately
  (`mov edx,[ebp]; mov eax,[edx+0xa4]`), and the function has no absolute references (no vtable or callback use). Nothing
  consumes its return value, so the post-call work cannot break a caller. The `void` typing is correct in effect.

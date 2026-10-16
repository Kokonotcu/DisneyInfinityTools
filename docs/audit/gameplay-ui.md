# Gameplay and UI runtime (`gameplay.hpp`, `gprt.hpp`, `ui.hpp`, `bldrt.hpp`, `caprt.hpp`, `di1compat.cpp` ui section)

Scope: every line of `native/di1compat/gameplay.hpp` (751), `gprt.hpp` (491), `ui.hpp` (250), `bldrt.hpp` (115), `caprt.hpp` (221) and `di1compat.cpp` 3444-3758 (ui section incl. `hkOpen`), plus the code they lean on: `logf`/`safeCopy`/`rt`/`hook` (66-128), `g_cfg` (131-237) and `loadConfig` (239-, only call 4121), VFS `zipFor`/`read10`/`read10z`/`memFile`/`norm`/`read10s` (373-495), session globals + `indexZip10` (551-606), `readStream`/`copyToMemFile`/`takeChdPath` (1114-1162), `stackOf` (1254-1267), `surfaceTable30`/`transcodeOct`/`transcode` (718-898), currency10 (2529-2664), `chdForLua` (3197-3210), install sites (4183, 4270-4358), `sys::readable` (sys.hpp:180-196), `oct::File` (oct.hpp:100-177), `luachunk::quote` (luachunk.hpp:170), `mission::beaconMap30` (mission.hpp:208-234), `di1compat.cfg.example`, octtest mode list. Repo at 684539b, unchanged (read-only).
Engine claims were checked by disassembling the shipped `DisneyInfinity3.exe` (static VAs, capstone; helper script `C:\Users\Koray\AppData\Local\Temp\claude\C--Users-Koray-Desktop-Koray-DisneyInfinityTools\9994e4a0-1a84-41af-9e5d-169372e85df2\scratchpad\vfy_gpui\x86d.py`, commands `dis <va>`, `callers <va>`, `refs <va>`, `fstart <va>`).

Verification: 48 findings checked: 31 confirmed, 15 corrected, 1 unverified, 1 rejected; 1 new.

**Gating facts verified (the caller asked for these):**
- `lockTrace10 = true` is the code default (di1compat.cpp:157) and `di1compat.cfg.example` does not set it, so it is **on in both the code defaults and the recommended config**. In this slice it installs the vehicle10 probe (gprt.hpp:462-464) and on its own keeps `hkFlashCall` (476) and `hkDeliver`/`hkMapState` (484) installed even if actionmap10/keys10 are off. It also installs 17 more locktrace hooks in di1compat.cpp:4270-4291 (outside this slice).
- No `gprt::install` (gprt.hpp:460-489), `bldrt::install` (bldrt.hpp:105-113) or `caprt::install` (caprt.hpp:202-219) gate references `serve10`: **confirmed**. The four ui hooks at di1compat.cpp:4319-4331 do require `serve10`. `hkOpen` (4183) is always installed.
- `g_cfg` is written only in `loadConfig` (grep: no other assignment), so every gate is constant after startup.
- Hook count: code defaults = 17 in this slice's runtime headers (gprt 12, bldrt 1, caprt 4) + AssetOpen. Recommended config = the same 17 + `hkZoneStr`, `hkFmvPath`, `hkCtxParse`, and `hkFindScreen` when the push-literal check at 4325-4326 passes, + AssetOpen.
- Under the code defaults (`install10` empty, `serve10` off) `g_movies10` / `g_screens10` are never filled (only the 1.0-serve branch at 3722-3726 inserts). So every gprt UI hook can never act, but it still pays its per-call lookups (GP-07, GP-08, GP-18).
- **Cached engine pointers across a zone change:** none in this slice is dereferenced after the call that read it.
  - `hkDeliver`'s `static uint8_t* last` (gprt.hpp:311) is only compared. The worst case of address reuse is one suppressed probe line.
  - `g_keysScreen` / `g_keysMovie` are cleared after every delivery (344).
  - `g_keep` holds generation-tagged handles, not pointers.
  - Every manager pointer (`*rt(0x0225F548)`, `*rt(0x02289E88)`, `*rt(0x0228AA3C)`, ...) is re-read on each call.
  - The engine object write `tool+0x74 = 1` (gprt.hpp:452) has the same lifetime as the engine's own cached verdict (GP-19).
  - The only dangling or reuse hazards are in two proposed fixes: the GP-06 caching tier and GP-07 (c). Both are flagged there.

## Findings
| ID | Title | Sev | Class | Where |
|---|---|---|---|---|
| GP-01 | `hkOpen` is a 208-line god function; its dispatch order is behaviour | High | NEEDS-VERIFICATION | di1compat.cpp:3549-3756 |
| GP-02 | gprt.hpp: 6 features, 2 probes, 12 hooks, not self-contained | Med | SAFE-REFACTOR | gprt.hpp:1-491 |
| GP-03 | gameplay.hpp mixes six domains | Med | SAFE-REFACTOR | gameplay.hpp:1-751 |
| GP-04 | `activityGraph30` 140-line god function | Med | NEEDS-VERIFICATION | gameplay.hpp:504-643 |
| GP-05 | `playsets10` 96-line god function | Med | NEEDS-VERIFICATION | gameplay.hpp:322-417 |
| GP-06 | `hkToolGate` redoes the full 1.0 lookup on every call for a refused tool | Med | NEEDS-VERIFICATION | gprt.hpp:441-458; gameplay.hpp:741-749 |
| GP-07 | `movie10`/`script10` on the per-frame delivery: syscall + heap + shared `g_sessLock` | Med | NEEDS-VERIFICATION | gprt.hpp:161-170, 275-282 |
| GP-11 | Engine-access code duplicated across gprt, bldrt, di1compat, npcrt | Med | SAFE-REFACTOR | gprt.hpp:24, 34-35, 64-69, 118-131, 277-281; di1compat.cpp:2561-2573, 3501, 3521-3538 |
| GP-41 | `keep()` calls `g_renderShut` with no null check | Med | BEHAVIOUR-CHANGE | caprt.hpp:97-98, 206-213 |
| GP-08 | `hkFlashCall` looks up the screen for every ExternalInterface call | Low | NEEDS-VERIFICATION | gprt.hpp:182-211 |
| GP-09 | `hkDeliver` probe work every frame; raw `char*` to `%s` | Low | NEEDS-VERIFICATION | gprt.hpp:307-345 |
| GP-10 | The locktrace10 probes are on by default (both configs) | Low | DOC-ONLY | di1compat.cpp:157; gprt.hpp:190-191, 310-321, 462-464 |
| GP-12 | Reverse-engineered offsets are unnamed magic numbers | Low | SAFE-REFACTOR | gprt.hpp, bldrt.hpp, caprt.hpp (all) |
| GP-14 | `hkToolGate` dereferences the zone manager without its own null check | Low | SAFE-REFACTOR | gprt.hpp:445-446 |
| GP-15 | `g_keysScreen`/`g_keysMovie` plain globals, no re-entry handling | Low | BEHAVIOUR-CHANGE | gprt.hpp:272-273, 323-325, 344 |
| GP-16 | Dead typedef `ActionStateFn` | Low | SAFE-REFACTOR | gprt.hpp:227 |
| GP-17 | `hkShowRibbon` returns -1 on success; one engine caller uses the value | Low | DOC-ONLY | gprt.hpp:62, 71 |
| GP-18 | Gating and hook naming inconsistent across runtime headers | Low | DOC-ONLY | gprt.hpp:460-489; bldrt.hpp:107-108; caprt.hpp:210-212; di1compat.cpp:4319-4331 |
| GP-19 | `tool+0x74 = 1` lifetime follows the engine's own cache | Low | DOC-ONLY | gprt.hpp:452 |
| GP-20 | `[0]` on vectors that may be empty | Low | BEHAVIOUR-CHANGE | gameplay.hpp:96, 515, 546, 622, 623 |
| GP-21 | `toolLevels10` section walk can wrap 32-bit `size_t` | Low | BEHAVIOUR-CHANGE | gameplay.hpp:716-719 (+730) |
| GP-22 | `apGivers30` rescans the rest of the file per section | Low | NEEDS-VERIFICATION | gameplay.hpp:435-457 |
| GP-23 | `activityGraph30` avoidable rescans and string-built class compares | Low | SAFE-REFACTOR | gameplay.hpp:532-563, 626, 635, 671 |
| GP-24 | Cryptic/latent behaviour in the link re-encoder (must be preserved) | Low | DOC-ONLY | gameplay.hpp:575, 576, 599-600 |
| GP-25 | `surfaceTable30()` built for every `.oct` even when unused | Low | SAFE-REFACTOR | di1compat.cpp:718-732, 750; gameplay.hpp:81-84 |
| GP-26 | Lua-text helpers implemented three times | Low | NEEDS-VERIFICATION | gameplay.hpp:124-214; ui.hpp:98-127, 178-182, 235, 240; mission.hpp:210-234 |
| GP-27 | Activity mission tables converted twice | Low | NEEDS-VERIFICATION | gameplay.hpp:682-694; di1compat.cpp:865-875 |
| GP-28 | `luaTokens`/`luaGlobalTable` approximate Lua | Low | DOC-ONLY | gameplay.hpp:126-155, 189-197 |
| GP-29 | `ChallengeZone` inserted before the closing `}` | Low | DOC-ONLY | gameplay.hpp:671-672 |
| GP-30 | `luaTable` cryptic one-liner; key read can run past the line | Low | SAFE-REFACTOR | ui.hpp:104, 118 |
| GP-31 | `mergeTips` can return -1 after editing | Low | DOC-ONLY | ui.hpp:136-139, 157; di1compat.cpp:3539-3545 |
| GP-32 | Play Sets Lua as a C++ raw string; `reserve` underestimates | Low | DOC-ONLY | ui.hpp:187, 196-223 |
| GP-33 | `fmvClip` only partly case-insensitive | Low | DOC-ONLY | ui.hpp:59, 62 |
| GP-34 | Stale/incomplete header comments | Low | DOC-ONLY | ui.hpp:1-3; gprt.hpp:1; gameplay.hpp:1-3 |
| GP-35 | `hkZoneStr` returns a pointer into a static after its lock scope | Low | DOC-ONLY | di1compat.cpp:3451-3466 |
| GP-36 | `g_zips.size()` read without `g_zipLock` | Low | SAFE-REFACTOR | di1compat.cpp:3666 |
| GP-37 | Two inline near-copies of `readStream` | Low | SAFE-REFACTOR | di1compat.cpp:3559-3572, 3601-3608 vs 1126-1138 |
| GP-38 | Magic substring arithmetic | Low | SAFE-REFACTOR | di1compat.cpp:3725 |
| GP-39 | `buildOnLot` semantics and magic values | Low | DOC-ONLY | bldrt.hpp:62-71, 80 |
| GP-40 | Link_In node-name probe duplicated, same identifiers | Low | SAFE-REFACTOR | bldrt.hpp:46, 96-103; caprt.hpp:146-156 |
| GP-42 | `g_keep` O(n) prune under its lock, never reset | Low | NEEDS-VERIFICATION | caprt.hpp:75-93 |
| GP-43 | `hkReq` reads the transform even when `req` failed the check | Low | BEHAVIOUR-CHANGE | caprt.hpp:160-163 |
| GP-44 | Probe work before the budget check | Low | SAFE-REFACTOR | caprt.hpp:154, 161, 173-175 |
| GP-45 | `defText` assumes 96-byte buffers | Low | SAFE-REFACTOR | caprt.hpp:65-71 |
| GP-46 | Readability checks applied inconsistently | Low | DOC-ONLY | caprt.hpp:117-136 |
| GP-47 | Runtime headers depend on hidden context | Low | SAFE-REFACTOR | gprt.hpp, bldrt.hpp, caprt.hpp |
| GP-48 | Generic prologue signatures barely guard 9 hooks (AssetOpen weakest) | Low | NEEDS-VERIFICATION | gprt.hpp:463, 471, 472; bldrt.hpp:111; caprt.hpp:215-218; di1compat.cpp:4183 |
| GP-N1 | `hkOpen`'s exception guard covers only the 1.0 transcode | Low | BEHAVIOUR-CHANGE | di1compat.cpp:3558-3619, 3703-3720 |

### GP-01 `hkOpen` is a 208-line god function; its dispatch order is behaviour
- **Where:** `native/di1compat/di1compat.cpp:3549-3756` (installed at 4183, always)
- **Severity / category / class:** High · god function / maintainability · NEEDS-VERIFICATION
- **Evidence:** the exact dispatch order, verified. Before step 1, the engine's own `r = g_open(...)` runs for every path (3554) and `safeCopy(p, 300, path)` runs (3557).
  1. vtxf merge (3558-3573): consumes `r` through the inline `vt[11]` dtor and returns a MemFile or a fresh `g_open`.
  2. tips (3575-3576): `tips30` consumes `r` through `readStream`.
  3. Play Sets menu (3578-3594): consumes `r`, returns a MemFile or a fresh `g_open`.
  4. zone list / ChallengeData (3597-3619): consumes `r` through the dtor. Order inside is `zones10` → `zonelist30` / `beacons30` → `beaconMap30`.
  5. **Fall-through, no return:** gamedb `bitmasks.lua` stack log under `trace` (3620-3625).
  6. chd (3626): `chdForLua(p, r)` consumes `r` if non-null. No `serve10` gate.
  7. di1sys (3627): only when `!r`.
  8. memtest (3628-3633): `copyToMemFile` consumes `r` and **returns nullptr on a short read** (the open then fails).
  9. **Fall-through:** block10 scan sets `blocked` (3634-3644).
  10. serve, `!r` only (3645-3748):
      - session hit (3646-3662);
      - 1.0 read with the flash-image scope, ChallengeData redirect, `_pc_con`, `_holo` and lipsync fallbacks (3663-3697);
      - transcode under try/catch (3703-3720);
      - `g_movies10` / `g_screens10` registration (3722-3726);
      - profiling (3731-3742).
  11. Final log, `trace || !r` (3750-3755).
- **Hot path?:** code defaults: every asset open, plus one `logf` per open (`trace = 1`: `GetLocalTime` + `fflush` under `g_logLock`). The serve path is inert (`serve10 = 0`). Recommended config: every open. Misses go down the serve path. Only misses and served files are logged. In both, the fixed per-open cost is small: one `readable` syscall, about 10 `endsWith` calls, and `missWhy` (SSO, no heap).
- **Why it matters:** every feature edit touches the central per-open function. The chain is mostly first-match-wins, but two stages (5 and 9) only have side effects and fall through. Whether `r` is consumed differs by branch.
- **Fix (behaviour-preserving):**
  - Keep the prologue (`prof`, `g_open`, `safeCopy`) in `hkOpen`.
  - Extract each stage as `static int* tryX(OpenCtx&)` returning a "not mine" sentinel (nullptr is not usable as the sentinel, because memtest and chd legitimately return nullptr). Call the stages in exactly the order above.
  - Stages 5 and 9 become void helpers at the same positions.
  - `OpenCtx` holds `path`, `p`, `flags`, `a3`-`a5`, `r`, `prof`/`tIn`/`t30`, `missWhy`, `blocked`, and the caller's `_AddressOfReturnAddress()`.
  - Move log strings verbatim.
- **Refactor risk:**
  - Order is behaviour.
  - A branch that consumed `r` must never touch it again. The original callers expect memtest's nullptr-on-short-read and chd's partial MemFile.
  - **`_AddressOfReturnAddress()` (3623, 3740) must be taken in `hkOpen` and passed in.** In a helper, `stackOf` would start in the helper's frame and could pick up extra exe-range words, changing the "stack:" log text.
  - `missWhy` is reassigned at 3666 and 3671.
  - The `prof` timestamps must be taken at the same points.
  - `zl` / `cd` (3597) are used only by stage 4.
- **Verification:** CORRECTED:
  - The session-hit block starts at 3645, not 3646. The block-list stage starts at 3634.
  - The chain is not purely first-match-wins (two fall-through stages).
  - memtest/chd can return nullptr or a partial file after consuming `r`. The report's "must then return a MemFile or a fresh g_open" is not true for them.
  - The `_AddressOfReturnAddress` risk is added.
  - Class changed to NEEDS-VERIFICATION (log text depends on the stack walk).

### GP-02 gprt.hpp: 6 features, 2 probes, 12 hooks, not self-contained
- **Where:** `native/di1compat/gprt.hpp:1-491`
- **Severity / category / class:** Med · god namespace / coupling · SAFE-REFACTOR
- **Evidence:** the file holds selectMenu10 (19-138), actionMap10 + input10 probe (140-211), focus10 (213-253), keys10 + delivery probes (255-345), accept10 (347-385), vehicle10 probe (387-405), tools10 (407-458), and one `install()` (460-489). The cfg comments call actionMap10/focus10/keys10 "ui:" (di1compat.cpp:226-228); the file header says "gp track". It uses these di1compat statics: `logf, rt, hook, safeCopy, g_cfg, g_sessLock, g_screens10, g_movies10, g_zipLock, zipFor`, `mz_*`, `sys::readable`. It compiles only at di1compat.cpp:4093.
- **Why it matters:** unrelated features share one namespace and one install.
- **Fix (behaviour-preserving):** per-feature sub-namespaces or headers, each with its own `install()`. `gprt::install()` calls them in today's order (1; 2; 3; 4-7; 8; 9-10; 11-12).
- **Refactor risk:**
  - Hook install order and hook-name strings (`hook()` logs them, di1compat.cpp:121/126).
  - The include must stay after those statics.
  - **Added:** some state is shared by several features and must remain a single definition:
    - `g_inputLines`/`inputRoom` is one 600-line budget used by hkFlashCall, hkMapState, hkDeliver (×2) and hkVehicleCtl. Splitting it into per-feature counters changes the log.
    - `movie10` is used by actionmap10, focus10 and keys10.
    - `zonePlayset10` is used by selectmenu10, accept10, tools10 and vehicle10.
    - `script10` is used by keys10 and probes.
- **Verification:** CONFIRMED (shared-state risk added).

### GP-03 gameplay.hpp mixes six domains
- **Where:** `native/di1compat/gameplay.hpp:1-751`
- **Severity / category / class:** Med · cohesion · SAFE-REFACTOR
- **Evidence:**
  - OCT transforms: `worldDilation30`/`curves30`/`surfaces30` (26-100) and `activityGraph30` (461-643).
  - A Lua-text lexer and editors (124-214).
  - Challenge conversion (216-277, 419-459, 645-704).
  - Zone-list playset registration (307-417).
  - Currency (279-305).
  - ToolSet binary parsing (706-749).
  - The header (1-3) names none of zone registration or currency.
- **Why it matters:** low cohesion; octtest and the DLL both include all of it.
- **Fix (behaviour-preserving):** split into headers (lua text, challenge, zonelist/playsets, tools, OCT transforms). The functions are `inline`, so ODR-safe.
- **Refactor risk:** include order (`octx.hpp`, `luachunk.hpp`); octtest.cpp includes the same functions.
- **Verification:** CONFIRMED.

### GP-04 `activityGraph30` 140-line god function
- **Where:** `native/di1compat/gameplay.hpp:504-643`
- **Severity / category / class:** Med · god function · NEEDS-VERIFICATION
- **Evidence:** five phases, all sharing `nodes, pinIdx, dIn, dOut, nRe, nDrop, newIdx, smUuid`:
  1. collect + validate nodes (509-523)
  2. pin index (524-536)
  3. link buffers (537-585)
  4. node records + AvatarList (586-637)
  5. splice (638-642)
- **Why it matters:** hard to change one phase safely.
- **Fix (behaviour-preserving):** extract `collectNodes`, `pinIndex`, `rewriteLinkBuffer`, `emitAvatarList`, `rewriteNode` over one shared struct.
- **Refactor risk:** `f.addName()` appends **new** names in call order (oct.hpp:133-147). Any reordering of `addName` / `nameScalar` / `intScalar` changes output bytes. Keep the GP-24 quirks. Check with octtest `--activity` byte-compare.
- **Verification:** CONFIRMED.

### GP-05 `playsets10` 96-line god function
- **Where:** `native/di1compat/gameplay.hpp:322-417`
- **Severity / category / class:** Med · god function · NEEDS-VERIFICATION
- **Evidence:** stages:
  - 1.0 order (333-339)
  - scan of the 3.0 text (341-363)
  - `record` lambda (365-374)
  - plan inserts + ids (375-403)
  - insert points (406-413)
  - reverse apply (414-415)

  `ordered.find(luachunk::quote(name))` is computed at 399 and twice on line 409.
- **Why it matters:** one body mixes parsing, id allocation and text emission.
- **Fix (behaviour-preserving):** split into those stages; keep the edit-application order and the `std::map` key order of `record`.
- **Refactor risk:** `record` mirrors zonereg.py byte for byte. Golden outputs: octtest `--zones10` / `--menu` (octtest.cpp:3327-3357).
- **Verification:** CONFIRMED.

### GP-06 `hkToolGate` redoes the full 1.0 lookup on every call for a refused tool
- **Where:** `native/di1compat/gprt.hpp:441-458`; `native/di1compat/gameplay.hpp:741-749`
- **Severity / category / class:** Med · CPU · NEEDS-VERIFICATION
- **Evidence:**
  - Disassembly of the engine gate 0x0150F7D0 confirms the cache: `cmp [ebx+0x74],-1; jne 0x0150F883`, and 0x0150F883 is `mov eax,[ebx+0x74]; ret 4`.
  - On the uncached path the gate writes 0, then 1 on an empty list or a prefix match. Otherwise it stores the zone's `AllowAllToolsAndPacks` (string 0x01DBFD98, via 0x0139DE80; 0x0150F858-0x0150F87A).
  - The hook writes `+0x74 = 1` only on success (452). For a refused tool the original returns its cached 0 at once, and the hook redoes all of this on every call:
    - `zonePlayset10()` (an engine zone-key read; in **every** zone);
    - in 1.0 zones: the zone name (0x01391040), a walk of the `toolLevels10()` map (≤177 keys, 78 with a list per gameplay.hpp:709), and `levelAllows` per non-empty list (a copy and lower-case of the zone name each, 744-745);
    - `ToolSetFind` (0x015F6A60) for every list that allows the zone (450).
  - Callers:
    - the gate is called twice inside FUN_0151b960 (0x0151BA17, 0x0151BC20);
    - FUN_0151b960 is called by FUN_015207e0, a loop over a tool collection (`[edi+0x14]` entries), which has 4 callers;
    - by FUN_01520e00, which resets `+0x74 = -1` at 0x01520F9F right before;
    - and by FUN_01521f90.
  - So each refresh runs the gate once per tool.
- **Hot path?:**
  - Code defaults: installed (tools10 = 1). No 1.0 zone can exist (no install10/serve10), so the cost is one engine zone-key read per refused-tool gate call.
  - Recommended config: installed. In 1.0 zones the full scan runs per refused tool per refresh.
  - The refresh rate (per frame or per event) is **UNVERIFIED**.
- **Why it matters:** if FUN_015207e0 runs per frame or per UI refresh, this is the most expensive per-call path in the slice (number of refused tools × the scan).
- **Fix (behaviour-preserving):**
  - (a) SAFE: lower-case the zone once and pass it to a `levelAllows` variant that takes it pre-lowered.
  - (b) NEEDS-VERIFICATION: FUN_0151b960 looks up the record that `prefixes` points into by the tool's own name, `ToolSetFind(table, "Tools", [tool+0x48])` (0x0151B9C0-0x0151B9CF; `prefixes == rec + 4`). A direct `toolLevels10().find(name)` could therefore replace the scan. It is identical only if `ToolSetFind`'s comparison (case) and the first-in-map-order match agree, and the logged name would be the tool's spelling.
  - (c) a negative cache keyed by (zone-name copy, `*rt(0x0228AA3C)`, `prefixes`).
- **Refactor risk:**
  - (c) caches engine pointers. The table pointer and `prefixes` can be reused after a reload (ABA), so it needs proof that the table is not rebuilt in place.
  - Logs are unaffected by (a)-(c): the success line prints once per tool because success sets the engine cache.
  - The first `toolLevels10()` call extracts from startup5.zip on the game thread under `g_zipLock`, a one-time stall behind any loader-thread extraction. Keep that behaviour or move it.
  - To settle the frequency: put an Interlocked counter on hkToolGate or FUN_015207e0's callers.
- **Verification:** CORRECTED:
  - The engine cache and the caller structure were verified by disassembly.
  - The zonePlayset10 cost applies in 3.0 zones too.
  - Fix (b) was added.
  - Frequency is still UNVERIFIED.

### GP-07 `movie10`/`script10` on the per-frame delivery: syscall + heap + shared `g_sessLock`
- **Where:** `native/di1compat/gprt.hpp:161-170, 275-282` (callers 197, 309, 320, 323); `native/di1compat/di1compat.cpp:590-606`
- **Severity / category / class:** Med · CPU / lock contention · NEEDS-VERIFICATION
- **Evidence:**
  - `hkDeliver` detours 0x01565330.
  - Its only direct callers are two sites in FUN_01566f10 (0x01566FA4, 0x01566FCF). FUN_01566f10 runs only with a non-null input and a loaded screen (+0x6d), and for a slot < 0 it loops slots 0..10 calling the delivery per active slot (0x01566F90-0x01566FAD).
  - `movie10(screen)` runs on every delivery call (309), for 3.0 screens too:
    - `safeCopy` (1 `K32QueryWorkingSetEx`, ~0.4 µs, sys.hpp:177-178);
    - `std::string("flash/") + m`, which hits the heap for any real path > 15 chars;
    - lower-casing;
    - `g_sessLock`;
    - a `std::set<std::string>` lookup.
  - `script10` is added for 1.0-movie screens (323), and again at 320 when logging.
  - `g_sessLock` is also held by `indexZip10` (with `g_zipLock`) across a whole archive's member loop (595-605) on loader threads.
- **Hot path?:**
  - Code defaults: installed (lockTrace10 || keys10). The cost is paid per delivery, although `g_movies10` is always empty.
  - Recommended config: the same, plus `script10` on 1.0-movie screens.
- **Why it matters:** this is on the main thread per frame (per input screen and slot). The size of the stall behind `indexZip10` is **UNVERIFIED**: a member loop of a few thousand entries can block one frame.
- **Fix (behaviour-preserving):**
  - (a) Declare `g_movies10` / `g_screens10` with `std::less<>` and look up through a `std::string_view` over a stack buffer. Same ordering, no heap.
  - (b) Give the two sets their own leaf mutex at all 6 sites: di1compat.cpp:3670-3671, 3722, 3724-3725; gprt.hpp:122, 168-169, 280-281. Each of those lock scopes touches only the one set.
  - (c) A memo keyed by the screen pointer is **not** recommended: screen addresses are reused after a zone change.
- **Refactor risk:**
  - Keep the `sys::readable` check (CrabeLoader dumps on every handled AV).
  - (b) changes interleaving only.
  - Lock order today is `g_zipLock → g_sessLock` (indexZip10). The new mutex must stay a leaf.
- **Verification:** CORRECTED: severity set to Med (was "Low-Med"); the delivery call structure was verified by disassembly.

### GP-08 `hkFlashCall` looks up the screen for every ExternalInterface call
- **Where:** `native/di1compat/gprt.hpp:182-211`
- **Severity / category / class:** Low · CPU · NEEDS-VERIFICATION
- **Evidence:**
  - 0x01564790 is installed as every screen movie's ExternalInterface handler (`mov [eax+0x1c], 0x01564790` at 0x015670AF).
  - Per call, the hook runs:
    - `gfxString`: 1-2 `readable` calls;
    - `safeCopy`: 1 `readable`;
    - `ScreenByName` 0x01550050 and `ScreenByHandle` 0x0154FED0: each walks the 5 per-player screen lists at mgr+0x384;
    - `movie10`: GP-07's cost.
  - The result is needed only for `UsesActionMap` with actionMap10, or for the probe while the 600-line budget lasts.
- **Hot path?:** code defaults and recommended config: installed (actionMap10 || lockTrace10). Runs per EI call of any movie. The call rate is **UNVERIFIED** (genericlist calls CanChangeSelection every frame while a key is held, comment 203-204).
- **Why it matters:** wasted work and syscalls on a possibly per-frame path.
- **Fix (behaviour-preserving):**
  - Compute `wantMap = name && args && g_cfg.actionMap10 && !strcmp(name, "UsesActionMap")` and `wantProbe = name && args && g_cfg.lockTrace10 && g_inputLines < 600 && strcmp(name, "CanChangeSelection")` first. Run `screenOfCall`/`movie10` only if either is true, then the original body unchanged.
  - The logs stay identical:
    - the rename to `"AppendActionMap"` still happens before the probe test, and neither name equals CanChangeSelection;
    - past 600, `inputRoom()` returns false, and the counter only grows.
- **Refactor risk:** the dropped work must be side-effect-free. That is now **verified**: the disassembly of 0x01550050 and 0x0154FED0 shows read-only list walks (compare `+0x10` name / `+0x54` handle, return `+0x54` / screen / -1 / 0; no stores).
- **Verification:** CORRECTED: the getters' purity (UNVERIFIED in the report) is settled; severity set to Low (was "Low-Med").

### GP-09 `hkDeliver` probe work every frame; raw `char*` to `%s`
- **Where:** `native/di1compat/gprt.hpp:307-345`
- **Severity / category / class:** Low · CPU / robustness · NEEDS-VERIFICATION
- **Evidence:**
  - With lockTrace10 on, the `safeCopy` at 316-317 runs every call before the change test at 318.
  - The mapper loop (326-341) is not gated by lockTrace10 (nor by keys10). It runs up to 256 raw reads of `es[i][0x10]` each frame.
  - Line 338 passes `*(const char**)(screen + 0x10)` straight to `logf`'s `%s`.
  - `wasDown[256]` (334) is shared across screens and slots, which affects only the log.
- **Hot path?:**
  - Code defaults: never (`ten` is always false; GP-07).
  - Recommended config: per delivery of a screen showing a 1.0 movie (Catalog, SelectMenu) while it takes input.
- **Why it matters:** small per-frame waste. The crash risk at 338 is low in practice: the engine itself `strcmp`s every live screen's `+0x10` in ScreenByName (0x0155007B), so the pointer is valid while the screen is alive.
- **Fix (behaviour-preserving):**
  - Move the `safeCopy` inside the `if` after `inputRoom()`.
  - Wrap the loop in `if (g_cfg.lockTrace10)` (constant; with it off `inputRoom()` never logs, so the `wasDown` state is unobservable).
  - Optionally skip the loop when `g_inputLines >= 600`.
  - Logs stay identical.
- **Refactor risk:** using a safeCopied name at 338 changes the text for names longer than the buffer (63 chars today) or unreadable pointers. Use a buffer ≥ 256 B or leave 338 as is.
- **Verification:** CORRECTED: severity set to Low (was "Low-Med"); the loop is also ungated by keys10; the 338 crash risk was re-assessed against the engine's own use.

### GP-10 The locktrace10 probes are on by default (both configs)
- **Where:** `native/di1compat/di1compat.cpp:157`; `native/di1compat/gprt.hpp:190-191, 310-321, 462-464`
- **Severity / category / class:** Low · config / logging overhead · DOC-ONLY
- **Evidence:**
  - `lockTrace10 = true` in the code defaults, and `di1compat.cfg.example` does not override it.
  - It installs the vehicle10 probe (462), and keeps hkFlashCall and hkDeliver/hkMapState installed (476, 484) even if their features are off.
  - All input10 and vehicle10 lines share one never-reset 600-line budget.
  - hkDeliver keeps a single `last`/`lastGates` pair (311-312), so two alternating 1.0 screens log every frame and starve the other probes.
  - Each `logf` does `GetLocalTime` + `fflush` under `g_logLock` (di1compat.cpp:70-84) on the calling thread.
- **Hot path?:** code defaults and recommended config: on. The log cost is bounded by the 600-line budget. The unbounded part is GP-09's pre-work.
- **Why it matters:** a "probe" flag that is effectively product behaviour (which hooks exist); diagnostics can be starved.
- **Fix (behaviour-preserving):** none. Document. Later options (behaviour changes): per-screen change tracking, per-probe budgets, default off.
- **Refactor risk:** log consumers and the user's debugging workflow.
- **Verification:** CORRECTED: severity Med → Low (bounded logging; the per-frame part is GP-09); the 17 out-of-slice locktrace hooks are noted.

### GP-11 Engine-access code duplicated across gprt, bldrt, di1compat, npcrt
- **Where:** `native/di1compat/gprt.hpp:20, 24, 34-35, 64-69, 118-122, 126-131, 277-281, 401, 445-446`; `native/di1compat/bldrt.hpp:52`; `native/di1compat/di1compat.cpp:2561-2562, 2573, 3501, 3521-3522, 3538`; `native/di1compat/npcrt.hpp:106`
- **Severity / category / class:** Med · duplication · SAFE-REFACTOR
- **Evidence:**
  - "Add screen by name" (ctor 0x0155A630, assign 0x0041A250 at +0x40, player +0x6C, AddScreen 0x015699B0 on `*0x02289E88`, dtor 0x0155C270) appears verbatim at 64-69 and 126-131.
  - "Lower-case the Lua script name and look it up in `g_screens10`" appears at 118-122 and 277-281.
  - Zone manager `*rt(0x0225F548)` (+`0x19C`) appears at 7 sites.
  - CString assign 0x0041A250: two typedefs (gprt.hpp:24, di1compat.cpp:3521) and one inline cast (di1compat.cpp:2573), used at 4 call sites.
  - The ribbon-name typedef appears twice (gprt.hpp:20; di1compat.cpp:3501).
- **Why it matters:** offsets duplicated across files can drift apart.
- **Fix (behaviour-preserving):** one engine helper section placed before di1compat.cpp:2559 (the first user): `zoneManager()`, `currentZone()`, `screenManager()`, `addScreenByName(name, player)`, `cstrAssign(dst, s)`, `screenScript10(screen)`.
- **Refactor risk:**
  - Keep the `__fastcall (self, edx, ...)` emulation, the zero-filled `alignas(8) uint8_t req[0x100]`, and the ctor/dtor pairing.
  - Keep each site's own null checks and arguments. For example, `hkShowRibbon` passes `*rt(0x0221CEA0)` and a2 = 1 to the ribbon getter, while `hkFindScreen` passes nullptr and 0.
- **Verification:** CORRECTED: the CString-assign "typedef declared 4 times" is really 2 typedefs + 1 inline cast (4 call sites).

### GP-41 `keep()` calls `g_renderShut` with no null check
- **Where:** `native/di1compat/caprt.hpp:97-98, 206-213`
- **Severity / category / class:** Med · robustness (latent crash) · BEHAVIOUR-CHANGE
- **Evidence:**
  - `if (comp) g_renderShut(comp, nullptr, 0);`
  - `g_renderShut` is set only if hook 0x01860B40 is accepted (208). `hook()` refuses on a prologue mismatch and leaves `original` null (di1compat.cpp:120-122).
  - `hkBase`/`hkDone` (211-212) are installed independently, so a refused render-shut hook with accepted base/done hooks makes the first already-shut-down 1.0 actor call a null pointer.
  - Today's install order (207-208 before 211-212) means `g_renderShut` is set before any `keep()` can run.
- **Why it matters:** a null call is a crash. It is reachable only if another patch changed 0x01860B40's prologue, a condition the code explicitly anticipates ("our exe carries other patches", di1compat.cpp:108).
- **Fix (behaviour-preserving):** the success path is unchanged by any of these:
  - `if (comp && g_renderShut)`. The refused case becomes a no-op: the actor stays hidden.
  - Or call `(g_renderShut ? g_renderShut : (RenderShutFn)rt(0x01860B40))`, which shows it. Pick one; either is a failure-path behaviour change for the user to decide.
  - Keep the install order.
- **Refactor risk:** none on the success path.
- **Verification:** CORRECTED: severity set to Med (was "Low-Med"; rubric: latent crash in a rare environment); a fix alternative was added.

### GP-12 Reverse-engineered offsets are unnamed magic numbers
- **Where:** `native/di1compat/gprt.hpp` (screen +0x0/+0xc/+0x10/+0x14/+0x50/+0x60/+0x6b/+0x6c/+0x6d; map +0x8/+0x20; request +0x40/+0x6c; UI manager +0x28C+p*0x20, +0xD54+slot*0x1BC4; movie vt +0x8C; input +0x3C; player manager +0x90/+0x108; mission +0x3DB/+0x140; vehicle ctl +0xF0; tool +0x74; GFx 0x8f/0x40; LUA_TSTRING 4), `bldrt.hpp:56, 62, 68, 69, 71`, `caprt.hpp:63, 68-72, 96-97, 119-125, 152-153, 162`
- **Severity / category / class:** Low · magic numbers · SAFE-REFACTOR
- **Evidence:** as listed. All offsets were checked against the cited lines.
- **Why it matters:** readability. The cross-file duplication part is GP-11.
- **Fix (behaviour-preserving):** `constexpr` offsets plus typed accessors in one engine-layout section, each with its RE source comment.
- **Refactor risk:** read widths must stay exact: byte reads at gprt.hpp:294 and 330, the u16 at 374, the byte at caprt.hpp:121.
- **Verification:** CORRECTED: severity Med → Low (rubric: local readability; the drift risk is counted in GP-11).

### GP-14 `hkToolGate` dereferences the zone manager without its own null check
- **Where:** `native/di1compat/gprt.hpp:445-446`
- **Severity / category / class:** Low · hidden coupling · SAFE-REFACTOR
- **Evidence:** `mgr` is re-read and dereferenced at 446. It is safe only because `zonePlayset10()` (444) returned true. The engine gate itself reads `[0x0225F548]+0x19C` unchecked (0x0150F7F6, 0x0150F858).
- **Why it matters:** an invisible precondition.
- **Fix (behaviour-preserving):** `if (!mgr) return r;`. Identical results.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-15 `g_keysScreen`/`g_keysMovie` plain globals, no re-entry handling
- **Where:** `native/di1compat/gprt.hpp:272-273, 323-325, 344`
- **Severity / category / class:** Low · threads / re-entrancy · BEHAVIOUR-CHANGE
- **Evidence:**
  - Set before `g_deliver` and cleared after it.
  - The movie's key handler runs synchronously inside the delivery (hkMapState → HandleEvent, 296), so a nested delivery (**UNVERIFIED**) would misroute key events or clear the outer values.
  - Main-thread-only is a comment (272), not a check.
- **Why it matters:** re-entrancy or a cross-thread caller would send key events to the wrong movie.
- **Fix (behaviour-preserving):** none. Saving and restoring the previous values differs only in the re-entrant case, so it is a behaviour change.
- **Refactor risk:** `thread_local` would also change behaviour for any cross-thread caller.
- **Verification:** CONFIRMED.

### GP-16 Dead typedef `ActionStateFn`
- **Where:** `native/di1compat/gprt.hpp:227`
- **Severity / category / class:** Low · dead code · SAFE-REFACTOR
- **Evidence:** no use (grep). `MapStateFn` (269) has the same signature.
- **Why it matters:** noise.
- **Fix (behaviour-preserving):** delete it.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-17 `hkShowRibbon` returns -1 on success; one engine caller uses the value
- **Where:** `native/di1compat/gprt.hpp:62, 71`
- **Severity / category / class:** Low · semantics · DOC-ONLY
- **Evidence:** there are 8 direct callers of 0x00B68F60.
  - FUN_00df6230 (0x00DF6417) does `cmp eax,-1; je`. Otherwise it posts `"OverrideModeToRibbon"` (0x01D19C90) to the returned screen handle.
  - 0x00C966C7 and 0x00FD97AB pass eax back as their own return value.
  - 0x00B7541F and 0x011E8466 ignore it.
  - So on the SelectMenu path the hook's -1 makes FUN_00df6230 skip `OverrideModeToRibbon` for the opened screen.
- **Why it matters:** returning the real handle `h` would change engine behaviour (an extra event to 1.0's SelectMenu).
- **Fix (behaviour-preserving):** none; document the -1 and its effect.
- **Refactor risk:** do not "fix" the return value in a refactor.
- **Verification:** CORRECTED: the report's UNVERIFIED question is settled by disassembly (the value is used).

### GP-18 Gating and hook naming inconsistent across runtime headers
- **Where:** `native/di1compat/gprt.hpp:460-489`; `native/di1compat/bldrt.hpp:107-108`; `native/di1compat/caprt.hpp:210-212`; `native/di1compat/di1compat.cpp:4319-4331`
- **Severity / category / class:** Low · config · DOC-ONLY
- **Evidence:**
  - The ui hooks in di1compat need `serve10`; no gprt, bldrt or caprt hook does.
  - Under the code defaults the gprt UI hooks are therefore installed but can never act (empty sets). They still pay their per-call lookups (GP-07, GP-08).
  - Hook names cite the feature even when the hook is installed only for a probe: "building lot spawn (building10)" under spawntrace10; "(shutdown10)" for base/done under capsuletrace10.
- **Why it matters:** surprising config semantics and misleading log names.
- **Fix (behaviour-preserving):** document only. Changing either changes which hooks exist or the log text.
- **Refactor risk:** hook-name strings are logged.
- **Verification:** CONFIRMED.

### GP-19 `tool+0x74 = 1` lifetime follows the engine's own cache
- **Where:** `native/di1compat/gprt.hpp:452`
- **Severity / category / class:** Low · correctness note · DOC-ONLY
- **Evidence:**
  - The engine caches its own zone-dependent verdict in `+0x74` the same way (the uncached path stores 1 for a prefix match or `AllowAllToolsAndPacks`, 0x0150F7ED-0x0150F87A).
  - Its only `mov [reg+0x74], -1` in the tool code range is in FUN_01520e00 (0x01520F9F), immediately followed by FUN_0151b960.
  - So a stale `1` after a zone change would equally affect 3.0's own allowed tools. The hook adds no new staleness.
  - Whether a zone change re-runs FUN_01520e00 is still **UNVERIFIED**.
- **Why it matters:** clarifies that no fix is needed.
- **Fix (behaviour-preserving):** none.
- **Refactor risk:** none.
- **Verification:** CORRECTED: class changed to DOC-ONLY based on the disassembly; a residual UNVERIFIED part is noted.

### GP-20 `[0]` on vectors that may be empty
- **Where:** `native/di1compat/gameplay.hpp:96, 515, 546, 622, 623`
- **Severity / category / class:** Low · OOB / UB · BEHAVIOUR-CHANGE
- **Evidence:** `f.ints(r)[0]` and `f.nameValues(...)[0]`. `ints` returns empty for a counted record with count 0. `nameValues` returns empty for a non-NAME, non-keyed record (oct.hpp:155-177). Line 531 is safe (`isInt` = SCALAR, so n = 1).
- **Why it matters:** malformed input → null deref → crash. `catch (...)` with /EHsc does not catch the AV.
- **Fix (behaviour-preserving):** a `first(v, def)` helper. This changes only the crash path; octtest golden runs show no diff on the fixed corpus.
- **Refactor risk:** none for valid data.
- **Verification:** CONFIRMED (class set to BEHAVIOUR-CHANGE: failure path only).

### GP-21 `toolLevels10` section walk can wrap 32-bit `size_t`
- **Where:** `native/di1compat/gameplay.hpp:716-719` (also 730)
- **Severity / category / class:** Low · integer overflow · BEHAVIOUR-CHANGE
- **Evidence:**
  - `if (sz < 4 || o + sz > n) return out; ... o += sz;`. A corrupt `sz` near 2^32 wraps on the 32-bit DLL.
  - Also at 730: `4 * (size_t)i` wraps for a huge list index. Reads stay inside the buffer mod 2^32, but a corrupt index could loop for ~2^31 iterations.
- **Why it matters:** a hang or garbage on a corrupt ToolSet.bin. The shipped file is fixed.
- **Fix (behaviour-preserving):** `if (sz < 4 || sz > n - o)` (`o + 8 <= n` holds). Identical for every valid file; changes only a corrupt file's outcome.
- **Refactor risk:** none.
- **Verification:** CONFIRMED (the line-730 note was added).

### GP-22 `apGivers30` rescans the rest of the file per section
- **Where:** `native/di1compat/gameplay.hpp:435-457`
- **Severity / category / class:** Low · CPU (load time) · NEEDS-VERIFICATION
- **Evidence:**
  - `ap.find("\nActivityIcon=", sec)`, `ap.find("\nActorTagName=", sec)` (438) and `ap.find("\nChallengeType=", sec)` (444) scan from each section to the next match. That is O(sections × file) when most sections lack the keys.
  - Magic `14` is the length of both literals.
  - Runs only for served `.ap` files containing ActivityIcon (di1compat.cpp:866).
- **Hot path?:**
  - Code defaults: never (the serve path is off).
  - Recommended config: once per matching served `.ap` (load time).
- **Why it matters:** load-time waste on large `.ap` files (sizes UNVERIFIED).
- **Fix (behaviour-preserving):** cache each next-match position and re-find only once `sec` passes it; named `constexpr` lengths.
- **Refactor risk:** **added.** `ap.insert(ins, add)` (452) shifts every later offset by `add.size()`, so cached positions beyond `ins` must be shifted or re-found. The report's "Risk: none" is wrong. Check with octtest `--challenge` byte-compare.
- **Verification:** CORRECTED: missing risk added; class changed to NEEDS-VERIFICATION.

### GP-23 `activityGraph30` avoidable rescans and string-built class compares
- **Where:** `native/di1compat/gameplay.hpp:532-535, 541, 562-563, 626, 635, 671`; `ActClass` 478-482
- **Severity / category / class:** Low · CPU (load time) · SAFE-REFACTOR
- **Evidence:**
  - `i < f.blockEnd(lpool)` recomputes a linear scan per iteration (oct.hpp:109-114); the loop never changes `recs.size()` or levels.
  - `actClass()` runs 2-4 times per link.
  - `std::string(c->cls10) == "..."` builds heap strings.
- **Hot path?:**
  - Code defaults: never.
  - Recommended config: per served activity `.mer` (load time).
- **Why it matters:** minor load-time waste.
- **Fix (behaviour-preserving):** hoist `blockEnd`; store `const ActClass*` in `N`; add `bool stateMachine`.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-24 Cryptic/latent behaviour in the link re-encoder (must be preserved)
- **Where:** `native/di1compat/gameplay.hpp:575, 576, 599-600`
- **Severity / category / class:** Low · hard to understand · DOC-ONLY
- **Evidence:**
  - 576 reuses the per-iteration `n` inside an expression.
  - 575 tests the **cumulative** `nRe`, so after the first rerouted buffer every later LinksBuffer is re-encoded with a rebuilt `r.type`.
  - 599-600 hard-code the type and copy `smUuid` with the last byte XOR `0x5a`.
- **Why it matters:** looks like a bug but defines today's output bytes. Byte identity with the original type is **UNVERIFIED** (octtest `--activity`, octtest.cpp:2999).
- **Fix (behaviour-preserving):** name `count`; comment; keep the semantics.
- **Refactor risk:** "fixing" 575 changes bytes.
- **Verification:** CONFIRMED.

### GP-25 `surfaceTable30()` built for every `.oct` even when unused
- **Where:** `native/di1compat/di1compat.cpp:718-732, 750`; `native/di1compat/gameplay.hpp:81-84`
- **Severity / category / class:** Low · CPU (load time) · SAFE-REFACTOR
- **Evidence:** the argument is evaluated eagerly: 2 `readable` calls + up to 109 `safeCopy`s (each with one `readable`), and a vector of up to 109 strings (heap only for names > 15 chars, plus vector growth). `surfaces30` returns 0 without SurfaceTypesPool or CollisionShapePool.
- **Hot path?:**
  - Code defaults: never.
  - Recommended config: per served `.oct` (load time; ~0.05 ms of syscalls each).
- **Why it matters:** small waste per served `.oct`.
- **Fix (behaviour-preserving):** test the two pools first (or pass a lazy provider). `surfaceTable30` has no side effects.
- **Refactor risk:** none.
- **Verification:** CORRECTED: "109 allocations" overstated (MSVC SSO); the cost is mainly ~111 `readable` syscalls.

### GP-26 Lua-text helpers implemented three times
- **Where:** `native/di1compat/gameplay.hpp:124-214, 410-415, 651, 675-676`; `native/di1compat/ui.hpp:98-127, 178-182, 235, 240`; `native/di1compat/mission.hpp:210-234`
- **Severity / category / class:** Low · duplication · NEEDS-VERIFICATION
- **Evidence:**
  - Three scanners: `luaTokens`/`assignedAt`, `luaTable` (line layout), and `skipQuoted`/`closeOf`.
  - Three "collect edits, sort desc, apply" copies.
  - Two quoters: ui `q` does not escape `\n`; `luachunk::quote` does (luachunk.hpp:170-175).
  - Two LE u32 helpers.
- **Why it matters:** drift between near-identical helpers.
- **Fix (behaviour-preserving):** one `luatext.hpp`, unifying only where octtest output provably matches.
- **Refactor risk:** `q` → `luachunk::quote` changes output for names containing `\n`. The lexers are not interchangeable.
- **Verification:** CONFIRMED.

### GP-27 Activity mission tables converted twice
- **Where:** `native/di1compat/gameplay.hpp:682-694`; `native/di1compat/di1compat.cpp:865-875`
- **Severity / category / class:** Low · wasted work (load time) · NEEDS-VERIFICATION
- **Evidence:** `activityGivers` reads and runs `activityTasks30` on each giver's mission only to test convertibility. The engine's later request transcodes the same file again.
- **Hot path?:**
  - Code defaults: never.
  - Recommended config: per served activity `.ap` (load time).
- **Why it matters:** about 19 extra extractions + conversions per Incredibles `.ap`.
- **Fix (behaviour-preserving):** a memo (mission path → converts?).
- **Refactor risk:** `transcode` runs on several loader threads, so the memo needs a lock. The 1.0 data never changes in a session.
- **Verification:** CONFIRMED (locking risk added).

### GP-28 `luaTokens`/`luaGlobalTable` approximate Lua
- **Where:** `native/di1compat/gameplay.hpp:126-155, 189-197`
- **Severity / category / class:** Low · robustness · DOC-ONLY
- **Evidence:** leveled long brackets (`[==[`, `--[==[`) are not handled. `luaGlobalTable` matches any depth-0 identifier followed by `{` (e.g. `f(name, {` or `name{`). Whether the corpus hits these is **UNVERIFIED**.
- **Why it matters:** a silent mis-edit on unusual input.
- **Fix (behaviour-preserving):** document. Adding `assignedAt` only after checking the corpus.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-29 `ChallengeZone` inserted before the closing `}`
- **Where:** `native/di1compat/gameplay.hpp:671-672`
- **Severity / category / class:** Low · latent output bug · DOC-ONLY
- **Evidence:** `"\tChallengeZone = \"NONE\",\n"` is inserted at the `}`. The result is invalid Lua if the previous field lacks a trailing comma. The "61/61" comment describes 3.0 files, not the 1.0 input.
- **Why it matters:** a mission table could fail to load.
- **Fix (behaviour-preserving):** none until verified.
- **Refactor risk:** none.
- **Verification:** UNVERIFIED: settle with octtest `--mischain` / `--activity` over the 35 graphs plus `loadstring` of each output.

### GP-30 `luaTable` cryptic one-liner; key read can run past the line
- **Where:** `native/di1compat/ui.hpp:104, 118`
- **Severity / category / class:** Low · clarity · SAFE-REFACTOR
- **Evidence:** 104 takes the table at offset 0 if present, else the **last** match. 118 `t.find('=', p) - p - 1` runs onto later lines when an entry line has no `=`.
- **Why it matters:** an undocumented rule.
- **Fix (behaviour-preserving):** split 104 into named steps (SAFE). Limiting the key to the line is a **behaviour change** for malformed layouts (different keys → different merge); keep it out of the refactor.
- **Refactor risk:** keep the first-at-0 / else-last rule.
- **Verification:** CORRECTED: the line-limit part reclassified as a behaviour change.

### GP-31 `mergeTips` can return -1 after editing
- **Where:** `native/di1compat/ui.hpp:136-139, 157`; `native/di1compat/di1compat.cpp:3539-3545`
- **Severity / category / class:** Low · semantics · DOC-ONLY
- **Evidence:** `TipCounts` is merged into `t30` and then `ZoneTipCategories` fails → -1. `tips30` serves the edited `t30` and logs `-1`.
- **Why it matters:** surprising contract.
- **Fix (behaviour-preserving):** document; preserve.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-32 Play Sets Lua as a C++ raw string; `reserve` underestimates
- **Where:** `native/di1compat/ui.hpp:187, 196-223`
- **Severity / category / class:** Low · maintainability · DOC-ONLY
- **Evidence:** about 26 lines of Lua with no syntax check (octtest `--menu` writes but does not parse). `reserve(chunk.size()*3+1500)` underestimates because a byte can become 4 characters (`\ddd`); this affects speed only.
- **Why it matters:** a Lua typo is found only in-game.
- **Fix (behaviour-preserving):** optionally have octtest `loadstring` the output.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-33 `fmvClip` only partly case-insensitive
- **Where:** `native/di1compat/ui.hpp:59, 62`
- **Severity / category / class:** Low · semantics · DOC-ONLY
- **Evidence:** only `http`/`HTTP` and `.bik`/`.BIK` are recognised.
- **Why it matters:** mixed case is treated as a clip name (whether the engine ever passes it is UNVERIFIED).
- **Fix (behaviour-preserving):** document. `_strnicmp` would change behaviour.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-34 Stale/incomplete header comments
- **Where:** `native/di1compat/ui.hpp:1-3`; `native/di1compat/gprt.hpp:1`; `native/di1compat/gameplay.hpp:1-3`
- **Severity / category / class:** Low · docs · DOC-ONLY
- **Evidence:** ui.hpp lists 5 cfgs. tutorial10, context10, playsetmenu10 and the holo `texBundleAs` (di1compat.cpp:1019) also use it. The runtime ui features live in gprt.hpp ("gp track").
- **Why it matters:** misleading navigation.
- **Fix (behaviour-preserving):** update the comments.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-35 `hkZoneStr` returns a pointer into a static after its lock scope
- **Where:** `native/di1compat/di1compat.cpp:3451-3466`
- **Severity / category / class:** Low · threads · DOC-ONLY
- **Evidence:** `return pick.c_str();` inside the `lock_guard` scope. The pointer outlives the lock, and a concurrent "LoadingScreens" query reassigns `pick`. The hook is on the zone string getter 0x0139E0B0, which has **266 direct callers**, including `zoneCurrency10` (2565). The fast path `key[0]=='L'` is cheap.
- **Why it matters:** safe only under the single-reader assumption (FUN_00b78c70, comment 3446).
- **Fix (behaviour-preserving):** document the assumption.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-36 `g_zips.size()` read without `g_zipLock`
- **Where:** `native/di1compat/di1compat.cpp:3666`
- **Severity / category / class:** Low · data race (benign) · SAFE-REFACTOR
- **Evidence:** `std::to_string(g_zips.size())` while loader threads insert under `g_zipLock` (di1compat.cpp:376-389).
- **Why it matters:** formally UB.
- **Fix (behaviour-preserving):** read the size under `g_zipLock` (no other lock is held at 3666), or keep an atomic counter. Same text.
- **Refactor risk:** do not nest it with the later `read10z` lock.
- **Verification:** CONFIRMED.

### GP-37 Two inline near-copies of `readStream`
- **Where:** `native/di1compat/di1compat.cpp:3559-3572, 3601-3608` vs `1126-1138`
- **Severity / category / class:** Low · duplication · SAFE-REFACTOR
- **Evidence:** the inline copies skip `vt[5]` Read when the size is 0 and do not `resize(got)`. `readStream` always calls Read and resizes.
- **Why it matters:** the copies differ subtly.
- **Fix (behaviour-preserving):** a variant helper that mirrors the inline semantics exactly.
- **Refactor risk:** changing `readStream` itself alters tips30, the playset menu and chd. Read(0) behaviour is UNVERIFIED.
- **Verification:** CONFIRMED.

### GP-38 Magic substring arithmetic
- **Where:** `native/di1compat/di1compat.cpp:3725`
- **Severity / category / class:** Low · clarity · SAFE-REFACTOR
- **Evidence:** `np.substr(13, np.size() - 17)`, where 13 = `strlen("presentation/")` and 17 = 13 + `strlen(".lua")`.
- **Why it matters:** readability.
- **Fix (behaviour-preserving):** named constants.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-39 `buildOnLot` semantics and magic values
- **Where:** `native/di1compat/bldrt.hpp:62-71, 64-66, 80, 86`
- **Severity / category / class:** Low · clarity · DOC-ONLY
- **Evidence:** the "not built" path still `return h;` (a non-zero handle with no object goes to `*out`, 86). The `CreateFn` arguments `0, zone, 0, 1, 0`, the flag `0xc` and vtable `0x1c` are unnamed. `_ReturnAddress()` (80) works only in the detour itself.
- **Why it matters:** surprising contract; a fragile caller test.
- **Fix (behaviour-preserving):** name the constants; document and preserve `return h`.
- **Refactor risk:** moving `_ReturnAddress()` into a helper silently disables building10.
- **Verification:** CONFIRMED.

### GP-40 Link_In node-name probe duplicated, same identifiers
- **Where:** `native/di1compat/bldrt.hpp:46, 96-103`; `native/di1compat/caprt.hpp:146-156`
- **Severity / category / class:** Low · duplication / naming · SAFE-REFACTOR
- **Evidence:** both read `call[1]` with `readable` and then `safeCopy` a name: bldrt at `+0x1c`, caprt at `+0x88`. Both namespaces declare `g_node` / `hkNode`.
- **Why it matters:** confusing in logs and stack traces.
- **Fix (behaviour-preserving):** a shared helper; distinct function names (the hook-name strings stay unchanged).
- **Refactor risk:** the signatures differ (`(void*)` vs `(void**, uint8_t*)`, both cdecl).
- **Verification:** CONFIRMED.

### GP-42 `g_keep` O(n) prune under its lock, never reset
- **Where:** `native/di1compat/caprt.hpp:75-93`
- **Severity / category / class:** Low · CPU / locking · NEEDS-VERIFICATION
- **Evidence:** past 8192 entries every insert walks the set calling `actorOf` (0x0074B200; hooked by missiontrace's `hkActorOf`, di1compat.cpp:3086/4243) under `g_keepLock`. It is quadratic if few handles are dead. The set is never cleared.
- **Hot path?:**
  - Code defaults: installed (shutdown10 && popin10), but no 1.0 expendables exist.
  - Recommended config: only past 8192 kept 1.0 expendables.
- **Why it matters:** a latent stall in a long session.
- **Fix (behaviour-preserving):** threshold-and-halve pruning or pruning on world exit, without calling engine code under the lock.
- **Refactor risk:** `hkActorOf` only counts calls inside missiontrace's per-thread `FpScope`, so fewer `actorOf` calls do not change logs.
- **Verification:** CONFIRMED.

### GP-43 `hkReq` reads the transform even when `req` failed the check
- **Where:** `native/di1compat/caprt.hpp:160-163`
- **Severity / category / class:** Low · robustness (probe) · BEHAVIOUR-CHANGE
- **Evidence:** `t[4..6]` (`req+0x14..0x20`) is read for the first 8 of each name even if `readable(req, 0x40)` failed.
- **Why it matters:** an AV in a default-off probe.
- **Fix (behaviour-preserving):** move the reads under the check. This changes only the fault path (no crash, different or absent line).
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-44 Probe work before the budget check
- **Where:** `native/di1compat/caprt.hpp:154, 161, 173-175`
- **Severity / category / class:** Low · CPU (probe) · SAFE-REFACTOR
- **Evidence:** `hkCust` runs `defText` (2 `readable` + 2 `safeCopy`) before the 300 budget. `firstFew(std::string(...))` allocates twice. All of this is capsuletrace10.
- **Hot path?:** code defaults and recommended config: off (capsuletrace10 = 0).
- **Why it matters:** waste only when probing.
- **Fix (behaviour-preserving):** move `defText` inside the `if`. Same log.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-45 `defText` assumes 96-byte buffers
- **Where:** `native/di1compat/caprt.hpp:65-71`
- **Severity / category / class:** Low · robustness · SAFE-REFACTOR
- **Evidence:** `safeCopy(data, 96, ...)`. The callers' buffers are 96 B (101, 130, 173, 188).
- **Why it matters:** a future smaller buffer would overflow.
- **Fix (behaviour-preserving):** take `char (&)[96]` or pass the sizes.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-46 Readability checks applied inconsistently
- **Where:** `native/di1compat/caprt.hpp:117-136`
- **Severity / category / class:** Low · policy · DOC-ONLY
- **Evidence:** `hkDone` reads `record[0x80]` and `*(uint32_t*)record` raw, then makes 3 `readable` calls per async completion of **every** expendable.
- **Hot path?:** code defaults and recommended config: installed (shutdown10 && popin10). Runs per async spawn completion (not per frame).
- **Why it matters:** no stated rule.
- **Fix (behaviour-preserving):** write the rule down: values the engine just passed or itself dereferences are trusted (see GP-13's rejection); values we index with are checked. Do not add checks on per-frame paths without measuring (sys.hpp:178-179: 85% of the main thread).
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-47 Runtime headers depend on hidden context
- **Where:** `native/di1compat/gprt.hpp`, `native/di1compat/bldrt.hpp`, `native/di1compat/caprt.hpp`
- **Severity / category / class:** Low · header hygiene · SAFE-REFACTOR
- **Evidence:** they rely on statics above di1compat.cpp:4093-4095 and on transitive includes (`<unordered_map>`, `<iterator>`, `<set>`, `<intrin.h>`) despite `#pragma once`.
- **Why it matters:** misleading as headers.
- **Fix (behaviour-preserving):** a required-symbols comment plus the standard includes.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### GP-48 Generic prologue signatures barely guard 9 hooks (AssetOpen weakest)
- **Where:** `native/di1compat/gprt.hpp:463, 471, 472`; `native/di1compat/bldrt.hpp:111`; `native/di1compat/caprt.hpp:215-218`; `native/di1compat/di1compat.cpp:4183`
- **Severity / category / class:** Low · robustness · NEEDS-VERIFICATION
- **Evidence:** 7 hooks use the MSVC EH prologue `6AFF68????????64A100000000`. The screen request (472) checks only `64A1000000006AFF68`. **AssetOpen**, the detour of `hkOpen`, checks only `6AFF68????????` (3 fixed bytes).
- **Why it matters:** `hook()`'s check (di1compat.cpp:110-123) would not notice a patched body behind a generic prologue.
- **Fix (behaviour-preserving):** append a few body bytes taken from the shipped exe.
- **Refactor risk:** a wrong longer signature refuses the hook, which is a behaviour change. Verify against `DisneyInfinity3.exe`.
- **Verification:** CORRECTED: AssetOpen and the screen-request hook added (7 → 9).

### GP-N1 `hkOpen`'s exception guard covers only the 1.0 transcode
- **Where:** `native/di1compat/di1compat.cpp:3558-3619` (unguarded), `3703-3720` (guarded)
- **Severity / category / class:** Low · robustness · BEHAVIOUR-CHANGE
- **Evidence:**
  - The comment at 3714 states the invariant: "never unwind into engine code (that ends in a silent fail-fast)". Only `transcode` is inside `try`.
  - These run in the detour with no `try`: the vtxf merge (`gen30::mergeVtxf`), `tips30`/`ui::mergeTips`, `ui::playsetMenu10`, the zone-list stage's `gameplay::zonelist30`/`beacons30`/`mission::beaconMap30`, and the `std::string` growth around them.
  - `zones10` and `chdForLua` have their own `try`.
  - Any `std::bad_alloc` (32-bit process; zone list and chunks of hundreds of KB) unwinds into the engine's AssetOpen caller. Whether any helper throws `std::out_of_range` on malformed input is UNVERIFIED.
- **Why it matters:** the author's own invariant is honoured on one path only. Failure mode: the game process fail-fasts during a load.
- **Fix (behaviour-preserving):** none. Wrapping each 3.0-edit stage in `try { } catch (...) { fall back to the unedited stream / g_open }` changes failure handling (fail-fast → served unedited). It needs the user's decision and must not destroy `r` twice.
- **Refactor risk:** when splitting GP-01 into helpers, keep the existing try scope exactly; do not widen it silently.
- **Verification:** NEW (verified by reading 3549-3756, 2619-2658, 3197-3210).

## Inventory
All line ranges below were re-checked against the sources at 684539b. All rows of the report's tables were correct except where marked "(corrected)".

### Functions, line ranges (count = lines)
| file | name | lines | count | hot/cold (code defaults / recommended) |
|---|---|---|---|---|
| gameplay.hpp | worldDilation30 | 26-47 | 22 | cold / per served .oct |
| gameplay.hpp | curves30 | 56-72 | 17 | cold / per served .oct |
| gameplay.hpp | surfaces30 | 81-100 | 20 | cold / per served .oct (GP-25) |
| gameplay.hpp | LuaTok / luaTokens | 125 / 126-155 | 1 / 30 | cold |
| gameplay.hpp | assignedAt, luaRenameKeys, luaRenameStrings | 157-161, 164-175, 178-186 | 5, 12, 9 | cold |
| gameplay.hpp | luaGlobalTable, luaTableEntries | 189-197, 200-214 | 9, 15 | cold |
| gameplay.hpp | challengeTypes30, beacons30, zonelist30 | 218-224, 229-248, 252-277 | 7, 20, 26 | cold |
| gameplay.hpp | currencies10 | 285-305 | 21 | zone-list load |
| gameplay.hpp | Playset10 / playsets10 | 321 / 322-417 | 1 / 96 | zone-list load (GP-05) |
| gameplay.hpp | challengeTypePath | 420-425 | 6 | per transcode / serve miss |
| gameplay.hpp | apGivers30 | 432-459 | 28 | per served .ap (GP-22) |
| gameplay.hpp | ActClass, actClasses, actClass | 478-482, 483-495, 496-500 | 5, 13, 5 | cold |
| gameplay.hpp | activityGraph30 | 504-643 | 140 | per served activity .mer (GP-04) |
| gameplay.hpp | activityTasks30, activityGivers, dnaGadgets30 | 648-678, 682-694, 697-704 | 31, 13, 8 | cold |
| gameplay.hpp | toolLevels10 | 711-737 | 27 | once |
| gameplay.hpp | levelAllows | 741-749 | 9 | per refused-tool gate call in 1.0 zones (GP-06) |
| gprt.hpp | zonePlayset10 | 32-36 | 5 | per ribbon query / refused-tool gate / activation / vehicle probe (engine zone-key read) |
| gprt.hpp | hkRibbonName | 38-42 | 5 | per ribbon query (8 call sites; FUN_00b476a0, the "ribbon screen open" check, has 21 callers; rate UNVERIFIED) |
| gprt.hpp | hkShowRibbon | 51-72 | 22 | per Tab |
| gprt.hpp | hkScreenReq | 85-95 | 11 | per event screen request |
| gprt.hpp | hkLuaAddScreen | 115-138 | 24 | per Lua AddScreen |
| gprt.hpp | movie10 | 161-170 | 10 | per delivery + per EI call (GP-07) |
| gprt.hpp | gfxString, screenOfCall, inputRoom | 172-180, 182-188, 191 | 9, 7, 1 | per EI call |
| gprt.hpp | hkFlashCall | 193-211 | 19 | per EI call (GP-08) |
| gprt.hpp | movieFocus10, hkFocusGain, hkFocusLoss | 232-239, 241-247, 249-253 | 8, 7, 5 | per focus change |
| gprt.hpp | script10 | 275-282 | 8 | per delivery of a 1.0-movie screen |
| gprt.hpp | hkMapState | 284-304 | 21 | per map entry per delivery (cheap fast path) |
| gprt.hpp | hkDeliver | 307-345 | 39 | per frame per input screen per slot (GP-07/09) |
| gprt.hpp | hkMissionActivate | 367-385 | 19 | per activation call (vtable method; work only on first) |
| gprt.hpp | hkVehicleCtl | 396-405 | 10 | per vehicle takeover |
| gprt.hpp | toolLevels10 (cached) | 422-439 | 18 | once (magic static, takes g_zipLock) |
| gprt.hpp | hkToolGate | 441-458 | 18 | per gate call; engine-cached except refused tools (GP-06) |
| gprt.hpp | install | 460-489 | 30 | once |
| ui.hpp | loadingScreen10, flashImageMovie, flashMovie, fmvClip | 19-26, 31-39, 42-50, 56-64 | 8, 9, 9, 9 | cold / per serve miss / per served file |
| ui.hpp | contextList10 | 74-92 | 19 | per context-list parse |
| ui.hpp | LuaEntry / luaTable | 98 / 101-127 | 1 / 27 | cold |
| ui.hpp | mergeTips | 132-160 | 29 | once |
| ui.hpp | playsetMenu10 | 176-225 | 50 | per Play Sets open |
| ui.hpp | texBundleAs | 233-248 | 16 | cold |
| di1compat.cpp | hkZoneStr | 3451-3466 | 16 | every zone-string read (266 callers), cheap fast path |
| di1compat.cpp | hkFmvPath | 3475-3486 | 12 | cold |
| di1compat.cpp | hkFindScreen | 3497-3506 | 10 | every screen-by-name lookup; pointer-compare fast path |
| di1compat.cpp | hkCtxParse | 3515-3527 | 13 | per parse |
| di1compat.cpp | tips30 | 3533-3546 | 14 | once |
| di1compat.cpp | hkOpen | 3549-3756 | 208 | per asset open (GP-01) |
| bldrt.hpp | buildOnLot, hkLotSpawn, hkLotState, hkNode, install | 49-74, 76-88, 91-95, 96-103, 105-113 | 26, 13, 5, 8, 9 | cold |
| caprt.hpp | def10, defText, mode | 61-64, 65-71, 72 | 4, 7, 1 | per base spawn / probe |
| caprt.hpp | kept, keep | 77-81, 82-105 | 5, 24 | per shutdown apply / per kept spawn |
| caprt.hpp | hkShut, hkRenderShut, hkDone | 106-111, 112-116, 117-136 | 6, 5, 20 | per actor shutdown / render shutdown / async completion |
| caprt.hpp | firstFew, hkNode, hkReq, hkCust, hkBase, hkByName | 139-145, 149-156, 157-166, 167-180, 181-195, 196-200 | 7, 8, 10, 14, 15, 5 | probes (default off) except hkBase |
| caprt.hpp | install | 202-219 | 18 | once |

### Hooks (gate defaults: code default; "rec." = recommended config)
| # | name string | VA | prologue | detour | gate | frequency (verified where noted) |
|---|---|---|---|---|---|---|
| 1 | vehicle control takeover (vehicle10 probe) | 0x00D612A0 | 6AFF68????????64A100000000 | gprt::hkVehicleCtl | lockTrace10 (on / on) | vtable method (ref 0x01D0EA78); per takeover |
| 2 | mission activation (accept10) | 0x015BCC10 | 83EC14568BF18A86DB03000084C0 | hkMissionActivate | accept10 (on / on) | vtable method (refs 0x01D167C0, 0x01DCD288); rate UNVERIFIED |
| 3 | tool level gate (1.0 lists) | 0x0150F7D0 | 538BD9837B74FF | hkToolGate | tools10 (on / on) | engine-cached; called twice in FUN_0151b960, once per tool per refresh (GP-06) |
| 4 | ribbon name (1.0 SelectMenu) | 0x00B42540 | 518B0D????????8B819C010000 | hkRibbonName | selectMenu10 (on / on) | 8 call sites incl. FUN_00b476a0 (21 callers) |
| 5 | ribbon open (1.0 SelectMenu) | 0x00B68F60 | 6AFF68????????64A100000000 | hkShowRibbon | selectMenu10 | 8 call sites; per Tab (GP-17) |
| 6 | screen request (ProgressLog) | 0x0156DDE0 | 64A1000000006AFF68 | hkScreenReq | selectMenu10 | per event screen request |
| 7 | screen AddScreen by name (1.0 scripts) | 0x0156A360 | 558BEC83E4F86AFF68????????64A100000000 | hkLuaAddScreen | selectMenu10 | per Lua AddScreen |
| 8 | movie ExternalInterface call (1.0 UsesActionMap; input10 probe) | 0x01564790 | 64A1000000006AFF68????????506489250000000081ECC8000000 | hkFlashCall | actionMap10 \|\| lockTrace10 (on / on) | every movie's EI handler (set at 0x015670AF) |
| 9 | screen focus gain (1.0 movies) | 0x01559480 | 568B742408578BF98B4E50C1E105 | hkFocusGain | focus10 (on / on) | per focus change (3 call sites) |
| 10 | screen focus loss (1.0 movies) | 0x01554590 | 64A1000000006AFF68????????506489250000000083EC28 | hkFocusLoss | focus10 | per focus change |
| 11 | screen action delivery (keys10; input10 probe) | 0x01565330 | 64A1000000006AFF68????????506489250000000083EC44 | hkDeliver | lockTrace10 \|\| keys10 (on / on) | 2 sites in FUN_01566f10; per input screen, per slot 0..10 when called with slot < 0 |
| 12 | action map entry state (keys10) | 0x00B48250 | 8B44240456578BF1508D4E04 | hkMapState | same as 11 | vtable method (ref 0x01CE4734); per map entry per query |
| 13 | building lot spawn (building10) | 0x00CEF120 | 538BDC83EC0883E4F083C404558B6B04 | bldrt::hkLotSpawn | building10 \|\| spawnTrace10 (on / on) | per lot spawn |
| 14 | lot state register (spawntrace10) | 0x00CEFE60 | 538B5C2408555657 | bldrt::hkLotState | spawnTrace10 (off / off) | per lot registration |
| 15 | SpawnBuilding Link_In (spawntrace10) | 0x011854B0 | 6AFF68????????64A100000000 | bldrt::hkNode | spawnTrace10 | per node fire |
| 16 | actor shutdown apply (shutdown10) | 0x007534C0 | 568B7424088B4604578BF9 | caprt::hkShut | shutdown10 && popin10 (on / on) | per shutdown apply |
| 17 | render component shutdown (shutdown10) | 0x01860B40 | 83EC08E8????????85C0745C | hkRenderShut | same as 16 | per render shutdown call |
| 18 | expendable base spawn (shutdown10) | 0x0160A3E0 | 558BEC83E4F06AFF68????????64A1 | hkBase | (16's gate) \|\| capsuleTrace10 | per base spawn, all packages |
| 19 | expendable async spawn done (shutdown10) | 0x0160A270 | 8B0D????????568B74240C | hkDone | same as 18 | per async completion, all |
| 20-23 | SpawnExpendable Link_In / spawn request / Customization reward spawn / spawn by name (capsuletrace10) | 0x017BF050 / 0x0160EBD0 / 0x00CBC5C0 / 0x0160F370 | 6AFF68????????64A100000000 | caprt::hkNode / hkReq / hkCust / hkByName | capsuleTrace10 (off / off) | probes |
| ui | AssetOpen | 0x0041CB10 | 6AFF68???????? | hkOpen | always | per asset open |
| ui | zone string field (ui: 1.0 loading screen) | 0x0139E0B0 | 83EC08568BF180BEE403000000 | hkZoneStr | serve10 && loadScreen10 (off / on) | 266 call sites; cheap fast path |
| ui | FMV path (ui: 1.0 clips) | 0x0067E460 | 81EC0401000053568BB42418010000 | hkFmvPath | serve10 && fmv10 (off / on) | per FMV |
| ui | screen by name (ui: tutorial element 61) | 0x0154FF90 | 837C2404007505 | hkFindScreen | serve10 && tutorial10 + push check 4324-4328 (off / on) | every screen-by-name call |
| ui | controller context list (ui: 1.0 tutorial contexts) | 0x01224880 | 5153558BE98A85A4000000 | hkCtxParse | serve10 && context10 (off / on) | per parse |

**Order and coupling to preserve (verified):**
- gprt install order 1; 2; 3; 4-7; 8; 9-10; 11-12.
- `hkShowRibbon` (gprt.hpp:53) and `hkFindScreen` (di1compat.cpp:3502) call 0x00B42540 through `hkRibbonName`. In a 1.0 zone, `hkFindScreen` therefore returns at 3503.
- `zoneCurrency10` (2565) goes through `hkZoneStr` when it is installed.
- caprt: 16-17 are installed before 18-19 (GP-41).
- `rt(0x0074B200)` goes through missiontrace's `hkActorOf`.
- `hkOpen` stage order as in GP-01; `zones10` → `zonelist30`/`beacons30` → `beaconMap30` (3611-3614).
- `transcodeOct`: `curves30` (748) runs after `locator30` (743) / `collision30` (745) / `transcodeCho` (747), and before `surfaces30` (750), `cho::const30` (755) and `addAllCounts` (756).

### Helpers (for duplicate matching)
| helper | location | note |
|---|---|---|
| luaTokens / assignedAt / luaRenameKeys / luaRenameStrings / luaGlobalTable / luaTableEntries | gameplay.hpp:126, 157, 164, 178, 189, 200 | token lexer family |
| record / lineAfter (lambdas) | gameplay.hpp:365, 406 | zonereg.py mirror |
| Edit-apply pattern | gameplay.hpp:651/675-676, 410-415; mission.hpp:233 | 3 copies |
| isInt / setName (lambdas) | gameplay.hpp:524, 587 | OCT helpers |
| levelAllows, challengeTypePath, toolLevels10 | gameplay.hpp:741, 420, 711 | |
| luaTable / q / u / le | ui.hpp:101, 178, 235, 240 | `q` ≠ `luachunk::quote` on `\n` |
| zonePlayset10, movie10, script10, gfxString, screenOfCall, inputRoom | gprt.hpp:32, 161, 275, 172, 182, 191 | `movie10` = di1compat `norm()` minus the `assets/` strip; `script10` duplicated inline at 118-122; the inputRoom pattern recurs at caprt.hpp:57/100/129/175/187 |
| toolLevels10 (cached) | gprt.hpp:422 | zip-extract pattern also at di1compat.cpp:3692-3695 |
| buildOnLot | bldrt.hpp:49 | |
| def10 / defText / mode / kept / keep / firstFew | caprt.hpp:61, 65, 72, 77, 82, 139 | |
| Link_In node read | bldrt.hpp:99-100; caprt.hpp:152-153 | GP-40 |

Engine globals and calls repeated across files:
| global / call | where |
|---|---|
| zone manager `*rt(0x0225F548)` | gprt.hpp:34, 401, 445; bldrt.hpp:52; di1compat.cpp:2561, 3538; npcrt.hpp:106 |
| `+0x19C` current zone | gprt.hpp:35, 446; di1compat.cpp:2562 |
| screen manager `*rt(0x02289E88)` | gprt.hpp:68, 130, 185, 329; di1compat.cpp:2961 |
| player manager `*rt(0x0225F540)` | gprt.hpp:89, 372 |
| CString assign 0x0041A250 | call sites gprt.hpp:66, 128; di1compat.cpp:2573, 3522 (typedefs gprt.hpp:24, di1compat.cpp:3521) (corrected) |
| screen request ctor / add / dtor 0x0155A630 / 0x015699B0 / 0x0155C270 | gprt.hpp:65-69, 127-131 |
| handle → actor 0x0074B200 | caprt.hpp:85, 133 |
| component of actor 0x0069ECD0 | caprt.hpp:97; npcrt.hpp:174, 211; di1compat.cpp:3344 (added) |

### Globals and statics
| name | location | type | writers / threads | lock | reset |
|---|---|---|---|---|---|
| gprt trampolines (12) | gprt.hpp:28-30, 114, 159, 228-230, 271, 365, 395, 420 | fn ptrs | `hook()` at install | n/a | n/a |
| g_inputLines | gprt.hpp:190 | volatile LONG | inputRoom (5 call sites) | Interlocked | never (600 budget) |
| g_keysScreen, g_keysMovie | gprt.hpp:272-273 | ptrs | hkDeliver set/clear, hkMapState read | none | after each delivery (GP-15) |
| hkDeliver::last, lastGates | gprt.hpp:311-312 | static locals | hkDeliver | none | never; `last` compared only |
| hkDeliver::wasDown[256] | gprt.hpp:334 | static local | hkDeliver | none | never |
| toolLevels10()::t | gprt.hpp:424 | static const map | magic static; g_zipLock inside init | init | immutable |
| g_movies10 / g_screens10 | di1compat.cpp:562/563 | std::set<std::string> | inserts 3722/3725 (loader threads); reads 3671, gprt 122/169/281 | g_sessLock | never; filled only by the serve10 path |
| bldrt::g_lotSpawn, g_lotState, g_node | bldrt.hpp:44-46 | trampolines | install | n/a | n/a |
| caprt trampolines | caprt.hpp:51-56, 148 | fn ptrs | install | n/a | `g_renderShut` used outside its detour (GP-41) |
| g_spawnLogged, g_doneLogged, g_keptLogged | caprt.hpp:57 | volatile LONG | probe budgets (300) | Interlocked | never |
| t_inCust, t_base, t_spark, t_keep | caprt.hpp:58 | `__declspec(thread) int` | per thread | n/a | t_keep reset after each g_shut |
| g_keepLock / g_keep | caprt.hpp:75-76 | mutex / unordered_set<uint32_t> | keep() (hkBase/hkDone), kept() (hkShut) | yes | never (GP-42) |
| firstFew::m, seen | caprt.hpp:141-142 | mutex / unordered_map | probes | yes | never (2000 cap) |
| actClasses()::m, challengeTypePath::pre, activityGivers::g/m | gameplay.hpp:485, 422, 685 | static const | magic static | n/a | immutable |
| contextList10::kept | ui.hpp:76 | static const array | none | none | immutable |
| hkZoneStr::mx, counter, pick | di1compat.cpp:3456-3458 | mutex, int, string | loading-screen query | partly (GP-35) | never |

### Checked, not an issue (from the report, re-verified)
- `hkMapState`: one branch on `g_keysScreen` after the original call.
- `hkRenderShut`: one TLS read.
- `hkShut`: an uncontended lock plus a hash lookup.
- `hkFindScreen`: pointer compare after the original call.
- `luaRenameKeys` iterates backwards; `playsets10` / `activityTasks30` apply edits in descending order.
- `texBundleAs` wraps stay in bounds.
- Magic statics are thread-safe.
- `def10` exact float compare: `kPopin10 = 100000.0f`, and 1e10 is exactly representable.
- The inputRoom wrap needs 2^31 calls.
- No VA is hooked twice in the slice; codebase duplicates 0x013A85D0 / 0x0145F720 are trace vs spawntrace10 and mutually exclusive (4211/4223 vs 4355-4357).
- No tracked `.py`/`.ps1` greps these log lines. harness.ps1 reads loader.log and pentane.log only. Keep the lines verbatim anyway.
- **Added:** `hkFocusGain`'s pointer chain is the engine's own (GP-13 rejection).
- **Added:** `hkFlashCall`'s engine getters are pure (GP-08).

### Refactor guardrails (kept from the report, with additions)
- SEH: no `__try` in the slice. Any new guarded-read helper must have no destructible locals.
- Prefer `sys::readable`, but do not add it on per-frame paths without measuring.
- Keep the dummy `edx` in every `__fastcall` detour; keep cdecl detours cdecl.
- `_ReturnAddress()` stays in `bldrt::hkLotSpawn`; `_AddressOfReturnAddress()` stays in `hkOpen` or is passed in (GP-01).
- `addName` emission order in `activityGraph30`.
- Install orders (gprt, caprt 16-17 before 18-19), `hkOpen` stage order, and `curves30` placement.
- Log text and hook names stay verbatim.
- The `g_inputLines` budget stays one shared counter.
- Do not flip `lockTrace10`, `trace`, or the `serve10` gating.
- Verification: octtest byte-compare (`--ui --texname --currency10 --challenge --activity --mischain --surf --tools --gpscan --zones10 --menu --beaconmap`, octtest.cpp:2576-3357), plus a timestamp-stripped `di1compat.log` diff of the same harness.ps1 session for gprt, bldrt and caprt.

## Rejected on verification
- **GP-13 `hkFocusGain` uses an unbounded player index and an unchecked pointer chain:** the hook reads exactly the chain the engine's original has just read with the same unchecked index.
  - The hook reads `*(mgr + 0x28C + screen[0x50]*0x20)` → `+8` → `[0]` (gprt.hpp:244-246), and it calls the original first (243).
  - The original 0x01559480 performs the identical reads, unchecked: `mov ecx,[esi+0x50]; shl ecx,5; mov eax,[ecx+edi+0x28c]` → `mov edx,[eax+8]` → `cmp [edx],esi` (0x01559488-0x015594A3).
  - So an index that would fault the hook has already faulted the engine. The proposed bound would add a check that cannot change any reachable outcome, and its constant was unverified.
  - The point is recorded as a policy rule in GP-46.

# Test driver, build and CI (`octtest.cpp`, `CMakeLists.txt`, `di1compat.def`, `di1compat.cfg.example`, `.github/workflows/check.yml`, README "Building"/"Testing")

Scope: `native/di1compat/octtest.cpp` (3,427 lines; `main` 2528-3427 read in full, every mode function's return paths
read, every cited line opened), `CMakeLists.txt` (12 lines), `di1compat.def`, `di1compat.cfg.example`,
`.github/workflows/check.yml` (33 lines), README.md 90-101 and 146-158, plus the plugin code the findings compare
against (`di1compat.cpp` 131-366, 372-527, 572-758, 767-899, 1052-1112, 1222-1250, 2619-2640, 3596-3620, 3776-3850,
4097-4170) and the cited header lines (`octx.hpp` 104-238, 914-1165; `cho.hpp` 273-301; `gameplay.hpp` 50-80, 415-432;
`mission.hpp` 36-104; `sys.hpp` 170-196; `audio.hpp` 118-153; `sx.hpp` 40-52). Repo at 684539b, worktree clean before
and after. No build was run in the repo. I re-used the reviewer's scratch builds (`scratchpad\build-audit\`, Ninja
Multi-Config, and `scratchpad\build-audit-w4\`, Ninja Release + `/W4`; MSVC 19.44.35223, CMake 3.31.6): read the
generated flags from `CMakeCache.txt` / `impl-*.ninja`, re-tallied `w4.log` with a script, ran `dumpbin` on the scratch
DLL, and ran the scratch `octtest.exe` with bad arguments and `--readable` (from the scratchpad directory).

Verification: 37 findings checked: 21 confirmed, 16 corrected, 0 unverified (sub-claims still open are marked inside
the findings), 0 rejected outright (4 sub-claims rejected, listed at the end); 0 new (three missed facts were folded into
OT-13, OT-33 and OT-34 instead of padding new IDs).

**Gate verdict (the question this area decides).** octtest cannot gate a behaviour-preserving refactor through exit
codes. Of 70 modes, 27 set a nonzero exit when (part of) their check fails, 19 only when setup fails or the code under
test refuses outright, 24 always return 0. The PF modes mostly compare against 3.0 twins or Python oracles, not against
the pre-refactor output, and none of them reaches the plugin-only pipelines in `di1compat.cpp` (`transcode()`,
`transcodeOct`, `read10`, `compile10`, `zones10`, `missionGraph30`, ...). Every mode except `--readable` needs local
game data. The only usable safety net is a recorded golden baseline (OT-27) plus moving plugin-only logic into headers
(OT-07, OT-09 to OT-13, OT-16). All uncaught exceptions end the process with 0xC0000409 and no output (reproduced), so
a gate does see a crash as a failure; what it cannot see is wrong bytes.

## Findings
| ID | Title | Sev | Class | Where |
|---|---|---|---|---|
| OT-07 | `--oct`/`--cho-const`/`--variants` re-create `transcodeOct` in a different order with fewer steps | High | NEEDS-VERIFICATION | octtest.cpp:2944-2976, 1058-1066, 1271-1277; di1compat.cpp:679-758 |
| OT-16 | Plugin-only translation logic is unreachable from octtest | High | NEEDS-VERIFICATION | di1compat.cpp:395-484, 610, 625-758, 767, 795, 822, 853-1112, 2619, 3787 |
| OT-19 | Exit codes cannot detect most regressions | High | DOC-ONLY | octtest.cpp main + mode functions (Table "CLI modes") |
| OT-27 | No runner, manifest or golden baseline for octtest | High | SAFE-REFACTOR | (missing); selftest.py, check.yml |
| OT-01 | `main` is a 900-line if-chain dispatcher with 45 inline modes | Med | SAFE-REFACTOR | octtest.cpp:2528-3427 |
| OT-02 | No usage text; bad arguments crash silently; empty runs pass | Med | BEHAVIOUR-CHANGE | octtest.cpp:3391-3426 |
| OT-03 | No top-level exception handler | Med | BEHAVIOUR-CHANGE | octtest.cpp:1718, 1748, 2577, 2748, 2819, 2831, 2952, 2992, 3010, 3054, 3343 |
| OT-04 | Dead `try`, catch blocks that hide failures | Med | BEHAVIOUR-CHANGE | octtest.cpp:1441, 1492-1494, 3405-3408, 3423 |
| OT-05 | Long mode functions | Med | SAFE-REFACTOR | octtest.cpp:1773-2084, 973-1212, 756-921, ... |
| OT-08 | The plugin's arena slack (4096) is never exercised offline | Med | SAFE-REFACTOR | octx.hpp:111-114, 213-236; di1compat.cpp:4156 |
| OT-09 | `--read10` is a stale copy of `read10`'s archive lookup | Med | NEEDS-VERIFICATION | octtest.cpp:3300-3317; di1compat.cpp:395-451 |
| OT-10 | Shader-cache key/lookup written three times | Med | NEEDS-VERIFICATION | octtest.cpp:318-336, 3250-3271; di1compat.cpp:3779-3850 |
| OT-11 | `--render`'s IO setup differs from `transcodeAssetRender` | Med | NEEDS-VERIFICATION | octtest.cpp:260-362, 3365-3390; di1compat.cpp:519-527, 572-586, 625-667 |
| OT-12 | Plugin config cannot be reused; octtest re-maps options by hand | Med | NEEDS-VERIFICATION | di1compat.cpp:131-237, 239-366, 4156-4164; octtest.cpp:2685, 2738-2746 |
| OT-13 | Small plugin pipelines re-implemented in octtest (some drifted) | Med | NEEDS-VERIFICATION | octtest.cpp:2547-2560, 2689, 2702, 2759-2784, 2977-2998, 3029-3034, 3131-3136, 3151-3166, 3338-3358 |
| OT-17 | `--readable` tests only `sys::readable`, not the plugin's own `readable` | Med | SAFE-REFACTOR | octtest.cpp:2455-2526; di1compat.cpp:1228-1241; sys.hpp:180-196 |
| OT-18 | Coverage that runs only with an env var or extra argument | Med | DOC-ONLY | octtest.cpp:270, 287, 309, 317, 3154, 3289 |
| OT-20 | 11 oracle scripts named by octtest/its headers are not in the repo | Med | DOC-ONLY | octtest.cpp:240-241, 1744, 2582, 2589, 2606, 2628, 2827, 2978, 3044, 3059; npcrt.hpp:394 |
| OT-22 | Timing assertions and run-to-run output noise | Med | DOC-ONLY | octtest.cpp:2521, 2436-2450, 2349, 348, 2949, 1798, 3377-3382 |
| OT-28 | No warning level (cl runs at /W1), no `/WX`, no `/permissive-` | Med | NEEDS-VERIFICATION | CMakeLists.txt:1-12 |
| OT-29 | Release builds have no PDB; RelWithDebInfo is different code | Med | NEEDS-VERIFICATION | CMakeLists.txt |
| OT-34 | CI does not build or test native code | Med | SAFE-REFACTOR | .github/workflows/check.yml:8-33; README.md:157-158 |
| OT-06 | Global and static mutable state in octtest | Low | SAFE-REFACTOR | octtest.cpp:308, 435-436, 1838-1839, 2331-2333, 2425-2426 |
| OT-14 | FMOD calls bypassing `audio::Api`; three copies of the iterator layout | Low | NEEDS-VERIFICATION | octtest.cpp:1814-1831, 1900-1996; audio.hpp:122-153, 857-858 |
| OT-15 | Code repeated inside octtest itself | Low | SAFE-REFACTOR | octtest.cpp (many) |
| OT-21 | Data-set-specific expected values hard-coded | Low | SAFE-REFACTOR | octtest.cpp:152, 227, 237-238, 1841-1872, 2277-2385, 2444 |
| OT-23 | Stale/misplaced comments, cryptic names, missing includes | Low | SAFE-REFACTOR | octtest.cpp:1-3, 26, 1547, 1594, 1759, 2143, 3142; CMakeLists.txt:10 |
| OT-24 | Test-only input handling that can crash or hang | Low | BEHAVIOUR-CHANGE | octtest.cpp:231-233, 511, 522, 2713, 2801-2803, 3198-3205 |
| OT-25 | CPU: quadratic counts check, repeated corpus walks (offline only) | Low | SAFE-REFACTOR | octtest.cpp:446-454, 763/807/865, 1043/1053 |
| OT-26 | octtest mixes acceptance tests, converters and probes | Low | SAFE-REFACTOR | octtest.cpp main |
| OT-30 | No `/GL`/LTCG or `/Gw` (`/Gy` is already on) | Low | NEEDS-VERIFICATION | CMakeLists.txt |
| OT-31 | octtest uses the dynamic CRT; README says the CRT is static | Low | SAFE-REFACTOR | CMakeLists.txt:9-12; README.md:99-100 |
| OT-32 | miniz compiled twice; `NOMINMAX` defines do nothing | Low | SAFE-REFACTOR | CMakeLists.txt:7,11; audio.hpp:17-20; sys.hpp:4-7; arc30.hpp:6; di1compat.cpp:8 |
| OT-33 | Nothing enforces x86 | Low | SAFE-REFACTOR | CMakeLists.txt:3 |
| OT-35 | No install step; README's `build/` folder not git-ignored | Low | SAFE-REFACTOR | README.md:95; .gitignore:10 |
| OT-36 | `/EHsc` with SEH `__try` (document, do not change) | Low | DOC-ONLY | di1compat.cpp (10x `__try`), sx.hpp (2x) |
| OT-37 | Config example shows 4 of 105 keys; unknown keys silently ignored | Low | BEHAVIOUR-CHANGE | di1compat.cfg.example; di1compat.cpp:251-354 |

### OT-07 `--oct`, `--cho-const` and `--variants` re-create `transcodeOct` with a different order and fewer steps
- **Where:** `native/di1compat/octtest.cpp:2944-2976` (`--oct`), `octtest.cpp:1058-1066` (`--cho-const`), `octtest.cpp:1271-1277` (`--variants` `convert`); `native/di1compat/di1compat.cpp:734-758` (`transcodeOct`), `di1compat.cpp:679-708` (`transcodeCho`)
- **Severity / category / class:** High · duplication with drift / test fidelity · NEEDS-VERIFICATION
- **Evidence:**
  - Plugin order: `transcodeAssetRender` 736 → `navmesh30` or `dropPool("NavMeshPool")` 738-740 → `addAssetPool` 741 → `skin30` 742 → `locator30` 743 → `camera30` if `camWorld10` 744 → `collision30` 745 → `realm30w(f, realmFile10 && path.rfind("realms/", 0) == 0)` 746 → `transcodeCho` 747 (`clips30(f, motion, &failed, clip10Ids, &ren)` 687, `links30` 694, `layout30` 697, `time30` 700, `anim30` 703, `scene30` 706) → `curves30` 748 → `surfaces30(f, surfaceTable30())` 750 → `const30` only for `choreographies/`/`exploders/` 754-755 → `addAllCounts` 756.
  - `--oct` order (2952-2971): `skin30` → `locator30` → `camera30` (unconditional) → `addAssetPool` → `collision30` → `realm30w(f, src.find("/realms/") != npos)` (a filesystem-path predicate, 2959-2961) → `curves30` (before the cho steps) → only with a 4th arg `cho`: `clips30(f, nullptr)`, `layout30`, `time30`, `anim30`, `scene30`, `const30` (no path gate) → `addAllCounts`. Missing: render, navmesh/drop, `links30`, `surfaces30`.
  - `clips30(f, nullptr)` with `ids = false`: `skip = !motion || ...` (`cho.hpp:292`) skips every motion clip and `clipPorts` is never consulted (`cho.hpp:283`), while the plugin defaults are `clip10 = clip10Motions = clip10Ids = true` (`di1compat.cpp:198-200`).
  - `--cho-const` (1058-1066): `addAssetPool`, `skin30`, `locator30`, `collision30`, `clips30(f, nullptr)`, `layout30`, `time30`, `anim30`, `scene30` (then `const30`); its comment (966-967) says "through the plugin's .oct steps". `--variants` `convert` (1271-1277): `addAssetPool`, `skin30`, `locator30`, `collision30`, `addAllCounts`.
  - Passing `&nfl` to `curves30` is behaviour-neutral (`gameplay.hpp:56-72`: the pointer is only written), so that difference is harmless.
  - UNVERIFIED: whether `addAssetPool` before/after `skin30..camera30`, and `curves30` before/after the cho steps, change the bytes. Settle it by running both orders on one 1.0 world and one choreography and diffing.
- **Why it matters:** These are the only offline exercises of the `.oct` pipeline, but they run a different pipeline from the one the plugin serves, so a refactor of `transcodeOct` or of the step order is not covered.
- **Fix (behaviour-preserving):** Move the step sequence (not the logging) into a header function, e.g. `octx::transcodeSteps(oct::File&, const Steps&, std::string& note)`, taking booleans/predicates for `camWorld10`, `realmFile10`+path, `motion`, `clip10Ids`, `surfaceTable`, `isChoOrExploder`, `curves10`, `surfaces10`, `choConst10`. Render and navmesh (which need `read10s`, `g_render`, `npcrt::skin10`) stay as callbacks. The plugin calls it with `g_cfg` values; octtest calls it with explicit options. Note strings must be produced byte-identically (they go to `di1compat.log`).
- **Refactor risk:** The move must keep the exact step order and every `note +=` string. octtest's `--oct` output will change once it runs the plugin's order; record the new baseline on purpose. `surfaceTable30()` reads engine memory and must stay a callback.
- **Verification:** CONFIRMED (added: `camera30` unconditional and the filesystem-path realm predicate in `--oct`; `curves30`'s flag pointer is neutral).

### OT-16 Plugin-only translation logic is unreachable from octtest
- **Where:** `native/di1compat/di1compat.cpp:853-1112` (`transcode()`), `734-758` (`transcodeOct`), `679-708` (`transcodeCho`), `625-667` (`transcodeAssetRender`), `767` (`missionGraph30`), `795` (`particleShaders`), `822` (`isSpark10`), `2619` (`zones10`), `395-484` (`read10`/`read10z`/`endsWith`/`norm`), `3787` (`compile10`), `610` (`padSmallDxt`), `239` (`loadConfig`), `1228-1241` (file-local `readable`, used at 1246); runtime headers included at `di1compat.cpp:621, 4092-4095`
- **Severity / category / class:** High · coverage / structure · NEEDS-VERIFICATION
- **Evidence:** All of these are `static` in `di1compat.cpp`; octtest includes only the format headers (`octtest.cpp:10-29`). `npcrt.hpp`, `vfxrt.hpp`, `gprt.hpp`, `bldrt.hpp`, `caprt.hpp` are not self-contained (`npcrt.hpp:1`: "Included by di1compat.cpp after its helpers (logf, hook, g_cfg, read10s, read30x)"). `mernode.hpp` is not in octtest either, but it is included at the top of `di1compat.cpp` (line 29) with the format headers; it is engine-ABI registration code (`mernode.hpp:231` `rt(k.va)`) and offline-untestable anyway; its own comment "Included by di1compat.cpp after rt()/logf()" (line 3) is stale.
- **Why it matters:** A refactor of `di1compat.cpp` (where the god functions are) has no offline safety net at all; only in-game runs and log diffs can catch regressions there.
- **Fix (behaviour-preserving):** Before refactoring the plugin, move the pure parts into headers unchanged (OT-07, OT-09 to OT-13), with engine-dependent pieces (`read10s`, `read30x`, `surfaceTable30`, `has30`, `g_cfg`) passed in as callbacks/values; then point octtest at them.
- **Refactor risk:** This is the refactor itself. Keep lock scopes (`g_zipLock`, `g_sessLock`), memo/cache behaviour and log strings in the plugin.
- **Verification:** CONFIRMED (added the `mernode.hpp` nuance: top-level include, stale comment).

### OT-19 Exit codes cannot detect most regressions
- **Where:** `native/di1compat/octtest.cpp` — `--chd` 2720-2734, `--dbtriggers` 2699-2709, `--gpscan` 3070-3127, `--fmod` 1874/2077/2083, `--render` 350-351; full list in the CLI-modes table
- **Severity / category / class:** High · test design · DOC-ONLY
- **Evidence:** Re-derived from every return path: 27 PF, 24 always 0, 19 SETUP/REFUSE (table below). Examples: `--chd` prints `MISMATCH` and returns 0 (2730-2733); `--dbtriggers` prints "DO NOT match" and returns 0 (2706-2708); `--gpscan` comment says "no parse may throw" (3071) but returns 0 after `FAIL` lines (3114-3126); `--fmod` prints `NOTHING` (1874) and `(wrong)` (2077) and returns 0 (2083); `--render` prints `problem:` lines and returns 0 when `transcodeWorld` succeeded (350-351, 361). Several PF modes count only part of what they print: `--warm` ignores generator problems (`probs`, 3295-3298), `--mtb` ignores `r.problems` (3190, 3195), `--clip-ids` fails only on "NOT CONVERTED" (1544), `--variants`/`--cho-scene` only on named counters (1319, 1388), `--counts` not on exceptions (3408), the default mode passes with 0 files (3425-3426).
- **Why it matters:** For a no-behaviour-change refactor these modes can only catch a regression by a byte comparison with output recorded before the refactor.
- **Fix (behaviour-preserving):** None to the tool itself; rely on OT-27. Changing exit codes is a separate octtest behaviour change (OT-02/03/04).
- **Refactor risk:** None.
- **Verification:** CORRECTED: the `NOTHING` verdict is at 1874 (2083 is the `return 0`); added the partial-check PF modes.

### OT-27 No runner, manifest or golden baseline for octtest
- **Where:** (missing); `selftest.py` (no `octtest` reference), `.github/workflows/check.yml`
- **Severity / category / class:** High (for the refactor) · process · SAFE-REFACTOR (additive tooling)
- **Evidence:** `grep octtest selftest.py` → nothing. The Python oracles (`fevcheck.py`, `fevdeps.py`, `mixercheck.py`, `vocheck.py`, `particles.py`, `renderoracle.py`, `skinlayout.py`, `portmap.py`) mention octtest only in docstrings/comments; none runs it.
- **Why it matters:** Without a frozen baseline octtest cannot gate anything beyond the 27 PF modes (OT-19).
- **Fix (behaviour-preserving):** Before touching the plugin, write a local manifest (script) of every mode with real arguments and the env-var variants of OT-18; record exit code, stdout with OT-22 masking, stderr, and sha256 of every written file, from today's build; re-run and diff after each refactor step.
- **Refactor risk:** None. Keep the manifest outside CI (it needs game data).
- **Verification:** CONFIRMED.
- **Done:** `golden.py` (record/check, baseline `base-23514ce`) runs every runnable mode plus the static source facts.

### OT-01 `main` is a 900-line if-chain dispatcher with 45 inline modes
- **Where:** `native/di1compat/octtest.cpp:2528-3427`
- **Severity / category / class:** Med · god function · SAFE-REFACTOR
- **Evidence:** 69 flag comparisons (67 `if (argc > N && std::string(argv[1]) == "--x")` plus the `bool menu` (3327) and `bool counts` (3391) forms) + the default = 70 modes; 25 call a function (24 `return xTest(...)` lines + `--variants` 2685), 45 are inline. `exts` declared at 2532, first used at 3399; `namespace fs` re-aliased at 2543, 2881, 2980, 3002; `--mer` decrements `argc` in place (2738) and then tests `argc > 5` again (2753); `--menu`/`--zones10` share one body (3327-3364); `choFileTest` reads `a[1]` (1582); signatures `(char** a)`, `(char** a, int argc)`, `(int argc, char** argv)`.
- **Why it matters:** The mode list is not visible; locals leak across modes; argument-count mistakes fall through to the default mode (OT-02).
- **Fix (behaviour-preserving):** A table `{flag, minArgc, fn}` and one `static int modeX(int argc, char** argv)` per inline body, moved unchanged with the same `argc` thresholds and return values; keep the dispatch order (first match wins; `--cho-time` is tested before `--cho-time10` but exact string compare makes order irrelevant except for `--menu`, whose threshold `argc > 5` must stay).
- **Refactor risk:** Low. Watch the in-place `argc` change in `--mer` (2738/2753) and the shared `--menu`/`--zones10` body.
- **Verification:** CORRECTED: severity High → Med (test-driver structure does not block the plugin refactor; rubric "large god function"); the 69 comparisons include 2 `bool` forms.

### OT-02 No usage text; bad arguments crash silently; empty runs pass
- **Where:** `native/di1compat/octtest.cpp:3391-3426` (fallback), every dispatch line
- **Severity / category / class:** Med · automation robustness · BEHAVIOUR-CHANGE (octtest CLI only)
- **Evidence:** Reproduced with the scratch build: `--typo`, `--render a b`, `nonexistent_dir`, `--menu x` each exit 0xC0000409 with empty stdout (`recursive_directory_iterator` on a non-directory throws, uncaught → `abort` → `__fastfail`). No arguments → `round-trip 0/0 exact`, exit 0. Also `--counts` with no root → "0 exact, 0 mismatch", exit 0.
- **Why it matters:** A wrong path in a gating script passes silently (0 files) and a typo looks like a crash.
- **Fix (behaviour-preserving):** None is behaviour-preserving for octtest; the fix (usage + exit 2 for unknown/short args, nonzero when 0 files were checked) changes octtest's exit codes. Do it as a separate commit after the OT-27 baseline is recorded.
- **Refactor risk:** None for the plugin.
- **Verification:** CORRECTED: severity High → Med; added `--counts` with no root and `--menu` with too few args; class is a behaviour change of octtest.

### OT-03 No top-level exception handler; throwing calls unguarded
- **Where:** `native/di1compat/octtest.cpp:1718` (`--banm`), 1748 (`--cho-clipdump`), 2577 (`--currency10`), 2748 (`--mer`), 2819 (`--bent`), 2831/2846 (`--nav`), 2952 (`--oct`), 2992 (`--challenge`), 3010 (`--activity`), 3054 (`--surf`), 3343 (`--zones10`/`--menu`); also 2646/2665 (`chd::lua` in `--chest`/`--perc`)
- **Severity / category / class:** Med · robustness · BEHAVIOUR-CHANGE (octtest only)
- **Evidence:** `oct::File::parse` throws (`oct.hpp:61, 67, 78`), `luachunk` throws (`luachunk.hpp:28, 35, 64, 104, ...`), `chd::lua` throws (`chd.hpp:108-118`). `luachunk::eval(c10)` at 3343 runs on an empty string when the member is missing → "not a Lua 5.1 x86 chunk". The plugin wraps `dnaGadgets30` in `try` (`di1compat.cpp:880`); `--challenge` (2992) does not.
- **Why it matters:** One bad file aborts a whole corpus run with no message.
- **Fix (behaviour-preserving):** Not behaviour-preserving for octtest: `try { ... } catch (std::exception& e) { printf("EXCEPTION %s\n", e.what()); return 3; }` around the dispatch changes crash → message + 3. Separate commit after the baseline.
- **Refactor risk:** None for the plugin.
- **Verification:** CONFIRMED.

### OT-04 A dead `try`, and catch blocks that hide failures
- **Where:** `native/di1compat/octtest.cpp:1492-1494`, 1441, 3405-3408, 3423
- **Severity / category / class:** Med · test correctness · BEHAVIOUR-CHANGE (octtest only)
- **Evidence:** 1492 `try { ba = slurp(...); bb = slurp(...); } catch (...) { continue; }` guards `slurp`, which does not throw (it returns "" on error, 31-35); `oct::File::parse` at 1494 is outside, after only a `size() < 4` check. 1441 `catch (...) { continue; }` skips parse failures without counting them. 3408 prints `FAIL` without `++cBad`, so `--counts` exits 0 (3423) on a throwing file. Under `/EHsc`, `catch (...)` never catches SEH exceptions.
- **Why it matters:** Failures disappear from totals and exit codes.
- **Fix (behaviour-preserving):** Not for octtest (exit codes change): move `parse` into the `try`, count failures. Separate commit.
- **Refactor risk:** None for the plugin.
- **Verification:** CONFIRMED.

### OT-05 Long mode functions
- **Where:** `native/di1compat/octtest.cpp:1773-2084` (`fmodTest`, 312), 973-1212 (`choConstTest`, 240), 756-921 (`choAnimTest`, 166), 2250-2388 (`voTest`, 139), 135-256 (`uiTest`, 122), 260-362 (`renderTest`, 103), 1219-1320 (`variantsTest`, 102)
- **Severity / category / class:** Med · god function (test) · SAFE-REFACTOR
- **Evidence:** `fmodTest` has ~7 sub-tests switched by `FMOD_CAPS` (early return 1778-1786), `FMOD_SPEAKERS`, `CUE_RUN`, `EV_RUN`, `EV_GEOM`, `EV_LOAD`, `EV_SECS`; callback state in `static` locals (1838-1839). `choConstTest` has 4 lambdas (`statics` 977, `count3` 982, `shapesOf` 990-1041, `hnum` 1127).
- **Why it matters:** Hard to see which checks run and what each asserts.
- **Fix (behaviour-preserving):** Split into named sub-checks called in the same order; keep every `printf` and its order.
- **Refactor risk:** Low; output order is part of the baseline.
- **Verification:** CORRECTED: `shapesOf` is 52 lines (990-1041), not 90.

### OT-08 The plugin's arena slack (4096) is never exercised offline
- **Where:** `native/di1compat/octx.hpp:111-114`, `octx.hpp:213-236`; `native/di1compat/di1compat.cpp:4156`
- **Severity / category / class:** Med · test does not match the shipped configuration · SAFE-REFACTOR
- **Evidence:** `inline int g_slack = 0;` (114; comment 111: "0 = the shipped-exact values octtest checks"); plugin sets `octx::g_slack = 4096;` (4156); octtest never assigns it. Slack changes structure, not only sizes: `if (statics || g_slack)` adds `ConstantAccumulatedSize32/64` records (215-220), `s32 += g_slack` (213), `DefaultDataSize`/`BaseDataNodeLinksAdditionalSize` get `g_slack / 16` (232, 236). `--counts` must stay at 0 (it compares with shipped 3.0 data), but `--oct`, `--cho-const`, `--variants` produce bytes the plugin never serves.
- **Why it matters:** The served form of every SubNetwork's counts is untested offline; a refactor of `addCounts` could break only the slack branches.
- **Fix (behaviour-preserving):** `inline constexpr int kPluginSlack = 4096;` in `octx.hpp`, plugin assigns it; octtest gets an opt-in `--slack N` / env var for the `addAllCounts` modes (no default change). Record a baseline with slack 4096.
- **Refactor risk:** Low.
- **Verification:** CORRECTED: severity High → Med (a handful of branch lines; golden output at slack 0 still covers the rest; the fix is trivial).
- **Done:** octtest reads `OCTX_SLACK=<n>`; golden.py runs `--counts`, `--oct`, `--cho-const` and `--variants` at 0 and 4096.

### OT-09 `--read10` is a stale copy of `read10`'s archive lookup
- **Where:** `native/di1compat/octtest.cpp:3300-3317`; `native/di1compat/di1compat.cpp:395-403`, `405-451`
- **Severity / category / class:** Med · duplication with drift · NEEDS-VERIFICATION
- **Evidence:** octtest `std::string zips[3]` (3305); plugin `zips[5]` (429-430) adds the 2-letter `shard` (417-420) and the `assetBufs10` buffer archive (426-430). The copy also lacks the `root10` `startup*.zip` fallback (443-449), the `assets/` strip (398-399) and the `choAlias10` retry (401-402). Plugin defaults `root10`, `assetBufs10`, `choAlias10` are all true (172, 223, 201).
- **Why it matters:** The probe answers wrongly for exactly the paths the latest plugin fixes added.
- **Fix (behaviour-preserving):** A pure header function `read10Candidates(const std::string& p, bool assetBufs)` returning the candidate list in today's order; `read10z` iterates it under `g_zipLock`; `--read10` prints it.
- **Refactor risk:** Low–Med: keep the candidate order, the empty-string candidate when `assetBufs10` is off, the lock and the `g_zips` cache in the plugin.
- **Verification:** CONFIRMED.

### OT-10 The shader compile cache is written three times
- **Where:** `native/di1compat/di1compat.cpp:3779-3850`; `native/di1compat/octtest.cpp:318-336`, `3250-3271`
- **Severity / category / class:** Med · duplication with drift · NEEDS-VERIFICATION
- **Evidence:** Plugin key `fnv64(std::string(target) + "\n" + hlsl)` (3830), hit requires `code.size() > 32 && !code.compare(0, 4, "DXBC")` (3839), blobs released (3844-3847). `renderTest` inlines FNV (319-320), hit test only `size() > 32` (325), never releases either blob (327-332, raw vtable slots 3/4). `--warm` has an `fnv` lambda (3250), the same weak hit test (3262), releases through vtable slot 2 (3255); its comment claims "same compiler, same keys" (3238).
- **Why it matters:** The cache filename keys the plugin's on-disk shader cache; a drift in one copy silently makes `--warm` fill a cache the plugin never hits.
- **Fix (behaviour-preserving):** A shared header with `fnv64`, the name format and `compileCached(compile, dir, hlsl, target, code, msg)`; the plugin keeps its `magenta10`/`bufview10`/idcolor substitution before the call. Adopting the `DXBC` check in octtest changes octtest only.
- **Refactor risk:** Low; the key string must stay byte-identical (`target + "\n" + hlsl`, `%016llx.dxbc`).
- **Verification:** CONFIRMED.

### OT-11 `--render`'s IO setup differs from the plugin's `transcodeAssetRender`
- **Where:** `native/di1compat/octtest.cpp:260-362`, `3365-3390`; `native/di1compat/di1compat.cpp:519-527`, `572-586`, `625-667`
- **Severity / category / class:** Med · duplication with drift · NEEDS-VERIFICATION
- **Evidence:** Plugin applies `npcrt::skin10` before rendering (635; `skin10` default true, 186); octtest cannot (`npcrt.hpp` not compiled in). `io.translateSkinned`: plugin `skinTr10 && skinLayout10 && !g_vf30.empty()` (644), octtest only `RENDER_SKINTR` (304). Plugin defaults `skinLayout10`, `skinTr10`, `sceneSettings10` true (141-143); octtest needs `RENDER_SKIN`/`RENDER_SKINTR`/`RENDER_SSF`, so plain `--render` tests a non-default configuration (deliberate for the Python oracle, comment 285). `io.translate`: plugin `shaders10 && translate10` (640; `shaders10` code default 0, recommended 1), octtest `RENDER_TRANSLATE` (284). `io.translatable` is octtest's own generate+compile (309-341), not `shader10`. The `ARC30_GAME` reader (273-282) and `--arc` (3375-3379) are copies of `read30x` (519-527); the plugin converts `/` to `\` (525), the copies do not (harmless for `CreateFile`).
- **Why it matters:** The render path the plugin runs under the recommended config (`shaders10 = 1`) is reproducible offline only with the right env-var combination and still without `skin10`.
- **Fix (behaviour-preserving):** A header helper that builds `render::IO` from explicit options + callbacks, used by both; octtest maps env vars to the same options.
- **Refactor risk:** Low–Med; `sceneSettingsIO` caching/locking and the log lines stay in the plugin.
- **Verification:** CONFIRMED.

### OT-12 Plugin configuration cannot be reused; octtest re-maps options by hand
- **Where:** `native/di1compat/di1compat.cpp:131-237` (`static struct {...} g_cfg`), `239-366` (`loadConfig`), `4156-4164`; `native/di1compat/octtest.cpp:2685`, `2738-2746`, `3287-3291`
- **Severity / category / class:** Med · duplication / coupling · NEEDS-VERIFICATION
- **Evidence:** The config is an anonymous struct, not a named `Cfg` (README:143 calls it "the `Cfg` structure"); `loadConfig` has 105 `strcmp` keys (251-354) and calls `logf` (244, 357-365). octtest maps options through 22 env vars and flags: `--mer` flags (2739-2745) mirror `g_mer.*` assignments (4159-4164; `mer::Table` defaults equal `g_cfg` defaults, `mer.hpp:57-62` vs `di1compat.cpp:154-162`); `--variants` `argv[4]` sets `octx::g_variantGroups` (2685; plugin 4157); `WARM_EXT30` stands for `skintr10` (3287).
- **Why it matters:** Each new key must be mirrored by hand to be testable offline.
- **Fix (behaviour-preserving):** Name the struct, move it and a parser that takes a log callback into a header; `g_cfg` stays a `static` instance in `di1compat.cpp`; octtest gets `--cfg <file>`.
- **Refactor risk:** Low–Med: keep key names, defaults, the parse format `" %63[^= ] = %259[^\r\n]"`, the "no %s: defaults" line and the three `config ...:` log lines byte-identical.
- **Verification:** CORRECTED: line ranges (131-237 / 239-366, not "130-370"), the struct is anonymous, 105 keys, `logf` dependency.

### OT-13 Small plugin pipelines re-implemented in octtest (some drifted)
- **Where:** `native/di1compat/octtest.cpp:2547-2560`, 2689, 2702, 3131-3136, 2759-2765, 2778-2784, 2977-2998, 3029-3034, 3151-3166, 2592, 2615, 2960, 3302, 3327-3363; `native/di1compat/di1compat.cpp:822-849`, 1085-1089, 899-912, 890-898, 853-888, 795-818, 1061-1063, 1105, 1096, 478-484, 2619-2640, 3596-3616
- **Severity / category / class:** Med · duplication with drift · NEEDS-VERIFICATION
- **Evidence:**
  - Spark rule: `--groups30`'s `isSpark` (2547-2560) copies the DNA lookup of `isSpark10` (839-844). The prefix differs (`"expendables_data/dna/dna/"` 2551 vs `"expendables_data/dna/"` 829) because octtest reads the extract layout (`dna/dna.zip` unpacked one level deeper); that is a path adaptation, not a different rule. octtest does not turn `\` into `/` and does not memoise (irrelevant offline).
  - DataMap callback (`choAlias add; dbTrigger10Add; normalise`, 1085-1089) exists in three partial copies: 2689, 2702, 3131-3136.
  - Mission chain: the plugin applies `luaEvents30`/`progress30`/`taskLines30` only when `t.compare(0, 9, "Mission =") == 0` (904-908); `--mission` applies them always (2761). Settled the reviewer's UNVERIFIED: `luaEvents30` has no own guard (`mission.hpp:43-50` remaps any line-opening `Event = N`), so on a non-`Mission =` file octtest changes bytes the plugin leaves alone; `progress30`/`taskLines30` only need `"\n\tParms = "` (`mission.hpp:79, 100`). `--mischain` does apply the gate (2770-2771). No mode runs the full `activityTasks30 → luaEvents30 → progress30 → taskLines30` chain. `--missiongraph` runs only `behaviour30` (2780); the plugin first runs `missionGraph30` (892), which octtest cannot reach.
  - Challenge routing: `--challenge` (2984-2993) routes by file name instead of `gameplay::challengeTypePath` (857, 3673; header `gameplay.hpp:420`) and lacks the plugin's `t[0] != 0x1b` (864-865), `"\nActivityIcon="` (866), the `dna && t[0] == 0x1b` gate (878) and the `try` around `dnaGadgets30` (880). The `--activity` reader lambda (3029-3034) copies 870-874.
  - Particle shaders: `--pd` (3151-3166) copies `particleShaders` (795-818) and the `cm` lambda (1061-1063) without the `"BNDL"` check (805); octtest returns 1 on bundle failure, the plugin serves without a channel map.
  - `--ai`'s `exists` is `fs::exists(scripts / filename)` (2592); the plugin's is `read10s(p)` (1105). `--sx` passes `<dir>/<file>` (2615), the plugin `norm(path)` (1096); `sx::transcode` appends the path to its output (`sx.hpp:43-47`), so bytes differ by path. UNVERIFIED whether any shipped layout makes them equal; settle by running `--sx` on the extract and the plugin on the same file and diffing.
  - Path normalisation copied at 2960 and 3302; plugin `norm` (478-484) also strips `assets/`.
  - `zones10`: the `world` lambda (3338-3341) is the plugin's (2627-2630) with `inst` for `g_cfg.install10`; octtest reads `startup2.zip` directly (3330-3336), the plugin uses `read10s` (2622); the step order zones10 → zonelist30 → beaconMap30 (3343, 3357) copies 3611-3614 without the `challenge10`/`beacon10` gates (defaults true) and without `currencies10`.
- **Why it matters:** Each copy is a place where offline and in-game behaviour can diverge unnoticed; the mission-gate divergence already exists.
- **Fix (behaviour-preserving):** Move each pure piece into a header function taking reader/predicate parameters (e.g. `mission::chain(t, events, progress)` including the `Mission =` gate; `gameplay::challengeKind(np, t)`); both sides call them; plugin log notes unchanged.
- **Refactor risk:** Low per item; the mission gate and the challenge `0x1b` gates are behaviour and must move intact.
- **Verification:** CORRECTED: the spark-prefix difference is a layout adaptation (not a different rule); the mission UNVERIFIED is settled (the gate is load-bearing); added the missing `dna && t[0] == 0x1b` gate; severity fixed at Med.

### OT-17 `--readable` tests only one of the plugin's two readability checks
- **Where:** `native/di1compat/octtest.cpp:2455-2526`; `native/di1compat/di1compat.cpp:1228-1241` (used at 1246); `native/di1compat/sys.hpp:180-196`
- **Severity / category / class:** Med · coverage · SAFE-REFACTOR
- **Evidence:** `readableTest` compares `sys::readable` with a VirtualQuery lambda (2457-2463). The plugin's own `static bool readable` (1228-1241, added in 684539b for the stack walk in `afterCall`, 1246) requires an explicit read-protection bit, `sys::readable` only rejects `PAGE_NOACCESS`/`PAGE_GUARD`. No test case uses `PAGE_EXECUTE` or `PAGE_EXECUTE_READ`, where the two can disagree. README:155 calls `--readable` "a self-test of the plugin's pointer checks". `--readable` passes on this machine (scratch build: `READABLE ok`, 0.41 µs per check).
- **Why it matters:** The latest crash fix's checker has no test, and it is the only native check that could run in CI.
- **Fix (behaviour-preserving):** Move the static `readable` into `sys.hpp` under a distinct name unchanged and add it (plus a `PAGE_EXECUTE` case) to the same table. Do not unify the two functions; that would change behaviour.
- **Refactor risk:** Low.
- **Verification:** CONFIRMED. Still UNVERIFIED: whether a `PAGE_EXECUTE` page faults on read under this OS/CPU (x86 paging has no read-disable bit, so likely not); a `VirtualAlloc(PAGE_EXECUTE)` + read case settles it.

### OT-18 Coverage that runs only when an environment variable or extra argument is given
- **Where:** `native/di1compat/octtest.cpp:270` (`ARC30_GAME`), 287 (`RENDER_SSF`), 309 (`RENDER_GENCHECK`), 317 (`RENDER_SKINTR`), 3154 (`--pd` with 8 args), 3289-3291 (`WARM_EXT30`)
- **Severity / category / class:** Med · coverage · DOC-ONLY
- **Evidence:** `gen30::generate` in `--render` only with `RENDER_GENCHECK`; `render::parseSsf` only with `RENDER_SSF`; `render::skinTarget30` only with `RENDER_GENCHECK`+`RENDER_SKINTR` or `--warm`+`WARM_EXT30`; `arc30` inside `--render` only with `ARC30_GAME`; `chan::layout` in `--pd` only with `argc > 7`; `cho::clips30` never with the plugin's `has30` predicate (always-true at 1602-1603, null at 1062/2964).
- **Why it matters:** A baseline recorded without these variants silently skips that code.
- **Fix (behaviour-preserving):** List each combination in the OT-27 manifest.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.
- **Done:** golden.py's manifest runs each of these env vars and extra arguments as its own case.

### OT-20 11 oracle scripts named by octtest or its headers are not in the repo
- **Where:** `native/di1compat/octtest.cpp:240-241, 1744, 2582, 2589, 2606, 2628, 2827, 2978, 3044, 3059`; `native/di1compat/npcrt.hpp:394`, `npc.hpp:162`; `native/di1compat/sm3.hpp:2`
- **Severity / category / class:** Med · test design · DOC-ONLY
- **Evidence:** Missing (no file in `git ls-files`): `aicheck.py` (2589), `challenge.py` (2978), `clip10.py` (1744), `ctx30.py` (241), `nodeflds.py` (240), `keycheck.py` (2582), `navconv.py` (2827), `surf.py` (3044), `sxcheck.py` (2606, 2628), `toolgate.py` (3059), and `skincheck2.py` (the `--skin` oracle, cited in `npcrt.hpp:394` and `npc.hpp:162`). Present: `renderoracle.py`, `fevcheck.py`, `mixercheck.py`, `vocheck.py`, `fevdeps.py`, `particles.py`, `gen30.py`, `oct.py`, `portmap.py`. `sm3.hpp:2` names `octtest --sm3`, which does not exist. The generator of the `pairs.txt` inputs (`--cho-clips` 1391, `--clip-ids` 1466, `--banm` 1652) is not documented.
- **Why it matters:** 11 modes' correctness evidence cannot be re-run; for those, only a golden baseline remains.
- **Fix (behaviour-preserving):** Record golden outputs (OT-27); note in each mode's usage line which oracle checks it and where it lives.
- **Refactor risk:** None.
- **Verification:** CORRECTED: 11 missing, not 10 (`skincheck2.py`); severity fixed at Med.

### OT-22 Timing assertions and run-to-run output noise
- **Where:** `native/di1compat/octtest.cpp:2521`, 2436-2450, 2349-2358, 348, 2949, 2520, 2446, 2006-2013, 1798, 3377-3382
- **Severity / category / class:** Med · flakiness / golden-diff noise · DOC-ONLY
- **Evidence:** `--readable` fails if a check costs > 50 µs (`bad += tNew > 50`, 2521); `--host` requires `tInstall < 1000` ms and waits 2 s (2436, 2450); `--vo` checks play position against the wall clock (2349ff.). Times printed by `--render` (348), `--oct` (2949), `--readable` (2520), `--host` (2446), `--fmod` `EVRUN` lines (2006, 2013). `--fmod` prints `EV` lines in `std::unordered_map byId` order (`audio.hpp:385`, octtest 1798). `--arc` writes progress to stderr (3377-3382).
- **Why it matters:** A naive golden diff will fail on every run.
- **Fix (behaviour-preserving):** In the harness, mask numeric `ms`/`us` fields and sort the `EV` lines; keep stderr separate.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.
- **Done:** golden.py masks timings per case, sorts unordered lines (`EV`, `--arc`), keeps stdout and stderr apart.

### OT-28 No warning level (cl runs at /W1), no `/WX`, no `/permissive-`
- **Where:** `native/di1compat/CMakeLists.txt:1-12`
- **Severity / category / class:** Med · build config · NEEDS-VERIFICATION
- **Evidence:** Scratch `CMakeCache.txt`: `CMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc`, `CMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG`; generated Release flags `/DWIN32 /D_WINDOWS /EHsc /O2 /Ob2 /DNDEBUG -std:c++17 -MT` (DLL) / `-MD` (octtest). No `/W3` because CMP0092 is NEW under `cmake_minimum_required(VERSION 3.21)` (`Windows-MSVC.cmake:458-463`), so `cl` uses its default `/W1`. MSBuild's own default is `Level1` (`Microsoft.Cl.Common.props:119`). UNVERIFIED that CMake's VS generator writes no `WarningLevel`: the reviewer's "Visual Studio 17 2022" configure failed in the 137-character scratch path (MSBuild try-compile `.tlog` path over MAX_PATH, an environment fact), so only Ninja was observed. `/W4` re-tally of `w4.log` (my script): 65 warnings, 50 unique sites — octtest unit 38 (24 × C4996 `getenv`, C4456 at 1945/2005, 12 header), `di1compat.cpp` unit 15 (C4456 at 1016/1576/3563, same 12 header), third-party 12 (minhook 10, miniz 2 × C4132 because `miniz.c` is compiled once per target). Header warnings: `mer.hpp:146` C4100, `mer.hpp:221,224` C4244 (×2 each), `gen30.hpp:524`/`cho.hpp:226` C4310, `dct.hpp:59` C4456, `audio.hpp:721,752` C4458, `gameplay.hpp:545` C4018, `mission.hpp:141` C4701. `cl /Zs /permissive- /W4` on both sources: 0 errors (`perm_*.log`). All first-party sources are pure ASCII.
- **Why it matters:** New real warnings (shadowing, narrowing, sign compare) go unseen.
- **Fix (behaviour-preserving):** `/W4` and `/utf-8` on first-party targets are codegen-neutral (SAFE). `/D_CRT_SECURE_NO_WARNINGS` for octtest. `/permissive-` turns on two-phase lookup and other conformance rules that can in principle pick a different overload without any diagnostic; `/Zs` only proves it compiles. Adopt it only after comparing the Release DLL's normalised disassembly with and without it. `/WX` later.
- **Refactor risk:** Low for `/W4`/`/utf-8`; `/permissive-` needs the binary comparison.
- **Verification:** CORRECTED: `/permissive-` is not proven codegen-neutral by a clean `/Zs` (risk raised from "checked clean"); MAX_PATH kept only as the reason the VS-generator point stays UNVERIFIED.

### OT-29 Release builds have no PDB; RelWithDebInfo is not the same code
- **Where:** `native/di1compat/CMakeLists.txt`
- **Severity / category / class:** Med · build config / diagnosability · NEEDS-VERIFICATION
- **Evidence:** Release: no `/Zi`, linker `/machine:X86 /INCREMENTAL:NO` only. `dumpbin /headers` on the scratch DLL: the debug directory has only a `coffgrp` entry, no `cv` (CodeView) entry; no `.pdb` file. RelWithDebInfo: `/Zi /O2 /Ob1` and `/debug /INCREMENTAL` (less inlining than `/Ob2`; `/DEBUG` and `/INCREMENTAL` turn the linker's default `/OPT:REF,ICF` off). CrabeLoader minidumps of the shipped DLL therefore cannot be symbolised.
- **Why it matters:** Crash reports (the project's main in-game diagnostic) are unreadable for the plugin's frames.
- **Fix (behaviour-preserving):** Release: compile `/Zi`, link `/DEBUG /OPT:REF /OPT:ICF /INCREMENTAL:NO` (restating `/OPT` keeps COMDAT folding/removal as today); do not ship the PDB.
- **Refactor risk:** Low, but `.text` will not be byte-identical: the CodeView record goes into `.rdata`, shifting later `.rdata` data, so absolute address operands in `.text` change. Verify with section sizes plus disassembly with addresses normalised, not with `dumpbin /headers` alone.
- **Verification:** CORRECTED: the proposed verification ("`.text` unchanged") would fail for a benign reason; replaced.

### OT-34 CI does not build or test native code
- **Where:** `.github/workflows/check.yml:8-33`; `README.md:157-158`; `native/di1compat/CMakeLists.txt`
- **Severity / category / class:** Med · CI · SAFE-REFACTOR
- **Evidence:** One `ubuntu-latest` job: pyflakes, imports, `--help`. No Windows job, no `cmake --build`, no `octtest --readable` (the only mode needing no data); no `enable_testing()`/`add_test`. README:157 says CI "checks everything that needs no game data", which is not true for the native code. The workflow header (lines 3-4) says it is also read by Gitea/Forgejo Actions and "needs ... a runner registered on the server".
- **Why it matters:** A refactor that breaks the native build or `sys::readable` is not caught.
- **Fix (behaviour-preserving):** A Windows job: configure `-A Win32` (or Ninja with `-DCMAKE_BUILD_TYPE=Release` from a vcvars32 shell), build Release, run `octtest --readable`. Allow a tolerance or an env var for the 50 µs timing check (OT-22).
- **Refactor risk:** None to the code; on Gitea/Forgejo a `windows-latest` job needs a registered Windows runner or it stays queued; keep it optional or GitHub-only until one exists.
- **Verification:** CONFIRMED (added the Gitea/Forgejo runner caveat).

### OT-06 Global and static mutable state in octtest
- **Where:** `native/di1compat/octtest.cpp:435-436`, 308, 1838-1839, 2331-2333, 2425-2426, 2685
- **Severity / category / class:** Low · coupling (test) · SAFE-REFACTOR
- **Evidence:** `g_fieldTally`, `g_skipWhy` (435-436) filled by `countsCheck`, printed in `main` (3421-3422); static locals `cf` (308), `calls`/`lastPid` (1838-1839), `dur`/`ends`/`endAt` (2331-2333), `dummy`/`hooks` (2425-2426); `main` sets `octx::g_variantGroups` (2685) and the audio singletons are mutated throughout. The statics exist because captureless lambdas are passed as C callbacks.
- **Why it matters:** Minor; one mode per process, so no cross-mode leakage today.
- **Fix (behaviour-preserving):** Context structs where no C callback is involved; keep statics for the callbacks.
- **Refactor risk:** Low.
- **Verification:** CONFIRMED.

### OT-14 FMOD calls bypassing `audio::Api`; three copies of the iterator layout
- **Where:** `native/di1compat/octtest.cpp:1814-1815`, 1830-1831, 1900-1901, 1929-1930, 1969-1979, 1995-1996, 1954-1963, 2125-2132, 2215-2226; `native/di1compat/audio.hpp:122-153`, 857-858
- **Severity / category / class:** Low · duplication · NEEDS-VERIFICATION
- **Evidence:** `struct Entity`/`struct Iter` at 1814-1815, 1830-1831 and `audio.hpp:857-858`. Local `GetProcAddress` bindings for `FMOD_Event_GetState`, `FMOD_System_GetChannelsPlaying`, `FMOD_Event_GetNumParameters`, `GetParameterByIndex`, `EventParameter_GetInfo/GetValue/GetRange`, `FMOD_System_GetWaveData`. Category-chain walk 3 times (the 2125-2132 copy has no depth limit and another format).
- **Why it matters:** Small duplication; offline-only code.
- **Fix (behaviour-preserving):** NOT via `AUD_E`/`AUD_X` as the report proposed: in `Api::load` a missing symbol sets `ok = false` (`audio.hpp:128-131`) and `load` returns false, so adding bindings there could make the plugin's audio host fail to start on a DLL lacking one. Bind these as optional members outside `ok` (e.g. a second getter that does not touch `ok`), and move the iterator types into `audio.hpp`.
- **Refactor risk:** Low with optional binding.
- **Verification:** CORRECTED: the proposed `AUD_E`/`AUD_X` fix is not behaviour-preserving; rewritten.

### OT-15 Code repeated inside octtest itself
- **Where:** `native/di1compat/octtest.cpp` (lines below)
- **Severity / category / class:** Low · duplication (test) · SAFE-REFACTOR
- **Evidence:** extract-reader lambdas ~11 (92, 93, 273-274, 301, 3032, 3152, 3153, 3184, 3185, 3212, 3244); table loads 8, of which 6 `render::Table` (266, 3156, 3181, 3210, 3240, 3368) and 2 `mer::Table` (2737, 3004); `gen30::vformats(slurp(...))` 5 (292, 305, 3213, 3245, 3289); MATP name parser 2 (54-68, 3275-3284); surfacetypes parser 2 (3047-3050, 3074-3077); clip-descriptor walkers 4 (1400, 1405, 1472, 1550); ClipDataBlock payload extraction 5 (1434, 1498, 1513, 1635, 1752); corpus listing with `motions/` 3 (805-813, 863-871, 1584-1591); inline `oct::MAGIC` checks 8 (767, 768, 816, 874, 1597, 3099, 3103, 3403) besides the helper `scene::tuple` (963); `dct` probe loop 2 (229-235, 2799-2805); pairs readers 3 (1437, 1488, 1714); lower-case loops 14 lines; `std::ofstream(..., binary)` temporaries 40; whole-file readers `slurp` (31), `audio::slurp` (2264, 2277), inline at 3401-3402.
- **Why it matters:** Readability of the test driver only.
- **Fix (behaviour-preserving):** A test-only `octtest_util.hpp`, extracted without changing any output string.
- **Refactor risk:** Low.
- **Verification:** CORRECTED: 2 of the 8 table loads are `mer::Table`; 8 inline MAGIC checks, not 9 (963 is the helper itself).

### OT-21 Expected values for one data set are hard-coded
- **Where:** `native/di1compat/octtest.cpp:152, 227, 237-238, 1841-1872, 2277, 2287-2288, 2323-2326, 2385, 2444`
- **Severity / category / class:** Low · readability / portability · SAFE-REFACTOR
- **Evidence:** `--ui`: sequence string (152), `kept == 2534` (227), 28 `Load_Tips_Incredibles_%.2d` (237-238). `--vo`: Edna (2323-2326, 2385), `LINEEVENT` table (2287-2288), `"\\3.0_assets_sound_dialog_"` (2277). `--fmod`: `billboardStart`, `fire_loop`, music/cinematic names (1841-1872). `--host`: `0.707946f` (2444). No absolute paths; 22 env vars; mixed `"\\assets\\..."` string joins (2256, 2260, 2273, 2282, 3307, 3330, 3339) vs `std::filesystem`.
- **Why it matters:** Only valid for one install/extract; readers cannot tell data facts from rules.
- **Fix (behaviour-preserving):** Named constants with a source comment each.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### OT-23 Stale or misplaced comments, cryptic names, missing includes
- **Where:** `native/di1compat/octtest.cpp:1-3, 26, 1547-1550, 1594, 1759-1764, 2143-2157, 3142-3146, 1097/1110, 1552/1573`; `native/di1compat/CMakeLists.txt:10`; `README.md:150-155`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:** File header documents 2 of 70 modes (1-3); `CMakeLists.txt:10` likewise describes octtest as only the round trip; README:150-155 lists 5. Comments above the wrong code: `--pd` doc (3142-3144) above `--vfxchan` (3145); `--cho-file` doc (1547-1549) above `parses30` (1550); `--fmod` doc (1759-1763) above `stdoutLog` (1764); `--vo` doc above `voPlay` (2143-2156). Counters `tc, tl, ti, tt, tb, ta, ts, tr, tk` (1594). Dead values `(void)conv` (1110), `(void)nSec` (1573). `#include <cstdarg>` between project headers (26); `<map>`, `<set>`, `<algorithm>`, `<functional>`, `<unordered_set>`, `<tuple>`, `<mutex>`, `<windows.h>` arrive only transitively (only `<chrono> <cstdio> <filesystem> <fstream> <iterator> <sstream>` are direct, 4-9). `/W4` C4456 at 1945/2005 (`auto it` hides the `Iter it{}` of 1831).
- **Why it matters:** Readability.
- **Fix (behaviour-preserving):** Move comments, add includes, rename counters (keep printed labels).
- **Refactor risk:** None.
- **Verification:** CONFIRMED (added `CMakeLists.txt:10`).

### OT-24 Test-only input handling that can crash or hang
- **Where:** `native/di1compat/octtest.cpp:2713`, 229-235, 2799-2805, 511, 522, 327-332, 3198-3205
- **Severity / category / class:** Low · robustness (test) · BEHAVIOUR-CHANGE (octtest only)
- **Evidence:** `--texname` `name.resize(name.size() - 4)` (2713) underflows on a name shorter than 4 (throws `length_error` → crash). `--dct`/`--ui` probe loops (2802, 231): unbounded when the table is full, `h % ns` divides by zero when `ns == 0`, `dct::u32` has no bounds check (`dct.hpp:40`, unlike `gen30::u32`, `gen30.hpp:42`). `--keep` dereferences `field(in, "Data")` unchecked (511, 522). `renderTest` never releases its D3D blobs or `HMODULE` (327-332, 310); `--zip` never calls `mz_zip_reader_end` (3198-3205). Env vars (22): `ARC30_GAME`, `ARC_SKIP`, `CHO_OUT`, `CHO_SHAPES`, `CUE_RUN`, `EV_GEOM`, `EV_LOAD`, `EV_RUN`, `EV_SECS`, `FMOD_CAPS`, `FMOD_SPEAKERS`, `GEN30_DUMP`, `GROUPS30_DNA`, `KEEP_OUT`, `OCTTEST_DUMP`, `RENDER_GENCHECK`, `RENDER_SKIN`, `RENDER_SKINTR`, `RENDER_SSF`, `RENDER_TRANSLATE`, `RENDER_VERBOSE`, `WARM_EXT30`.
- **Why it matters:** Malformed input crashes or hangs the test.
- **Fix (behaviour-preserving):** Guards change crash/hang into messages, i.e. octtest behaviour; separate commit after the baseline.
- **Refactor risk:** None for the plugin.
- **Verification:** CORRECTED: removed the `--vo` `argv[argc] == nullptr` bullet (the C++ standard guarantees `argv[argc]` is null, so `argv[4]` with `argc == 4` is well-defined).

### OT-25 CPU: quadratic counts check, repeated corpus walks
- **Where:** `native/di1compat/octtest.cpp:446-454`, 763/807/865, 1043/1053
- **Severity / category / class:** Low · performance (offline only) · SAFE-REFACTOR
- **Evidence:** `countsCheck` copies the whole `oct::File` (`oct::File g = f;` 446) and rewrites it (`g.write() == b` 454) once per SubNetwork → O(#SubNetworks × file size). `choIndex(a[3], sub)` is walked at 1043 and again at 1053; `choIndex` at 763 and again at 807/865.
- **Hot path?:** code defaults: not plugin code; recommended config: not plugin code (offline test only).
- **Why it matters:** Slower corpus runs only.
- **Fix (behaviour-preserving):** Optional: compare only the edited SubNetwork's byte range; cache the corpus index per call.
- **Refactor risk:** None if output strings stay.
- **Verification:** CONFIRMED.

### OT-26 octtest mixes three jobs
- **Where:** `native/di1compat/octtest.cpp` main
- **Severity / category / class:** Low · design · SAFE-REFACTOR
- **Evidence:** Acceptance tests with exit codes; converters for external oracles (`--sxkeys`, `--cho-clipdump`, `--skin`, `--challenge`, `--nav`, `--surf`, `--tools`, `--ai`, `--sx`); interactive probes (`--zip`, `--member`, `--read10`, `--voplay`, `--fmod` with `EV_RUN`/`CUE_RUN`).
- **Why it matters:** Users of the tool cannot tell which modes assert anything.
- **Fix (behaviour-preserving):** Group modes by job in the dispatch table and usage text.
- **Refactor risk:** None.
- **Verification:** CONFIRMED (severity fixed at Low).

### OT-30 No `/GL`/LTCG or `/Gw` (`/Gy` is already on)
- **Where:** `native/di1compat/CMakeLists.txt`
- **Severity / category / class:** Low · build / performance · NEEDS-VERIFICATION
- **Evidence:** Release flags above have no `/GL`, no `/Gw`, link without `/LTCG`. `/Gy` is implied by `/O2` (`/O2` = `/Og /Oi /Ot /Oy /Ob2 /GF /Gy`).
- **Hot path?:** code defaults: speculative, no measurement; recommended config: speculative.
- **Why it matters:** Possible small gains for miniz/minhook calls from the C++ unit; nothing measured.
- **Fix (behaviour-preserving):** Optional `INTERPROCEDURAL_OPTIMIZATION` for Release. It changes inlining and layout (which also affects the profiler's stack-walk heuristics in `afterCall`), so it needs an in-game re-test.
- **Refactor risk:** Medium relative to its benefit; not recommended during the refactor.
- **Verification:** CORRECTED: `/Gy` is not missing (implied by `/O2`).

### OT-31 octtest uses the dynamic CRT; README says the CRT is static
- **Where:** `native/di1compat/CMakeLists.txt:9-12`; `README.md:99-100`
- **Severity / category / class:** Low · build config · SAFE-REFACTOR
- **Evidence:** `MSVC_RUNTIME_LIBRARY "MultiThreaded"` only on `di1compat` (9); generated flags: DLL `-MT` in every config (Debug too, not `-MTd`), octtest `-MD`. README:99-100 "The CRT is linked statically" is true for the DLL only; `octtest.exe` needs the VC++ runtime.
- **Why it matters:** octtest fails to start on a machine without the redistributable; docs inaccurate.
- **Fix (behaviour-preserving):** Set the same property on octtest. Leave the DLL's Debug CRT as is (switching to `/MTd` changes Debug builds).
- **Refactor risk:** Low.
- **Verification:** CONFIRMED.

### OT-32 miniz compiled twice; no include dirs or definitions; `NOMINMAX` does nothing
- **Where:** `native/di1compat/CMakeLists.txt:7, 11`; `native/di1compat/audio.hpp:17-20`; `native/di1compat/sys.hpp:4-7`; `native/di1compat/arc30.hpp:6`; `native/di1compat/di1compat.cpp:8`
- **Severity / category / class:** Low · build hygiene · SAFE-REFACTOR
- **Evidence:** `third_party/miniz/miniz.c` in both targets (two objects, `-MT` and `-MD`). No `target_include_directories` (quoted includes resolve next to the including file). `NOMINMAX` in `audio.hpp`/`sys.hpp` comes after `<windows.h>` was already included — by `arc30.hpp:6` in octtest (included at octtest.cpp:16, before `sys.hpp` 21 and `audio.hpp` 25) and by `di1compat.cpp:8` — so the headers write `(std::min)`/`(std::max)`.
- **Why it matters:** Hygiene only.
- **Fix (behaviour-preserving):** A `miniz` static library (one per CRT, or after OT-31 one shared); `target_compile_definitions(NOMINMAX)` on first-party targets. `WIN32_LEAN_AND_MEAN` only if the build still succeeds (it can only fail to compile, not change code).
- **Refactor risk:** Low. An `OBJECT` library carries one CRT setting, so it cannot serve `/MT` and `/MD` targets at once.
- **Verification:** CONFIRMED.

### OT-33 Nothing enforces x86
- **Where:** `native/di1compat/CMakeLists.txt:3`
- **Severity / category / class:** Low · build config · SAFE-REFACTOR
- **Evidence:** Only a comment (3) and README:95 say `-A Win32`; the VS 2022 generator defaults to x64 on an x64 host. Settled the reviewer's UNVERIFIED: an x64 configure fails at `di1compat.cpp:3883`/`3896` (inline `__asm`, not supported by MSVC on x64), and MinHook would want `hde64` (`minhook/trampoline.c:41`), which is not in the source list. octtest, however, has no x86-only code and would build and run as a 64-bit program, testing 64-bit codegen of the headers (and failing to load the 32-bit FMOD DLLs).
- **Why it matters:** A CI job or script that forgets `-A Win32` gets a confusing compile error for the DLL and a silently different octtest.
- **Fix (behaviour-preserving):** `if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4) message(FATAL_ERROR "Win32 only (-A Win32)")`.
- **Refactor risk:** None.
- **Verification:** CORRECTED: the UNVERIFIED is settled; added the x64 octtest hazard.

### OT-35 No install step; README's `build/` folder not git-ignored
- **Where:** `README.md:95`; `.gitignore:10`; `native/di1compat/CMakeLists.txt`
- **Severity / category / class:** Low · build hygiene · SAFE-REFACTOR
- **Evidence:** No `install()`; README describes a manual copy (106). README:95 builds into `<repo>/build/`; `.gitignore` has only `native/**/build/`; `git check-ignore -v build/x` matches nothing (exit 1).
- **Why it matters:** Following the README leaves an untracked tree in the repo root.
- **Fix (behaviour-preserving):** Add `/build/` to `.gitignore`; optionally `install(TARGETS di1compat RUNTIME DESTINATION Pentane/Plugins)`.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### OT-36 `/EHsc` with SEH `__try` (document, do not change)
- **Where:** `native/di1compat/di1compat.cpp` (10 × `__try`, `catch (...)` at 3717); `native/di1compat/sx.hpp` (2 × `__try`, `catch (...)` at 673); `native/di1compat/octx.hpp:1013`; `native/di1compat/octtest.cpp:1441, 1492`
- **Severity / category / class:** Low · documentation · DOC-ONLY
- **Evidence:** Under `/EHsc`, `catch (...)` does not catch SEH exceptions; SEH is handled only inside `__try` (and the project avoids faulting at all because CrabeLoader dumps first-chance AVs).
- **Why it matters:** A refactor must not "fix" this by switching to `/EHa` (changes unwinding and which faults `catch (...)` swallows) or by merging `__try` functions into ones needing unwinding (C2712).
- **Fix (behaviour-preserving):** Document the invariant next to the CMake flags. Do not switch to `/EHa`.
- **Refactor risk:** None.
- **Verification:** CONFIRMED.

### OT-37 Config example shows 4 of 105 keys; unknown keys silently ignored
- **Where:** `native/di1compat/di1compat.cfg.example`; `native/di1compat/di1compat.cpp:239-366` (chain 251-354); `native/di1compat/di1compat.def`
- **Severity / category / class:** Low · usability · BEHAVIOUR-CHANGE (log-only)
- **Evidence:** The example has `install10`, `serve10 = 1`, `shaders10 = 1`, `trace = 0` (matches README's recommended config). `loadConfig` has 105 keys and no final `else`, so a typo such as `shader10` is ignored silently. `di1compat.def` is required and correct: there is no `dllexport` in the source; the `.def` exports `Pentane_PluginInformation DATA`, `Pentane_PluginDependencyCount DATA` and the undecorated `__stdcall Pentane_Main` (`di1compat.cpp:60, 64, 4097-4098`); `dumpbin /exports` on the scratch DLL lists exactly those three.
- **Why it matters:** Misconfiguration is invisible.
- **Fix (behaviour-preserving):** Logging unknown keys adds log lines (external scripts grep `di1compat.log`), so it is a small, additive behaviour change for the user to approve.
- **Refactor risk:** Very low.
- **Verification:** CORRECTED: 105 keys (not "about 80"); class is a (log-only) behaviour change.

## Inventory

### Functions in `octtest.cpp` (trimmed: functions over 40 lines, plus globals; all ranges re-scanned mechanically)
49 functions in total (`main` included); ranges of the shorter ones match the original Table 1.

| Function | Lines | Len | Mode(s) |
|---|---|---|---|
| `pdEmitters` (+ `struct PdEmitter` 41) | 42-86 | 45 | `--vfxchan` |
| `vfxChanTest` | 88-130 | 43 | `--vfxchan` |
| `uiTest` | 135-256 | 122 | `--ui` |
| `renderTest` | 260-362 | 103 | `--render` |
| `countsCheck` | 437-478 | 42 | `--counts` |
| `choAnimTest` | 756-921 | 166 | `--cho-anim` |
| `choConstTest` | 973-1212 | 240 | `--cho-const` |
| `variantsTest` | 1219-1320 | 102 | `--variants` |
| `choSceneTest` | 1322-1389 | 68 | `--cho-scene` |
| `choClipsTest` | 1393-1464 | 72 | `--cho-clips` |
| `clipIdsTest` | 1470-1545 | 76 | `--clip-ids` |
| `choFileTest` | 1579-1650 | 72 | `--cho-file`, `--cho-corpus` |
| `fmodTest` | 1773-2084 | 312 | `--fmod` |
| `mixerTest` | 2090-2141 | 52 | `--mixer` |
| `voPlay` | 2157-2248 | 92 | `--voplay` |
| `voTest` | 2250-2388 | 139 | `--vo` |
| `readableTest` | 2455-2526 | 72 | `--readable` |
| `main` | 2528-3427 | 900 | dispatch + 45 inline modes |

Globals: `g_fieldTally`, `g_skipWhy` (435-436, `--counts`). Header globals written by octtest: `octx::g_variantGroups` (2685); never written: `octx::g_slack` (OT-08).

### CLI modes and exit behaviour (re-derived from every return path)
Exit: **PF** = a failed check sets a nonzero exit (in some modes only part of the printed checks, noted); **SETUP** =
nonzero only on setup/input errors or when the code under test refuses outright (returns false); wrong bytes never fail;
**0** = always 0. Every mode exits 0xC0000409 on an uncaught exception. Totals: 27 PF, 19 SETUP, 24 always 0 (= 70).

| Flag | Line | Exit | Notes |
|---|---|---|---|
| *(roots)* | 3391-3426 | PF | passes with 0 files |
| `--counts` | 3391-3424 | PF | exceptions not counted (3408); 0 roots → exit 0 |
| `--readable` | 2530 / 2455 | PF | includes 50 µs timing check |
| `--host` | 2533 / 2416 | PF | timing (< 1000 ms) |
| `--fmod` | 2534 / 1773 | SETUP | verdicts printed only (`fevcheck.py`) |
| `--fevdeps` | 2535 / 2392 | SETUP | `fevdeps.py` |
| `--mixer` | 2536 / 2090 | SETUP | `mixercheck.py` |
| `--vo` | 2537 / 2250 | PF | + `vocheck.py` |
| `--voplay` | 2538 / 2157 | SETUP | probe |
| `--render` | 2539 / 260 | SETUP | 1 only if `transcodeWorld` fails; problems printed |
| `--vtxf` | 2540 / 367 | PF | |
| `--groups30` | 2541 | 0 | |
| `--currency10` | 2576 | 0 | |
| `--sxkeys` | 2581 | 0 | `keycheck.py` missing |
| `--ai` | 2588 | PF | refusals only; `aicheck.py` missing |
| `--sx` | 2605 | PF | refusals + flip names; `sxcheck.py` missing |
| `--keep` | 2641 / 498 | PF | |
| `--chest` | 2642 | SETUP | 1 if `chestInfoLua` refuses |
| `--perc` | 2653 | SETUP | 1 if `perceptionInfoLua` refuses |
| `--cho-layout` | 2673 / 566 | 0 | |
| `--cho-time` | 2674 / 590 | PF | |
| `--cho-clips` | 2675 / 1393 | 0 | |
| `--clip-ids` | 2676 / 1470 | PF | "NOT CONVERTED" only |
| `--cho-clipdump` | 2677 / 1745 | 0 | `clip10.py` missing |
| `--cho-file` | 2678 / 1579 | PF | |
| `--cho-corpus` | 2679 / 1579 | PF | |
| `--banm` | 2680 / 1709 | 0 | |
| `--cho-time10` | 2681 / 619 | 0 | |
| `--cho-anim` | 2682 / 756 | PF | |
| `--cho-scene` | 2683 / 1322 | PF | 3 named counters only |
| `--cho-const` | 2684 / 973 | PF | |
| `--variants` | 2685 / 1219 | PF | named counters + `bad` |
| `--cho-alias` | 2686 | 0 | |
| `--dbtriggers` | 2699 | 0 | prints "DO NOT match", exit 0 |
| `--ui` | 2710 / 135 | PF | |
| `--texname` | 2711 | SETUP | |
| `--chd` | 2720 | 0 | even on MISMATCH |
| `--mer` | 2735 | SETUP | 2 table, 1 refused |
| `--mission` | 2759 | 0 | ignores the plugin's `Mission =` gate (OT-13) |
| `--mischain` | 2766 | PF | |
| `--missiongraph` | 2778 | 0 | `behaviour30` only |
| `--beaconmap` | 2785 | 0 | |
| `--dct` | 2792 | PF | round trip; probe "not found" also 1 |
| `--btd` | 2807 | SETUP | |
| `--bent` | 2817 | SETUP | |
| `--nav` | 2826 | PF | `navconv.py` missing |
| `--skin` | 2851 | 0 | `skincheck2.py` missing |
| `--dmeval` | 2868 | SETUP | |
| `--psdna` | 2880 | 0 | |
| `--playset` | 2898 | SETUP | |
| `--avperc` | 2909 | SETUP | |
| `--gamedb` | 2934 | SETUP | |
| `--oct` | 2944 | 0 | timings in output |
| `--challenge` | 2977 | 0 | `challenge.py` missing |
| `--activity` | 2999 | SETUP | 2 on table load only |
| `--surf` | 3044 | 0 | `surf.py` missing |
| `--tools` | 3059 | 0 | `toolgate.py` missing |
| `--gpscan` | 3070 | 0 | even on FAIL |
| `--datamap` | 3128 | 0 | |
| `--vfxchan` | 3145 / 88 | 0 | |
| `--pd` | 3146 | SETUP | `particles.py` |
| `--mtb` | 3179 | PF | problems not counted |
| `--zip` | 3197 | PF | probe |
| `--gen30` | 3207 | PF | |
| `--warm` | 3237 | PF | compile failures only; generator problems not counted |
| `--read10` | 3300 | 0 | stale (OT-09) |
| `--member` | 3318 | PF | probe |
| `--zones10` | 3328 | SETUP | |
| `--menu` | 3327 | SETUP | |
| `--arc` | 3365 | PF | |

Only `--readable` needs no game data or derived tables.

### Coverage: header function → octtest mode (re-grepped; header lines checked mechanically, all matched)
| Header | Function (line) | Plugin caller | octtest mode(s) |
|---|---|---|---|
| arc30 | `readMember` 93 | `read30x` | `--arc`, `--member`, `--menu` (archive), `--render` (`ARC30_GAME`) |
| bent | `transcode` 44, `resolve` 21 | yes | `--bent` |
| btd | `convert` 73, `btm` 88, `blocks` 20 | yes | `--btd` |
| chan | `layout` 292 | vfx `cm` | `--vfxchan`, `--pd` (8 args) |
| chan | `shaderRoles` 263 | — | `--vfxchan` (112) |
| chd | `lua` 131 | yes | `--chd`, `--keep`, `--chest`, `--perc` |
| chd | `compile` 138 | yes | `--chd` (exit 0) |
| cho | `clips30` 273 | `transcodeCho` | `--cho-file`/`--cho-corpus` (always-true motion); `--oct cho`, `--cho-const` (null motion) |
| cho | `links30` 312 | yes | `--cho-file`/`--cho-corpus` only |
| cho | `layout30` 915, `time30` 354, `anim30` 419, `scene30` 814 | yes | `--cho-layout`/`--cho-time`/`--cho-time10`/`--cho-anim`/`--cho-scene` resp., `--cho-file`, `--cho-const`, `--oct cho` |
| cho | `const30` 553 | yes | `--cho-const`, `--oct cho` |
| cho | `banm30` 852 | yes | `--banm` (exit 0) |
| cho | `Aliases::add` 880 / `resolve` 894 | read10, datamap | `--cho-alias` (exit 0) |
| cho | `clipBlock30` 183, `clipPorts` 242 | internal | `--cho-clips`, `--clip-ids` (1504), `--cho-clipdump`, via `clips30` |
| ports10 | `portMap` 197 | via `clipPorts`/`links30` | `--clip-ids`, `--cho-file` (indirect) |
| datamap | `normalise` 13 | yes | `--datamap`, `--cho-alias`, `--dbtriggers` |
| dct | `reseed` 109, `hashes` 129, `supplement` 142 | yes | `--dct`, `--ui` |
| gameplay | `curves30` 56 (+ `worldDilation30` 26) | yes | `--oct`, `--gpscan` |
| gameplay | `surfaces30` 81 | yes (engine table) | `--surf`, `--gpscan` (Lua-parsed table) |
| gameplay | `challengeTypes30` 218, `beacons30` 229 | yes | `--challenge` |
| gameplay | `zonelist30` 252 | yes | `--challenge`, `--zones10` |
| gameplay | `currencies10` 285 | yes | `--currency10` |
| gameplay | `playsets10` 322 | yes | `--zones10`, `--menu` |
| gameplay | **`challengeTypePath` 420** | 857, 3673 | **NONE** |
| gameplay | `apGivers30` 432, `activityGraph30` 504, `activityTasks30` 648, `activityGivers` 682, `dnaGadgets30` 697 | yes | `--challenge`, `--activity`, `--gpscan`, `--mischain` (tasks) |
| gameplay | `toolLevels10` 711, `levelAllows` 741 | gprt | `--tools` |
| gen30 | `generate` 565 (+ sm3.hpp) | `shader10` | `--gen30`, `--warm`, `--render` (`RENDER_GENCHECK`) |
| gen30 | `mergeVtxf` 261, `vformats` 157, `ventries`/`children`/`field`/`typeSizes` | yes | `--vtxf`, `--render`, `--gen30`, `--warm` |
| luachunk | `eval` 100, `rewrite` 32 | yes | `--currency10`, `--groups30`, `--zones10`/`--menu`; `--datamap`, `--cho-alias`, `--dbtriggers` |
| mer | `Table::load` 65, `transcode` 160, `mtc` 482, `addAliases` 88 | yes | `--mer` (+ `--aliases`, mtc arg), `--activity` |
| **mernode.hpp** | all | yes | **NONE** (not included in octtest) |
| mission | `luaEvents30` 43 | yes (gated 904) | `--mission` (ungated), `--mischain` (gated) |
| mission | `progress30` 77, `taskLines30` 98 | yes (gated) | `--mission` (ungated) |
| mission | `behaviour30` 176, `beaconMap30` 208 | yes | `--missiongraph`; `--beaconmap`, `--zones10` |
| npc | `navmesh30` 129, `psDna30` 603, `skinLayouts`/`skinRemap`, `dataMap`, `gamedb30`, `playsetText`, `coreExpendables10`, `percLists`/`percAdds` | yes / npcrt | `--nav`, `--psdna`, `--skin`, `--dmeval`, `--gamedb`, `--playset`, `--avperc` |
| oct | `File::*` | everywhere | default mode (codec acceptance) and every oct mode |
| oct | `dropPool` 125 | NavMeshPool drop (740) | only inside `realm30w` (`octx.hpp:1156-1160`) via `--oct`; the NavMeshPool drop branch **NONE** |
| octx | `addCounts` 116 | yes | `--counts` (slack 0 only) |
| octx | `addAllCounts` 1165, `addAssetPool`, `skin30`, `locator30`, `collision30` | yes | `--oct`, `--cho-const`, `--variants` (slack 0) |
| octx | `camera30` 778, `realm30w` 914 | yes | `--oct` only |
| octx | **`g_slack` ≠ 0 branches** (213-236) | plugin 4096 | **NONE** |
| render | `transcodeWorld` 765 | yes | `--render` |
| render | `transcodeBundle` 757 | yes | `--mtb`, `--pd` |
| render | `parseSsf` 226 | yes | `--render` (`RENDER_SSF`) |
| render | `skinTarget30` 458 | yes | `--render` (GENCHECK + SKINTR), `--warm` (`WARM_EXT30`) |
| sx | `transcode` 48 (+ `is10` 41) | yes | `--sx` |
| sx | `ai::bmx`/`btx`, `keyId`, `flipMotionName`, `eventTo30` | yes / — | `--ai`, `--sxkeys`, `--sx` |
| sx | **runtime** (`init`, `provide`, `bind`, `hkLink`, `hkCallByName`, `call30`, `stub`, natives, `keyCell`) | hooks | **NONE** |
| sys | `keepRecords`, `dbTrigger10Add`/`dbTrigger10`, `readable`, `chestInfoLua`, `perceptionInfoLua`, `groups30` | yes (`readable` 22×) | `--keep`, `--dbtriggers`, `--readable`, `--chest`, `--perc`, `--groups30` |
| sys | **`loaderUids`, `setLockUids`, `lockUids`, `perc30`, `snapshotPerc30`, `applyPopin10`** | engine memory | **NONE** |
| ui | `loadingScreen10`, `flashImageMovie`, `flashMovie`, `fmvClip`, `contextList10`, `mergeTips`; `playsetMenu10`; `texBundleAs` | yes | `--ui`; `--menu`; `--texname` |
| vfx | `transcode`, `p3d30`, `isPd10` | yes | `--pd` |
| vfx | **`p3dAlias` 212** | `indexZip10` (604) | **NONE** |
| audio | `install`, `annoSource`, `choreographyStarted`, `hkSetPos`/`hkListener`/`hkSrcPos`, host/music/vo internals | yes | `--host`, `--vo`, `--fmod`, `--mixer`, `--voplay` |
| audio | **`hkBankReq`, `hkPost`, `hkStop`, `hkUnreg`, `hkAction`, `hkFader`, `hkMusicState`, `hkSetState`, `hkMusicOnce`, `hkStackTick`, `hkQueuedPost`, `hkPostExt`, `hkLoadFile`, `hkDlgReq`; `Host::standIn`/`stopObject`/`pause`; `Vo::onLinePosted`/`lineOf`; `hostedMusic`** | hooks | **NONE** |
| **npcrt / gprt / vfxrt / bldrt / caprt** | all (incl. `npcrt::skin10` 396, a load-time step) | yes | **NONE** (not self-contained) |
| di1compat.cpp | `transcode()`, `transcodeOct`, `transcodeCho`, `transcodeAssetRender`, `missionGraph30`, `particleShaders`, `isSpark10`, `zones10`, `read10*`/`norm`, `compile10`, `padSmallDxt`, `loadConfig`, file-local `readable` | — | **NONE** (approximations only, OT-07/09-13) |

### Generic helpers in `octtest.cpp` (for cross-file duplicate matching)
| Name | Line | Purpose | Known duplicates |
|---|---|---|---|
| `slurp` | 31 | whole file → string, "" on error | `audio::slurp` (`audio.hpp:928`); `slurpFile` lambda (`di1compat.cpp:4141`); fread loop in `compile10` (3832-3837); inline at octtest 3401-3402 |
| `field` | 491 | named child of `chd::V` | `find_if` in `chd.hpp:120` |
| `sameTree` | 481 | deep equality of `chd::V` | — |
| `choIndex` | 533 | corpus index | `scene::files` 951; 3 `motions/` loops |
| `anim::kid` | 701 | child record by name | `octx::SnEdit::child` (`octx.hpp:456`); `kid` lambda `cho.hpp:459` |
| `anim::str1` | 707 | first name value of a child | `str1` lambda `cho.hpp:464` |
| `anim::flt` | 748 | float payload | memcpy at 628 |
| `anim::subnets` | 668 | header paths (deliberately independent oracle) | `octx::SnEdit` ctor (`octx.hpp:406-447`) |
| `scene::tuple` | 963 | `MAGIC` check | 8 inline copies in octtest; ≥4 in `di1compat.cpp` |
| `stdoutLog` | 1764 | log sink | `logf` (`di1compat.cpp:70`) |
| `parses30` | 1550 | 3.0 clip-block validator | `shape` (1405), `ids30` (1472), descriptor read (1400) |
| `canonBin`/`canonText` | 1654/1675 | canonical tuple text | `canon` lambda (852) |
| lambda `fnv` | 3250 | FNV-1a 64 | inline 319-320; `fnv64` (`di1compat.cpp:3779`) |
| lambda `blob` | 3252 | read+release ID3DBlob via vtable | 327-332 (no release); `compile10` (3844-3847) |
| lambda `world` | 3338 | 1.0 world exists | `di1compat.cpp:2627-2630` |
| lambda `ends` | 3088 | suffix test | `endsWith` (`di1compat.cpp:472`) |
| lambda `probe` | 229 | `dct` engine probe | 2799-2805 |
| extract readers `r10`/`r30`/`read10` | 92, 93, 273, 301, 3152, 3153, 3184, 3185, 3212, 3244 | extract reader | `read10s`/`read30x` (plugin) |
| lambda `run` | 1843 | Sleep(16)+tick loop | 2245, 2345, 2369, 2372 |
| timers `lap`/`t` | 2947 / 2510 | timing | chrono at 275-279, 300, 348 |
| lower-case loops | 148, 164, 197, 198, 230, 2275, 2549, 2800, 2960, 2985, 3031, 3087, 3302, 3397 | lower-case | `npc::lower` (`npc.hpp:361`), `audio::lower` (`audio.hpp:53`), `sx::ai::lower` (`sx.hpp:799`), `norm` (`di1compat.cpp:478`) |

### Build facts (scratch build, Ninja; verified)
- Release: `/DWIN32 /D_WINDOWS /EHsc /O2 /Ob2 /DNDEBUG -std:c++17` + `-MT` (DLL) / `-MD` (octtest); link `/machine:X86 /INCREMENTAL:NO` (+ `/DEF:di1compat.def` for the DLL). No `/W*`, `/Zi`, `/GL`, `/Gw`, `/permissive-`; `/Gy` and `/GF` implied by `/O2`.
- RelWithDebInfo: `/Zi /O2 /Ob1 /DNDEBUG`, link `/debug /INCREMENTAL`. Debug: `/Zi /Ob0 /Od /RTC1` with `-MT` (not `-MTd`).
- Exports: `Pentane_Main`, `Pentane_PluginDependencyCount`, `Pentane_PluginInformation`; machine x86; no CodeView debug entry.

## Recommended order for the behaviour-preserving refactor (verified)
1. Freeze the baseline (OT-27) with the OT-18 variants and OT-22 masking: the only way the 43 non-PF modes can catch a regression.
2. Move plugin-only pure logic into headers without edits (OT-07, OT-09 to OT-13, OT-16), plugin log notes byte-identical; then point octtest at it and re-baseline octtest on purpose.
3. Restructure octtest (OT-01, OT-05, OT-06, OT-15, OT-23); exit-code changes (OT-02, OT-03, OT-04, OT-24) as separate commits that re-baseline.
4. Build config: `/W4` and CRT/CI changes are codegen-neutral; `/permissive-` (OT-28), PDB (OT-29) and LTCG (OT-30) need binary comparison or in-game re-test.

## Rejected on verification
No finding is rejected outright. Sub-claims rejected:
- OT-24 "`--vo` relies on `argv[argc] == nullptr`" as a crash risk: the standard guarantees `argv[argc]` is null; well-defined.
- OT-13 "`--groups30` tests a different spark rule": the rule is the same; only the path prefix is adapted to the extract layout.
- OT-30 "no `/Gy`": `/Gy` is implied by `/O2`.
- OT-05 "90-line lambda `shapesOf`": it is 52 lines (990-1041).

# Rendering, shaders and particles (render.hpp, gen30.hpp, sm3.hpp, vfx.hpp, chan.hpp, vfxrt.hpp, shaders10 runtime) (`native/di1compat/render.hpp`, `native/di1compat/gen30.hpp`, `native/di1compat/sm3.hpp`, `native/di1compat/vfx.hpp`, `native/di1compat/chan.hpp`, `native/di1compat/vfxrt.hpp`, `native/di1compat/di1compat.cpp` 3758-4090)

Scope: every line of the six headers; di1compat.cpp 60-260 (logf, hook(), g_cfg), 380-530 (read10/read10z/read30x), 560-830 (render/transcodeOct/particleShaders), 905-925 and 995-1090 (.oct/.mtb/.particledata/.p3d cases), 3545-3575 and 3600-3756 (hkOpen: vtxf merge, serve10 gate, transcode try/catch), 3758-4100 (whole shaders10 runtime), 4100-4361 (Pentane_Main, hook install); octtest.cpp 45-82, 250-362, 2530-2545, 3140-3299; oct.hpp 25-145; arc30.hpp 85-150; audio.hpp 905-935; npc.hpp 170-190; gen30.py, autobundle.py, renderoracle.py, rentable.py (Q lines) where a claim depended on them. Engine claims were checked by disassembling the installed `DisneyInfinity3.exe` / `DisneyInfinity1.exe` with capstone (scripts in the scratchpad `verify-render` folder): FUN_0045c820, FUN_0045c5c0, FUN_0044d430, FUN_00461020 call sites, FUN_00476af0/00478710/00660170, 1.0 FUN_0040bac0/0040baf0 and 3.0 FUN_0040bb50/0040bb80.
Verification: 50 findings checked: 22 confirmed, 28 corrected, 0 unverified, 0 rejected; 2 new.

## Key facts (verified)

1. **Nothing in the six headers runs per frame or per draw.** Every render/gen30/sm3/vfx/chan entry point runs from `hkOpen` → `transcode` (di1compat.cpp:3708), and only on the 1.0 serve path: `!r && !blocked && g_cfg.serve10 && g_cfg.install10[0]` (3645). So they never run under code defaults (`serve10 = false`); under the recommended config they run once per served 1.0 asset at load time. Exception: `gen30::mergeVtxf`/`vformats` run on every open of `shaders/vformat.vtxf` when `shaders10` is on (3558-3567). The load-time hot loops are `convertVertex`/`decode`/`encode` (render.hpp:142-201, called at 919) and the per-index remap (903-907).
2. **vfxrt.hpp:**
   - `hkPkg` is installed whenever `vfx10 && vfx_universal10` (both default 1). It is therefore installed under code defaults **and** the recommended config, even with `serve10 = 0`. It runs on every particle-package request, which is cold.
   - The three spawn-trace hooks are installed only with `vfx_trace` (default 0 in both configs).
3. **Per-draw code next to this slice: `hkCloneRec` (di1compat.cpp:3924-3935), confirmed by disassembly.**
   - FUN_0045c5c0 is a find-or-clone. It walks the record chain via +0x0e, compares +0x10 with the vformat, and returns an existing clone. Otherwise it appends a record and calls FUN_0044d430, which calls `ID3D11Device::CreateInputLayout` (vtable +0x2c).
   - All 4 call sites are inside FUN_00461020 (0x4613c2, 0x461512, 0x46168f, 0x4619e5). Each is guarded by `cmp [pass+0x18], primVformat; je`, so it runs for every draw-pass whose primitive vformat differs from the pass's.
   - FUN_00461020 is reached from the renderer's job code (0x0047ca6a → FUN_00ab0ae0), which makes it per-frame draw submission.
   - Installed only with `shaders10` (absent under code defaults; per draw under the recommended config).
4. **`hkShaderByName` (FUN_0045c820) is not a per-draw path, per static evidence.**
   - The engine has 39 call sites. They sit in creation-time wrappers: FUN_00476af0 allocates a 0x20-byte handle (`call 0x406e80` with 0x20) per lookup; FUN_00660170 is a particle-system constructor that looks up 3 literal names; 0x00aae7a8 stores the result into a member.
   - Some particle-side callers (FUN_0066a8e0, FUN_0066c9a0) may run per spawn. That is UNVERIFIED; it would be settled by a per-second lookup counter in the fps log.
   - `linkShader` runs on every lookup when `lazyLink10` is on (default 1 in both configs, but only reachable with `shaders10`).
5. **Exceptions are caught at three points; raw out-of-bounds reads are not caught.**
   - `.oct`: `transcodeOct` has its own catch at di1compat.cpp:916-924, which sets `note = "oct parse failed: ..."` and serves the raw bytes.
   - Other transcodes: caught at 3714-3720 (`std::exception` and `...`).
   - `Table::load` runs in `Pentane_Main` (4152) with no `try`.
6. **The generated HLSL text is load-bearing.** It is the disk-cache key `fnv64(target + "\n" + hlsl)` (3830; octtest copies at 319-322 and 3250/3259). It is also scanned for `register(t` to build the pass masks (gen30.hpp:640-643), and string-matched by the debug views (3795-3828).
7. **The h131 signedness question is settled by disassembly (RE-33).**
   - 1.0 FUN_0040bac0 and 3.0 FUN_0040bb50 are byte-identical code: `imul eax,eax,0x83; movsx ecx,cl; add eax,ecx`, stopping at `n` or NUL. That is signed, so vfx.hpp matches the engine exactly.
   - The lower-casing variants (1.0 FUN_0040baf0, 3.0 FUN_0040bb80) call the CRT on the **sign-extended** char.
   - audio.hpp's unsigned `h131` differs from the engine for bytes 0x80 and above.

## Findings
| ID | Title | Sev | Class | Where |
|---|---|---|---|---|
| RE-01 | Python twins / parity harnesses drifted; header comments claim checks that cannot pass | High | SAFE-REFACTOR | sm3.hpp:1-2, gen30.hpp:1-4, render.hpp:1-4, octtest.cpp:258-362, 3207-3236 |
| RE-N1 | `g_recCs`: render thread enters it per draw while loader threads hold it across fflush'ed logs, CreateInputLayout and an O(shaders) scan | Med | NEEDS-VERIFICATION | di1compat.cpp:3918-4006 |
| RE-02 | Generated HLSL text is a hidden contract in four places | Med | DOC-ONLY | sm3.hpp:144-338, gen30.hpp:327-334/410-454/636-654, di1compat.cpp:3787-3850, octtest.cpp:318-322/3250-3262 |
| RE-03 | `render::transcodeMaterials` is a 256-line god function | Med | NEEDS-VERIFICATION | render.hpp:499-754 |
| RE-04 | `render::transcodeWorld` is a 216-line god function | Med | NEEDS-VERIFICATION | render.hpp:765-980 |
| RE-05 | A failing `transcodeWorld` leaves a half-converted `.oct` that is still served | Med | BEHAVIOUR-CHANGE | render.hpp:885/909/916/945-948/962; di1compat.cpp:648-652, 734-757, 915-920 |
| RE-06 | `sm3::toHlsl` is a 195-line god function | Med | NEEDS-VERIFICATION | sm3.hpp:144-338 |
| RE-07 | `chan::roles` is a 132-line god function | Med | NEEDS-VERIFICATION | chan.hpp:107-238 |
| RE-08 | `gen30::generate`/`buildShd`: anonymous 7-tuple and two-phase `.shd` write | Med | NEEDS-VERIFICATION | gen30.hpp:491-560, 565-656 |
| RE-10 | Same 1.0 `.shd` read and inflated up to 3x per material; 3.0 `.shd` reads uncached | Med | SAFE-REFACTOR | render.hpp:513/558/621/686/689 |
| RE-11 | Whole bundles copied into `srcs` per material | Med | SAFE-REFACTOR | render.hpp:478, 573-576, 595, 711-714, 726-737 |
| RE-14 | Large output buffers grow without `reserve` and are copied, not moved | Med | SAFE-REFACTOR | render.hpp:929-942; di1compat.cpp:655-658, 1038, 1044 |
| RE-18 | Table/install/data indices used unchecked (UB, not an exception) | Med | BEHAVIOUR-CHANGE | render.hpp:588, 595, 653, 717, 727, 731 |
| RE-19 | Geometry reads lack bounds checks | Med | BEHAVIOUR-CHANGE | render.hpp:846-871, 892-918 |
| RE-20 | `Table::load` not robust and runs at boot outside any `try` | Med | BEHAVIOUR-CHANGE | render.hpp:39, 54-109; di1compat.cpp:4152 |
| RE-21 | `sm3::parse`/`toHlsl` read out of bounds on malformed bytecode | Med | BEHAVIOUR-CHANGE | sm3.hpp:43, 54-69, 227-271 |
| RE-22 | `vfx::transcode` can read past a record when an offset points beyond `keys` | Med | BEHAVIOUR-CHANGE | vfx.hpp:142-151 |
| RE-23 | `parseBundle`/`parseSsf` trust counts and indices from the file | Med | BEHAVIOUR-CHANGE | render.hpp:226-279, 315-370, 409-413 |
| RE-27 | `.shd` layout offsets duplicated as magic numbers in five places | Med | SAFE-REFACTOR | gen30.hpp:53-91/497-559/651-652, render.hpp:513-515, chan.hpp:260-262/276, di1compat.cpp:4049-4053 |
| RE-32 | Helpers duplicated within the slice and across the codebase | Med | SAFE-REFACTOR | see finding |
| RE-33 | Two `h131`s disagree on char signedness; only vfx.hpp's matches the engine | Med | BEHAVIOUR-CHANGE | vfx.hpp:35-41; audio.hpp:921-926; vfxrt.hpp:79-84 |
| RE-38 | Shader-generator callbacks read hidden globals; `g_vf30`/`g_tails10` rewritten on every vtxf open | Med | NEEDS-VERIFICATION | di1compat.cpp:3786-3830, 4035-4038, 4083-4090, 3558-3567 |
| RE-39 | `g_sh10Lock` held across generate + D3DCompile + disk I/O + engine registration; `linkShader` re-scans on every lookup | Med | NEEDS-VERIFICATION | di1compat.cpp:4027-4064, 3937-4006, 4068-4079 |
| RE-48 | `ventries` can loop forever on a truncated vtxf (boot-time hang) | Med | BEHAVIOUR-CHANGE | gen30.hpp:130-153 |
| RE-09 | `render::Table` mixes three domains and parses gen30's lines | Low | SAFE-REFACTOR | render.hpp:43-110 |
| RE-12 | Generic donor search not memoised; comparison key copies strings | Low | SAFE-REFACTOR | render.hpp:602-648 |
| RE-13 | Texture de-duplication is a linear search, written twice | Low | SAFE-REFACTOR | render.hpp:732-734, 744-748 |
| RE-15 | Per-vertex conversion does avoidable work (load-hot, bit-exact) | Low | NEEDS-VERIFICATION | render.hpp:39, 112-201 |
| RE-16 | `blockEnd` recomputed per node and in loop conditions | Low | SAFE-REFACTOR | render.hpp:834-836, 956, 960, 964 |
| RE-17 | Records classified by comparing name strings | Low | NEEDS-VERIFICATION | render.hpp:773-781, 835-856, 957, 961, 964, 977 |
| RE-24 | Pass-byte table (Q lines, `passByte`) is effectively dead | Low | DOC-ONLY | gen30.hpp:39, 530-531, 646-654; render.hpp:81-84 |
| RE-25 | Dead code, dead fields, unreachable branches | Low | SAFE-REFACTOR | gen30.hpp:342-343/362/429/435/443/630, render.hpp:10/222/233/429, sm3.hpp:17/113-131 |
| RE-26 | Test switches compiled into the shipping shader-compile path | Low | SAFE-REFACTOR | di1compat.cpp:3789-3828 |
| RE-28 | Pass-word bitfields have no names | Low | SAFE-REFACTOR | gen30.hpp:285-294, 504-505, 580, 609 |
| RE-29 | D3D9 opcodes are bare integers | Low | SAFE-REFACTOR | sm3.hpp:52-63, 154-155, 223-271 |
| RE-30 | `Binding30` register numbers lost the Python names | Low | SAFE-REFACTOR | gen30.hpp:428-438, 522 |
| RE-31 | `parseSsf` is cryptic | Low | SAFE-REFACTOR | render.hpp:226-279 |
| RE-34 | Emitter mesh hash vs `.p3d` alias: 8-char and case edge cases | Low | DOC-ONLY | vfx.hpp:86, 212-220; di1compat.cpp:602-604 |
| RE-35 | `vfxrt.hpp` compiles only by include position; statics in a header | Low | SAFE-REFACTOR | vfxrt.hpp:1-126; di1compat.cpp:4092 |
| RE-36 | `hkPkg` holds `g_uniLock` across an engine call | Low | DOC-ONLY | vfxrt.hpp:38-50 |
| RE-37 | `vfxrt` hidden side effects (in-place lowering, map growth past the cap) | Low | SAFE-REFACTOR | vfxrt.hpp:65-84 |
| RE-40 | Cross-header invariants never stated in code | Low | DOC-ONLY | vfx.hpp:94/157/168; chan.hpp:242-256; render.hpp:475/717-719; di1compat.cpp:1059-1063 |
| RE-41 | `render::IO` mixes I/O callbacks, policy flags and data | Low | SAFE-REFACTOR | render.hpp:425-454 |
| RE-42 | Names collide across namespaces; one-letter names | Low | SAFE-REFACTOR | see finding |
| RE-43 | Diagnostics weaker than the Python twin's | Low | BEHAVIOUR-CHANGE | render.hpp:798, 903-904; gen30.hpp:583 |
| RE-44 | Shader-cache files written non-atomically, barely validated | Low | NEEDS-VERIFICATION | di1compat.cpp:3829-3849 |
| RE-45 | Global text pointers are mutable | Low | SAFE-REFACTOR | gen30.hpp:327; sm3.hpp:137, 142 |
| RE-46 | `chan::shaderRoles` cache computes outside its lock | Low | DOC-ONLY | chan.hpp:263-287 |
| RE-47 | Bitwise CRC-32 reimplemented | Low | NEEDS-VERIFICATION | gen30.hpp:466-475 |
| RE-49 | Small redundancies in `transcodeMaterials` | Low | SAFE-REFACTOR | render.hpp:554-557, 585, 663-672 |
| RE-50 | Lua-text scanning depends on exact formatting | Low | DOC-ONLY | gen30.hpp:161, 168, 220, 227 |
| RE-N2 | `hkShaderByName` → `shader10` has no exception boundary | Low | BEHAVIOUR-CHANGE | di1compat.cpp:4027-4079 |

### RE-01 The Python twins and parity harnesses have drifted; header comments claim checks that cannot pass
- **Where:**
  - `native/di1compat/sm3.hpp:1-2`, `native/di1compat/gen30.hpp:1-4`, `native/di1compat/render.hpp:1-4`
  - `native/di1compat/octtest.cpp:258-362` (`--render`), `:3207-3236` (`--gen30`)
- **Severity / category / class:** High · testing, stale documentation · SAFE-REFACTOR (test-only additions + comment fixes)
- **Evidence:**
  - **No `--sm3` mode.** sm3.hpp:2 says "octtest --sm3 checks the text against it". There is no `--sm3` mode: the mode list is octtest.cpp:2530-3391, and a grep for `sm3` in octtest.cpp finds nothing.
  - **`--render` does not compare.** render.hpp:3 says "octtest --render checks it byte for byte". `renderTest` only writes outputs (353-355). The comparison is a manual diff against `renderoracle.py` output (octtest.cpp:258-259). The oracle (`autobundle.resolve`/`_substitute`) has neither the block-A re-layout by parameter name (render.hpp:679-696; autobundle.py:189-197 only splices the first float4) nor the colour-buffer donor pass (render.hpp:613-648; autobundle.py:222-228 `max(ok)`).
  - **`--gen30` compares against gen30.py dumps, but gen30.py lacks C++-only behaviour:**
    - sampler 1 decoded through `ng10` (gen30.hpp:428, 444); gen30.py has `ENGINE_TEX = {0: 0, 1: 1}` (gen30.py:34) and `treg` (159-169);
    - the Blinn divide in the light sample (gen30.hpp:449-451); gen30.py:172-175 lacks it;
    - the pass-mask overwrite of +0x1c/+0x1d (gen30.hpp:646-654); gen30.py:304 writes only `b1c`;
    - skinned passes (gen30.hpp:350-356, 388-391, 584-593); no "skin" in gen30.py or sm3.py;
    - the depth-only "quiet" PS (sm3.hpp:309-313); no `void main` in sm3.py.
  - In addition, `--gen30` calls `generate(..., nullptr, g)` with no `skinTarget` (3219), so even the C++ skin path has no harness.
- **Why it matters:** "identical bytes / identical HLSL" cannot be proven with the existing tools. A refactor regression in these paths would go unnoticed.
- **Fix (behaviour-preserving):** before any refactor, add golden-snapshot modes from the *current C++*:
  - every 1.0 shader's `.shd` + per-program HLSL, with and without `skinTarget`;
  - `--render` outputs under the `RENDER_TRANSLATE`/`RENDER_SKIN`/`RENDER_SKINTR`/`RENDER_SSF` permutations;
  - `--mtb`, `--pd`, `--vtxf`, `--vfxchan` outputs.

  Diff byte for byte after each step, then fix the three comments.
- **Refactor risk:** none at runtime. The risk lies in *not* doing this first.
- **Verification:** CONFIRMED.

### RE-N1 `g_recCs`: the render thread enters it per draw while loader threads hold it across fflush'ed logging, CreateInputLayout and an O(shaders) scan
- **Where:** `native/di1compat/di1compat.cpp:3918-3935` (`g_recCs`, `RecLock`, `hkCloneRec`), `:3937-4006` (`linkShader`), `:4068-4079`, `:70-84` (`logf`)
- **Severity / category / class:** Med · concurrency / lock held across I/O and engine calls · NEEDS-VERIFICATION
- **Evidence:**
  - **`hkCloneRec` holds `RecLock` around the engine clone** (3926-3927). It also reads `c[6]` and dereferences the shader name (3929) on *every* call, before testing `c[7]`. Per key fact 3, FUN_0045c5c0 runs per draw-pass with a vformat mismatch on the render thread. Its slow path also calls CreateInputLayout under the lock (engine behaviour, necessary).
  - **`linkShader` holds the same lock (3939) for:**
    - the scan over all registered shaders (3948-3950);
    - per fresh pass: six FUN_00450970 lookups (3971-3974) and `inputLayout10` → FUN_0044d430 → `ID3D11Device::CreateInputLayout` (3984);
    - `logf` calls. `"ours: %s pass %d ..."` is logged for **every pass of every translated shader**, ungated (3989-3991, `shaderIndex == 0x3ff` always holds for ours). Also `"fade probe"` (3956-3958), `"late link"` (4001) and `"linked %u shaders"` (4004).
  - `logf` takes `g_logLock` and `fflush`es every line (82).
  - `linkShader` runs on every shader lookup with `lazyLink10` (4077), on whichever thread calls FUN_0045c820 (loader threads per the comment at 3915-3917).
- **Hot path?:**
  - code defaults: none (`shaders10 = false`: no hooks).
  - recommended config: `hkCloneRec` runs per draw-pass with a vformat mismatch, every frame. Uncontended, that is one EnterCriticalSection/Leave pair. It is contended whenever a loader thread links a shader's first use (loading and streaming).
- **Why it matters:** the render thread can stall behind file I/O (one fflush per translated pass) and D3D object creation on another thread, which shows as frame hitches while streaming. Magnitude UNVERIFIED.
- **Fix (behaviour-preserving):**
  1. In `linkShader`, collect the log lines in a local vector and `logf` them after the `RecLock` scope. The text is unchanged; their position relative to other threads' lines may change.
  2. Compute `shaderIndex` lazily on the first fresh pass (also RE-39). Or compute it before taking the lock, but only after confirming the shader array at 0x0215CFC0/C8 has no writer after boot (FUN_0045c7f0 registration, `hkLinkAll`'s sort at 4013).
  3. In `hkCloneRec`, compute `nm` only inside `if (!c[7])`. This is byte-neutral.

  Do NOT move the record append, the count increment or `inputLayout10` out of the lock. They must stay atomic against the render thread's clone; see the 2026-10-13 freeze in the comment at 3915-3917.
- **Refactor risk:**
  - Keep `InitializeCriticalSection(&g_recCs)` before the three hooks (4230).
  - Never take `g_sh10Lock` while holding `g_recCs`. Today `hkShaderByName` takes them sequentially, not nested.
  - Log line order is part of the log contract: needs an in-game run.
- **Verification:** NEW (call graph verified by disassembly; contention magnitude UNVERIFIED: time the `RecLock` wait inside `hkCloneRec` under `fpslog10`/`loadprof10`).

### RE-02 Generated HLSL text is a hidden contract in four places
- **Where:**
  - `native/di1compat/sm3.hpp:144-338`
  - `native/di1compat/gen30.hpp:327-334, 410-454, 636-654`
  - `native/di1compat/di1compat.cpp:3787-3850`
  - `native/di1compat/octtest.cpp:318-325, 3257-3262`
- **Severity / category / class:** Med · coupling / frozen output · DOC-ONLY
- **Evidence:**
  1. **Cache key:** `sprintf_s(name, "\\%016llx.dxbc", (unsigned long long)fnv64(std::string(target) + "\n" + hlsl));` (3830). The key is computed from the *debug-view-swapped* text. The same formula is re-implemented at octtest.cpp:319-322 (`RENDER_GENCHECK`) and 3250/3259 (`--warm`). Both octtest copies accept any cached file over 32 bytes (325, 3262); `compile10` also requires the `DXBC` magic (3839), so the copies have already drifted.
  2. **Pass texture masks come from scanning the text:** `pr.hlsl.find("register(t", at)` (gen30.hpp:640-643). The NG10 declaration (emitted for samplers 1 and 2) declares t1; sampler 2 adds t3 and t4.
  3. **Debug views match generated identifiers:** `"tLight"` (3795, 3804, 3828), `"o.o1 = float4(0, 0, 0, 0);"` (3797, the text of gen30.hpp:332), `"SV_Target1"` (3803).
  4. **Output order comes from ordered containers:** sm3.hpp:147-150 (`std::map`/`std::set`); gen30.hpp:339-341 (`fmt`, `creg`, `used`); `cbuffers()` iterates `used` (414).
- **Why it matters:** any whitespace, order or naming change in emission has four effects:
  - it renames every cache file (a full recompile, and `--warm` caches stop matching);
  - it can change the mask bytes;
  - it can silently break the debug views;
  - it violates the user's byte-identical-HLSL rule.
- **Fix (behaviour-preserving):**
  - Document the text as frozen. Do not swap these containers for `unordered_*`; keep `pyrepr` (sm3.hpp:79-108) exact.
  - Optional SAFE-REFACTOR: move the key formula into one helper shared by the plugin and both octtest sites. That needs the octtest copies to keep their own (weaker) acceptance test, or a decision to add the DXBC check there.
  - Name the debug-view match strings as constants shared with gen30.hpp.
  - Deriving masks from `Binding30::sampler` calls changes no HLSL, but it may change `.shd` bytes; only with a golden diff.
- **Refactor risk:** very high if emission code is "cleaned up"; low if only the call structure around it changes.
- **Verification:** CORRECTED:
  - Severity High → Med: it is an invariant to document, not a blocker or a crash.
  - Class set to DOC-ONLY.
  - Added the octtest/plugin acceptance drift (no DXBC check in octtest).
  - Corrected the octtest line ranges.

### RE-03 `render::transcodeMaterials` is a 256-line god function
- **Where:** `native/di1compat/render.hpp:499-754`
- **Severity / category / class:** Med · god function · NEEDS-VERIFICATION (mtb bytes and `r.problems`/`io.log` order)
- **Evidence:** one function does all of the following:
  - `sizes3`/`sizes1` (502-519, `sizes1` caches 3 ints per 1.0 shader);
  - 1.0 bundle parse (521-523);
  - 3.0 bundle cache (525-539);
  - skin classification (547-557);
  - translated tier (558-583);
  - real tier (584-596);
  - same-sh/generic donor search with two passes (598-650);
  - slot substitution (653-677);
  - block A by parameter name (678-698);
  - verbose log (699-708);
  - TEXB merge (723-738);
  - SSET re-lay (739-749);
  - build (750-752).

  Nesting reaches 7 levels at 636: `for k`(542) → `else`(597) → `else`(602) → `for pass`(628) → `for donors`(629) → `for need`(634) → `for x`(636).
- **Why it matters:** tier precedence and side-effect order are hard to see. Every perf fix in RE-10..13 must thread through it.
- **Fix (behaviour-preserving):** extract `shd10Sizes`, `Bundle30Cache`, `classifySkin`, `tryTranslated`, `addReal`, `pickDonor`, `substituteSlots`, `relayBlockA`, `mergeSources` and `relaySceneSettings`, with `Result` as the accumulator.
- **Refactor risk:** keep the exact order of:
  - the `r.problems`, `r.tiers`, `r.shaders` and `r.translated` writes;
  - `io.read10`/`io.read30`;
  - `io.log`;
  - `io.translatable`. It has side effects: it generates, compiles and registers engine shaders (di1compat.cpp:641 → 4027-4064, under `g_sh10Lock`). It must stay evaluated only after the read and `parse10` succeed (`&&` short-circuit at 558-559).

  Every `continue` path pushes a problem (586 via `bundle30`, 649, 651), which is what makes 717 safe. Splitting must keep that.
- **Verification:** CORRECTED: severity High → Med (rubric: large god function = Med). All sub-ranges confirmed.

### RE-04 `render::transcodeWorld` is a 216-line god function
- **Where:** `native/di1compat/render.hpp:765-980`
- **Severity / category / class:** Med · god function · NEEDS-VERIFICATION
- **Evidence:** in one function:
  - pair scan (768-782);
  - material transcode (784);
  - vmap/smap (786-821);
  - scene-tree walk (834-859);
  - plan choice including the synthesised "same" plan and colour tail (870-899);
  - 16-bit remap (900-907);
  - vertex conversion (908-920);
  - stream-major layout (924-931);
  - index buffers and record rewrites (932-950);
  - pool sizes and the `IndexBufferPool` record insert (953-973);
  - RenderCaps removal (974-978).
- **Fix (behaviour-preserving):** extract `collectPrimPairs`, `mapFormats`, `mapSkinFormats`, `readPrimitives`, `planFor`, `remapIndices`, `convertPrimitive`, `layoutNode`, `patchBufferPools` and `dropMaterialRenderCaps`.
- **Refactor risk:**
  - In-place `f.recs` edits whose indices are cached in `ps[i]`. Only 971 (insert) and 977 (erase) change `recs.size()`, both after the node loop.
  - The append order of `nv`/`ni`/`ni2`.
  - `wide` is decided per node (934).
  - The failure returns mid-loop (RE-05).
- **Verification:** CORRECTED: severity High → Med.

### RE-05 On failure, `transcodeWorld` leaves the `.oct` half-converted, and it is still served
- **Where:**
  - `native/di1compat/render.hpp:885, 909, 916, 945-948, 962`
  - `native/di1compat/di1compat.cpp:648-652, 734-757, 915-920`
- **Severity / category / class:** Med · error-path correctness · BEHAVIOUR-CHANGE
- **Evidence:**
  - Each node's records are rewritten inside the node loop (945-948).
  - Later nodes can `return false` at 885, 909 and 916; after the loop, 962 can too.
  - `transcodeAssetRender` then skips the `g_served` stores (648-652). `transcodeOct` carries on, and its later steps set `changed` (737-756). `out = f.write()` is served (918-920).
  - The engine's next `.mtb` open goes through the stand-alone `.mtb` case (1003-1046, materials only), and the 1.0 `_0.vbuf`/`_0.ibuf` are served raw. The already-converted nodes therefore point at 3.0 vformat hashes and offsets of buffers that were never served.
  - Exceptions are not affected. The `.oct` catch (921-923) serves the raw file.
- **Why it matters:** garbage geometry (or a fault) for an asset whose conversion fails part-way.
- **Fix:** a behaviour change, limited to the failure path; the user must decide it. Two options:
  - convert into scratch vectors and commit record edits only after every node succeeds (same output on success);
  - have `transcodeAssetRender` snapshot `f.recs` and restore it on failure. That costs a full copy of `recs` on the success path too.

  The pure refactor must keep today's behaviour.
- **Refactor risk:** splitting can change which partial edits survive. Add a golden test with a truncated vbuf.
- **Verification:** CONFIRMED (added the `.mtb` follow-on path and the 921 catch).

### RE-06 `sm3::toHlsl` is a 195-line god function
- **Where:** `native/di1compat/sm3.hpp:144-338`
- **Severity / category / class:** Med · god function · NEEDS-VERIFICATION (HLSL bytes)
- **Evidence:** in one function:
  - declaration pre-scan (153-162);
  - **seven** lambdas: `reg`, `mod`, `s`, `scal`, `emit`, `maskOf`, `store` (164-219);
  - an opcode switch of 28 case labels covering 30 opcodes, plus 5 skipped (220-273);
  - VS shell including the `precise` and reversed-Z idioms (279-299);
  - PS shell including G-buffer and quiet depth-only output (300-317);
  - temp declarations (318-323);
  - epilogue (325-333);
  - join (335-337).
- **Fix (behaviour-preserving):** `scanDecls`, an `Emitter` holding `temps/body/depth`, `emitInstruction`, `emitVsShell`, `emitPsShell`.
- **Refactor risk:** HIGH, because the text is the cache key (RE-02). Emission is two-phase in **three** places, not just `temps`:
  - `temps` is filled by `reg()` (167) during the body and declared at 318-323;
  - `Binding30::used` is grown by `B.constant()` during the body and read by `B.cbuffers()` at 274;
  - `Binding30::needed` is filled by `B.inputConst()` (284) and read by `generate` after `toHlsl` (614-615).

  All of these must keep "emit body first, then shells".
- **Verification:** CORRECTED: seven lambdas (not six); 30 opcodes in 28 labels (not 25); added the `used`/`needed` ordering constraints.

### RE-07 `chan::roles` is a 132-line god function
- **Where:** `native/di1compat/chan.hpp:107-238`
- **Severity / category / class:** Med · god function · NEEDS-VERIFICATION (feeds served particle bytes)
- **Evidence:** in one function:
  - line split with copies (115-120);
  - SM4 signature `sscanf_s` (123-138);
  - SM3 `dcl_` (139-154);
  - taint pass with special cases for sincos, sample, dp*, matrix and nrm (157-216);
  - classification (217-237).

  Nesting is 6 deep at 202-203.
- **Fix (behaviour-preserving):** split into `parseSignatures`, `propagateTaint` and `classify`.
- **Refactor risk:** low. The result is cached by `shaderRoles` (263-287) and checked by `octtest --vfxchan` (octtest.cpp:3145).
- **Verification:** CONFIRMED.

### RE-08 `gen30::generate` (92 lines) and `buildShd` (70 lines) use an anonymous 7-tuple and a two-phase `.shd` write
- **Where:** `native/di1compat/gen30.hpp:491-560, 565-656`; the tuple at 492, 526, 575, 628, 639
- **Severity / category / class:** Med · readability / coupling · NEEDS-VERIFICATION (`.shd` bytes)
- **Evidence:**
  - Fields are read positionally: `std::get<3>(passes[k]) == pr.id || std::get<4>(passes[k]) == pr.id` (639).
  - `buildShd` writes +0x1c (531). `generate` then overwrites +0x1c/+0x1d of every pass inside a technique (651-652).
  - The program-to-pass link is recovered by id search (636-639).
- **Fix (behaviour-preserving):**
  - Use `struct PassOut { uint32_t word, vf, tech, vs, ps, vsSize, psSize; }`.
  - Record the pass index on `Program` at creation.
  - Compute the masks before `buildShd` and pass them in. Passes outside every technique must keep the `passByte` value, and +0x1d stays 0 there.
- **Refactor risk:**
  - The id search returns the first matching pass. Recording the index is identical unless two `progId`s collide (CRC-32 of distinct strings: negligible, but it is a difference).
  - The structured binding at 526 relies on the field order.
- **Verification:** CONFIRMED.

### RE-10 The same 1.0 `.shd` is read and inflated up to three times per material; 3.0 `.shd` reads are uncached
- **Where:** `native/di1compat/render.hpp:513, 558, 621, 686, 689`; callees di1compat.cpp:395-451, 486-495, 519-527; arc30.hpp:93-148
- **Severity / category / class:** Med · load-time CPU and I/O · SAFE-REFACTOR
- **Evidence:**
  - **1.0 reads per material:** 558 (translated attempt), 621 (generic tier), 689 (block-A re-layout). Only `sizes1` caches anything (508-519, three ints).
  - **3.0 reads:** 686 reads the 3.0 `.shd` for every same-sh/generic material.
  - **Cost of `read10z`:** it holds the process-wide `g_zipLock` for the whole candidate walk and inflation (407-438). That serialises all loader threads' 1.0 reads.
  - **Cost of `arc30::readMember`:** `CreateFileA`, a read and decrypt of the whole archive index, and the member inflate, all under the process-wide `cipherLock()` (95-145).
  - `io.translatable` → `generate` reads the `.shd` once more (gen30.hpp:569), but only on the first call per shader name (memoised in `g_sh10`).
- **Hot path?:**
  - code defaults: never (`serve10 = 0`).
  - recommended config: once per material of every served 1.0 asset with a bundle, at load.
- **Why it matters:** it scales with material count, and the global locks make it serialise other loader threads. Magnitude UNVERIFIED: `octtest --render` with `ARC30_GAME` prints the read count and ms (octtest.cpp:270-282, 348).
- **Fix (behaviour-preserving):**
  - Add a per-call memo `std::map<std::string, std::optional<Shd10>>` / `<ShdA30>` fed by the same `io.read*` calls in the same order.
  - Reuse the parsed `Shd10` for `sizes1` when `parse10` succeeds (`sizeA` 0x46, `sizeB` 0x6a, `slotsB.size()` = byte 0x69).
- **Refactor risk:**
  - `sizes1` accepts 0x6c bytes or more; `parse10` needs 0xa4 or more and a NUL after the slot names. Keep `sizes1`'s own fallback.
  - Do not memoise across calls: `read10` falls back to `g_choAlias.resolve` (401), which grows at runtime (1086). That is irrelevant for `shaders/` paths, but keep the memo per call anyway.
  - Do not reorder `io.translatable` before the reads.
- **Verification:** CONFIRMED (added the lock-serialisation aspect).

### RE-11 Whole bundles are copied into `srcs` for every material
- **Where:** `native/di1compat/render.hpp:478, 573-576, 595, 711-714, 726-737`
- **Severity / category / class:** Med · load-time CPU and memory · SAFE-REFACTOR
- **Evidence:**
  - `struct Src { Bundle b; int i; };` (478).
  - `srcs.push_back({*bnd, mi->second.idx});` (595) copies a whole 3.0 bundle for each real material.
  - `one.textures = b10.textures; ... srcs.push_back({one, 0});` (574-576, 712-714) makes two copies of the 1.0 texture list per material.
  - The merge reads only `s.b.mats[s.i]`, `s.b.slot()` and `s.b.textures` (726-737).
- **Hot path?:** code defaults: never; recommended config: per material of each served bundle, at load.
- **Fix (behaviour-preserving):**
  - Store `{const std::vector<std::string>* textures; Material m;}`, or `{const Bundle* b; int i;}` into `b10`/`cache30`. `std::map` nodes are stable, and both are locals that live past the merge (until 753).
  - Synthesised materials can live in a `std::deque<Material>`.
- **Refactor risk:** low (lifetimes as stated); 717 must read the same material.
- **Verification:** CONFIRMED.

### RE-14 The large output buffers grow without `reserve`, and are then copied rather than moved
- **Where:** `native/di1compat/render.hpp:929-942`; `native/di1compat/di1compat.cpp:655-658, 1038, 1044`
- **Severity / category / class:** Med · load-time CPU and 32-bit address space · SAFE-REFACTOR
- **Evidence:**
  - `nv += streams[i][s];` without a reserve (931). `buf.append((const char*)&v, wide ? 4 : 2);` per index (941).
  - `g_served[...] = r.mtb` / `r.vbuf` / `r.ibuf` / `r.ibuf2` are copies (655-658), although after them only `tiers`, `textures10`, `skinned` and `sceneSettings` are read (661-664).
  - In the `.mtb` case, `g_served[p] = r.mtb; ... out = r.mtb;` (1038, 1044) makes two copies where one move would do.
- **Hot path?:** code defaults: never; recommended config: once per served world, buffers tens of MB (render.hpp:181 "52 MB world").
- **Fix (behaviour-preserving):**
  - Reserve `nv` per node with the sum of `streams[i][s].size()`.
  - `buf.reserve(buf.size() + subs[i].size() * (wide ? 4 : 2))`.
  - `std::move` into `g_served` (test `empty()` before each move); in the `.mtb` case, copy once into `g_served` and move into `out`.
- **Refactor risk:** none (identical content).
- **Verification:** CONFIRMED (added the 1038/1044 copies).

### RE-18 Table, install and data indices are used unchecked (UB, not an exception)
- **Where:** `native/di1compat/render.hpp:588, 595, 653, 717, 727, 731`
- **Severity / category / class:** Med · robustness · BEHAVIOUR-CHANGE
- **Evidence:**
  - `Material dm = bnd->mats[donor->idx];` (653).
  - `bnd->mats[mi->second.idx]` (588, verbose).
  - `srcs.back().b.mats[srcs.back().i]` (717); `s.b.mats[s.i]` (727); `s.b.textures[t]` (731).
  - `idx` comes from di1compat_render.txt (built from a 3.0 *extract*), but the bundle is read from the user's install by `read30x` → `arc30`. A different 3.0 build puts `idx` out of range.
- **Fix:** use `.at()` or bounds checks that push a problem.
  - For `.oct`, the throw is caught at di1compat.cpp:921-923 (`note = "oct parse failed: ..."`, raw served).
  - For `.mtb`/particles, at 3714.

  Valid data is unchanged. For invalid data it trades UB for a raw serve plus a different log note, which is why this is a behaviour change.
- **Refactor risk:** none for valid data. Land it separately.
- **Verification:** CORRECTED:
  - The `.oct` catch is at 921 (not 3714).
  - Class: BEHAVIOUR-CHANGE (log and serve outcome change on mismatched data).

### RE-19 Geometry reads lack bounds checks
- **Where:** `native/di1compat/render.hpp:846-871, 892-918`
- **Severity / category / class:** Med · robustness (malformed or mismatched data) · BEHAVIOUR-CHANGE
- **Evidence:**
  - `f.ints()` returns `std::vector<int32_t>` (oct.hpp:154). `ida[1]`, `ida[3]`, `vda[0]` and `vda[3 + 3*s]`/`vda[4 + 3*s]` are indexed without size checks (866-918).
  - **Index reads:** `ib[2 * (ida[1] + q)]` (904) is int32 arithmetic (it can overflow) and is not checked against `ibuf10.size()`.
  - **Vertex offset:** `lo + plan.ss[s] > vbuf10.size()` (916) can wrap when `vda` is negative.
  - **Stride check:** 908-909 only proves `plan.ss.size() >= vda[0]`, yet 913-915 reads `vda[3 + 3*s]` for every `s < plan.ss.size()`.
  - **Missing records:** `P p{}` makes a missing Idata, Vdata, CRC or MatRef silently use record 0 (849-857).
  - **Empty target list:** `T.targets.at(src)[0]` (871) is UB on an empty vector. That is unreachable unless the flat-order pairs (768-782) disagree with the per-primitive records.
- **Fix:** checks that push a problem and return false before any record edit (or accept RE-05).
- **Refactor risk:** none for valid data.
- **Verification:** CORRECTED: severity Med-Low → Med (rubric: latent bug on malformed input); added the unchecked `ida`/`vda` element access and the int32 type.

### RE-20 `Table::load` is not robust, and it runs at boot outside any `try`
- **Where:** `native/di1compat/render.hpp:39, 54-109`; `native/di1compat/di1compat.cpp:4152`
- **Severity / category / class:** Med · robustness · BEHAVIOUR-CHANGE
- **Evidence:**
  - **Throwing parses:** `std::stoul`/`std::stoi` (65, 80, 83, 88-89, 102, 105) throw on a corrupt line. The exception escapes `Pentane_Main` and the game dies at boot.
  - **Uninitialised locals:** `int n;` (63, 91), `int cb, off, sz;` (77), `int kind, x;` (79) and `int v;` (82). In a chained `t >> a >> n`, a failed earlier extraction leaves `n` untouched (the sentry fails), so `p.ss.resize(n)` (92) gets garbage.
  - **`Op` initialisers:** `dsi, doff, size, kind` have none (39).
  - **Inconsistent validation:** only D and M lines test `t.fail()` (71, 74).
  - **Unknown types:** an unknown X-op type rejects the whole table (100). That is fail-closed by design. The report's argument from gen30's `inputType` map is weak: those are VS input types, not plan types.
- **Fix:** initialise the locals and `Op` fields, and wrap `load` in `try { } catch (const std::exception&) { return false; }`. A corrupt table then disables render instead of crashing at boot.
- **Refactor risk:** none for valid tables.
- **Verification:** CORRECTED: class BEHAVIOUR-CHANGE; the unknown-type point is reframed as a design note.

### RE-21 `sm3::parse` / `toHlsl` read out of bounds on malformed bytecode
- **Where:** `native/di1compat/sm3.hpp:43, 54-69, 227-271`
- **Severity / category / class:** Med · robustness (1.0 install files) · BEHAVIOUR-CHANGE
- **Evidence:**
  - **Length checks:** `if (i + 1 + len > n)` (55) does not require `len >= 2` for `dcl` (58), `len >= 5` for `def` (61) or `len >= 1` for `defi`/`defb` (60).
  - **Relative-address token:** it is read at `tok(i + 1 + k)` *after* `k` has reached `len` (67), which can read past the instruction or `n`.
  - `tok` is a raw `memcpy`.
  - **`toHlsl`** indexes `S[0..2]` without checking `o.src.size()` (227-271, 250).
- **Fix:** check `len` and `S.size()` per opcode and throw `std::runtime_error`. That is already caught in `generate` (gen30.hpp:604-619).
- **Refactor risk:** none for valid bytecode.
- **Verification:** CORRECTED: severity Low → Med (rubric); added the relative-token overread.

### RE-22 `vfx::transcode` can read past a record when an offset points beyond `keys`
- **Where:** `native/di1compat/vfx.hpp:142-151`
- **Severity / category / class:** Med · robustness (malformed package) · BEHAVIOUR-CHANGE
- **Evidence:**
  - For `i < last`, `end = recs[i + 1].off` comes from the file. 149 validates the span, not `end <= keys`.
  - `in.substr(off, end - off)` clamps at `in.size()`, so `src[0x105]` (151) and `emitter30` (66-96) can read past `src`.
  - An `off > in.size()` instead throws `std::out_of_range` from `substr`; that is caught at di1compat.cpp:3714 as "TRANSCODE EXCEPTION".
- **Fix:** after the `stable_sort`, `if (!recs.empty() && recs.back().off > keys) return false;`. This is **not** strictly equivalent: inputs that throw today would return false instead (the log loses the "TRANSCODE EXCEPTION" note; the raw file is served either way). The per-record `chanMap` calls before the old failure point (side effect: the `shaderRoles` cache) also stop.
- **Refactor risk:** none for valid packages.
- **Verification:** CORRECTED: severity Low → Med; the "provably equivalent" claim is wrong for the throwing case.

### RE-23 `parseBundle` and `parseSsf` trust counts and indices from the file
- **Where:** `native/di1compat/render.hpp:226-279, 315-370, 409-413`
- **Severity / category / class:** Med · robustness · BEHAVIOUR-CHANGE
- **Evidence:**
  - `oct::u(b + p, 4)` with `p` unchecked (330, 334, 340, 343).
  - The shader and material name strings are read with an unbounded `(const char*)b + p` (332, 337), and SSET names likewise (361).
  - `shaders[sidx[i]]` (348).
  - TEXB `emplace_back(raw, o + 20 + 12 * i, 8)` (326) throws only if the position is past the end, and silently truncates otherwise.
  - `o += csize` can wrap (367).
  - `parseSsf` part A reads `words + 4*i` for `i < nA` even when `nA > npar` (256).
  - `buildBundle` writes `ssetBlocks.size()` as the count but emits every `ssetNames` entry (412-413).
- **Fix:** bounds checks that return false.
- **Refactor risk:** none for valid data.
- **Verification:** CORRECTED: severity Low → Med (rubric); added the unbounded string reads.

### RE-27 `.shd` layout offsets are duplicated as unnamed magic numbers in five places
- **Where:** `native/di1compat/gen30.hpp:53-73, 81-91, 497-559, 651-652`; `render.hpp:513-515`; `chan.hpp:260-262, 276`; `di1compat.cpp:4049-4053`
- **Severity / category / class:** Med · duplication / readability · SAFE-REFACTOR
- **Evidence:**
  - **1.0 header:** npar/npass/ntech 0x0c-0x0e, nA 0x44, sizeA 0x46, slotsB 0x69, sizeB 0x6a, passes 0xa4 at a stride of 0x30, VS +0x18.
  - **3.0 header:** 0xE0 header, pass stride 0x50, npass 0x19, VS +0x28, PS +0x30, mask +0x1c/+0x1d.
  - **Uncommented bytes:** `h[0x90]`, `0x94`, `0xcc`, `0xcd`, `0xd9` and `0xda` (515-524).
- **Fix (behaviour-preserving):** one `constexpr` offset set plus accessors, used from all five places.
- **Refactor risk:** none (renaming).
- **Verification:** CONFIRMED.

### RE-32 Helpers duplicated within the slice and across the codebase
- **Where:** as listed below
- **Severity / category / class:** Med · duplication · SAFE-REFACTOR (with the do-not-merge rules)
- **Evidence:**
  - **LE readers:** `gen30::u32/u16` are *checked*, returning 0 when out of range (gen30.hpp:42-43). The unchecked ones are `vfx::rd32/rd16` (vfx.hpp:30-31), `btd::u32` (btd.hpp:15) and `dct::u32` (dct.hpp:40). `oct::u` is byte-wise (oct.hpp:32). octtest uses both families for the same job (octtest.cpp:48-80 and 3275-3283).
  - **LE writers:** the render `u32` lambda (render.hpp:376), gen30 `put32`/`put16` (gen30.hpp:498-499), `vfx::wr32/wr16` (vfx.hpp:32-33), ui `le` (ui.hpp:240).
  - **Align-up:** `render::al` (render.hpp:212), plus inline `(x + 15) & ~15` / `(x + 3) & ~3` at gen30.hpp:59, 84, 87, 301, 305, 539, 547, 551, 553 and vfx.hpp:98, 162, 164. Elsewhere: btd.hpp:28, 48; ui.hpp:238, 242, 245; npc.hpp:260; mernode.hpp:260; octtest.cpp:58, 63, 66, 3278.
  - **Vertex element type → size: three tables.** `render::vtype` (render.hpp:27-30, `n*csize`), npc.hpp:170-171 and `gen30::typeSizes` (parsed from vtxf).
  - **1.0 program path** `%x` then `%08x`: gen30.hpp:483-489 and chan.hpp:280-283.
  - **`.shd` path string:** render.hpp:513, 558, 621, 686, 689; gen30.hpp:569; chan.hpp:275-277.
  - **D3DCompiler loading and hand-written `ID3DBlob`:** chan.hpp:40-48; di1compat.cpp:3768-3772, 4168-4169; octtest.cpp:307-332 and 3246-3256. octtest.cpp:326-332 never `Release`s `c`/`e`.
  - **Return-address → static VA:** `vfxrt::retVa` (vfxrt.hpp:10) is identical to `callerVa` (di1compat.cpp:2742).
  - **FNV-64:** di1compat.cpp:3779, octtest.cpp:319-320 and 3250.
  - **BNDL chunk walks:** render.hpp:315-370, di1compat.cpp:1008-1013, octtest.cpp:54-71 and 3276-3284.
  - **Lower-casing:** `ehash` is ASCII-only (gen30.hpp:23); `tolower` loops at gen30.hpp:268, 273 and vfxrt.hpp:82; `lower()` in audio.hpp:53, npc.hpp:361 and sx.hpp:799; path normalisation at di1compat.cpp:398, 481, 602, 827.
  - **Reader aliases:** `gen30::Read` (479), `chan::Read` (258) and `render::IO::read10/read30` (426-427) are the same signature.
- **Fix (behaviour-preserving):** `bytes.hpp` (`rd<T>` checked and unchecked under different names, `wr<T>`, `alignUp`), `d3dc.hpp` (compiler, disassembler, `Blob`, cache key), one BNDL chunk walker, one `ReadFn`, one type-size table.
- **Refactor risk:**
  - Never merge checked and unchecked readers: `parse10`, `parseA30` and `ctab` rely on the 0-on-out-of-range behaviour.
  - Keep each site's lower-casing semantics.
  - Never merge the two `h131`s (RE-33).
- **Verification:** CORRECTED: ui `le` is defined at 240 (247 is a use); added the triplicated type-size table and octtest 66.

### RE-33 Two `h131` hashes disagree on char signedness; only vfx.hpp's matches the engine
- **Where:** `native/di1compat/vfx.hpp:35-41`; `native/di1compat/audio.hpp:921-926`; `native/di1compat/vfxrt.hpp:79-84`
- **Severity / category / class:** Med · correctness when unifying · BEHAVIOUR-CHANGE
- **Evidence (disassembly of the installed exes):**
  - **Case-kept variant:** 1.0 FUN_0040bac0 equals 3.0 FUN_0040bb50 byte for byte: `test esi,esi; mov cl,[edx]; test cl,cl; imul eax,eax,0x83; movsx ecx,cl; add eax,ecx; inc edx; dec esi; jne`. It is signed and stops at `n` or NUL. vfx.hpp:39 (`(int32_t)(signed char)`, `i < n && s[i]`) is exact.
  - **Lower-cased variant:** 1.0 FUN_0040baf0 / 3.0 FUN_0040bb80 do `movsx eax,al; push eax; call <crt>` and then add the result. The CRT returns the sign-extended byte unchanged for values ≥ 0x80 in the C locale, so this variant is signed too. vfxrt's `nameHash` (lower via `(unsigned char)`, then signed `h131`) matches it.
  - **audio.hpp:924** adds `(uint8_t)*s` or `tolower((uint8_t)*s)`, which is **unsigned** in both modes and has no length bound. Its comment pairs the right addresses (1.0 FUN_0040bac0, 3.0 FUN_0040bb80) with the wrong signedness.
- **Why it matters:**
  - Audio hashes of names containing bytes ≥ 0x80 differ from the engine's (latent; ASCII names are unaffected).
  - A naive merge in either direction changes one side's hashes.
- **Fix:**
  - Interim: name them `h131Signed`/`h131Unsigned` and document the deviation.
  - Making audio.hpp engine-exact changes its hashes for non-ASCII bytes, which needs the user's decision.
- **Refactor risk:** a silent hash change for non-ASCII names.
- **Verification:** CORRECTED: the open question is settled by disassembly; vfx.hpp is correct and audio.hpp deviates.

### RE-38 The shader generator's callbacks read hidden globals; `g_vf30`/`g_tails10` are rewritten on every vtxf open
- **Where:** `native/di1compat/di1compat.cpp:3786-3830, 4035-4038, 4083-4090, 3558-3567, 642-645`
- **Severity / category / class:** Med · hidden parameters / latent race · NEEDS-VERIFICATION
- **Evidence:**
  - `thread_local g_compiling10` (3786) is an implicit argument to `compile10` (3792, 3806), set at 4035.
  - `compile10` depends on `g_cfg` (RE-26).
  - The `mergeVtxf` wrapper writes `g_tails10` (through `gen30::mergeVtxf`) and reassigns `g_vf30` (4086-4087), unlocked. It runs on **every** non-write open of `shaders/vformat.vtxf` when `shaders10` is on (3558-3567), not structurally once.
  - Readers: `transcodeAssetRender` on loader threads (644-645) and `shader10` (4037-4038). Their only protection is the comment "Read once at boot" (4082).
- **Hot path?:** code defaults: no (`shaders10 = 0`); recommended config: once per vtxf open (boot), readers at load.
- **Why it matters:** if the engine reopens the vtxf while worlds load, `g_vf30` reassignment races with readers (use-after-free).
- **Fix (behaviour-preserving):**
  - Pass the shader name through the `Compile` callback.
  - Publish the merged formats once (`std::call_once`, or an atomic pointer to an immutable struct). The served text is unchanged.
- **Refactor risk:** keep the log line at 4088 per open.
- **Verification:** CORRECTED: the wrapper runs per vtxf open, not once. Whether the engine ever reopens it is UNVERIFIED: count `"shaders10: vformat.vtxf +"` lines per session in di1compat.log.

### RE-39 `g_sh10Lock` is held across generate, D3DCompile, disk I/O and engine registration; `linkShader` re-scans on every lookup
- **Where:** `native/di1compat/di1compat.cpp:4027-4064, 3937-4006, 4068-4079`
- **Severity / category / class:** Med · lock held across I/O and engine calls · NEEDS-VERIFICATION
- **Evidence:**
  - `shader10` takes `g_sh10Lock` (4029) and, on a miss, holds it across:
    - `gen30::generate` (1.0 reads under `g_zipLock`);
    - `compile10`: cache `fopen`/`fread`, D3DCompile, cache `fwrite`;
    - `registerProgram10`: the engine's FUN_0045C230/350 (CreateVertexShader/CreatePixelShader);
    - `finalizeShd10`: FUN_0045CF40;
    - `logf`.
  - `hkShaderByName` takes the same lock for every `'x'` name (4074); other names skip it.
  - `linkShader` scans all registered shaders for `shaderIndex` (3948-3950) even when `seen` and nothing is fresh.
- **Hot path?:**
  - code defaults: not installed.
  - recommended config: `shader10` creation once per `'x'` name, mostly from the material transcode (io.translatable, loader threads).
  - Lookups are creation-time per disassembly (key fact 4), not per draw. `linkShader` runs per lookup (`lazyLink10 = 1`).
- **Why it matters:** one shader's compile blocks every other loader thread's `'x'` lookup and transcode. The scan is wasted work on every repeat lookup and enlarges RE-N1's lock hold.
- **Fix (behaviour-preserving):**
  - Compile outside the lock with a per-name in-flight latch. `registerProgram10` (static `next[]`, `g_prog10`, the engine tables) and the `g_sh10` publish must stay under the lock.
  - Compute `shaderIndex` lazily on the first fresh pass. That is byte-identical within the same locked call.
- **Refactor risk:**
  - Concurrent compiles of identical program text would write the same cache file concurrently (`fopen "wb"`, 3848); combine with RE-44's atomic write.
  - Keep record creation under `g_recCs`.
  - The log text is unchanged, but its order may differ.
- **Verification:** CORRECTED: frequency settled statically (creation-time, not per draw); severity Low-Med → Med (lock across I/O and engine calls); added the concurrent cache-write risk.

### RE-48 `ventries` can loop forever on a truncated vtxf (boot-time hang)
- **Where:** `native/di1compat/gen30.hpp:130-153`
- **Severity / category / class:** Med · robustness (malformed install file) · BEHAVIOUR-CHANGE
- **Evidence:**
  - `q = t.find('"', i)` and `i = t.find(']', e) + 1` are unchecked (136-138). With no `]`, `i` becomes `npos + 1 == 0`.
  - The scan then restarts at the first `{` of the file (145) and re-reaches the same malformed `[`, appending to `out` each round. The result is unbounded growth and a hang.
  - Callers: the vtxf merge in `hkOpen` (an engine thread, at boot), `vformats` and `npc` (npc.hpp:173).
- **Fix:** check `npos` and `break`.
- **Refactor risk:** none for valid files.
- **Verification:** CORRECTED: severity Low → Med (rubric); the forward-progress failure is confirmed as a concrete infinite loop.

### RE-09 `render::Table` mixes three domains and parses gen30's lines
- **Where:** `native/di1compat/render.hpp:43-110`
- **Severity / category / class:** Low · cohesion · SAFE-REFACTOR
- **Evidence:**
  - Ten tables from three domains:
    - material tiers: `shaders`, `donors`, `matindex`, `filled`, `notReal`, `genericSlots`;
    - vertex plans: `targets`, `plans`, `skinPlans`;
    - the translator: `shaders10`.
  - One if/else chain on the line tag (58-107) also parses gen30's V/W/Q lines (77-84).
- **Fix (behaviour-preserving):** split into `MaterialTables`, `VertexPlans` and `gen30::Tables`, each with its own line parser, behind one dispatcher (same file format).
- **Refactor risk:** low; the format is defined by rentable.py.
- **Verification:** CORRECTED: severity Med → Low (a 68-line struct is not a large god struct).

### RE-12 The generic donor search is not memoised, and its comparison key copies strings
- **Where:** `native/di1compat/render.hpp:602-648`
- **Severity / category / class:** Low · load-time CPU · SAFE-REFACTOR
- **Evidence:**
  - Every generic material walks `T.donors` up to twice (628-629), with `std::set` lookups per need set (634-638).
  - Pass 0 rebuilds `"g_" + role + "_sampler"` and calls `ehash` (which copies the string, gen30.hpp:22) per role per candidate (624, 644).
  - `auto key = [&](int cm, const std::string& s, const Ref& rr) { return std::make_tuple(cm, rr.count, s, rr.extract, rr.idx); };` (646) copies two strings on each side of every comparison.
- **Hot path?:** code defaults: never; recommended config: per generic-tier material, at load.
- **Fix (behaviour-preserving):**
  - Compare inline: `std::tie(common, ref.count, sh, ref.extract, ref.idx) > std::tie(bestCommon, donor->count, *bestSh, donor->extract, donor->idx)`. The ordering is the same.
  - **Do not** return `std::tie`/`std::forward_as_tuple` from the existing lambda: `cm` is a by-value parameter, so the reference would dangle.
  - Memoise per call on `(m10.shader, drawn[k])`.
  - Precompute the role hashes once.
- **Refactor risk:** the memo must still push `m10.name + ": no 3.0 shader accepts its vertex format"` (649) per material.
- **Verification:** CORRECTED: severity Med-Low → Low; the fix as written (tie from the lambda) would be UB.

### RE-13 Texture de-duplication is a linear search, written twice
- **Where:** `native/di1compat/render.hpp:732-734, 744-748`
- **Severity / category / class:** Low · CPU / duplication · SAFE-REFACTOR
- **Evidence:** `while (at < tex.size() && tex[at] != h) ++at; if (at == tex.size()) tex.push_back(h);` appears in the merge and in the `ssfBlock30` lambda.
- **Hot path?:** code defaults: never; recommended config: O(slots × textures) per served bundle, at load.
- **Fix (behaviour-preserving):** one `findOrAdd` with an `unordered_map<std::string, uint32_t>` beside the vector; the first-use order is unchanged.
- **Refactor risk:** none if the first-use order is kept.
- **Verification:** CORRECTED: severity Low-Med → Low.

### RE-15 Per-vertex conversion does more work than it needs to (hot at load, bit-exactness critical)
- **Where:** `native/di1compat/render.hpp:39, 112-201`
- **Severity / category / class:** Low · load-hot CPU · NEEDS-VERIFICATION (bit-exact output)
- **Evidence:**
  - `switch (v.code)` per component in `decode` (147) and `encode` (172).
  - At 194, `o.dflt.empty() ? 0.0 : i < (int)o.dflt.size() ? o.dflt[i] : 0.0` contains a redundant `empty()` test.
  - `doubleToHalf` uses `frexp`/`ldexp` (127-131).
  - `Op` carries **three** `std::string`s (`st`, `dt`, `bytes`), a vector and two `VType`s (39).
  - The loop itself is already allocation-free (`reserve` at 911; `srcp` is reused).
- **Hot path?:** code defaults: never; recommended config: per vertex of every served world at load (in_city: 52 MB, ~4M indices).
- **Fix (behaviour-preserving):**
  - Precompute `double dfltPad[4]` per X op at load (`i < size ? dflt[i] : 0`).
  - Optionally flatten each `Plan` into a POD op array.
  - Keep the double maths, `nearbyint` and the clamp order (164-171).
- **Refactor risk:** HIGH.
  - Do not use F16C or float→half (double rounding; this is CPython `PyFloat_Pack2`).
  - Do not use `lround` (Python `round()` is half-to-even).
  - `nearbyint` follows the current FP rounding mode (x87 control word / MXCSR, both set by `_controlfp`). That the host never changes it is UNVERIFIED; D3D11 does not touch it.
- **Verification:** CORRECTED: three strings in `Op` (not two); MXCSR-only → the FP control state; severity Low-Med → Low.

### RE-16 `blockEnd` is recomputed: twice per node, and in loop conditions
- **Where:** `native/di1compat/render.hpp:834-836, 956, 960, 964`; oct.hpp:109-114
- **Severity / category / class:** Low · load-time CPU · SAFE-REFACTOR
- **Evidence:**
  - `blockEnd` is an O(subtree) scan.
  - The 834 loop calls it in the step and again at 836.
  - 956, 960 and 964 re-evaluate it in the loop condition.
- **Hot path?:** code defaults: never; recommended config: per served world at load.
- **Fix (behaviour-preserving):** hoist it into locals. For the 834 loop, compute `end` at the top of the body (the `continue` at 835 runs before 836 today) and step with `n = end`.
- **Refactor risk:** `setInt`/`setInts` change payloads only, never `recs.size()` or the levels; the insert at 971 comes after the loops.
- **Verification:** CONFIRMED (added the `continue` ordering note).

### RE-17 Records are classified by comparing name strings
- **Where:** `native/di1compat/render.hpp:773-781, 835-856, 957, 961, 964, 977`
- **Severity / category / class:** Low · load-time CPU · NEEDS-VERIFICATION
- **Evidence:** `const std::string& n = f.name(rec); if (n == "MaterialReference") ...` runs over every record (773-779).
- **Fix:** resolve the name indices once and compare `rec.name` against the **set** of all indices whose string matches.
- **Refactor risk:** the names table can hold duplicates ("first wins", oct.hpp:139). Out-of-range indices map to `"?"` (oct.hpp:105).
- **Verification:** CONFIRMED.

### RE-24 The pass-byte table (Q lines, `Tables::passByte`) is effectively dead
- **Where:** `native/di1compat/gen30.hpp:39, 530-531, 574-578, 646-654`; `native/di1compat/render.hpp:81-84`; rentable.py:78
- **Severity / category / class:** Low · dead logic, misleading · DOC-ONLY
- **Evidence:**
  - `buildShd` writes `r[0x1c]` from `passByte[{tech, w}]` (530-531). `generate` overwrites +0x1c/+0x1d for every pass inside a technique (646-653).
  - Passes outside every technique look up `{0, w}`, but rentable.py:78 emits `Q` only `if t is not None and v`.
  - A Q value therefore survives only if some technique hash is 0.
  - gen30.py:304 still uses `b1c`, another drift.
- **Fix:** comment it as superseded (DOC-ONLY). Deleting the lookup is byte-neutral only if no `Q 00000000 ...` line exists. No di1compat_render.txt is in the repo, so that is UNVERIFIED.
- **Refactor risk:** deletion changes `.shd` bytes only in the technique-0 case.
- **Verification:** CORRECTED: severity Med → Low (dead code); class DOC-ONLY.

### RE-25 Dead code, dead fields and unreachable branches
- **Where:** `native/di1compat/gen30.hpp:342, 343, 362, 429, 435, 443, 630`; `render.hpp:10, 222, 233, 429`; `sm3.hpp:17, 113-131`
- **Severity / category / class:** Low · dead code · SAFE-REFACTOR
- **Evidence (each verified by grep):**
  - `envOff` always returns false (343; used 429, 443).
  - `num == 1 ? 1` (435) is unreachable after 428.
  - `Binding30::sname` (342, 362) is never read.
  - `Generated::name` (630) is never read.
  - `Ssf::stride` (222, 233) is never read.
  - Base `sm3::Binding` defaults are overridden; only the base `sample` is called (gen30.hpp:453).
  - `R_LOOP`/`R_PREDICATE` (17) are unused.
  - `IO::verbose` (429) is never set by di1compat.cpp; only octtest sets it.
  - render.hpp includes `<cstdio>` (10) but calls no stdio function.
- **Fix:** delete them, or mark them as kept for Python parity. Each is byte-neutral.
- **Refactor risk:** **not none.** render.hpp's `<cstdio>` (10) precedes `#include "gen30.hpp"` (17), and gen30.hpp (486-487) and sm3.hpp (98) call `sprintf_s` without including `<cstdio>`. Add `<cstdio>` to gen30.hpp and sm3.hpp before removing it. Today both TUs include it earlier anyway (di1compat.cpp:13, octtest.cpp:5).
- **Verification:** CORRECTED: the include-order risk was missing.

### RE-26 Test switches compiled into the shipping shader-compile path
- **Where:** `native/di1compat/di1compat.cpp:3789-3828`
- **Severity / category / class:** Low · test code in production · SAFE-REFACTOR
- **Evidence:**
  - `bufview10 == 8` (3790), `6..7` (3803) and `magenta10` (3826) swap the HLSL.
  - Six literal shaders are embedded: `magenta` plus five `bufview` (3813-3825).
  - They detect passes by matching generated text.
- **Fix:** move them into `debugViewHlsl(target, hlsl)`, keeping the key computed from the swapped text (3830). `#ifdef`-ing them out is a BEHAVIOUR-CHANGE (the `bufview10`/`magenta10` config keys would stop working).
- **Refactor risk:** the cache key and the `idcolor` log text must stay as they are.
- **Verification:** CORRECTED: severity Low-Med → Low; compile-out flagged as a behaviour change.

### RE-28 Pass-word bitfields have no names
- **Where:** `native/di1compat/gen30.hpp:285-294, 504-505, 580, 609`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:**
  - `((w & 7) << 3) | ...` (287); `((w >> 6) & 0xf) > 1` (504); `((w >> 3) & 7) == 0 && ((w >> 12) & 0xf) == 3` (505); `== 2` (580); `== 1` (609).
  - gen30.py:19 names the fields.
- **Fix:** `constexpr` accessors plus `enum DepthKind { Final = 0, DepthOnly = 1, GBuffer = 2 }`.
- **Refactor risk:** none.
- **Verification:** CORRECTED: severity Low-Med → Low; added 505.

### RE-29 D3D9 opcodes and register codes are bare integers
- **Where:** `native/di1compat/sm3.hpp:52-63, 154-155, 223-271`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:** `op == 31`, `op == 81 || op == 48 || op == 47`, the `noDst` set, `case 66: case 95:`, `case 88:`, `case 90:`.
- **Fix:** an `enum Opc` with D3DSIO values (DCL 31, DEFB 47, DEFI 48, DEF 81, CMP 88, DP2ADD 90, TEXLDL 95, BREAKP 96, ...). No text changes.
- **Refactor risk:** none.
- **Verification:** CORRECTED: severity Low-Med → Low.

### RE-30 `Binding30` register numbers lost the Python's names
- **Where:** `native/di1compat/gen30.hpp:428-438, 522`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:** `5 + num - 3` (435) and `(5 | (slots << 4))` (522). gen30.py:34-36 has `ENGINE_TEX`, `LIGHT10`, `SLOT10`, `SLOT30`.
- **Fix:** `constexpr int SLOT10 = 3, SLOT30 = 5, LIGHT10 = 2, NG10_REG = 1;` with identical emitted text.
- **Refactor risk:** none if the text is unchanged.
- **Verification:** CONFIRMED.

### RE-31 `parseSsf` is cryptic
- **Where:** `native/di1compat/render.hpp:226-279`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:** two magic-number sets (230-231) and a dozen two-to-four-letter locals per pass (266-273). The offsets cite only FUN_ addresses.
- **Fix:** `struct SsfLayout` with two `constexpr` instances, plus a `PassHeader` view.
- **Refactor risk:** none.
- **Verification:** CORRECTED: severity Med → Low (local readability).

### RE-34 Emitter mesh hash vs `.p3d` alias: 8-char and case edge cases
- **Where:** `native/di1compat/vfx.hpp:86, 212-220`; `native/di1compat/di1compat.cpp:602-604`
- **Severity / category / class:** Low · possible mismatch · DOC-ONLY
- **Evidence:**
  - `h131(a.data() + 0xf8, 8)` (86) hashes up to 8 chars, while `p3dAlias` rejects names over 7 (216). That only matters for a char[8] with no NUL, which 1.0 itself could not print safely.
  - Correction: `p3dAlias` gets an already lower-cased member name from its caller (`indexZip10`, 602-604), while the emitter hashes the name with its case kept. A 1.0 emitter mesh name containing capitals would miss its alias.
- **Fix:** document both invariants; check the 1.0 packages for 8-char or upper-case mesh names before changing either side.
- **Refactor risk:** do not "fix" one side without data.
- **Verification:** CORRECTED: the case point (the caller lower-cases; the risk is capitals on the emitter side). The data check remains UNVERIFIED (scan the 65 1.0 meshes' emitter names).

### RE-35 `vfxrt.hpp` compiles only because of where it is included, and puts statics in a header
- **Where:** `native/di1compat/vfxrt.hpp:1-126`; `native/di1compat/di1compat.cpp:4092`
- **Severity / category / class:** Low · coupling · SAFE-REFACTOR
- **Evidence:**
  - It uses `g_base`, `g_cfg.install10`, `g_gameDir`, `logf`, `safeCopy`, `sys::readable`, `vfx::h131`, `tolower`, `strlen`, `_strnicmp` and `GetFileAttributesA` without including or declaring them.
  - It includes only `<intrin.h>`, `<map>` and `<mutex>`.
  - It defines `static` hook pointers and mutexes in a header. This is documented as intended (1-2).
- **Fix:** add the `#include`s and `extern` declarations, or make it a .cpp with a small context interface.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### RE-36 `hkPkg` holds `g_uniLock` across an engine call
- **Where:** `native/di1compat/vfxrt.hpp:38-50`
- **Severity / category / class:** Low · concurrency · DOC-ONLY
- **Evidence:**
  - `std::lock_guard<std::mutex> lock(g_uniLock); uint32_t* u = g_pkg("particles/universal10", flags);` (43-44) holds the lock across the engine's package load (file I/O) and a `logf`.
  - The mutex is non-recursive. A self-deadlock would need the nested load to request a *1.0-only* package. A nested request for `particles/universal10` itself does not lock, because `only10("universal10")` is false.
  - The zone package's own load (47) is unlocked.
- **Hot path?:** code defaults and recommended config: installed in both (`vfx10`, `vfx_universal10` default 1); per particle-package request, cold.
- **Fix:** document the assumption; optionally add a `thread_local` recursion guard.
- **Refactor risk:** keep the call order (universal10 before the zone package) and the two log lines.
- **Verification:** CONFIRMED (re-entrancy UNVERIFIED, as stated).

### RE-37 `vfxrt` has small hidden side effects
- **Where:** `native/di1compat/vfxrt.hpp:65-84`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:**
  - `nameHash(char*)` lower-cases its argument in place, so the logged name is lower-case (102, 122).
  - `++g_traceSeen[...] > 3 || g_traceLines >= 3000` (69) inserts map entries after the cap.
- **Hot path?:** `vfx_trace` only (default 0 in both configs).
- **Fix:** test `g_traceLines >= 3000` first (same result). Keep logging the lowered name; do not move the lowering.
- **Refactor risk:** log text.
- **Verification:** CONFIRMED.

### RE-40 Invariants between headers are never stated in code
- **Where:** `native/di1compat/vfx.hpp:94, 157, 168`; `chan.hpp:242-256`; `render.hpp:475, 717-719`; `di1compat.cpp:1059-1063`
- **Severity / category / class:** Low · coupling · DOC-ONLY
- **Evidence:**
  - **Constant key:** `emitter30` uses `constKey` for any `c < 0 || c >= n`, but `transcode` appends the constant key only if some `c < 0` (157, 168). This is correct only because `chan::map` yields `-1..n10-1` (250-251) and `n` is the same `src[0x105]`.
  - **Material-index alignment:** `Result::shaders` must align one-to-one with 1.0 material indices (`shaders[mat]`, 1062). That holds whenever it is used, since a failure returns `{}` (811-812).
  - **String-keyed counters:** `Result::tiers` keys are spelled in two files.
- **Fix:** asserts or comments at both ends; struct fields for `tiers`.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### RE-41 `render::IO` mixes I/O callbacks, policy flags and data
- **Where:** `native/di1compat/render.hpp:425-454`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:** 3 I/O callbacks, 2 policy callbacks, 4 bools and 3 context pointers in one struct.
- **Fix:** split into `Sources`, `Policy` and `Context`.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### RE-42 Names collide across namespaces, and many are one letter
- **Where:** `render.hpp:39` and `sm3.hpp:26` (`Op`); `sm3.hpp:27` and `gen30.hpp:477` (`Program`); `gen30.hpp:297` and `chan.hpp:292` (`layout`); `gen30.hpp:208` (`field`); `chan.hpp:242` (`map`)
- **Severity / category / class:** Low · naming · SAFE-REFACTOR
- **Evidence:**
  - One-letter parameters (`T`, `L`, `B`, `r`, `f`, `s`).
  - `toHlsl` has `S` (221) and the lambda `s` (194) side by side.
  - render.hpp's "crc" variables (777, 788) hold the engine GUID hash. They are named after the file's `vformatCRC` field.
- **Fix:** rename.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### RE-43 Diagnostics are weaker than the Python twin's
- **Where:** `native/di1compat/render.hpp:798, 903-904`; `native/di1compat/gen30.hpp:583`
- **Severity / category / class:** Low · diagnostics · BEHAVIOUR-CHANGE (log text)
- **Evidence:**
  - `Idata[0]`/`Idata[2]` are silently ignored (903-904).
  - `"vformat " + std::to_string(crc)` is decimal (798).
  - `"vformat unknown"` omits the format (583).
- **Fix:** log the format in hex and add NOTE lines. `r.problems` and `g.problems` are written to di1compat.log (649, 1032, 4039), and the log text is a preserved contract (external scripts grep it). So this is a log change for the user to approve.
- **Refactor risk:** log text.
- **Verification:** CORRECTED: the report's "nothing parses them" overlooks the log contract → BEHAVIOUR-CHANGE.

### RE-44 Shader-cache files are written non-atomically and barely validated
- **Where:** `native/di1compat/di1compat.cpp:3829-3849`
- **Severity / category / class:** Low · robustness · NEEDS-VERIFICATION
- **Evidence:**
  - A cache file is accepted if `code.size() > 32 && !code.compare(0, 4, "DXBC")` (3839).
  - It is written with `fopen_s(..."wb")` + `fwrite` (3848).
  - A truncated blob that passes makes `registerProgram10` fail ("program %08X not created"). The failure is memoised for the session and the bad file persists across sessions.
- **Fix:** write `<name>.tmp`, then `MoveFileExA(..., MOVEFILE_REPLACE_EXISTING)`. Valid caches are unchanged.
- **Refactor risk:** none for valid caches. It is a prerequisite if compiles ever run outside `g_sh10Lock` (RE-39).
- **Verification:** CONFIRMED.

### RE-45 Global pointers are mutable even though they are never written
- **Where:** `native/di1compat/gen30.hpp:327`; `native/di1compat/sm3.hpp:137, 142`
- **Severity / category / class:** Low · const-correctness · SAFE-REFACTOR
- **Evidence:** `static const char* GBUFFER_EPILOGUE`, `inline const char* VARYINGS[]`, `inline const char* REVERSED_Z`. `NG10` (gen30.hpp:422) is correctly `static constexpr`.
- **Fix:** `constexpr const char*` / `const char* const`.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### RE-46 `chan::shaderRoles` computes outside its lock
- **Where:** `native/di1compat/chan.hpp:263-287`
- **Severity / category / class:** Low · concurrency (benign) · DOC-ONLY
- **Evidence:** the lock is released during the compute (272-284), so two threads may compute the same key; `cache[key] = r` overwrites with an equal value. Misses (empty results) are cached for the process.
- **Fix:** none needed; document it.
- **Refactor risk:** none.
- **Verification:** CONFIRMED.

### RE-47 Bitwise CRC-32 reimplemented
- **Where:** `native/di1compat/gen30.hpp:466-475`
- **Severity / category / class:** Low · duplication · NEEDS-VERIFICATION
- **Evidence:** a bit-at-a-time IEEE CRC-32 used by `progId`, at most twice per pass (cold). miniz's `mz_crc32` is linked into both binaries.
- **Fix:** optional swap to `mz_crc32(MZ_CRC32_INIT, ...)`. The verifier ran the same loop against Python's `zlib.crc32` on 2,000 random inputs: identical. `mz_crc32` is miniz's zlib-compatible CRC, so the swap is byte-neutral. Still add one octtest assertion, because `progId` feeds `.shd` bytes.
- **Refactor risk:** `.shd` program ids if not identical.
- **Verification:** CORRECTED: equivalence checked (was UNVERIFIED).

### RE-49 Small redundancies in `transcodeMaterials`
- **Where:** `native/di1compat/render.hpp:554-557, 585, 663-672`
- **Severity / category / class:** Low · readability · SAFE-REFACTOR
- **Evidence:**
  - The "is real" predicate is written twice (555, 585).
  - `skinTarget30(T, c)` is evaluated twice per format (557).
  - The O(slots²) loop at 669-671 equals `for ti < min(targets.size(), set.size()): if (targets[ti] < dm.slots) used[targets[ti]] = set[ti]`, because `targets` is strictly increasing (667).
- **Fix:** an `isReal` helper, a local for the skin target, and the simpler loop.
- **Refactor risk:** none (equivalence holds).
- **Verification:** CONFIRMED.

### RE-50 Lua-text scanning depends on exact formatting
- **Where:** `native/di1compat/gen30.hpp:161, 168, 220, 227` (and npc.hpp:173 through `ventries`)
- **Severity / category / class:** Low · robustness, readability · DOC-ONLY
- **Evidence:**
  - `t.find("main", v.start)` matches any substring.
  - `"semantic = \""` and `"sizeInBytes = "` need exact spacing.
  - `"\ttypes"` needs a tab.
- **Fix:** document the assumed formatting. Any tokenizer must reproduce the matches exactly, because the `mergeVtxf` output is the served vformat.vtxf.
- **Refactor risk:** served vtxf bytes.
- **Verification:** CONFIRMED.

### RE-N2 `hkShaderByName` → `shader10` has no exception boundary
- **Where:** `native/di1compat/di1compat.cpp:4027-4064, 4068-4079`
- **Severity / category / class:** Low · error handling at a hook boundary · BEHAVIOUR-CHANGE
- **Evidence:**
  - `generate` catches only around `sm3::parse`/`ctab`/`toHlsl` (gen30.hpp:604-619).
  - The rest of the path can throw and nothing catches it in a hook context: allocation everywhere (`std::bad_alloc` in a 32-bit process under load pressure, cf. RE-14), `engineId.at(pid)` (4052), and the string and map work in `compile10` and `registerProgram10`.
  - When `shader10(name, true)` is first reached from `hkShaderByName` (a 33-char `'x'` name the transcode did not create first), an exception unwinds into engine frames. The comment at 3714 notes that ends in a silent fail-fast.
  - Through `io.translatable` (641) the same code is covered by the transcode catch (3714).
- **Hot path?:** code defaults: not installed; recommended config: only on a creation through the hook, which is rare because the transcode normally creates every `'x'` shader first (render.hpp:558-559).
- **Fix:** `try { ... } catch (...) { logf(...); return nullptr; }` around `shader10`'s create path (keeping the memoised nullptr). This is a behaviour change: a crash becomes a lookup miss. Per the comment at 4071-4073, an unresolved name once made the engine's loader wait forever, so a miss can hang instead. The user must choose.
- **Refactor risk:** do not catch around `linkShader`'s record writes half-done.
- **Verification:** NEW (code read; no engine data needed).

## Inventory

Line ranges were re-checked mechanically against HEAD 684539b. Hot/cold key:
- **boot:** plugin init.
- **asset:** once per served asset; only with `serve10` (recommended config), never under code defaults.
- **asset-hot:** per vertex or index of an asset, at load.
- **shader:** once per translated shader per session.
- **diag:** diagnostic flag only.
- **draw:** per draw-pass, every frame.

### render.hpp
| Name | Lines | Count | Purpose | Hot/cold |
|---|---|---|---|---|
| `UNSET` | 21 | 1 | empty slot sentinel | – |
| `VType` / `vtype` | 24-35 | 12 | element type lookup | boot |
| `Shader`, `Op`, `Plan`, `Ref` | 38-41 | 4 | table records (`Op`: 3 strings, vector, 2 VTypes) | – |
| `Table` (`load` 54-109) | 43-110 | 68 (56) | parse di1compat_render.txt | boot |
| `halfToDouble` | 112-117 | 6 | half → double | asset-hot |
| `doubleToHalf` | 120-139 | 20 | CPython PyFloat_Pack2 | asset-hot |
| `decode` | 142-157 | 16 | element → doubles | asset-hot |
| `encode` | 160-179 | 20 | doubles → element | asset-hot |
| `convertVertex` | 183-201 | 19 | plan ops per vertex | asset-hot |
| `Material`, `Bundle` | 204-210 | 7 | bundle model | – |
| `al` | 212 | 1 | align up | asset |
| `SsfPart`, `Ssf` | 220-225 | 6 | scene-settings format | – |
| `parseSsf` | 226-279 | 54 | parse .ssf (1.0/3.0) | first use |
| `ssfBlock30` | 283-312 | 30 | SSET block 1.0 → 3.0 | asset |
| `parseBundle` | 315-370 | 56 | BNDL → TEXB/MATP/SSET | asset |
| `buildBundle` | 373-422 | 50 | → BNDL bytes | asset |
| `IO` | 425-454 | 30 | callbacks/flags/context | – |
| `skinTarget30` | 458-465 | 8 | first target with an R plan | asset / shader |
| `Result` | 467-476 | 10 | outputs | – |
| `Src` | 478 | 1 | merge input (copies a bundle) | – |
| `hex8` | 480-486 | 7 | bytes → hex | asset |
| `rendercaps30` | 489-493 | 5 | RenderCaps map | asset |
| `transcodeMaterials` | 499-754 | 256 | tiers, merge, build | asset (god) |
| `transcodeBundle` | 757-761 | 5 | materials only | asset |
| `transcodeWorld` | 765-980 | 216 | materials + geometry + .oct | asset (god, hot loop) |

### gen30.hpp
| Name | Lines | Count | Purpose | Hot/cold |
|---|---|---|---|---|
| `ehash` | 20-34 | 15 | engine name hash (FUN_0044a9f0/0044aa40) | asset/shader |
| `Tables` | 36-40 | 5 | globals, wordRules, passByte | – |
| `u32`, `u16` | 42-43 | 2 | **checked** LE reads | shader |
| `Param`/`Pass`/`Tech`/`Shd10` | 45-48 | 4 | 1.0 .shd model | – |
| `parse10` | 51-74 | 24 | parse 1.0 .shd | asset/shader |
| `ShdA30`/`parseA30` | 78-93 | 16 | 3.0 block A | asset |
| `Const`/`ctab` | 96-113 | 18 | CTAB | shader |
| `VFormats`/`VEntry`/`ventries` | 116-155 | 40 | vtxf entries (also npc.hpp:173) | boot |
| `vformats` | 157-181 | 25 | GUID → semantic → type | boot |
| `ColourTail` | 193 | 1 | colour-tail split | – |
| `children` | 196-206 | 11 | Lua child tables | boot |
| `field` | 208-215 | 8 | `key = "..."` | boot |
| `typeSizes` | 217-231 | 15 | type → bytes | boot |
| `relayColour` | 234-258 | 25 | move trailing colour | boot |
| `mergeVtxf` | 261-283 | 23 | 3.0 vtxf + 1.0 formats | each vtxf open |
| `convWord`/`word30` | 285-294 | 10 | pass word 1.0 → 3.0 | shader |
| `Layout`/`layout` | 296-311 | 16 | block offsets | asset/shader |
| `blockB30` | 313-325 | 13 | block B re-lay | asset/shader |
| `GBUFFER_EPILOGUE` | 327-334 | 8 | HLSL text (frozen) | – |
| `Binding30` | 336-464 | 129 | resource binding (overrides) | shader |
| `crc32`/`progId` | 466-475 | 10 | program ids | shader |
| `Program`/`Generated`/`Read`/`Compile` | 477-480 | 4 | generator I/O | – |
| `program10` | 483-489 | 7 | read 1.0 program `%x`/`%08x` | shader |
| `buildShd` | 491-560 | 70 | assemble 3.0 .shd | shader |
| `generate` | 565-656 | 92 | 1.0 shader → .shd + HLSL | shader |

### sm3.hpp
| Name | Lines | Count | Purpose | Hot/cold |
|---|---|---|---|---|
| `Reg` | 16-17 | 2 | register types | – |
| `usage` | 18-23 | 6 | dcl usage names | shader |
| `Operand`/`Op`/`Program` | 25-27 | 3 | instruction model | – |
| `rtype`/`dstOf`/`srcOf` | 29-38 | 10 | token decode | shader |
| `parse` | 40-75 | 36 | tokens → ops | shader |
| `pyrepr` | 79-108 | 30 | Python float repr (frozen) | shader |
| `Binding` | 111-135 | 25 | binding interface | shader |
| `VARYINGS`, `REVERSED_Z` | 137-142 | 6 | HLSL fragments (frozen) | – |
| `toHlsl` | 144-338 | 195 | ops → HLSL | shader (god) |

### vfx.hpp
| Name | Lines | Count | Purpose | Hot/cold |
|---|---|---|---|---|
| `rd32`/`rd16`/`wr32`/`wr16` | 30-33 | 4 | unchecked LE | asset |
| `h131` | 36-41 | 6 | ×131 signed, bounded (engine-exact, RE-33) | asset |
| `isPd10` | 43-46 | 4 | 1.0 trailer test | asset |
| `xc` | 50-59 | 10 | x → c table | asset |
| `emitter30` | 63-100 | 38 | emitter 1.0 → 3.0 | asset |
| `constKey` | 104 | 1 | constant curve key | asset |
| `ChanMap` | 108 | 1 | callback type | – |
| `param30` | 111-122 | 12 | param 1.0 → 3.0 | asset |
| `Stats` | 124 | 1 | counters | – |
| `transcode` | 128-194 | 67 | package 1.0 → 3.0 | asset |
| `p3d30` | 198-208 | 11 | mesh 8 → 12 B vertices | asset |
| `p3dAlias` | 212-220 | 9 | member → hashed path | zip index |

### chan.hpp
| Name | Lines | Count | Purpose | Hot/cold |
|---|---|---|---|---|
| `disasm` | 38-57 | 20 | D3DDisassemble (own Blob/LoadLibrary) | per shader (cached) |
| `Opnd`/`opnd` | 59-87 | 29 | operand parse | per shader |
| `operands` | 89-103 | 15 | operand split | per shader |
| `roles` | 107-238 | 132 | taint roles | per shader (god) |
| `map` | 242-256 | 15 | 3.0 → 1.0 channel | per emitter |
| `Read` | 258 | 1 | reader alias | – |
| `shaderRoles` | 263-287 | 25 | cached roles | per emitter |
| `layout` | 292-302 | 11 | re-layout decision | per emitter |

### vfxrt.hpp
| Name | Lines | Count | Purpose | Hot/cold |
|---|---|---|---|---|
| `retVa` | 10 | 1 | return address → VA (= `callerVa`) | per package / diag |
| `PkgFn`, `g_pkg`, `g_uniLock` | 27-29 | 3 | package hook state | – |
| `only10` | 31-36 | 6 | 1.0-only package test (2 GetFileAttributesA) | per package |
| `hkPkg` | 38-50 | 13 | universal10 before a 1.0 zone package | per package request (both configs) |
| `g_traceLock`/`g_traceSeen`/`g_traceLines` | 59-61 | 3 | trace dedup | diag |
| `trace` | 65-77 | 13 | log a placement | diag per spawn |
| `nameHash` | 80-84 | 5 | lower in place + h131 | diag |
| `hkEffect` | 89-93 | 5 | effect create trace | diag per spawn |
| `hkEmitter` | 98-104 | 7 | emitter create trace | diag per spawn |
| `hkAttached` | 112-124 | 13 | slaved emitter trace | diag per spawn |

### di1compat.cpp shaders10 runtime
| Name | Lines | Count | Purpose | Hot/cold |
|---|---|---|---|---|
| `Blob` | 3768-3771 | 4 | hand-written ID3DBlob (dup chan.hpp:40-43) | – |
| `fnv64` | 3779-3784 | 6 | cache key / program key hash | shader |
| `compile10` | 3787-3850 | 64 | debug views, disk cache, D3DCompile | per program |
| `registerProgram10` | 3853-3876 | 24 | engine program registration | per program |
| `finalizeShd10` | 3878-3890 | 13 | engine FUN_0045CF40 via inline asm | shader |
| `inputLayout10` | 3892-3907 | 16 | engine FUN_0044D430 (CreateInputLayout) | per fresh pass |
| `RecLock` | 3920 | 1 | RAII on `g_recCs` | – |
| `hkCloneRec` | 3924-3935 | 12 | serialise FUN_0045C5C0 | **draw** (recommended config) |
| `linkShader` | 3937-4006 | 70 | pass-state records | per shader lookup |
| `hkLinkAll` | 4010-4023 | 14 | boot linker replacement | boot |
| `shader10` | 4027-4064 | 38 | generate/register/finalize `'x'` shader | once per name |
| `hkShaderByName` | 4068-4079 | 12 | lookup hook | per lookup (creation-time) |
| `mergeVtxf` (wrapper) | 4083-4090 | 8 | vtxf merge; sets globals | each vtxf open |

### Hooks in this area
| Hook name string | Static VA | Detour | Installed when | Frequency |
|---|---|---|---|---|
| "shader by name" | 0x0045C820 | `hkShaderByName` | `shaders10` | per shader lookup (creation-time) |
| "shader linker (deferred)" | 0x00472050 | `hkLinkAll` | `shaders10 && lazylink10` | once at renderer init |
| "pass-state clone (locked)" | 0x0045C5C0 | `hkCloneRec` | `shaders10` | per draw-pass with a vformat mismatch |
| "particle package load (vfx)" | 0x00672620 | `vfxrt::hkPkg` | `vfx10 && vfx_universal10` (both default 1) | per package request |
| "effect create (vfx trace)" | 0x0065E3A0 | `vfxrt::hkEffect` | `vfx_trace` | per effect spawn |
| "emitter create (vfx trace)" | 0x0066A1C0 | `vfxrt::hkEmitter` | `vfx_trace` | per emitter spawn |
| "slaved emitter create (vfx trace)" | 0x0066E7F0 | `vfxrt::hkAttached` | `vfx_trace` | per slaved emitter spawn |

### Generic helpers (for cross-file duplicate matching)
| Helper | Signature | Location | Notes |
|---|---|---|---|
| `render::al` | `size_t al(size_t, size_t)` | render.hpp:212 | inline copies: RE-32 |
| `render::hex8` | `std::string hex8(const std::string&)` | render.hpp:480 | |
| `render::vtype` | `bool vtype(const std::string&, VType&)` | render.hpp:25 | size table dup npc.hpp:170 |
| `render::halfToDouble` / `doubleToHalf` | `double(uint16_t)` / `uint16_t(double)` | render.hpp:112 / 120 | keep exact |
| `render::decode` / `encode` | `(const VType&, const uint8_t*, double*)` / `(const VType&, const double*, char*)` | render.hpp:142 / 160 | keep exact |
| buildBundle `u32` lambda | `(std::string&, uint32_t)` | render.hpp:376 | dup gen30 `put32`, `vfx::wr32`, ui `le` (ui.hpp:240) |
| `gen30::ehash` | `uint32_t(const std::string&, bool lower = false)` | gen30.hpp:20 | used by npc.hpp:188 |
| `gen30::u32` / `u16` | `uint32_t(const std::string&, size_t)` | gen30.hpp:42-43 | **checked**; unchecked dups btd.hpp:15, dct.hpp:40, vfx.hpp:30-31 |
| `gen30::children` / `field` / `ventries` | – | gen30.hpp:196 / 208 / 121 | mission.hpp has its own `field` |
| `gen30::crc32` / `progId` | `uint32_t(const std::string&)` | gen30.hpp:466 / 472 | = zlib CRC-32 (RE-47) |
| `gen30::program10` | `bool(const Read&, uint32_t, const char*, std::string&)` | gen30.hpp:483 | dup chan.hpp:280-283 |
| `sm3::pyrepr` | `std::string(double)` | sm3.hpp:79 | frozen |
| `vfx::h131` | `uint32_t(const char*, size_t)` | vfx.hpp:36 | signed, engine-exact; audio.hpp:921 is unsigned |
| `vfx::p3dAlias` | `std::string(const std::string&)` | vfx.hpp:212 | input lower-cased by caller |
| `chan::disasm` | `std::string(const std::string&)` | chan.hpp:38 | own Blob/LoadLibrary |
| `vfxrt::retVa` | `uint32_t(void*)` | vfxrt.hpp:10 | = `callerVa` di1compat.cpp:2742 |
| `vfxrt::nameHash` | `uint32_t(char*)` | vfxrt.hpp:80 | lowers in place |
| `fnv64` | `uint64_t(const std::string&)` | di1compat.cpp:3779 | dups octtest.cpp:319, 3250 |
| `norm` | `std::string(const char*)` | di1compat.cpp:478 | dups 398, 602, 827 |

### Globals and statics
| Name | Location | Type | Writers | Threads | Guarded? |
|---|---|---|---|---|---|
| `render::UNSET` | render.hpp:21 | constexpr | – | – | const |
| `vtype::T`, `ssfBlock30::LUT10/LUT30`, `inputType::I`, `usage::U`, `parse::noDst`, `toHlsl::SW`/`cmp`, `xc::k`, `opnd::consts`, `roles::skip` | various | function statics | init once | loader | magic static |
| `gen30::GBUFFER_EPILOGUE` | gen30.hpp:327 | `static const char*` (mutable pointer) | none | – | RE-45 |
| `sm3::VARYINGS`, `sm3::REVERSED_Z` | sm3.hpp:137, 142 | `inline const char*` | none | – | RE-45 |
| `chan::disasm::fn` | chan.hpp:45 | `static Fn` | init once (LoadLibraryA) | loader | magic static |
| `chan::shaderRoles::mx`/`cache` | chan.hpp:265-266 | mutex / map | `shaderRoles` | loader | yes; compute unlocked (RE-46) |
| `vfxrt::g_pkg`, `g_effect`, `g_emitter`, `g_attached` | vfxrt.hpp:28, 88, 97, 111 | fn pointers | MinHook at install | boot | before enable |
| `vfxrt::g_uniLock` | vfxrt.hpp:29 | `std::mutex` | – | package loaders | RE-36 |
| `vfxrt::g_traceLock`/`g_traceSeen`/`g_traceLines` | vfxrt.hpp:59-61 | mutex / map / LONG | `trace` | spawn threads | yes |
| `g_render` | di1compat.cpp:564 | `render::Table` | Pentane_Main (4152) | read-only after boot | – |
| `g_vf10` | di1compat.cpp:565 | VFormats | Pentane_Main (4173) | read-only after boot | – |
| `g_vf30`, `g_tails10` | di1compat.cpp:566-567 | VFormats / map | `mergeVtxf` wrapper on **every** vtxf open (3567 → 4086-4087) | loader threads read | **no** (RE-38) |
| `g_d3dCompile`, `g_shaderCache` | di1compat.cpp:3773-3774 | fn ptr / string | Pentane_Main (4169-4170) | read-only after | – |
| `g_sh10Lock`, `g_sh10` | di1compat.cpp:3775-3776 | mutex / `unordered_map<string, uint8_t*>` | `shader10` | loader + lookup threads | yes |
| `g_prog10`, `registerProgram10::next` | di1compat.cpp:3777, 3860 | map / `uint32_t[2]` | `registerProgram10` (only from `shader10`) | same | yes (`g_sh10Lock`) |
| `g_compiling10` | di1compat.cpp:3786 | `thread_local std::string` | `shader10` | per thread | hidden parameter |
| `g_recCs` | di1compat.cpp:3918 | CRITICAL_SECTION (initialised 4230) | – | render thread (`hkCloneRec`) + loaders (`linkShader`) | RE-N1 |
| `g_linked`, `linkShader::told` | di1compat.cpp:3919, 3986 | set / int | `linkShader` | loaders, boot | yes (`g_recCs`) |
| `g_cloneRec`, `hkCloneRec::told` | di1compat.cpp:3923, 3931 | fn ptr / int | MinHook / `hkCloneRec` | render thread | yes (`g_recCs`) |
| `g_shaderByName`, `g_linkAll` | di1compat.cpp:4067, 4009 | fn ptrs | MinHook | boot | before enable |
| `sceneSettingsIO::mx/done/ok/f10/f30` | di1compat.cpp:574-576 | statics | `sceneSettingsIO` | loader | yes (local mutex) |

### Missing includes (compile only because of include order)
- **gen30.hpp:**
  - `sprintf_s` (486-487) needs `<cstdio>`. Today it comes from render.hpp:10 (included before gen30.hpp at 17) and from di1compat.cpp:13 / octtest.cpp:5 (see RE-25).
  - `isspace`/`isalnum`/`isdigit`/`tolower` (129, 141, 176, 224, 226, 268, 273) need `<cctype>`.
  - `atoi` (228, 642) needs `<cstdlib>`.
  - `memcpy` arrives only via sm3.hpp's `<cstring>`.
- **sm3.hpp:** `sprintf_s` (98) needs `<cstdio>`.
- **render.hpp:** fine through its own includes; `<cstdio>` is unused by render.hpp itself but load-bearing for gen30/sm3.
- **npc.hpp (adjacent area):** uses `gen30::ventries`/`gen30::ehash` (173, 188) without including gen30.hpp; it relies on render.hpp being included earlier in both TUs.
- **vfxrt.hpp (by design, lines 1-2):**
  - standard headers it lacks: `<string>`, `<windows.h>`, `<cstring>`, `<cctype>`, `<cstdint>`;
  - project headers: `"sys.hpp"`, `"vfx.hpp"`;
  - TU statics `g_base`, `g_cfg`, `g_gameDir`, `logf` and `safeCopy`.
- **vfx.hpp and chan.hpp:** self-sufficient.

### Rules for the refactor (keep output bytes identical)
1. Freeze the emitted HLSL (RE-02/06): no container swaps, spacing or order changes; `pyrepr` exact; emit the body before the shells (`temps`, `used`, `needed`).
2. Keep the conversion arithmetic double-based (RE-15).
3. Keep ordering-sensitive accumulators in order: `r.problems`, `r.tiers`, `r.shaders`, `tex` first-use order, `srcs`, `nv`/`ni`/`ni2`, `f.recs` edits.
4. `io.translatable` has engine side effects and runs only after the `.shd` read and `parse10`.
5. Never merge checked and unchecked byte readers, or the two `h131`s.
6. Log text **and order** are a contract: any change to `logf` placement (RE-N1, RE-39) or message text (RE-43) needs an in-game run and the user's approval.
7. `g_recCs` must keep covering the record append, count and chain update, and must be initialised before its hooks. Never nest `g_sh10Lock` inside it.
8. Land the error-path fixes (RE-05, RE-18..23, RE-48, RE-N2) separately, labelled as behaviour changes.
9. Build the golden oracle first (RE-01).

## Rejected on verification
- none

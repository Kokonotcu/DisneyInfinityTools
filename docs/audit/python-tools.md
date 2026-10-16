# Python and PowerShell toolkit: what is stale, broken or no longer needed

## Decision (owner, 2026-10-16) and what was done

The owner's rule: **keep a tool if it genuinely reveals useful information about 1.0 or 3.0 files; otherwise remove
it.** Applied as follows (uncommitted; `git status` shows the deletions):

| Outcome | Tools | Why |
|---|---|---|
| **Removed** | `native30.py`, `expl30.py`, `expack.py`, `variant.py`, `crabemod.py`, `mtb-retex.py`, `shot.ps1`, `btd-info.ps1` | Converters that wrote into the install (di1compat replaced them); a bisection helper and a packager for their output; a bundle patcher built on a disproved premise; a screenshot copy of `harness.ps1`'s; a header dump whose numbers are wrong on 1.0 files (`btdblocks`/`btdconv` read both games correctly) |
| **Also removed** (second decision: delete when the C++ does the same job) | `subnet.py`, `btdscan.py` | `subnet.py`'s count rules are `octx::addCounts`, checked against every shipped 3.0 SubNetwork by `octtest --counts`. `btdscan.py`'s rule (one block per TriangleList shape) is `btd.hpp`, and `btdblocks.py` asserts it over the corpus |
| **Also removed** (owner's call) | `arenapatch.py` | The exe fix for the render-arena overflow. `arenas.py`/`mdmp.py` stay: they read crash dumps |
| **Kept** (the table below said "remove") | `diffworld.py`, `ranges.py` (+ `shippedranges.json`), `mtbdiff.py`, `rendercmp.py` | Each one reads shipped files and reports something about them that the C++ does not compute: field-level differences, the value ranges 3.0 ships, how the same material and the same shader differ between the games |
| **Kept, still broken** | `fevcheck.py`, `fevdeps.py`, `vocheck.py`, `particles_val30.py` | They are octtest's oracles; fixing their hard-coded paths is listed below |

The references were updated:
- README tool tables;
- `requirements.txt`'s lupa comment;
- `capped.ps1`'s example;
- `conv.py`'s docstring;
- CI's run-on-import skip set;
- a note at the top of FORMATS.md: its citations of removed tools stay valid as history (commit `684539b`).

Left as is: `crabe/di3tools/` still describes itself as a bench for `native30` worlds (its zone dump, travel and spawn
entries still work), and the comment on `render.hpp:1` still names `native30`.

The analysis that led to this follows, unchanged.

Scope: every `*.py` and `*.ps1` in the repository root (61 Python files, 5 PowerShell), plus `requirements.txt`, the CI
workflow, `.gitignore` and the two data files at the root. Nothing was run except `python -m pyflakes *.py`, which only
reads. "Nothing depends on it" means three searches came back empty: the import graph across all root modules,
`git grep` over the repository, and a search of `native/di1compat/*.hpp|*.cpp` for both the file name and the bare stem.

Git dates are no help here: the history was rewritten, and almost every file shows a last commit between 2026-10-12 and
2026-10-14. The verdicts rest on content, references and the README's own rule ("A tool stays in this repository only
when it has a check against shipped game data").

Checked directly while writing this section:
- the hard-coded `D:\` paths in `fevcheck.py:7`, `vocheck.py:12`, `particles.py:33-34` and `particles_val30.py:5`;
- the four pyflakes warnings;
- the `.gitignore` gap for a root `build/` folder (`git check-ignore build/x.obj` matches nothing);
- Pillow missing from `requirements.txt`;
- the `--help` failures of `mertable.py` (`main(*sys.argv[1:7])` at :96) and `renderoracle.py` (:29);
- `zonereg.py:5-7` describing itself as superseded;
- commit `b45315f` ("matp merge verified in-game"), which contradicts the premise of `mtb-retex.py`.

## Verdicts at a glance

| Verdict | Files |
|---|---|
| **Remove** (superseded or one-off; nothing needed depends on them) | `native30.py`, `expl30.py`, `expack.py`, `variant.py`, `crabemod.py`, `ranges.py` (+ `shippedranges.json`), `subnet.py`, `btdscan.py`, `btd-info.ps1`, `mtb-retex.py`, `mtbdiff.py`, `rendercmp.py`, `shot.ps1` |
| **Remove if you no longer diff worlds** | `diffworld.py` |
| **Broken, fix (the C++ tests cite them as oracles)** | `fevcheck.py`, `fevdeps.py`, `vocheck.py` |
| **Broken, fold into another tool** | `particles_val30.py` into `particles.validate30` |
| **Possibly obsolete (needs your call)** | `arenapatch.py` (with `arenas.py`, `mdmp.py`) |
| **Superseded, but keep as a reference** | `realm30.py`, `zonereg.py`, `btdconv.py`, `btdblocks.py`, `foreign.py`, `graph.py`, `particles.py`, `shdiff.py`, `ssf.py` |
| **Keep** | everything else (table below) |

Removing tools is a decision for you. Several of the "remove" files are still cited as fact sources in FORMATS.md, and
those citations would need new sources: `native30` (15 citations), `expl30` (8), `expack` (5), `ranges` (2), `variant` (1),
`crabemod` (1).

## Every file

Sizes are on-disk KB. "selftest" means the tool is listed in `selftest.py` TESTS (lines 15-30). "C++" means comments in
`native/di1compat` cite it.

| File | KB | Purpose | Depended on by | Verdict | Key evidence |
|---|---|---|---|---|---|
| arenapatch.py | 2.8 | Patches the exe's render frame-arena immediates (FUN_00aae3e0); `--revert` | nothing; README:248; FORMATS §13 (820-833) | Keep, but confirm it is still needed | di1compat does no arena handling (no hits for `aae3e0`, `0x300000`, `prearena`, `2150d68`). The README rewrite (4cac902) dropped "Fixed: arenapatch.py" from the crash list, and the install steps never run it |
| arenas.py | 1.4 | Arena sizes from a minidump | nothing; imports mdmp (arenas.py:7) | Keep (debugging), goes with arenapatch | README:248 |
| autobundle.py | 13.5 | 1.0 to 3.0 material resolution | conv.py:296, renderoracle.py:8, rentable.py:27; C++ render.hpp:3,495,652,768 | Keep | Feeds `di1compat_render.txt` and the `--render` oracle |
| btdblocks.py | 2.4 | `.btd` block tiling rule plus selftest | btdconv.py:26; btd.hpp:1; selftest:20 | Keep (reference) | Ported to btd.hpp |
| btdconv.py | 7.3 | 1.0 to 3.0 `.btd`/`.btm` | native30.py:389; btd.hpp:1; selftest:21 | Keep (reference) | The oracle for btd.hpp. Needs `btd-a-const.bin` at import (:28) |
| btdscan.py | 1.3 | Survey of worlds with several TriangleList meshes | nothing | **Remove** | btdblocks' `_check` (:33-40) counts the same SerializeIndex records and asserts the rule |
| chd.py | 13.6 | `.chd` codec, compile from Lua | chd.hpp:1; selftest:26 | Keep | |
| conv.py | 21.9 | Shared conversion library | meshexport:25, renderoracle:8, realm30:22, native30:43, expl30:18, variant:58; render.hpp:3,488,823,974; di1compat.cpp:609 | Keep (library and oracle) | `world_by_path`, `mat2quat`, `rename_files`, `TRANSFORM_FIELDS` and `LAYOUT` go dead if native30 and expl30 are removed |
| crabemod.py | 4.2 | Packages converter output as a CrabeLoader mod | nothing | **Remove** | README:240 lists it as superseded. It packages native30/realm30/expl30 `--assets` output (:3). Its docstring is also wrong about CrabeLoader (see the CrabeLoader section) |
| dct.py | 8.8 | `.dct` codec | mer.py:67, savegame.py:34; dct.hpp:1; selftest:24 | Keep | |
| di3arc.py | 13.1 | 3.0 archive codec | extract:31, meshexport:110, realm30:21, zones:40; arc30.hpp:1; selftest:16 | Keep | |
| diffworld.py | 9.1 | Field-by-field diff of worlds or files | nothing (no FORMATS or C++ citations) | Remove, unless you diff octtest output against shipped files | README:239 lists it as superseded |
| dispfind.py | 2.0 | Capstone sweep for a displacement or immediate | nothing | Keep (debugging) | README:249 |
| disx.py | 2.4 | Disassembles runtime addresses from pentane.log | nothing; in CI's run-on-import skip set | Keep (debugging) | README:249 |
| expack.py | 2.8 | Merges expendables packages into plain Lua with pop-in defaults | nothing | **Remove** | di1compat does this at load: di1compat.cpp:1073-1074 (rewards10), :3308 and sys.hpp:658-659 (pop-in) |
| expl30.py | 5.8 | Offline 1.0 exploder to 3.0, written into the install | nothing | **Remove** (imports native30, so they go together) | di1compat serves 1.0 exploders at load. README:228 still lists it as active; its "17/17 re-encoded" figure is luabc's selftest |
| extract.py | 3.9 | Install to extract tree | di1compat.cpp:371,516 cite its archive layout | Keep | README setup |
| fevcheck.py | 2.4 | Oracle for `octtest --fmod` | octtest.cpp:1763,1829 | **Broken, fix** | `SND = r'D:\SteamLibrary\...'` (:7); the whole check runs at import and reads `sys.argv[1]` (:17-19) |
| fevdeps.py | 1.7 | Oracle for `octtest --fevdeps` | octtest.cpp:2391 | **Broken, fix** | Hard-coded `D:\` path (:7); runs at import (:17-32) |
| foreign.py | 3.0 | Record kinds 3.0 never ships | conv.py:332 (`check_foreign`), used by realm30.py:118 (selftest), native30, expl30 | Keep (reference) | README:239 says superseded, but realm30's selftest needs it |
| gen30.py | 17.8 | 1.0 shader to 3.0 `.shd` plus HLSL | rentable.py:73; shdiff/mtbdiff/rendercmp:10; gen30.hpp:2,4,562; di1compat.cpp:3758; octtest.cpp:3207-3234 | Keep | Missing from the README tool tables; fails CI `--help` |
| graph.py | 11.7 | SubNetwork sizing rule, `expand()` | native30.py:42, expl30.py:17; selftest:19 | Keep (reference) | Its only consumers are superseded, but the selftest keeps it under the repo's rule |
| luabc.py | 3.5 | Lua 5.1 32-bit to 64-bit; `run_package` | zonereg:22, expack:14, expl30:88; selftest:23 | Keep | |
| matp.py | 9.9 | `.mtb` codec | 9 tools; render.hpp:203; selftest:17 | Keep | |
| mdmp.py | 2.1 | Minidump reader | arenas.py:7; FORMATS:9 | Keep (debugging) | |
| mer.py | 31.5 | Meridian codec, catalogue, mtc | native30:139,169; its catalogue feeds mertable; mer.hpp:1; selftest:25 | Keep | Large but cohesive |
| mertable.py | 5.7 | Generates `di1compat_mer.txt` | README:111,217; mer.hpp:12 | Keep, fix (see Gaps) | Fails CI `--help` (TypeError: `main(*sys.argv[1:7])`, :96) |
| meshexport.py | 13.3 | OBJ export, `--check` | selftest:27 | Keep | Imports Pillow (:124), which is missing from requirements.txt |
| mixercheck.py | 3.9 | Oracle for `octtest --mixer` | octtest.cpp:2087 | Keep, fix `--help` (IndexError :47) | Not in the README |
| mtb-retex.py | 3.8 | Repoints a working bundle's TEXB entries | nothing (FORMATS:499,544 only) | **Remove** | Its premise (merged bundles render nothing, :3-13) was disproved by b45315f "matp merge verified in-game". It also duplicates matp's BNDL/TEXB parsing (:22-40 vs matp.py:85-102) |
| mtbdiff.py | 5.3 | One-off audit: 1.0 materials through gen30 vs shipped 3.0 | nothing | **Remove** | Not in README/FORMATS/C++; fails pyflakes at :55 |
| native30.py | 27.1 | Offline native world conversion | expl30.py:20; render.hpp:1 (historical mention) | **Remove** | README:238 lists it as superseded; 15 FORMATS citations to re-source |
| nodedump.py | 1.7 | Prints one geometry node's records | nothing | Keep (debugging); could merge into an `oct.py` dump command | No check of its own |
| oct.py | 17.8 | Tuple-file codec | about 22 importers; oct.hpp:1,3; octtest.cpp:1; selftest:15 | Keep | |
| par.py | 0.5 | `pmap` | 24 import sites | Keep | |
| particles.py | 10.4 | `.particledata` layouts and conversion (vfx.hpp oracle) | particles_val30:6; vfx.hpp:1; octtest.cpp:3143 | Keep (reference), fix | `D:\DI-Extract` fallback (:33-34) instead of `paths.py`; fails CI `--help` |
| particles_val30.py | 3.0 | Independent 3.0 particledata validator | nothing | **Broken, merge** into `particles.validate30` (particles.py:146) | `sys.path.insert(0, r'D:\DI-Extract\wt\vfx')` (:5); reads data at import (:9); its docstring calls it `val30.py` (:3). Its extra checks should move to validate30. The two disagree on param key 0xfff (validate30 accepts it at particles.py:166; this file rejects it at :29-33) |
| paths.py | 4.6 | Root resolution | 55 import sites | Keep | Docstring :11 says "the PowerShell tools call this"; none does |
| pentane_install.py | 5.8 | Installs Pentane, patches the exe | README:104 | Keep | |
| portmap.py | 12.0 | Generates `ports10.hpp` | ports10.hpp:1-2; cho.hpp:31,36; README:203 | Keep | |
| ranges.py | 1.9 | Values and counts above the shipped maximum | nothing | **Remove** (with `shippedranges.json`, its only reader, :39) | README:239 lists it as superseded |
| realm30.py | 8.8 | Offline realm conversion plus selftest | octx.hpp:885,899,913; di1compat.cpp:196; selftest:22 | Keep (reference) | The C++ `realmFile10` rule cites it |
| rendercmp.py | 4.3 | One-off audit: shader outputs, ours vs 3.0 | nothing | **Remove** | Not in README/FORMATS/C++; Windows-only (`sm3.d3ddisasm`) |
| renderoracle.py | 1.2 | Oracle for `octtest --render` | octtest.cpp:259; README:153 | Keep, fix `--help` (TypeError, :29) | Reads roots only from the environment and has no flags |
| rentable.py | 7.4 | Generates `di1compat_render.txt` | README:110,217; render.hpp:4; gen30.hpp:4 | Keep | |
| savegame.py | 7.7 | PC save codec | selftest:30 | Keep | |
| selftest.py | 3.5 | Runs the self-tests | README:156 | Keep | Needlessly in CI's skip set; it has a `__main__` guard (:73) |
| shd.py | 9.4 | `.shd` decode, both games | gen30:31-32, ssf:16, rendercmp/shdiff/mtbdiff:10 | Keep | Missing from the README tables; fails CI `--help` |
| shdfmt.py | 4.9 | 3.0 `.shd` passes, ISGN, slot roles | autobundle, meshexport:88, rentable, slotcheck:17 | Keep | |
| shdiff.py | 7.7 | Audit: gen30 output vs shipped 3.0 shaders | gen30.hpp:633 cites its 3794/3802 result | Keep (reference), fix pyflakes :126 | Not in the README |
| skinlayout.py | 3.1 | Skinned vs rigid layout check | README:213 | Keep, fix `--help` | |
| slotcheck.py | 3.0 | Texture-binder check | selftest:29 | Keep | |
| sm3.py | 21.1 | D3D9 SM3 parse, HLSL, d3dcompile | gen30:31, rendercmp:10; sm3.hpp:1,78,141 | Keep | Fails CI `--help`; needs `d3dcompiler_47.dll` (undocumented) |
| ssf.py | 10.2 | Scene-settings `.ssf` / SSET | render.hpp:208,214 | Keep (reference), fix pyflakes :187 | Not in the README |
| streamlayout.py | 3.2 | Asserts stream-major layout | selftest:28 | Keep | Runs on import (CI skip set) |
| subnet.py | 3.6 | SubNetwork count "fitting stage" | native30.py:45 only | **Remove** | `fit()` (:77-96) prints candidates and asserts nothing. The rules now live in octx.hpp:100-106. FORMATS.md:376,889 ("no rule yet") are stale |
| variant.py | 6.1 | Variants of the installed offline world for launch bisection | nothing | **Remove** | Works on `<game>/assets/worlds/out_intro2` (:6,16), which only native30 writes. Also named in CI's skip set (check.yml:24) |
| vocheck.py | 8.7 | Oracle for `octtest --vo` | octtest.cpp:2146,2150,2380 | **Broken, fix** | Hard-coded `D:\` path (:12); runs at import (:117-163) |
| vtxf.py | 12.2 | `vformat.vtxf`, vertex conversion | 8 importers; render.hpp:23; selftest:18 | Keep | |
| zonereg.py | 6.6 | Offline zone registration (describes itself as SUPERSEDED, :5-7) | gameplay.hpp:312,315,366; luachunk.hpp:162 | Keep (reference) | The `zones10` C++ code is checked against it; also provides `--revert` |
| zones.py | 8.3 | Zone list inspect and edit | nothing | Keep | Still the tool for inspecting 3.0 records |
| btd-info.ps1 | 2.2 | `.btd` header dump | nothing (FORMATS:620,653,656,867,902 only) | **Remove** | Superseded by btdblocks/btdconv. It reads 3.0 layout offsets only, so its numbers are wrong on 1.0 files (:17-19 vs FORMATS:622, :627-631). It reads only the first block, line 25 is dead (overwritten at :26), and the `.btm` note (:7-8) is a comment only |
| capped.ps1 | 1.5 | Runs a command under a process-tree memory cap | portmap.py:17 | Keep | Its example names native30 (:4) |
| harness.ps1 | 9.0 | Unattended in-game test through CrabeLoader | ui.hpp:171 | Keep, fix | `$Game = 'D:\SteamLibrary\...'` (:19); defaults to `-Zone OUT_Intro2` (native30's output zone) while ui.hpp:171 runs it with IN_City. See the CrabeLoader section for what changed upstream |
| lua.ps1 | 1.2 | Runs one Lua chunk through CrabeLoader | README:246 | Keep, or merge into harness.ps1 | Near-copy of harness.ps1:29-44; `D:\` default (:4) |
| shot.ps1 | 0.7 | Screenshot | nothing (no references, not in README) | **Remove** | An older copy of harness.ps1's `Shot` (:53-63) without the DPI fix (:55-58), so it captures only part of a scaled screen |

## Broken tools in detail

- **`fevcheck.py`, `fevdeps.py`, `vocheck.py`**:
  - They hard-code the 1.0 sound folder on `D:\SteamLibrary`, against README:170 ("No tool assumes a machine").
  - They have no `__main__` guard, so merely importing them runs the whole check.
  - They glob with a Windows-only `r'\*.fev'`.
  - Fix: take the 1.0 install as an argument (as `mixercheck.py` does) and add a guard. octtest.cpp cites all three as oracles, so do not delete them.
- **`particles_val30.py`**: depends on `D:\DI-Extract\wt\vfx` and reads game data at import. Most of its checks repeat `particles.validate30` (particles.py:146-170); its extra checks belong there.

## CI is red today

`.github/workflows/check.yml` was last changed in 6207be7 (2026-10-12). The tools that fail were added after that.
- **Lint fails.** pyflakes exits 1 with four warnings (reproduced):
  - `mtbdiff.py:55`: unused `e`
  - `shdiff.py:126`: unused `sa`, `sc`
  - `ssf.py:187`: redefinition of `paths`
- **The import step would fail.** CI runs it with `sys.argv == ['-']`:
  - `fevcheck` raises IndexError at :19, and it is first in sort order.
  - `fevdeps` (:26) and `vocheck` (:137) would fail the same way.
  - `particles_val30` raises FileNotFoundError at :9.
- **The `--help` step would fail for 15 tools**, because they don't parse `--help`:
  - `paths` SystemExit: gen30, shd, sm3, shdiff, rendercmp, mtbdiff, skinlayout.
  - TypeError: mertable (:96), renderoracle (:29).
  - IndexError: mixercheck (:47).
  - FileNotFoundError: particles (:176), particles_val30, fevcheck, fevdeps, vocheck.
- No `.ps1` is checked, and nothing native is built (see the test-driver section).

## Gaps in tools you keep

- **`di1compat_mer.txt` cannot be rebuilt from the repo alone.** README install step 3 requires it, but `mertable.py` needs inputs that no tool in the repository produces: `regclasses_30.json`, `pins10_all.json`, optionally `defs30.json` and `classdefs_30.json`. Its own docstring (mertable.py:6-21) calls them "research scratch" and "from the exe". Only `catalogue.json` comes from `mer.py catalogue`.
- **`octtest --gen30` compares against a "gen30.py dump dir"** of `.shd` and `<shader>.<id>.hlsl` files (octtest.cpp:3207-3208). `gen30.py`'s command line (:363-368) only shows one shader or `--all`; it has no dump mode.
- **README tool tables are missing** gen30, sm3, shd, ssf, shdiff, particles, fevcheck, fevdeps, vocheck and mixercheck.
- **requirements.txt**:
  - Every listed package is used.
  - **Pillow is missing**: meshexport.py:124 needs it. CI installs it (check.yml:17), so CI hides the gap.
  - The lupa comment ("only luabc.run_package") is stale: `zonereg.py:32` imports `lupa.lua51` too.
- **Undocumented system dependency**: sm3, gen30 and rendercmp need `d3dcompiler_47.dll` (sm3.py:345, :443). rentable does not.
- **`.gitignore`** ignores `native/**/build/` but not the root `build/` that README:95 tells you to create (`-B build`).
- **`btd-a-const.bin`** (120 bytes, three non-zero bytes) is still read at import by `btdconv.py:28`, and btd.hpp:33-38 embeds the same constant. Keep it while btdconv stays (or inline it as a bytes literal).
- **`__pycache__/`** is ignored and untracked. It is fine.

## Helpers duplicated across tools

1. **Three Lua table-literal parsers:**
   - `vtxf.lua_table` (vtxf.py:31-64; used by zones.py:28, an odd home for it)
   - `chd.lua` (chd.py:162-192)
   - `mixercheck.tokens/parse` (mixercheck.py:14-42)
2. **Three DXBC chunk walkers:** `shdfmt._chunk` (shdfmt.py:19-28), inline in `shd.rdef` (shd.py:103-108), inline in `shdiff.sig` (shdiff.py:17-23).
   - RDEF is parsed twice: `shdfmt.textures` and `shd.rdef`.
   - ISGN is parsed twice: `shdfmt.isgn` and `shdiff.sig`.
3. **Three shader-folder lookups:** `matp.shd_dir` (matp.py:28), `shd.shader_dir` (shd.py:46), `shdfmt.shd_path` (shdfmt.py:69).
4. **Two `.shd` pass readers:** `shd.parse` and `shdfmt.passes` (shdfmt.py:63-66).
   - `shdfmt.passes` walks from 0xF8 to the end of the file instead of stopping at the pass count, so trailing data is read as passes.
   - Only `fp.exists()` at :86 filters the result. The impact is not verified.
5. **Small helpers defined twice:**
   - `_al`: matp.py:43 and ssf.py:25.
   - `_cstr`: matp.py:47 (latin-1, returns the end) and shdfmt.py:31 (utf-8, value only).
6. **`h131` twice, and they disagree:** particles.py:49-54 sign-extends each byte; vocheck.py:13-16 does not. They give different results for bytes 0x80 and above.
   - The engine sign-extends. Disassembly of 1.0 `FUN_0040bac0` and of 3.0 `FUN_0040bb50`/`FUN_0040bb80` shows `movsx` in all three.
   - So particles.py is right and vocheck.py is wrong for non-ASCII names.
   - The C++ has the same split (`vfx.hpp:36` correct, `audio.hpp:921` not); see the cross-cutting section.
7. **BNDL/TEXB parsing:** `matp.Bundle` and `mtb-retex.chunks/texb_entries`.
8. **Hard-coded `D:\` paths outside paths.py:** fevcheck:7, fevdeps:7, vocheck:12, particles:33-34, particles_val30:5, harness.ps1:19, lua.ps1:4.
9. **PowerShell copies:** the CrabeLoader Lua runner (harness.ps1:29-44 vs lua.ps1:5-20) and the screenshot code (harness.ps1:53-63 vs shot.ps1:3-9).
10. **Others:**
    - "Count plus examples" tallies: shdiff.py:41-50 and mtbdiff.py:43-50.
    - The ID3DBlob vtable boilerplate twice in sm3.py (:341-362, :439-452). `d3ddisasm` is also defined after the `__main__` block (:420 vs :439).
    - PE RVA-to-file-offset helpers: pentane_install.py:36-40 and disx.py:35-39.
    - Geometry-node enumeration: nodedump.py:11-27 and `conv.geo_nodes` (conv.py:35-45).

Not duplicates:
- `vtxf.name_hash` and `shd.ehash` differ on purpose (case-insensitive vs case-sensitive, documented at shd.py:28-29).
- `dct.lookup2` is already shared by mer.py and savegame.py.

No Python tool is a god-script. mer.py (31.5 KB) and native30.py (27.1 KB) are large but cohesive.

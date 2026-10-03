# DI3Tools

Tools for porting Disney Infinity 1.0 worlds into 3.0 (PC Gold Edition). Only verified tools are kept here. The superseded experiments stay in `..\_tools`.
Format notes: https://claude.ai/artifact/FNnTo1rpHJ7UAWH7UoCooP

| Tool | What | Verified by |
|---|---|---|
| `oct.py` | `.oct` scene container read/modify/write. Writes the header `0x14` record count, which the engine allocates from | `--selftest`: byte-exact round-trip of every world `.oct` |
| `world30.py` | Converts any 1.0 world to 3.0 (`--source --world --offset --bundle x.mtb|auto [--static] [--dry]`). Converts vertex formats per primitive (`vtxf.plan`) and uses the 32-bit IndexBuffer 2 above 65535 vertices per node. `--static`: flattens the scene hierarchy to world matrices, shows LOD0 only, and drops gameplay links | sumo + `adv_cbt_protectmonolithsmall` load in-game; reproduces sumo byte-for-byte |
| `graph.py` | Meridian graph sizing per geometry node | `--selftest` re-derives dor_bay n=2 and lb_zonetwo n=3 exactly |
| `split-mesh.py` | Vertex/index buffer splitting: primitive-major (multi-node) and stream-major (multi-primitive node) | `--selftest` |
| `mtb-retex.py` | Retextures a shipped `.mtb` by swapping TEXB name hashes only. Use this instead of synthesising bundles | sumo's 3 textures rendered in-game |
| `matp.py` | `.mtb` codec from the engine's own parser (FUN_00472590). Per-material block sizes come from the shader (`.shd` u16 @0x6a + @0xa2), and block B opens with `shd[0xa1]` texture slots (u32 TEXB index, 8 B stride). Reads 1.0 bundles too (`Bundle(raw, SHD_DIR_10)`: sizes at `.shd` 0x46/0x6a, slots at 0x69, 4 B stride; 9,638/9,638 predicted exactly). `merge()` combines materials from shipped bundles verbatim and only rebases texture indices | `--selftest`: layout predicted for 48,057/48,057 shipped bundles; 26,416/26,416 TEXB+MATP rebuilt byte-exact; sumo with its 3 real merged materials renders in-game, 0 D3D throws |
| `autobundle.py` | Builds a world bundle per material, most faithful first: **real** (same GUID ships in 3.0, verbatim) > **same-shader** (3.0 material with the same logical shader, holding the 1.0 material's own textures) > **generic** (common 3.0 shader that accepts the vertex format, own textures in order). Substitutes keep the 1.0 GUID and hash. Checks vertex format per primitive and lists 1.0 textures to copy into the pool. `world30.py --bundle auto` uses it and writes nothing if unresolved | sumo resolves byte-identical to the in-game-verified bundle; `--survey`: 3,748/4,424 material slots resolve (2,280 real, 1,143 same-shader, 325 generic); monolith (3 real, 5 same-shader, 1 generic) renders correctly in-game |
| `mtb-build.py` | `.mtb` grouped-header layout reader/builder (n<=2) | `--selftest` rebuilds stock out_intro2.mtb byte-exact |
| `btd-convert.ps1` | 1.0 -> 3.0 collision `.btd` (needs `btd-a-const.bin`), `-Offset` must match the mesh offset | 323/330 shipped pairs rebuilt byte-exact; collision works in-game |
| `btd-info.ps1` | `.btd` header dump | |
| `zone-info.ps1` / `show-zone.ps1` / `add-zone.ps1` | Menu/zone registration (needs `_ShowInMenu_` + both `available*Build` flags) | used for every launch |
| `par.py` | `pmap`: process pool over all cores (CPU-bound Python can't use threads). Used by the corpus scans and world30's vertex conversion | parallel caches byte-identical to serial; oct self-test 2 min -> 8 s, matp 3m17 -> 41 s |
| `capped.ps1` | Runs a command and kills its whole process tree above `-MaxGB`. The `python` on PATH is a launcher stub, so the real interpreter is a child process | kills a 400 MB child under a 200 MB cap |
| `arenapatch.py` | Exe patch: render frame arenas A/B/C 1.25 MB/3 MB/16 KB -> 5 MB/16 MB/64 KB (FUN_00aae3e0). Retail bump allocators return NULL on overflow; Metroville crashed in the draw loop. Verifies exact original bytes, backs up, `--revert` | sizes read live from dumps (`arenas.py`); no fixed block-size constant in the exe |
| `arenas.py` | Reads the 3 render contexts' frame arenas (cur/end/base) from a minidump | |
| `dispfind.py` | Capstone sweep of the exe for a `[reg+disp]` operand or an immediate, on all cores | found the arena config writer |
| `btdblocks.py` / `btdscan.py` | Multi-mesh `.btd` survey: one block per TriangleList shape (SerializeIndex). The end-of-block rule is partial (24/98 1.0, 122/267 3.0) | WIP |
| `slotcheck.py` | Per-material texture-binder check: PS slot range vs block B, TEXB index range, and slot encoding (`(idx,0)` / `(ffffffff,ffffffff)` only) | flags the 122 bad Metroville materials; 0 after the autobundle fix; 0/836 shaders read past block B |
| `ranges.py` | Every `.oct` field and per-file record count vs the largest any shipped 3.0 world uses (cache `shippedranges.json`) | run on Metroville |
| `variant.py` | One-variable variants of the INSTALLED world for launch bisection: `--keep 178-188,199-220` materials, `--visible a:b`, `--rendercaps`, `--refmax`, `--dedupe`, `--padtex`, `--restore`. Always starts from `_backupariant-base.*` | `--keep 178-220` reproduces `--materials 43 --skip 178` byte-exact |
| `nodedump.py` | Dumps one geometry node's records for diffing | |
| `streamlayout.py` | Corpus check: multi-primitive nodes are stream-major | 4915/4915 shipped nodes |
| `disx.py` | Disassembles at a runtime address from `pentane.log` (capstone + pefile) | |
| `vtxf.py` | `vformat.vtxf` parser. **`vformatCRC` = `name_hash(format GUID)`** (FUN_0044aa40) | hash reproduces `0x63ae710f` and sumo's `0xea1acc4c` |
| `shdfmt.py` | Per `.shd` pass: declared vertex format vs the VS's DXBC input signature (records at 0xF8, 80 B: +0 vformatCRC, +0x10 `<id>.vp`, +0x18 `<id>.fp`) | |
| `mdmp.py` | Minimal minidump reader, rebased to the exe's load address | reads the live vertex-format registry from `_dumps\*.dmp` |

## Vertex format path (from Ghidra, 2026-10-02)
- `shaders/vformat.vtxf` is loaded by `FUN_004642f0` into a registry at `0x02170664`, with 52 B per format and the count at `0x0216f660`.
- At runtime the registry holds 41 entries: slot 0 is empty and the rest are the 40 formats from the file.
- The primitive loader `FUN_004553c0` stores `lookup(vformatCRC)` as a **byte**. An unknown CRC returns `0xbadddec1`, which truncates to slot `0xc1`. That slot is empty, so the per-mesh input layout (`FUN_0045c5c0` -> `FUN_0044d430` -> `ID3D11Device::CreateInputLayout`) is built from nothing.
- 1.0's `ea1acc4c` (sumo, monolith) has a declaration identical to 3.0's `63ae710f`. 12 of the 44 1.0 formats have an exact 3.0 twin (`vtxf.py`).

## Status (2026-10-03)
**Works in-game:** sumo (3 real materials) and monolith (`--bundle auto`: 3 real, 5 same-shader, 1 generic), both with geometry, collision, lighting and correct colours.

**Metroville (`in_city --static`):** materials (442) and vertex formats convert, and the world gets through loading. Crashes so far, each fix moving it later:
1. Physics body with a null shape (0x7bcc0c). Fixed: static mode uses the donor floor collision.
2. d3d11 crash in `PSSetShaderResources` from the texture binder (FUN_0045a1c0, call at 0x45a2c4).
3. Draw list NULL in the draw loop (0x47d330), frame arena overflow. Fixed: `arenapatch.py`.
4. Heap block-list corruption (0x403fc0). Fixed: static mode strips every record kind 3.0 never ships (`foreign.py`), including a duplicate 1.0 LayoutPool.
5. **SOLVED (2026-10-03):** foreign texture-slot encodings. `autobundle._substitute` wrote only the 4-byte index of each 8-byte block B slot, leaving the donor's second word: 118 slots `(idx, ffffffff)` and 4 `(ffffffff, 0)`, forms that occur 0 times in 25,052 shipped slots (always `(idx, 0)` or `(ffffffff, ffffffff)`). The engine then corrupts its heap (a GeometryNode's allocator record at 0x403fc0) or binds a garbage SRV (the d3d11 AV). Found by launch bisection with `variant.py --keep` down to a crashing pair (materials 198 + 217) that each load alone. Fixed in autobundle; `slotcheck.py` now flags the mixed forms. **Full Metroville loads in-game** (442 materials, 2,067 nodes; user: "it loaded and the skybox has changed as well"). The "t9 mask" and "material count" leads were wrong.

**Open:** multi-block `.btd` conversion (Metroville has 724 blocks, monolith 2; `btdblocks.py` is WIP); shared vertices duplicated (52 MB vbuf); generic materials dropping vertex colour.

## Status (2026-10-03, end of session)
**Metroville runs natively with collision** (b84fbf0): `world30.py --source ...\in_city --world out_intro2 --bundle auto --offset 76,-165,-404 --static --realm rlm_moa_motunui_day`. Backup `_backup\metroville-COLLISION-WORKS-2026-10-03`.

**Expendables (buildings) port, native 1.0 system, IN PROGRESS** (a7b8e9e +):
- World: `ExpendablePool` + 549 `Expendable` links kept; each socket gets a graph entry (Translation + Orientation). 3.0 still registers both (FUN_01604820).
- Package: 3.0 loads `assets/expendables_data/<playset>.lua`. `expendables_data.zip` is renamed `.disabled` and the 17 packages are loose; `playsetx.lua` = `expack.py` merge (plain Lua text) of 3.0 playsetx + 1.0 `terrainbuildingsexpendables`. All 198 1.0 definitions register (verified in a memory dump).
- Control: 1.0 entries pointed at `Exploders/psx_Barrel` (+ its DnaFile) -> barrels appeared at the sockets in-game.
- Models: 189/195 exploders converted (`expl30.py`) and packed as `assets/exploders/xx/name.zip` (`di3arc.py`, 2961/2961 shipped archives re-packed byte-exact). The engine only sees assets inside archives.
- **CONFIRMED: buildings spawn natively** once the 1.0 `.dnax` are inside `dna.zip` (all 85 exploder archives opened in-game). Remaining: all pieces bright green (same tint the psx_Barrel control showed until picked up, so likely a state tint), most faces culled, only some sockets filled.
- (earlier) Real models: no building appeared and no exploder archive was requested. **Hypothesis then:** the 1.0 `.dnax` files were loose and the dna lookup is archive-only, so spawning aborts before the model. **Fix installed, NOT yet launched:** the 3 dnax were added into `dna.zip` (`di3arc.add_files`; shipped copy in `_backup\dna.zip.shipped`).
- Missing in the 1.0 extract: bstone01_socket01_04(_d), bstone01_socket02_04(_d), mfact05_socket05(_d). Look in the 1.0 install's archives.
- Undo list: `_backup\expendables-test-files.txt`.

## Status (2026-10-03, end of day) -- native Metroville
`native30.py` (replaces the deleted world30; shared code in `conv.py`) builds in_city natively: hierarchy, cells, bounds kept; only world-space fields offset so the city's densest block sits at the spawn origin; pools written in shipped order (graph before associations -- this is what made the 549 building sockets spawn). In-game: full city, ground-floor expendables, spawn on a street. Open: VFX render solid magenta / over-bloomed in this world only; a cluster of odd meshes (black overhead structure, rubble wall) at the spawn; some walls show a mottled mask texture; no ledges/interaction (gameplay data = plan B: convert Metroville's own graph + .mer).

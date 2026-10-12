# DI3Tools

Tools for porting Disney Infinity 1.0 worlds into 3.0 (PC Gold Edition). Only verified tools are kept here. The superseded experiments stay in `..\_tools`.
Format notes: https://claude.ai/artifact/FNnTo1rpHJ7UAWH7UoCooP

## Setup
- Python 3.10+, `pip install -r requirements.txt`. `lupa` is only needed by `luabc.run_package` (`expack.py`, `expl30.py --package`).
- No tool assumes a machine. Three roots, each from the command line, else the environment (`paths.py`):

  | root | flag | env | what |
  |---|---|---|---|
  | game | `--game` | `DI3_GAME` | the 3.0 install; found through Steam when unset |
  | 3.0 extract | `--ext30` | `DI_EXT30` | the folder holding `assets/`, as `extract.py` writes it |
  | 1.0 extract | `--ext10` | `DI_EXT10` | the same for 1.0 |

  `python paths.py` shows what resolves and from where. Output tools (`native30`, `realm30`, `expl30`) write into the game's `assets/` unless `--assets <dir>` names another (a mod folder, a staging copy).
- Build the extracts once: `python extract.py "<3.0 install>" D:\DI-Extract\3.0` and the same from the 1.0 install (its zips are plain). `--only worlds shaders ...` limits it to subtrees. `autobundle` needs `textures`, `materials`, `shaders` and the asset folders it indexes; a full extract is simplest.
- Corpus measurements (`matindex.json`, `donors.json`, `texindex.json`, `shippedkeys.json`, `shippedpools.json`, `filledroles.json`) are built on first use next to the scripts (`paths.cached`, versioned) and store paths relative to the extract. Delete them after extracting another game build. `shippedranges.json` is committed; it was rebuilt from the stock Steam 3.0 Gold install.
- `python selftest.py` runs every self-test against the configured roots (one line each; a test whose roots are missing is skipped). 2026-10-12, 3.0 extract only: 13 passed, 2 skipped (need the 1.0 extract).

## Tools
Numbers measured on the stock Steam 3.0 Gold install (extract of worlds, shaders, materials, realms, exploders, expendables_data, lang, startup2) unless dated otherwise.

| Tool | What | Verified by |
|---|---|---|
| `paths.py` | Root resolution shared by every tool (above) | |
| `extract.py` | Install -> extract tree: `X/name.zip` -> `X/name/` (3.0 index archives through their index; 1.0 plain zips). Long-path safe | 4,115 archives extracted; every other tool's self-test runs on the result |
| `oct.py` | Tuple files (`.oct/.mer/.merdebug/.bent/.banm/.bct`) read/modify/write. Types decode from their bit fields (shape, keyed, kind, count width, int width), which covers `.mer`'s 0x17/0x27 and 0x11F/0x21F that no table had. Ints are signed. `set_name(s)`/`set_names` for name records. Writes the header `0x14` record count, which the engine allocates from | `--selftest`: 5,079/5,079 3.0 tuple files byte-exact, and rewriting every int/float record is a no-op |
| `di3arc.py` | 3.0 archive codec. Key detected from the header on read (default, PSX, or none); `key_for(path)` = PSX when `psx` appears anywhere in the path under `assets/`. New members carry the shipped `KF 0x13 'MD5'` extra. `list` / `unpack` / `pack` | 21,133/21,133 shipped archives re-packed byte-exact (19,297 default key, 1,047 PSX, 789 unencrypted); `--selftest`: 2,961/2,961 exploders + `pack_dir` extras identical to shipped |
| `native30.py` | Native 1.0 -> 3.0 world conversion (`--source --world --zone [--start x,y,z / --start-locator] [--realm] [--donor] [--assets] [--translate] [--dry]`) | in-game: Metroville (see Status) |
| `conv.py` | The pipeline `native30` / `realm30` / `expl30` share: `prepare` (materials, dropped pools, geometry + vertex formats, RenderCaps), `rename_files`, `check_foreign`, `copy_textures`; the shipped pool order measured over every 3.0 `.oct` (`shipped_pools`, `sort_pools`, `foreign_pools`); buffers, record copy, `world_matrices` | pool order: 0 conflicting pairs over 3,905 shipped files, and it agrees with the hand-written order it replaces on every observed pair |
| `realm30.py` | 1.0 realm -> 3.0 `realms/<name>.zip` | `--selftest` reproduces shipped `rlm_intro_day` records (needs the 1.0 extract) |
| `expl30.py` / `expack.py` / `luabc.py` | Exploders (expendable models); merged expendables packages (plain Lua); 32-bit big-endian Lua 5.1 -> 64-bit for lupa | `luabc --selftest`: 17/17 3.0 packages re-encoded; in-game: buildings spawn |
| `graph.py` | Meridian graph sizing per geometry node | `--selftest` re-derives out_intro2 (n=1), dor_bay (n=2), lb_zonetwo (n=3) exactly |
| `matp.py` | `.mtb` codec from the engine's own parser (FUN_00472590). Per-material block sizes come from the shader (`.shd` u16 @0x6a + @0xa2), and block B opens with `shd[0xa1]` texture slots (u32 TEXB index, 8 B stride). Reads 1.0 bundles too (`Bundle(raw, 1)`: sizes at `.shd` 0x46/0x6a, slots at 0x69, 4 B stride; 9,638/9,638 predicted exactly). `merge()` combines materials from shipped bundles verbatim and only rebases texture indices | `--selftest`: 48,057/48,057 layouts on a full extract (2026-10-03), 7,218/7,218 + 6,545/6,545 rebuilt byte-exact on this one; sumo with 3 merged materials renders in-game |
| `autobundle.py` | World bundle per material, most faithful first: **real** (same GUID ships in 3.0, verbatim) > **same-shader** (3.0 material with the same logical shader, holding the 1.0 material's own textures) > **generic** (common 3.0 shader that accepts the vertex format, own textures in order into the slots whose role shipped materials fill -- never shadow/dither/colorBuffer..., which no shipped material fills). Substitutes keep the 1.0 GUID and hash. Lists 1.0 textures 3.0 does not ship in its global pool | `--survey` (2026-10-03): 3,748/4,424 material slots resolve; monolith renders correctly in-game |
| `vtxf.py` | `vformat.vtxf` parser and vertex conversion. **`vformatCRC` = `name_hash(format GUID)`** (FUN_0044aa40). Converts between any real types (float16/32, unorm8, snorm16, 3 <-> 4 components) and between integer types; missing components from the 3.0 default | `--selftest`: all 12 vformatCRCs used by shipped 3.0 `.oct` hash from a 3.0 GUID; conversion round trips; `ea1acc4c -> 63ae710f` twin (with the 1.0 extract) |
| `meshexport.py` | World / exploder / realm geometry -> OBJ in world space (positions, UVs, normals, a material per primitive), either game | `--check`: 57,978 shipped 3.0 primitives stay inside their buffers under the 3.0 Idata rule; exported psx_barrel, dor_bay, psx_l3_planet_main render correctly |
| `dct.py` | `.dct` localisation dictionaries: `list` / `export` (JSON) / `import`, new keys hashed with lookup2 + the file's initval | `--selftest`: 471/471 shipped files round-trip and export->import byte-exact; hash reproduces 337,023/337,023 stringids keys |
| `btdconv.py` | 1.0 -> 3.0 collision `.btd`, every block, + its `.btm` block table (layout in its docstring; `btd-a-const.bin` = the 120-byte section A constant). Replaces `btd-convert.ps1` | 323/330 single-block shipped pairs rebuilt byte-exact (2026-10-03); collision works in-game |
| `btdblocks.py` / `btdscan.py` / `btd-info.ps1` | `.btd` block tiling (one block per TriangleList shape), survey, header dump | `btdblocks --selftest`: block count = shapes in 267/267 3.0 worlds |
| `zones.py` | Zone records: `info` (effective fields through `_Inherits_`), `menu` (the gate: `_ShowInMenu_` + both `available*Build` flags), `show`, `add`. Reads/edits `--zonelist FILE`, the loose install copy, or straight inside `startup2.zip` (`di3arc.add_files`, `.orig` kept) | edits byte-identical to the three retired `.ps1` tools on 6 cases; startup2.zip edit + revert round-trips on a copy (in-game: untested) |
| `chd.py` | `.chd` compiled gamedb tables (inventory, locks, rumpusdata): `dump` / `export` (JSON) / `import`; `lua()` compiles a gamedb `.lua` | `--selftest`: 33/33 shipped files round-trip and export->import byte-exact; 29/30 sibling `.lua` compile to their `.chd` exactly (the 30th differs in the shipped data) |
| `savegame.py` | PC saves (Toy Box `S...A`, its header/screenshot, P0 settings, help): `show` / `export` / `import`, checksums (lookup2) and zlib recomputed | `--selftest <dir>`: 16/16 local saves decoded, both checksums verified, round trip byte-exact (read-only); in-game acceptance untested |
| `crabe/di3tools/` | CrabeLoader mod (with CrabeMenu: F5 > Mods > DI3Tools): log the player position as `--start=x,y,z`, dump zone records, force-travel, swap the sky, spawn a `.dnax` | untested in-game; every API call cited against CrabeLoader's source |
| `slotcheck.py` | Per-material texture-binder check: slot count, TEXB index range, slot encoding | flags the 122 bad Metroville materials; 0 after the autobundle fix; 0/836 shaders read past block B |
| `foreign.py` / `ranges.py` | Record kinds 3.0 never ships; values/counts beyond the shipped maximum | run on Metroville |
| `variant.py` | One-variable variants of the INSTALLED world for launch bisection (`--world`, `--keep 178-188,199-220`, `--visible a:b`, `--rendercaps`, `--refmax`, `--dedupe`, `--padtex`, `--restore`); backup beside the world | `--keep 178-220` reproduces `--materials 43 --skip 178` byte-exact |
| `arenapatch.py` | Exe patch: render frame arenas A/B/C 1.25 MB/3 MB/16 KB -> 5 MB/16 MB/64 KB (FUN_00aae3e0). Verifies the original bytes, backs up to `<exe>.prearena`, `--revert` | sizes read live from dumps (`arenas.py`); no fixed block-size constant in the exe |
| `arenas.py` / `mdmp.py` | Frame arenas / any static address from a minidump, rebased to the exe's load address | |
| `dispfind.py` / `disx.py` | Capstone sweep for a `[reg+disp]` operand or an immediate; disassembly at runtime addresses (base = the last `EXE base` line of `pentane.log`, or `--base`) | found the arena config writer |
| `shdfmt.py` | `.shd` passes, VS input signature vs the vertex format, and `slot_roles`: what each material texture slot feeds, from the pixel shaders' DXBC resource tables (slot i -> t(first + i)) | 834/836 shaders name each slot unambiguously; shipped materials fill diffuse0 14,621/14,621 and dither 0/11,825 |
| `nodedump.py` / `streamlayout.py` / `subnet.py` | One node's records; vbuf stream layout (4,915/4,915 shipped multi-primitive nodes stream-major); SubNetwork count fitting | |
| `mtb-retex.py` | Swaps TEXB name hashes in a shipped `.mtb`. Superseded for synthesis by `matp.merge` | sumo's 3 textures rendered in-game |
| `par.py` / `capped.ps1` | `pmap` over all cores; run a command under a whole-process-tree memory cap | |

Retired 2026-10-12 (in git history): `mtb-build.py` (its "u16 index, u16 generation" slot model is the one that crashed Metroville; `matp.py` replaces it), `split-mesh.py` (replaced by `conv.build_buffers`), `btd-convert.ps1` (one block; `btdconv.py` does every block), `zone-info.ps1` / `show-zone.ps1` / `add-zone.ps1` (`zones.py`).

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
5. **SOLVED (2026-10-03):** foreign texture-slot encodings. (Refined 2026-10-12, see below: only the `(ffffffff, 0)` slots were lethal.) `autobundle._substitute` wrote only the 4-byte index of each 8-byte block B slot, leaving the donor's second word: 118 slots `(idx, ffffffff)` and 4 `(ffffffff, 0)`, forms that occur 0 times in 25,052 shipped slots (always `(idx, 0)` or `(ffffffff, ffffffff)`). The engine then corrupts its heap (a GeometryNode's allocator record at 0x403fc0) or binds a garbage SRV (the d3d11 AV). Found by launch bisection with `variant.py --keep` down to a crashing pair (materials 198 + 217) that each load alone. Fixed in autobundle; `slotcheck.py` now flags the mixed forms. **Full Metroville loads in-game** (442 materials, 2,067 nodes; user: "it loaded and the skybox has changed as well"). The "t9 mask" and "material count" leads were wrong.

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

## Status (2026-10-12) -- tooling pass
- Every tool is directory-agnostic (`paths.py`, `--game/--ext10/--ext30`, `--assets`); `extract.py` rebuilds the extract trees from an install.
- **Correction to crash 5** (disassembly of the slot resolver `Shader_ParseBindings`, 0x45D5C0, called from the `.mtb` parser): a slot is skipped only when `w0 & w1 == 0xffffffff`; otherwise the engine reads `table[w0]`, drops the old pointer's refcount and bumps the new one. So the 4 `(ffffffff, 0)` slots read `table[-1]` (garbage pointer + refcount write = the heap corruption / garbage SRV); the 118 `(idx, ffffffff)` slots resolved normally. The autobundle fix stands.
- `di3arc` key rule widened: the PSX key goes with `psx` anywhere in the archive's path (48 shipped archives, e.g. `choreographies/psx/.../temp_set.zip`, were read with the wrong key before); 789 sound/dialog archives are unencrypted.
- `oct.py` 0x1F is `key + int8` (shipped as `_DebugOnly_ParamValue = <param>, 0/255`), not a name; it raised before.
- `vtxf.py` converts colours/normals across types instead of padding them (the "generic materials dropping vertex colour" lead); conversion targets for formats without a 3.0 twin may change on the next Metroville build.
- `native30.py`: `--zone` is required (the start graph's namespace; it was always `OUT_Intro2`), the start is `--start` or a graph locator (no Metroville constant in the code), `--dry` writes nothing.
- `btdconv.py` no longer reads a donor `.btd`: the only donor bytes kept (block header +56..63) are `0c 00 06 00 02 00 00 00` in all 6,866 shipped blocks.
- Generalisation pass: one conversion pipeline in `conv.py`; `Oct.kids` / `oct.children` replace four `kids` variants; corpus caches through `paths.cached`; native30's hand-written pool order and drop list are measured (the measured order differs from the old list only for SoundSourcePool vs LightRigPool/ShadowCasterPool, which no shipped file holds together); the generic material tier fills material roles only. `GENERIC_SLOTS` (1.0 slot order of shader d0e71afc) and `NOT_REAL` stay hand-written: they need the 1.0 shaders' resource tables / an in-game observation.

## Next (needs the game running)
1. **Spawn.** Static analysis (2026-10-12) downgrades the "intro zone" lead: the avatar routine 0xDEA280 waits on a ReaderMgr *hold* named "Intro" (a set at ReaderMgr+0xB4C that also holds "LevelLoad"; check 0xDCC5D0), not on the zone's name; `WorldIsIntro` defaults to false; `OverrideInitialCreation` / `OverrideCreation` choose the avatar creation. None of them places the player. What does: zone records can take their start points from another zone (`PSX_L1_Planet_Main` has `AlternateStartPointZone = "PSX_L1_Planet_Details"`). Try a record with `AlternateStartPointZone` naming a zone whose start graph works, and log the live values with `crabe/di3tools` (Dump zone record).
2. **CrabeLoader + CrabeMenu as the test harness** (bink2w32 proxy, same exe build: TimeDateStamp 0x5851C5D1; our helpers are in `crabe/di3tools`): free camera (culled faces, the odd meshes at the spawn), `Game.LoadSkyDome` (does the magenta / over-bloomed VFX follow the realm?), `Game.CreateActor("DNAFile=...dnax", x, y, z)` (one exploder alone; the green tint: `ActorHasState`), `Game.LoadLevel(zone, true)` and `Game.ZoneString/Bool/Int` (zone records after `_Inherits_`), its Lua chunk log (which packages / `.dnax` load). Its `Shader_ParseBindings` / `Shader_LookupCrc` fallbacks are installed unconditionally (no `crabe.toml` switch), and they turn our bugs into missing textures: bisect crashes without CrabeLoader.
3. **Ship as a mod instead of editing the install.** Output tools take `--assets <game>/mods/<id>` for CrabeLoader's VFS (whole files keyed on the path after `assets/`). It cannot replace a member inside an archive (dna.zip stays `di3arc.add_files`) and loose files under a path a shipped zip covers stay invisible; zonelist and expendables packages could become in-memory chunk patches instead of disabled archives.
4. **Loose `.dnax`.** CrabeLoader's character mods serve loose `characters/<x>/<x>.dnax` (+ a DataMap `DNA|` entry), "verified in game", against our "dna lookup is archive-only". One launch with a loose exploder `.dnax` at its zip-internal path settles it.
5. **Missing 1.0 sockets** (bstone01_socket01_04(_d), bstone01_socket02_04(_d), mfact05_socket05(_d)): rerun `extract.py` on the 1.0 install; its zips are plain and are now read directly.
6. **2.0 Gold Edition** (none installed here): `extract.py` it, then `vtxf` twin count, `matp.py --ver` sizes, `oct.py --selftest` -- if 2.0 is closer to 3.0 than 1.0, the Marvel playsets port through `native30` and 2.0 bundles become exact-GUID donors.
7. Smaller: does the game accept an edited save (`savegame.py`) and a `zones.py` edit made inside `startup2.zip`; `subheaps.xml` as a data-only heap knob before more exe patches; the 1.0 exe address table in accountrev/infinity-restored-mp for runtime comparisons; derive `GENERIC_SLOTS` from the 1.0 shaders' resource tables once a 1.0 extract is at hand (`shdfmt.slot_roles` does the 3.0 side).
8. Never run dasorik's infinity-mod-tool on a working install: each apply deletes every file under `assets/` that is not in its stock list.

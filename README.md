# DI3Tools

Tools for porting Disney Infinity 1.0 worlds into 3.0 (PC Gold Edition). Only verified tools are kept here. The superseded experiments stay in `..\_tools`.
Format reference: [FORMATS.md](FORMATS.md) (every format these tools read or write, with what each fact was measured on).

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
- Corpus measurements (`matindex.json`, `donors.json`, `texindex.json`, `shippedkeys-<kind>.json`, `shippedpools.json`, `shippedassociations.json`, `filledroles.json`) are built on first use next to the scripts (`paths.cached`, versioned) and store paths relative to the extract. Delete them after extracting another game build. `shippedranges.json` is committed; it was rebuilt from the stock Steam 3.0 Gold install.
- `.github/workflows/check.yml` (Gitea / Forgejo / GitHub Actions; needs Actions and a runner on the server): lint, every module imports with no roots set, every tool answers `--help`. Game data cannot be in CI, so the data self-tests stay local:
- `python selftest.py` runs every self-test against the configured roots (one line each; a test whose roots are missing is skipped). 2026-10-12, 3.0 extract only: 13 passed, 2 skipped (need the 1.0 extract).

## Tools
Numbers measured on the stock Steam 3.0 Gold install (extract of worlds, shaders, materials, realms, exploders, expendables_data, lang, startup2) unless dated otherwise.

| Tool | What | Verified by |
|---|---|---|
| `paths.py` | Root resolution shared by every tool (above) | |
| `extract.py` | Install -> extract tree: `X/name.zip` -> `X/name/` (3.0 index archives through their index; 1.0 plain zips). Long-path safe | 4,115 archives extracted; every other tool's self-test runs on the result |
| `oct.py` | Tuple files (`.oct/.mer/.merdebug/.bent/.banm/.bct`) read/modify/write. Types decode from their bit fields (shape, keyed, kind, count width, int width), which covers `.mer`'s 0x17/0x27 and 0x11F/0x21F that no table had. Ints are signed. `set_name(s)`/`set_names` for name records. Writes the header `0x14` record count, which the engine allocates from | `--selftest`: 5,079/5,079 3.0 tuple files byte-exact, and rewriting every int/float record is a no-op |
| `di3arc.py` | 3.0 archive codec. Key detected from the header on read (default, PSX, or none); `key_for(path)` = PSX when `psx` appears anywhere in the path under `assets/`. New members as shipped: deflate 9 / memLevel 9, fixed DOS time, `KF 0x13 'MD5'` extra, kind + TEXB member order. `list` / `unpack` / `pack` | 21,133/21,133 shipped archives re-packed byte-exact (19,297 default key, 1,047 PSX, 789 unencrypted); `--selftest`: `pack_dir` from the unpacked files alone rebuilds 2,961/2,961 shipped exploders (365/365 realms, 576/579 worlds) |
| `native30.py` | Native 1.0 -> 3.0 world conversion (`--source --world --zone [--start x,y,z / --start-locator] [--realm] [--donor] [--assets] [--translate] [--dry]`) | in-game: Metroville (see Status) |
| `conv.py` | The pipeline `native30` / `realm30` / `expl30` share: `prepare` (materials, dropped pools, geometry + vertex formats, RenderCaps), `rename_files`, `check_foreign`, `copy_textures`; the shipped pool order measured over every 3.0 `.oct` (`shipped_pools`, `sort_pools`, `foreign_pools`); buffers, record copy, `world_matrices` | pool order: 0 conflicting pairs over 3,905 shipped files, and it agrees with the hand-written order it replaces on every observed pair |
| `realm30.py` | 1.0 realm -> 3.0 `realms/<name>.zip` | `--selftest` reproduces shipped `rlm_intro_day` records (needs the 1.0 extract) |
| `expl30.py` / `expack.py` / `luabc.py` | Exploders (expendable models); merged expendables packages (plain Lua); 32-bit big-endian Lua 5.1 -> 64-bit for lupa | `luabc --selftest`: 17/17 3.0 packages re-encoded; in-game: buildings spawn |
| `graph.py` | Meridian graph sizing per geometry node | `--selftest`: reproduces all 1,367 shipped SubNetworks of the generated shape (n up to 105) |
| `mer.py` | Meridian graphs (`.mer` + `.merdebug`): `show`, `catalogue` (every node type, its parameters and links), `mtc` (the `.mtc` class manifest) | `--selftest` over 584 graphs: 428,387 parameters decode and match their debug names; 117,117 links resolve; 584/584 `.mtc` rebuilt byte-exact |
| `matp.py` | `.mtb` codec from the engine's own parser (FUN_00472590). Per-material block sizes come from the shader (`.shd` u16 @0x6a + @0xa2), and block B opens with `shd[0xa1]` texture slots (u32 TEXB index, 8 B stride). Reads 1.0 bundles too (`Bundle(raw, 1)`: sizes at `.shd` 0x46/0x6a, slots at 0x69, 4 B stride; 9,638/9,638 predicted exactly). `merge()` combines materials from shipped bundles verbatim and only rebases texture indices | `--selftest`: 48,057/48,057 layouts on a full extract (2026-10-03), 7,218/7,218 + 6,545/6,545 rebuilt byte-exact on this one; sumo with 3 merged materials renders in-game |
| `autobundle.py` | World bundle per material, most faithful first: **real** (same GUID ships in 3.0, verbatim) > **same-shader** (3.0 material with the same logical shader, holding the 1.0 material's own textures) > **generic** (common 3.0 shader that accepts the vertex format, own textures in order into the slots whose role shipped materials fill -- never shadow/dither/colorBuffer..., which no shipped material fills). Substitutes keep the 1.0 GUID and hash. Lists 1.0 textures 3.0 does not ship in its global pool | `--survey` (2026-10-03): 3,748/4,424 material slots resolve; monolith renders correctly in-game |
| `vtxf.py` | `vformat.vtxf` parser and vertex conversion. **`vformatCRC` = `name_hash(format GUID)`** (FUN_0044aa40). Converts between any real types (float16/32, unorm8, snorm16, 3 <-> 4 components) and between integer types; missing components from the 3.0 default | `--selftest`: all 12 vformatCRCs used by shipped 3.0 `.oct` hash from a 3.0 GUID; conversion round trips; `ea1acc4c -> 63ae710f` twin (with the 1.0 extract) |
| `skinlayout.py` | Skinned vs rigid vertex layout: a skinned node's primitives (NumInfluences) are stored in their 3.0 format's `skin` variant, rigid ones in `main`. `vtxf.plan(.., 'skin')` + `rentable.py` R lines + di1compat `skinlayout10` write converted skinned primitives that way | 19,841 shipped exploders + choreographies: rigid 75,895/75,895 `main`, skinned 3,396/3,396 `skin` (six formats); `--assert` on `octtest --render` output (2026-10-14): 125/125 skinned in `skin`, the earlier build's 125 flagged |
| `meshexport.py` | World / exploder / realm geometry -> OBJ in world space (positions, UVs, normals, a material per primitive), either game. `--textures`: an .mtl with each material's colour (diffuse0) and normal (normalMap0) map as PNG, the .tbody found in the world folder, the install (loose or inside `textures/XX.zip`) or the extract | `--check`: 57,978 shipped 3.0 primitives stay inside their buffers under the 3.0 Idata rule; exported psx_barrel, dor_bay, psx_l3_planet_main render correctly; `--textures` on psx_l3_planet_main / dor_bay / holojim: every material textured (39 PNGs, 0 missing), a texture-sampled render matches the level |
| `diffworld.py` | Two worlds (folders) or files, field by field: tuple-file records addressed by identity (NodeName / MaterialName / Name ..., Association Type:NodeName) so an inserted node does not shift the rest; `.mtb` materials by GUID; buffers by bytes. Files pair up with the world name stripped (in_city_0.vbuf vs out_intro2_0.vbuf) | a world against itself: 10/10 identical; against a copy with 3 edits: exactly those 3 (+ the header count and name table) |
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
| `crabemod.py` | Converter output written with `--assets <game>/mods/<folder>` -> a CrabeLoader mod: `mod.json` (manifest v1, id checked), `--zip` for sharing, `--check` lists files a shipped archive's folder covers (the VFS serves whole files the game opens through the OS; a stock install has `worlds/out_intro2.zip`, so a port under a new world name is the safer mod) | tested on a stand-in folder; in-game: untested |
| `harness.ps1` / `lua.ps1` | Unattended in-game test: boots 3.0 through Steam, gets past the autosave notice and the title screen, opens character select, sets a sku and launches a zone with `characterSelected`; waits for the avatar, runs Lua probes, screenshots, quits. `lua.ps1` runs one chunk in the running game. Needs CrabeLoader built from source with `crabe/crabeloader-no-guards.patch` (upstream 2928d52; the patch turns off its fallback shader and Meridian array sanitizer) | 2026-10-12: fresh boot -> avatar in OUT_Intro2 at the configured start (-85.85,155.35,619.19) in ~35 s, 0 exceptions, clean quit |
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
5. **SOLVED (2026-10-03):** foreign texture-slot encodings (refined 2026-10-12, below). `autobundle._substitute` wrote only the 4-byte index of each 8-byte block B slot, leaving the donor's second word: 118 slots `(idx, ffffffff)` and 4 `(ffffffff, 0)`, forms that occur 0 times in 25,052 shipped slots (always `(idx, 0)` or `(ffffffff, ffffffff)`). The engine then corrupts its heap (a GeometryNode's allocator record at 0x403fc0) or binds a garbage SRV (the d3d11 AV). Found by launch bisection with `variant.py --keep` down to a crashing pair (materials 198 + 217) that each load alone. Fixed in autobundle; `slotcheck.py` now flags the mixed forms. **Full Metroville loads in-game** (442 materials, 2,067 nodes; user: "it loaded and the skybox has changed as well"). The "t9 mask" and "material count" leads were wrong.

**Open:** shared vertices duplicated (52 MB vbuf). (Multi-block `.btd`: `btdconv` converts every block; vertex colour: `vtxf` converts it.)

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

## Status (2026-10-12) -- shipped rules applied
Every rule below is measured on the stock 3.0 install and now applied by the tools (FORMATS.md has the details):
- **Spawn chain** (exe: StartPositions 0x13a85d0 asks the starts registered for the loading zone, then
  AlternateStartPointZone, then any zone, else returns the origin). A `.mer` is only parsed when the `.oct` has
  `ExternalFilePool [Meridian]` -- every shipped world with a `.mer` has it, none without; native30 now writes it.
  The `.mtc` beside a `.mer` is its node/link class manifest (`mer.mtc` rebuilds 584/584); native30 used to copy
  out_intro2's (Catalyst, RealmLoader), declaring no PlayerStartPosNode or LocatorNode, and now generates its own.
  The start graph is holohendy's ground start (LocatorNode -> PlayerStartPosNode + RealmCache), its locator pool and
  `Layout|LOC_Player` binding with it; the X-wing manifest of the old donor is gone.
- **Collision** as all 23,509 shipped instances: per instance a `COL_i` graph slot at the instance's Transform,
  CollisionInstanceToDataNode + CollisionAggregate on `Layout|COL_i|Transform|In WorldBasis.Transform`;
  `AggregateCount` = instances (3,905/3,905).
- **Order**: pools and associations in the shipped orders in all three converters; record kinds checked against
  the shipped files of the same kind (worlds / realms / exploders).
- **Archives**: `pack_dir` reproduces shipped archives from their files alone -- 2,961/2,961 exploders, 365/365
  realms, 576/579 worlds (deflate 9 / memLevel 9, fixed DOS time, kind and TEXB member order, MD5 extras).
- **Vertices**: colours/normals convert across types; a padded tangent handedness is +1 (shipped: +-1, never 0).
- **Graph sizing**: verified on 1,367/1,367 shipped graphs of the generated shape (a shared Transform node adds 32 B).
- **Crash 5, refined** (slot resolver `Shader_ParseBindings` 0x45D5C0): a slot is unset only when
  `w0 & w1 == 0xffffffff`, else `table[w0]` is read and refcounted -- the 4 `(ffffffff, 0)` slots read `table[-1]`;
  the 118 `(idx, ffffffff)` resolved normally.
- Checked, no rule to apply: expendable pop-in distances (size predicts the shipped pair in 31.6% of 453 models,
  a constant in 17.2%) -- `expack.PORT_DEFAULTS` stays explicit.

## Status (2026-10-12, main machine) -- fully native track started
Goal now (user): 1.0 Metroville running natively on 3.0 -- the 1.0 files served unaltered from the 1.0 install and
redirected into the 3.0 systems that replaced them; new systems only where 3.0 has none (native30 = the old
converted-copy approach). Research and roadmap: `D:\DI-Extract\_research\PLAN.md`.
- **Harness** (`harness.ps1`, `lua.ps1`): unattended boot -> zone -> avatar -> probes -> screenshot -> quit.
- **di1compat** (`native/di1compat`, Pentane plugin): hooks the asset open FUN_0041cb10; paths 3.0 lacks are read in
  place from the 1.0 install's zips and returned as engine `Genie::MemFile`s, through per-format load-time
  transcodes. `octtest.exe`: the C++ .oct codec round-trips 9,251/9,251 tuple files; OCT-1 regenerates the counts
  of 4,299/4,299 shipped processor-free SubNetworks.
- **IN_City registered** (`zonereg.py`): the 1.0 IN_City zone record verbatim + PlaysetProperties.Incredibles
  (1.0 record + 3.0 keys, ID 11) + OrderedPlaysets entry in the loose zonelist (original kept as
  `zonelist.lua.pre-incredibles`). Launching IN_City makes the engine request `worlds\in_city\*`.
- In-game ladder so far (harness, zone IN_City):
  1. raw in_city.oct -> AV in the NavMesh blob parser FUN_01909fb0 -> NavMeshPool removed in memory (interim)
  2. -> NULL basis conversion in FUN_004d0160 -> SubNetwork counts (OCT-1) -> .oct accepted
  3. -> in_city.mtb raw -> int3 in Shader_LookupCrc FUN_004509d0 -> **render transcode** (`render.hpp`, port of
     autobundle.resolve + conv.convert_geometry; `octtest --render` output == `renderoracle.py` byte for byte on
     in_city: .oct/.mtb/52 MB .vbuf/.ibuf; donors read straight from the 3.0 archives, `arc30.hpp`, 10,177/10,177
     equal the extract; table `rentable.py` -> Pentane/Plugins/di1compat_render.txt, 3.0-side data only)
  4. -> world loads, avatar at the origin; playset avatar rejected ("Missing figure") until the playset's own 1.0
     files are served: `IncrediblesDataMap.lua` / `Incredibles_actors.lua` / `lang/IN` (resolver: folder-named and
     per-asset archives) -> locks loader AV on Lua text -> **GameDB hook**: CHD-mode locks/inventory loaders run in
     their own Lua mode for a .lua path (PS-4)
  5. -> 1.0 mission .mtb raw -> asset sessions for every 1.0 .oct with a .mtb (missions, characters, props)
  6. -> CRT fail-fast 0xC0000417 (found by hooking __invoke_watson): 128-byte path buffer of the resident-file
     handlers FUN_0041fef0, overrun by doubled DataMap paths -> **DataMap transcode** (`datamap.hpp`, PS-2: 1,235
     entry strings of IncrediblesDataMap into 3.0 path forms, chunk rewritten by `luachunk.hpp`; lupa loads it)
  7. -> 1.0 characters with default data + base links -> OCT-1 extended (DefaultDataSize, BaseDataNodeLinksCount /
     AdditionalSize, links converted; `octtest --counts` 4,305/4,305; the 1.0 inc_phonebooth == 3.0's twin)
  8. -> 1.0 character rigs WITH processors (inc_delivery_box) -> NULL basis conversion -> **OCT-1 for processor
     graphs** (`octx.hpp`): content counts exact on 7,129/7,129 shipped 3.0 processor/constant graphs (Basis,
     BasisConversion, ProcessorNode, ComponentFamily/Member, VariantGroup/Member, LayerBind; headers all variable,
     ConstantHeaderCount/ConstantLayoutCount 0 -- FUN_004fe4d0 builds header tables independent of Static); arena
     sizes as upper bounds (FUN_004b4970 sums count*(align-1)+size per capability key; arenas grow / DataNodes fall
     back to the heap; bound >= shipped on 7,129/7,129); DefaultDataSize = floor4(DefaultData + DefaultDataIndices
     payloads) 1,516/1,518. All 1.0-only processor types are registered in 3.0 (FUN_00501670).
  9. -> particles/IN_City.mtb raw -> int3 in Shader_LookupCrc -> **standalone bundle transcode**
     (`render::transcodeBundle`: 207/233 materials real by GUID, generic donors limited to 3.0 particle bundles;
     world path byte-identical after the refactor)
  10. -> playset avatars inc_*_ps.bent 4.1 rejected by the "4.1f" gate FUN_0076fb50 -> **.bent codec** (`bent.hpp`,
     NPC-4): FileSets/Layouts flattened to Model{Filename,Channels}/Files sorted, paths resolved; over 583 1.0 .bent:
     1 model / 1 instance each, Type follows the extension, non-default Base ConnectionMaps go onto their Channel
     (FUN_00787200 reads it there). inc_bob vs 3.0's: 352/361 shared Files resolve to the same motion
  11. -> RumpusData AV (DBFileLoader_Callback_CHD fed rumpusdata.lua) -> **CHD compiler** (`chd.hpp`, port of
     chd.py, 29/30 exact = the Python result) + hook on FUN_018db450: the .lua opened during that call is compiled
     to CHD (path marker is process-wide: the engine opens it on another thread)
  12. -> avatar stays AV_Proxy + "Missing Disney Infinity Figure": every playset starts in proxy mode (IGPGameFlow
     ctor) until the playset flow takes the reader figure. A hardware write watchpoint on the reader tag's toy id
     showed FUN_00b83b90 resetting the selection to none every frame: the PC virtual reader remembers a character
     per playset only for 3.0's four (FUN_00b7f910) -> **hook**: a playset without a slot keeps the selection (the
     flow's own Gateway[sku].playset check still applies; Gateway.lua lists Incredibles for the 1.0 figures)
  13. -> no popup; figure check passes; av_mrincredible_ps creation starts but never completes (retried ~60 s).
     Found on the way: 1.0 archives carry their entity's motions under full asset paths -> **archive overlay**
     (every 1.0 archive served from gets its full-path members indexed; 292 files from it); 1.0 clips -> **clip30**
     (External, MinTime/MaxTime from ClipDataBlock, all-static header partition; 2,995/2,995 shipped clips)
  14. -> the world graph was never loaded: 3.0 builds '<name>.mer' only for an .oct with an AssetPool
     (FUN_0077cb60 / FUN_0076c100) -> **AssetPool** from the 1.0 asset associations (pieces with SubNetworkRef -1
     left out, as the 3.0 inc_phonebooth twin) + **Meridian transcode** (`mer.hpp`, `mertable.py` ->
     di1compat_mer.txt): parameters into Mer_ParameterDescription/ParameterValues/ParameterStrings, pins on every
     instance, links v3, RootNodeCount, stand-ins for the 3 unregistered classes; .mtc rebuilt. in_city.mer/.mtc/.ap
     and the 4 mission graphs now load. Static DataNodes made variable outside all-static graphs (instancing
     relocates header node pointers: crash in FUN_004b4470 fixed).
  15. **now**: AV instancing a 1.0 prop's 'Anchor' (DNA Geometry component -> FUN_007734f0 -> FUN_004ff0e0 ->
     FUN_004f5900 at 0x4f5a13: the instance header's +0xc handle resolves NULL during the clone). The transcoded
     inc_phonebooth SubNetwork equals its 3.0 twin record for record (except an empty ProcessorNodePool); the file
     differs in the scene tree (1.0 keeps debris piece_1..7 Geometry nodes at the root, 3.0 moved them to
     inc_phonebooth_debris) and associations. Next: find which header's +0xc dies (live breakpoint at 0x4f5a25,
     eax==0, or a trace on FUN_004b4610 writes), then the remaining graph-runtime issues.
     The plugin logs the first access violation with a call-site-validated stack ("ACCESS VIOLATION ... stack:").
     Trace hooks (cfg trace=1): actor registry, figure check, Gateway playset, reader list/gate, playset figure
     slot, asset instancing, SubNetwork instance guard, header clone, `watch(addr)`. Fit scripts:
     `_research/scratch/impl-2026-10-12`.

## Next (needs the game running)
1. **Rebuild and launch Metroville** with the rules above (`native30.py ... --dry` first; `diffworld.py` against
   the build that worked). Expect the start position, collision and expendables to change. If the player still
   spawns at the origin, break on 0x13a85d0 (its result) and 0x147ff90 / 0x17640a0 (the locator transform); if a
   checkpoint wins, 0x14397ae (`InitPlayersAtClosestCheckpoint`, set on PSX_Master -- try a record without it).
2. **CrabeLoader + CrabeMenu as the test harness** (`crabe/di3tools`): position for `--start`, zone records,
   forced travel, sky swap (does the magenta VFX follow the realm?), spawning one `.dnax`. Its shader / binding
   fallbacks are always on: bisect crashes without CrabeLoader.
3. **Ship as a mod** (`--assets <game>/mods/<id>`, `crabemod.py --check`): a new world name avoids the shipped
   `worlds/out_intro2.zip`; members inside archives stay `di3arc.add_files` / `zones.py`.
4. **Loose `.dnax`**: CrabeLoader's character mods serve them loose, against "the dna lookup is archive-only".
5. **Missing 1.0 sockets** (bstone01_socket01_04(_d), bstone01_socket02_04(_d), mfact05_socket05(_d)): `extract.py`
   on the 1.0 install.
6. **2.0 Gold Edition** (none installed here): `extract.py`, then the vtxf twin count, `matp.py --ver`,
   `oct.py --selftest`.
7. With a 1.0 extract: `GENERIC_SLOTS` from the 1.0 shaders' resource tables (`shdfmt.slot_roles` is the 3.0
   side); the RenderCaps 0x2000 mapping; does the game accept an edited save / a zonelist edited inside startup2.zip.
8. Never run dasorik's infinity-mod-tool on a working install: each apply deletes every non-stock file under `assets/`.

# Disney Infinity formats: 1.0 -> 3.0 PC Gold Edition

The format reference for this repo. Everything here comes from the tools' docstrings, comments and code, and from `README.md`. Each section names the module that reads or writes the format. Where this page gives a layout that only the code shows, it cites the function.

Conventions:
- All values are little-endian unless stated.
- "Shipped" means the stock Steam 3.0 Gold install, or its extract, unless a section says otherwise.
- Counts like `5,079/5,079` are self-test results, quoted with the corpus they were measured on.
- Exe addresses are static Ghidra addresses (image base `0x400000`; `mdmp.Dump.read_static`). Runtime addresses differ by the ASLR'd load base (see [section 13](#13-executable-facts-disneyinfinity3exe)).

Contents:
1. [Overview](#1-overview)
2. [Archives (.zip)](#2-archives-zip)
3. [Tuple files (.oct .mer .merdebug .bent .banm .bct)](#3-tuple-files-oct-mer-merdebug-bent-banm-bct)
4. [Worlds, realms, exploders (.oct content, .vbuf/.ibuf)](#4-worlds-realms-exploders)
5. [Meridian graphs](#5-meridian-graphs)
6. [Materials (.mtb) and textures (.tbody)](#6-materials-mtb-and-textures-tbody)
7. [Shaders (.shd/.vp/.fp) and vertex formats (vformat.vtxf)](#7-shaders-and-vertex-formats)
8. [Collision (.btd, .btm)](#8-collision-btd-btm)
9. [Localisation (.dct)](#9-localisation-dct)
10. [Gamedb (.chd, zonelist.lua)](#10-gamedb-chd-zonelistlua)
11. [Lua bytecode and expendables packages](#11-lua-bytecode-and-expendables-packages)
12. [Save files](#12-save-files)
13. [Executable facts](#13-executable-facts-disneyinfinity3exe)
14. [1.0 vs 3.0 differences](#14-10-vs-30-differences)
15. [Open questions](#15-open-questions)

---

## 1. Overview

### 1.1 The asset tree
Code: `extract.py`, `di3arc.py`

The 3.0 game data lives under `<game>/assets/` as **index archives** named `<kind>/.../<name>.zip` ([section 2](#2-archives-zip)). `extract.py` turns every archive `X/name.zip` into a folder `X/name/` that holds its members under their member names. The other tools read that tree. The mappings the code relies on:

| Archive | Extracted path | Holds |
|---|---|---|
| `worlds/<w>.zip` | `worlds/<w>/<w>.oct` ... | one world ([1.2](#12-how-a-world-is-split)) |
| `shaders/shaders.zip` | `shaders/shaders/` | `vformat.vtxf`, `<shader>.shd`, `<8hex>.vp`, `<8hex>.fp` |
| `textures/<xx>.zip` (`xx` = first byte of the hash, as hex) | `textures/<xx>/textures/<hash>.tbody` | the global texture pool (`autobundle.in_pool`) |
| `realms/<r>.zip` | `realms/<r>/<r>.oct` | sky geometry and light rigs; some carry their own `textures/` folder |
| `exploders/<xx>/<name>.zip` (`xx` = first 2 letters, lower case) | `exploders/<xx>/<name>/<name>.oct` | expendable models (e.g. `exploders/ps/psx_barrel.zip`) |
| `materials/**` | `materials/**.mtb` | standalone material bundles |
| `expendables_data.zip` | `expendables_data/expendables_data/<playset>.lua` | expendables packages ([11](#11-lua-bytecode-and-expendables-packages)) |
| `startup2.zip` | member `gamedb/core/zonelist.lua` | zone records ([10.2](#102-zonelistlua-zone-records)) |
| `gamedb/...` | `gamedb/**.chd` (e.g. `empire/rumpusdata.chd`, its `.lua` beside it) | compiled gamedb tables ([10.1](#101-chd-compiled-gamedb-tables)) |
| `lang/...` | `lang/<pkg>/*.dct` | localisation ([9](#9-localisation-dct)) |
| `dna.zip` | | `.dnax` files (README: buildings spawned once the 1.0 `.dnax` were inside it) |

Archive counts on the stock install: 21,133 archives in total, of which 19,297 use the default key, 1,047 the PSX key, and 789 (sound/dialog) are unencrypted (`di3arc.py`, README). 1.0 Gold Edition archives are **plain zips** (`extract.members`).

### 1.2 How a world is split
Code: `native30.py`, `conv.py`

A world is one archive (`worlds/<w>.zip`) whose members are:

| Member | Format | Section |
|---|---|---|
| `<w>.oct` | tuple file: scene tree, primitives, materials list, pools, graph, associations, `ExternalFilePool` | [3](#3-tuple-files-oct-mer-merdebug-bent-banm-bct), [4](#4-worlds-realms-exploders), [5](#5-meridian-graphs) |
| `<w>.mer`, `<w>.merdebug` | tuple files: the Meridian gameplay graph (start chain, RealmCache ...) and its debug twin. Parsed only when the `.oct`'s `ExternalFilePool` lists `Meridian` | [5.3](#53-mer-and-merdebug), [5.5](#55-the-start-chain) |
| `<w>.mtc` | the `.mer`'s node/link class manifest | [5.4](#54-mtc-the-class-manifest) |
| `<w>.mtb` | material bundle | [6](#6-materials-mtb-and-textures-tbody) |
| `<w>.btd`, `<w>.btm` | collision blocks and their block table | [8](#8-collision-btd-btm) |
| `<w>.ap` | empty in a world without an `ActorParameters` entry (346 of 346 such worlds). `native30` writes it empty, and `realm30` does the same for realms. The non-empty form is not documented | [15](#15-open-questions) |
| `<w>_0.vbuf` | vertex buffer | [4.4](#44-vertex-and-index-buffers) |
| `<w>_0.ibuf`, `<w>_2.ibuf` | 16-bit and 32-bit index buffers | [4.4](#44-vertex-and-index-buffers) |

The textures live in the global pool or in the asset's own `textures/` folder, not in the world archive ([6.3](#63-textures-tbody)). Some worlds keep their buffers in another archive (sub-zones; `meshexport._check_one`). `oct.is_tuple_file` warns that `.mer`, `.merdebug`, `.ap` and `.mtc` also exist in formats other than the tuple format, so check the magic. Of the 640 shipped `.mer`, 56 (55 exploders, 1 realm) are empty, 0 bytes, and so are their twins (`mer.py`).

### 1.3 What the engine sees
- **The engine finds an archive member only through the archive's hash index** (`di3arc.py`). It bsearches the index (FUN_012ea270 -> FUN_012e9e60). Third-party tools that rewrite the index leave the zip central directory stale, so read archives through the index (`di3arc.unpack_index`), as the game does.
- README (expendables port): "The engine only sees assets inside archives." The `.dnax` lookup was believed to be archive-only, and the buildings spawned only after the `.dnax` went into `dna.zip`.
- Loose files are used in place of an archive's members once that archive is disabled. The README describes `expendables_data.zip` renamed to `.disabled` with its 17 packages loose. `zones.py` reads a loose `gamedb/core/zonelist.lua` when `startup2.zip` is "disabled and unpacked".
- A world's `.mer` is read only when its `.oct` lists it in `ExternalFilePool` (0x77ca30; [5.5](#55-the-start-chain)).
- CrabeLoader's VFS (`--assets <game>/mods/<id>`) serves whole files keyed on the path after `assets/`, when the game opens that path through the OS (`crabemod.py`). It cannot replace a member inside an archive, and loose files under a path that a shipped zip covers stay invisible (README, Next 3). `crabemod --check` lists them.
- How loose world and exploder folders written by `native30`/`expl30` relate to these rules is not settled ([15](#15-open-questions)).

### 1.4 The three tool roots
Code: `paths.py`

| Root | Flag | Env | What | Fallback |
|---|---|---|---|---|
| game | `--game` | `DI3_GAME` | the 3.0 install (`DisneyInfinity3.exe`, `assets/`) | Steam library list -> `<library>/steamapps/common/Disney Infinity 3.0 Gold Edition` |
| 3.0 extract | `--ext30` | `DI_EXT30` | the folder holding `assets/`, as `extract.py` writes it | none |
| 1.0 extract | `--ext10` | `DI_EXT10` | the same for 1.0 | none |

- Resolution order: command line, then the environment, then (game only) Steam.
- `paths.use()` copies the command-line values into the environment, so worker processes see the same roots.
- Output tools (`native30`, `realm30`, `expl30`) write into `<game>/assets/` unless `--assets <dir>` is given.
- Corpus caches (`matindex.json`, `donors.json`, `texindex.json`, `shippedkeys-<kind>.json` for worlds / realms / exploders, `shippedpools.json`, `shippedassociations.json`, `filledroles.json`) are built by `paths.cached` next to the scripts. They are versioned and store paths relative to the extract; delete them after extracting another game build. `shippedranges.json` is committed and was rebuilt from the stock Steam 3.0 Gold install.
- `selftest.py` runs every self-test against the configured roots and skips a test whose roots are missing (2026-10-12, 3.0 extract only: 13 passed, 2 skipped).

---

## 2. Archives (.zip)
Code: `di3arc.py` (`read`, `write`, `member`, `pack_dir`, `add_files`, `unpack_index`); `extract.py`

**A 3.0 archive is not a plain zip.** Layout, decoded from shipped files:

```
[index]     'PK\xff\xff'  u32 count
            count x (u32 MurmurHash3_x86_32(name, seed 0), u32 member offset)    sorted by hash
[members]   zip local header (30 B) + name + data (deflate)                       each at its offset
[directory] zip central directory records (PK\x01\x02), each with a 23-byte extra
[end]       zip end record (PK\x05\x06, 22 B)                                     last 22 bytes of the file
```

- **Cipher**: AES-128-CTR with a big-endian counter from 0 and an empty nonce (`di3arc._x`).
  - The index is encrypted from keystream position 0.
  - Each member restarts the keystream at its own offset. The cipher covers the local header, the name (and local extra, `di3arc.read`) and only the first `0x200` bytes of the data. `.dct` members are encrypted in full.
  - The directory and the end record share one keystream, **end record first** (positions 0..21), then the directory (22..). In the file the directory still comes before the end record (`di3arc.write`).
- **Keys**:

  | Name | Key | Used when |
  |---|---|---|
  | default | `681BBEEA63160188F9B7945104A51499` | every other archive |
  | PSX | `7DDD6D92F3A46ABAF061EBC3C01D7D88` | `psx` appears anywhere in the archive's path under `assets/`, case-insensitive (`key_for`). Holds for 1,047 of 1,047 shipped PSX-key archives and no other, `choreographies/psx/.../temp_set.zip` included |
  | none | | 789 archives (sound/dialog) |

  Readers detect the key from the header: the key that turns the first 4 bytes into `PK\xff\xff` (`detect_key`). Writers take it from `key_for(path)`.
- **MD5 extra**: every central directory record carries `'KF' u16 0x13 'MD5'` + the MD5 of the uncompressed member (checked on 1,789 of 1,789). New members get the same extra (`di3arc.member`).
- **Member fields as shipped** (`di3arc.member` writes them):
  - deflate level 9, **memLevel 9**. This reproduces every member of every exploder, world and realm archive (19,142 of 19,142) and 98.9% of a sample of all archives.
  - DOS time/date `0xA1C3`/`0x742F` on every deflated member (stored `.anno` members: `0`/`0xA021`).
  - zip version 20 (made by and needed), flags 0, method 8.
- **Member order** in a single-asset archive (`pack_dir`, `KIND_ORDER`): by kind, then by name:

  `oct`, `bent`, `mtb`, `textures/*.tbody`, `_N.vbuf`, `_N.ibuf`, `btd`, `btm`, `mer`, `merdebug`, `mtc`, `ap` (other extensions last)

  - Textures follow the bundle's TEXB order (`matp.texb`, [6.1](#61-bundle-layout)). That holds for 21 of 21 shipped exploders whose texture names are not sorted.
- `add_files` keeps existing members byte for byte (and the end record's tail) and appends or replaces the named ones.
- Hashed names are Latin-1 bytes.
- Verified:
  - 21,133/21,133 shipped archives re-packed byte-exact (README).
  - **`pack_dir` rebuilds shipped archives from their unpacked files alone**: 2,961/2,961 exploders (`di3arc --selftest`), 365/365 realms and 576/579 worlds (README). This covers member order, headers, compression, extras and the index.
  - A freshly packed archive reads back the same through both readers, with the shipped MD5 extras (`di3arc --selftest`).
- The **1.0 Gold Edition** archives are plain zips (`PK\x03\x04`). `extract.members` reads them with `zipfile`.

---

## 3. Tuple files (.oct .mer .merdebug .bent .banm .bct)
Code: `oct.py` (`Oct`, `Rec`, `spec`, `paylen`)

All of these extensions share one record format. Each record's payload is kept as raw bytes, so `save()` is byte-exact unless something is edited.

**Header** (`0x3c` bytes):

| Off | Field |
|---|---|
| 0x00 | u32 magic `0x45017629` |
| 0x04 | f32 `1.1` |
| 0x08 | u32 0 |
| 0x0C | u32 name table size |
| 0x10 | u32 record data size |
| 0x14 | u32 **record count**: the engine allocates from it |
| 0x18..0x3B | zeros |

- **The record count must be rewritten.** It matches the walked count in 43,346 of 43,356 shipped files; the other 10 hold 0, an unwritten field that `oct.py` preserves. If a file gains records and keeps its old count, the loader reserves too few objects and constructs the last ones past the end of its block. That is why 1 and 2 primitives loaded and 3 never did (`Oct.to_bytes` comment).
- The name table follows the header: NUL-terminated Latin-1 strings. Record names and name values are u16 indices into it.

**Record**: `u16 flag` (type = `flag & 0x3ff`, level = `flag >> 10`), `u16 name index`, then the payload. Records form a pre-order tree through their levels. A record belongs to the nearest enclosing record whose name ends in `Pool` (`Oct.parse`). Top-level pools are level 1 (`conv.pool_rec`).

**The type is a bit field.** Every type in 5,079 shipped 3.0 tuple files decodes by it (3,905 `.oct`, 584 `.mer`, 584 `.merdebug`, 6 `.bent`):

| Bits | Field | Values |
|---|---|---|
| 0-1 | shape | 1 node, 2 list (count + elements), 3 scalar (a blob is a scalar with a count) |
| 2 | keyed | the payload starts with a u16 name (the key) |
| 3-5 | kind | 1 name (u16 index), 2 float32, 3 int, 4 byte (blob) |
| 6-7 | count width - 1 | u8 / u16 / u24 element count (lists and blobs only) |
| 8-9 | int width - 1 | 1..4 bytes per int (int kind only) |

Payload = [u16 key if keyed] + [count, for lists and blobs] + elements. An element is 2 bytes for a name, 4 for a float, 1 for a byte, and the int width for an int (`oct.paylen`). Codes that follow from the rule (`oct.spec`) and appear in the repo:

| Type | Meaning | Seen in |
|---|---|---|
| `0x01` | node, no payload | `Data` (`graph.py`) |
| `0x05` | keyed node: u16 key | pool entries (`Node`, `Association`, `DataNode` ...); tools renumber their keys `'0','1',...` |
| `0x0A` / `0x4A` | name list, u8 / u16 count | `HeaderStrings` (widens past 255 names; `out_mark_sidescroll_v002` ships 4,278) |
| `0x0B` | one name | name values. `conv._reindex` and `conv.rename_files` treat `0x05`/`0x0B` as the name-bearing types; `rename_files` renames a world's file names (`<w>_0.vbuf` ...) through them |
| `0x0F` | key + name | |
| `0x12` | float list, u8 count | `Translation` (`graph.py`) |
| `0x1B` | int8 scalar (`0x11B` int16 ... `0x31B` int32) | `oct.py` dump example |
| `0x17`, `0x27` | key + float32; key + blob (u8 count) | `.mer` |
| `0x1F`, `0x11F`, `0x21F` | key + int8 / int16 / int24 | `0x11F`/`0x21F` in `.mer`. `0x1F` ships as `_DebugOnly_ParamValue = <param name>, 0 / 255` |

- **Ints are signed.** The only shipped values with the top bit set are `LinkedNodeRefs`/`Nodes = -1` (`0xff`, `0xffff`). The exporter picks the narrowest signed width, and `set_int`/`set_ints` do the same: they widen but never narrow.
- Unsigned 32-bit values such as CRCs are stored as the same bytes. Readers mask them with `& 0xffffffff`.
- **Blobs**: `set_bytes` rewrites a blob and keeps its key. Its count widens with the data (`0x23 -> 0x63 -> 0xA3`) and never narrows. `native30` uses it for the `.mer` parameter blobs ([5.5](#55-the-start-chain)).
- Verified: `oct --selftest` round-trips 5,079/5,079 3.0 tuple files byte-exact, and rewriting every int and float record is a no-op.

---

## 4. Worlds, realms, exploders
Code: `conv.py` (shared pipeline), `native30.py`, `realm30.py`, `expl30.py`, `meshexport.py`, `foreign.py`, `ranges.py`, `streamlayout.py`, `nodedump.py`

### 4.1 Pools and their order
- A `.oct` is a set of level-1 pools. Pools named in the code:
  - Scene and geometry: `SceneTreeNodePool`, `MaterialPool`, `VertexBufferPool`, `IndexBufferPool`, `CellPool`, `DisplayLayerPool`, `SceneNodeRefsPool`.
  - Links and collision: `AssociationPool`, `CollisionInstancePool`.
  - Graph and layout: `SubNetworkPool` (the graph), `MeridianLocatorPool`, `LayoutPool`, `AssetPool`, `ExternalFilePool` (its `Meridian` entry makes the engine read the `.mer`).
  - Gameplay: `ExpendablePool`, `CurvesPool`, `NavMeshPool`, `BoundingVolumePool`.
  - Lighting and sound: `LightRigPool`, `ShadowCasterPool`, `SoundSourcePool`.
  - Realm: `RealmDataPool`.
  - 1.0 only: `SurfaceTypesPool`.
- **Order matters.** Every shipped 3.0 `.oct` (worlds, realms, exploders) agrees on one pool order: 0 conflicting pairs over 3,905 files (`conv.shipped_pools`, cached as `shippedpools.json`). It agrees with the hand-written order it replaced on every observed pair (README).
  - `conv._agreed_order` builds it: no two files may order a pair both ways (asserted); the order is topological; pools no file holds together tie by their mean relative position.
  - The cached order:

    `NavMesh, BoundingVolume, ExternalFile, Curves, Ticker, SubNetwork, CollisionShape, CollisionInstance, PhysicsConstraint, PhysicsBody, ReverbSphere, LightRig, SoundSource, ShadowCaster, DisplayLayer, LightSet, Cell, CellAssignment, MaterialBundle, Material, VertexBuffer, IndexBuffer, SceneTreeNode, SceneNodeRefs, MeridianLocator, Layout, Track, TrackLinkCustomData, TrackSectionCustomData, TrackNode, TrackLink, TrackSection, Asset, AOField, Association` (each `...Pool`), then the level-1 records `ExporterDate`, `ExporterTime`.
  - The graph (`SubNetworkPool`), locators, layout and assets must come **before** `AssociationPool`. When the graph was appended after it, the graph-bound links were resolved before the graph existed, and 2 exploder archives opened instead of 85 (`native30.py`).
  - `conv.sort_pools` applies it in all three converters. Records before the first level-1 record stay in front. A pool 3.0 never ships goes right before a named one (`native30`: `ExpendablePool` before `LayoutPool`).
- **Pools 3.0 never ships**: `Realm*` and `SurfaceTypes`, measured as the top-level pools no shipped 3.0 `.oct` holds (`conv.foreign_pools`). Exception: `ExpendablePool` is a 1.0 pool that 3.0 does not ship but still registers (FUN_01604820), so `native30` keeps it.
- **`ExternalFilePool`**: a world has it exactly when it ships a `.mer` (every shipped world with a `.mer` has it, none without; README). Its `Type` entries are `[Meridian]` or `[ActorParameters, Meridian]`. A world without `ActorParameters` ships an empty `.ap` (346 of 346; `native30`).
- **Pools `native30` drops**: `CurvesPool` (ships in 131 3.0 worlds, but 1.0's is not converted), `NavMeshPool`, `BoundingVolumePool` (the 1.0 world-bounds blob caused endless respawns) and `MeridianLocatorPool`.
  - It replaces `LayoutPool`/`AssetPool`/`SubNetworkPool`/`ExternalFilePool` with the 3.0 donor's (`out_intro2`; asserted `ExternalFilePool` = `[Meridian]`).
  - It takes the locator pool from the start donor `holohendy` ([5.5](#55-the-start-chain)).
- **Pools `expl30` drops**: `BoundingVolumePool`, `MeridianLocatorPool`, `NavMeshPool`, `SceneNodeRefsPool`.

### 4.2 Scene tree nodes
- `SceneTreeNodePool` holds `Node` entries. Fields named in the code:
  - Common: `Type`, `NodeName`, `Uuid` (blob: u8 16 + 16 bytes; `realm30._uuid`), `DisplayLayer`, `ParentNodeReferences`, `LocalToParentMatrix` (16 f32), `Visible`.
  - Bounds and fades: `BoundingOBBCenter`, `BoundingSphereCenter`, `DistanceAlphaFadeStart/End`.
  - Links: `LinkedNodeRefs`, `LinkedSceneTreeNodeRefs`, `LightRefs`.
  - Geometry: `Primitives` > `Primitive`, `NumPrimitives`.
- Node `Type` values: `Scene`, `Transform`, `Geometry`, `Camera`, `Light`, `LodGroup`, `RealmData`.
- **A shipped 3.0 Transform node has exactly** `NodeName, Uuid, DisplayLayer, Type, ParentNodeReferences, LocalToParentMatrix` (`conv.TRANSFORM_FIELDS`).
- **LodGroup**: 3.0 never ships this node type (131 in Metroville). `native30.native_scene` turns it into a Transform in place and hides the geometry below every child but the first (child 0 = most detailed).
- **Light**: in worlds, 1.0 light fields become a plain Transform. Exploder point lights "broke every VFX in-game". In realms the light is rebuilt as a 3.0 light ([4.7](#47-realms)).
- **Matrices**: row-major 4x4 with row vectors; the translation is at `[12:15]`; world = local x parent (`conv._mul`).
  - `DataNode Orientation` is a quaternion `[x, y, z, w]` taken from the normalised rows (`conv.mat2quat`). That convention was measured on 46,036 shipped Dilation orientations.
- **Which fields are world-space** (`native30` field audit):
  - World-space: the root scene nodes' `LocalToParentMatrix` (children inherit it), `BoundingOBBCenter`, `CellPool.CellToWorldMatrix`, `CollisionInstancePool.Transform`, and the graph slots.
  - Local: `BoundingSphereCenter`, child matrices, vertices and the `.btd`. Moving the sphere centres while missing the cells is what hid the buildings in the old static build.

### 4.3 Primitives: Vdata, Idata, vformatCRC, MaterialReference
Code: `conv.build_buffers`, `conv.convert_geometry`, `meshexport.Buffers`

```
Vdata  [streams, vertices, (vertex buffer #, byte offset, stride) x streams]   -> VertexBufferPool FileName
Idata  [index buffer #, start, base vertex, count]                             -> IndexBufferPool Width + FileName
```

| | 1.0 | 3.0 |
|---|---|---|
| `Idata[1]` start | in **indices** | in **bytes** |
| index values | relative to the primitive's own streams: vertex at `Vdata[3+3s] + value * stride` | node-global: vertex = value - `Idata[2]` |
| `Idata[2]` | not used | base vertex = number of node vertices before this primitive |
| `Idata[0]` | not used (values read as u16) | index buffer #: 0 = 16-bit (`Width` 2), 2 = 32-bit (`Width` 4) |

- In 1.0, reading indices from the buffer start works only when one node owns the whole mesh (sumo). Otherwise every node silently gets the first node's vertices (`conv.build_buffers`).
- **3.0 nodes mix vertex formats freely.** Each primitive carries its own stream offsets and strides; 2,420 shipped nodes mix formats.
- `meshexport --check`: 57,978 shipped 3.0 primitives stay inside their buffers under the 3.0 rule.

### 4.4 Vertex and index buffers
- Files: `<name>_0.vbuf`, `<name>_0.ibuf` (16-bit), and `<name>_2.ibuf` (32-bit, e.g. `dor_tp_mc_2.ibuf`).
  - A node with more than 65,535 vertices gets 32-bit indices in IndexBuffer 2, as the 3 such shipped 3.0 nodes do.
  - `IndexBuffer 2` gets a `FileName` record only when it is used (`conv.convert_geometry`).
- `VertexBufferPool` > `VertexBuffer` {`Size`, `FileName`}. `IndexBufferPool` > `IndexBuffer` {`Width`, `Size`, `FileName`}.
- **Layout**: within a multi-primitive node the vbuf is **stream-major**: all of stream 0 for every primitive, then all of stream 1. This holds for 4,915/4,915 shipped multi-primitive nodes (`streamlayout.py`). Across nodes, each node's streams are contiguous (primitive-major across nodes).

### 4.5 RenderCaps
Code: `conv.rendercaps30`

- **Primitive `RenderCaps`**: 1.0 sets `0x1000` on every drawn primitive. `0x1000` never ships in 3.0, where the equivalent is `0x200` with the same low bits: `0x200`-`0x207` on 44,191 of 44,232 non-zero shipped primitives. `rendercaps30` maps `(v & 0xfff) | 0x200`.
- **1.0 value 0** is drawn in 1.0 but **not** in 3.0. Every Metroville building body (109 nodes) and 1,407 of 1,535 exploder primitives carried 0 and were invisible, so 0 also maps to `0x200`.
- `0x2000 -> 0x400` is a guess: 2 primitives in `in_city`, and 3.0 ships `0x600`.
- **Material `RenderCaps`** (in `MaterialPool`) is 1.0-only and is removed (`conv.prepare`).
- `variant.py --rendercaps` applies the same `rendercaps30` to primitives still in 1.0 form (0, or `0x3000` bits set).

### 4.6 Associations
Code: `conv.sort_associations`, `native30.py`, `expl30.py`

- `AssociationPool` > `Association` {`Type`, `NodeName`, `SceneTreeNodeRef`, `CollisionInstanceRef`, ...}. `conv.association_kind` groups the types into kinds:

  | Kind | Types |
  |---|---|
  | `Collision` (`conv.COLLISION`) | `CollisionInstanceToSceneTreeNode`, `CollisionInstanceToDataNode`, `CollisionAggregate`, `CollisionShapeToCollisionInstance` |
  | `Physics` (`conv.PHYSICS`) | `PhysicsBodyToSceneTreeNode`, `PhysicsBodyToDataNode`, `PhysicsBodyAggregate`, `CollisionShapeToPhysicsBody`, `PhysicsConstraintToPhysicsBody` |
  | `NodeBindings` (`conv.NODE_BINDINGS`) | `Transform`, `Anchor`, `PrimaryAnchor`, `PlacementNode`, `Geometry`, `Material`, `RealmData` |
  | any other type | its own kind: `MeridianLocator`, `CellLink`, `VisHierarchyLink`, `Curve` ... |

  - 1.0 only: `PrimaryAsset` (dropped; 3.0 has an `AssetPool` instead) and `Expendable` (3.0 never ships it but still resolves it, FUN_01604820).
- **Order** (`conv.shipped_associations`, cached as `shippedassociations.json`): each kind is one contiguous run in 3,886 of 3,886 shipped `.oct` that hold associations, and no two files order a pair of kinds both ways. The cached order:

  `MeridianLocator, SoundSource, ReverbSphere, Ticker, MeridianData, Collision, CellLink, CellAssignment, VisHierarchyLink, NodeBindings, SkinInfluence, Physics, Curve`

  - **Collision links are grouped per instance**, by ascending `CollisionInstanceRef`, in `COLLISION` order: `S?DAH` (SceneTreeNode optional, DataNode, Aggregate, sHape). Over 23,509 shipped instances there are 14,129 SDAH groups and 9,380 DAH groups.
  - `sort_associations` applies both orders, keeps everything else in its relative order and renumbers the entries `'0','1',...`. All three converters use it. A kind 3.0 never ships goes right after a named kind (`native30`: `Expendable` after `VisHierarchyLink`).
- `Expendable`: the engine resolves `NodeName + '|Transform'` as a graph DataNode (FUN_01604820). `native30` renames them `Layout|SOCK_<i>`.
- **DataNode link paths**: a 1.0 link `Anchor|X` becomes `Anchor|X|Transform|In WorldBasis.Transform` in 3.0 (`expl30.py`, measured on `psx_barrel`).
- **`MeridianLocator`** names `<slot>|Transform|In WorldBasis.Transform`, the Transform DataNode the slot's processors output. 34,456 shipped `MeridianLocator` bindings do this, and 0 bind a Dilation: 0x6b6830 requires a Transform DataNode and resolves a Dilation to null (the "transform fails" the debugger showed; `native30`). The start locator binds `Layout|LOC_Player|Transform|In WorldBasis.Transform` ([5.5](#55-the-start-chain)).
- **Collision in worlds** (`native30`, as every shipped instance: 23,509 of 23,509):
  - Per instance `i`: a graph slot `COL_i` at the instance's own `Transform` ([5.6](#56-graph-slots)).
  - `CollisionInstanceToDataNode` and `CollisionAggregate` with the **same** `NodeName` `Layout|COL_i|Transform|In WorldBasis.Transform`. Every part of that name is a graph header.
  - The 1.0 `CollisionShapeToCollisionInstance` is kept and renamed `Layout|COL_i`.
  - No scene link: `SceneTreeNodeRef -1`, as in 9,380 shipped instances.
  - **`AssetPool` `AggregateCount` = instances = `CollisionAggregate` links** (3,905 of 3,905 files).
- **Collision in exploders** (`expl30`):
  - The 1.0 `CollisionInstanceToDataNode` path gets the 3.0 suffix.
  - A `CollisionAggregate` with the same `NodeName` is added, with `SceneTreeNodeRef` = the geometry node.
  - `AggregateCount` = instances.
- **What `native30` keeps**:
  - From 1.0: `CellLink`, `VisHierarchyLink`, `Expendable` and `CollisionShapeToCollisionInstance`. The rest are graph bindings (plan B) and are dropped.
  - From the donor: its `Layout` bindings (`Transform`, `Anchor`, `PrimaryAnchor`, `PlacementNode`). They point at the 1.0 Camera node, which becomes the graph root `Layout` in place (no renumbering), parented to the Scene node.

### 4.7 Realms
Code: `realm30.py`. Rules measured on `rlm_intro_day`, which ships in both games; `--selftest` reproduces 3.0's records.

- **RealmData node**: `RealmDataPool` becomes a `RealmData` scene node at index 1.
  - It is the parent of every former root child, with `NodeRef` = itself and `DisplayLayer 0`.
  - It gains `AddToZone 1` and `RotateToGravity` / `RotateSkipLighting` / `BlendWithGravity 0`.
  - Every node reference >= 1 shifts by one.
- **Light**: 1.0 named fields become `LightType` 6 (Directional) or 7 (Ambient), with `FeatureCaps 0`.
  - `LightData`: directional = `[r, g, b, intensity, 0 x6]`, ambient = `[r, g, b, intensity]`.
  - The light keeps its `Uuid`.
- **LightRig** gains a `Uuid` after `LightRefs`.
- **Dropped and added**: `SurfaceTypesPool` and `PrimaryAsset` are dropped. `AssetPool 'Root'` is added before `AssociationPool`.
- **Order and check**: pools and associations go into the shipped orders ([4.1](#41-pools-and-their-order), [4.6](#46-associations)). Record kinds are checked against the shipped realms (`check_foreign(s, 'realms')`).
- **Files**: `<r>.oct`, `<r>.mtb` and an empty `<r>.ap`, packed with `di3arc.pack_dir` and `key_for('realms/<r>.zip')`.
- **Textures**: the realm's 1.0-only textures go into the zip's `textures/` folder, as `rlm_moa_motunui_day` ships them.

### 4.8 Exploders
Code: `expl30.py`

- An exploder is a one-node asset in the world format: `Scene > Transform 'Anchor' > one Geometry node`, with links named `Anchor|<node>`.
- Its graph is sized for that node under the root `Anchor` ([5](#5-meridian-graphs)).
- `AssetPool` and `SubNetworkPool` come from the 3.0 template `psx_barrel`. Pools and associations go into the shipped orders, and record kinds are checked against the shipped exploders (`check_foreign(s, 'exploders')`).
- `expl30` writes the loose folder `exploders/<xx>/<name>/`, with internal file names lower case. The README packs these as `assets/exploders/xx/name.zip` with `di3arc.py`.

### 4.9 Checking a built file
- `foreign.py` lists the record kinds that no shipped 3.0 `.oct` **of the same asset kind** has (worlds, realms or exploders; cached as `shippedkeys-<kind>.json`). Key = (pool, name, parent name, level, type), with digit names folded to `#`.
  - The command line marks a key `TYPE ONLY` when the same (pool, name, parent, level) ships with another type.
  - A shipped file that does not parse is reported and left out of the reference set.
- `ranges.py` lists values and per-file counts above the shipped maximum (`shippedranges.json`).
- `conv.check_foreign(s, kind, allow)` asserts that none are left except the ones a tool keeps on purpose (`native30`: `ExpendablePool`, `ExpendableNodeRef`).

---

## 5. Meridian graphs
Code: `graph.py` (SubNetwork sizing), `mer.py` (`.mer`, `.merdebug`, `.mtc`), `native30.py` (start chain, slots, locators), `subnet.py` (count fitting)

There are two graphs. The **SubNetwork** graph inside the `.oct` holds positions as DataNodes and processor chains ([5.1](#51-where-the-subnetwork-graph-lives)-[5.2](#52-the-sizing-rule-graphpy), [5.6](#56-graph-slots)). The **Meridian gameplay graph** in the `.mer` holds node classes, parameters and links ([5.3](#53-mer-and-merdebug)-[5.5](#55-the-start-chain)).

### 5.1 Where the SubNetwork graph lives
- `SubNetworkPool` > `SubNetwork` (level 2) > fields and inner pools (level 3) > entries (level 4) > fields (level 5+) (`subnet.subnets`, `native30.graph_locator`).
- Header fields: `HeaderStrings`, `HeaderStringIndices`, `HeaderLayout`, the counts below, and `VariableAccumulatedSize32/64`.
- Inner pools: `DataNodePool`, `ProcessorNodePool`, `VariantGroupPool`, `BasisConversionPool`.
- `DataNode` {`Type` (`Dilation`, `Transform`), `Header`, `BasisRef`, `Data` {`Translation`}, `Orientation`}. `ProcessorNode` {`Header`, `DataNodeRefs`}. `BasisConversion` {`FromBasisRef`, `ToBasisRef`, `DataNodeRef`}.
- **Header numbering**: header numbers are **1-based** (in `HeaderLayout` pairs and `DataNode.Header`), while `HeaderStringIndices` is **0-based**. Reading them 0-based picked a newspaper stand's Dilation instead of the start point.
- **`HeaderLayout`** holds (header, childCount) pairs, breadth-first: each pair hands out the next `c` headers.

### 5.2 The sizing rule (graph.py)
**The graph is allocated, not descriptive: every geometry node owns a slice of it.** Four launches crashed at `OctanePlayerMain+0xdcddd7` with `eax = 0` because scene nodes were added to a SubNetwork sized for one. For `n` geometry nodes:

| Item | Value |
|---|---|
| headers | root, `Visibility`, `Transform`, one per node NAME, then per node a 5-block: `Transform`, `In WorldBasis.Transform`, `In WorldBasis`, `Convert To WorldBasis`, `ConvertDilationToTransform` |
| `VariableHeaderCount` | `3 + n + 5n` |
| `VariableDataNodeCount` | `2 + 3n` (2 shared: `Visibility`, then a `Dilation` or a `Transform`; then 3 per node) |
| `ProcessorNodeCount` | `2n` |
| `VariantGroupCount` / `VariantMemberCount` | `n` / `2n` |
| `HeaderLayout` | `[0,1, 1,n+2]` then per node i: `[4+i,1, b,3, b+1,1]` with `b = 4+n+5i` |
| `VariableAccumulatedSize32` | `'1': [4, 36+x+148n, 2+5n]`, `'4': [4, 12n, 2n]` |
| `VariableAccumulatedSize64` | `'1': [8, 40+x+168n, 2+5n]`, `'4': [8, 24n, 2n]` |

- **`x` is the second shared DataNode's extra size** (`graph.SHARED_EXTRA`): 0 for a `Dilation`, **32 bytes for a `Transform`** (3 of the 1,367 shipped graphs of this shape). `graph.expand` reads the two shared types from the file. `plan` refuses any other type ("size not measured").
- **Header 1 is the parent transform's own name**, not a literal `Layout`. For example, `lb_zonetwo`'s is `LB_ZoneTwo1`.
- **Every per-node Dilation must carry `Data{Translation}`.** `out_intro2`'s single node sits at the origin, so its exporter omitted the block. Cloning that left a null where the engine dereferences one: the `eax = 0` fault.
- The rule was derived from `dor_bay`, `dor_intro` and `mba_holo_art_test_jeffz` (n=2) against `out_intro2` (n=1).
- Verified (`graph --selftest`):
  - It re-derives `out_intro2` (n=1), `dor_bay` (n=2) and `lb_zonetwo` (n=3) exactly, processors and variant groups included.
  - It then reproduces **all 1,367 shipped SubNetworks of the generated shape** (n up to 105; README): headers, the five counts, `HeaderLayout` and both accumulated sizes. "The generated shape" means headers 2-3 are `Visibility`, `Transform`, the headers match the plan, and the shared DataNodes are `Visibility` + `Dilation`/`Transform`.
- 3.0 adds per-SubNetwork counts that 1.0 graphs lack: `ConstantHeaderCount` ... `BaseDataNodeLinksCount`, `ConstantLayoutCount` (`subnet.COUNTS`). Apart from the five sizes in the table above, no rule is recorded for them; `subnet.py --fit` is the fitting stage.

### 5.3 .mer and .merdebug
Code: `mer.py` (`load`, `show`, `catalogue`). Decoded from the 3.0 extract.

- **Files**: 640 `.mer` under `worlds/`, `exploders/` and `realms/`, each with a `.merdebug` twin.
  - 584 are tuple files ([3](#3-tuple-files-oct-mer-merdebug-bent-banm-bct)). The other 56 (55 exploders, 1 realm) are empty, 0 bytes, and so are their twins.
  - The twin is the same file plus `_DebugOnly_` records.
- **Nodes**: `MeridianNodePool` > `MNode = '<i>'` (i = 0..N-1 in file order); `Type` = the node class.
- **Namespaces**: `NameSpaceBegin` .. `NameSpaceEnd` bracket each namespace: 1 per file in 568 files, up to 29 (merged layouts), never nested.
  - Begin holds `Name`, `GraphFileName`, [`UserNamespace`], `GraphVersion`, `RootNodeCount` (= the nodes up to its End), `EnableNetworking`, [`IsMerged`, `ReferenceEmbeddedNamespace`, `MergedLayoutPath`].
  - It also holds the namespace's parameter blobs `ParameterValues` and `ParameterStrings`.
- **Every other node** carries:

  | Record | Meaning |
  |---|---|
  | `Mer_ParametersOffset` | its block in the namespace's `ParameterValues` (empty blocks share the next offset) |
  | `Mer_ParameterDescription` | `u32 n`, n x (`u8 type`, `u32 lookup2(name, seed 0)`, `i32 offset in the block`), sorted by offset |
  | `NumInLinks`, `NumOutLinks` | its link counts in `LinksBuffer` |
  | `Mer_NoConnections` | child nodes that only hang off a `Parent` |
  | pins: `<name> = <pin index>` | a SUBSET of the class's pins, often none. A name can repeat (DeathCloudNode `Reset` = 2 and 10). The numbering is per class, except the per-interface `connection_*` pins |
  | ports: `<name> = <MNode index>` | used instead of pins in 31 classes (EventSurge, LuaNode, NetSendInt, the camera family) |
  | `Parent`, `MerSpawnGroup` | the owner (NodeGroup, Choreography, SpawnActorNode ...); the SpawnGroupNode / NodeDefinition |
  | `LeafNameHash` | lookup2 of the INTERNAL port name (`Begin` behind an authored `Entered~`) |
  | `IsProxy` | 1 |
  | `Uuid` | 16 B, = `_DebugOnly_Uuid` |
  | `BoundObjectName` | a scene path, e.g. `Layout\|LOC_PlayerStart` |
  | `ResolveAnchorPathName` | key `BoundObjectName`, 16 zero bytes |
  | `Mer_DebugOnly_DataTypeName`, `Mer_DataTypeNameHash` | on interface nodes (InterfaceUser, GraphInterface, NodeDefinition, Object): the type name, and lookup2 of it lower-cased |
  | `Mer_ConnectionDescription` | interface nodes only: `u32 n`, n x (`u32 type`, `u32 lookup2('connection_<pin>')`), for the node's `connection_` pins in declaration (not pin) order |

  - **Ports** are sub-nodes (Event, DataFetch, DataSource ..., each carrying `LeafNameHash`) placed right before the node. Sub-nodes list no pins. In links, Event / DataSource / EventSurge send from pin 1, and Handler / DataFetch receive at pin 0.
  - **Connection types** at the `connection_` pins: 0 input (impulse or data), 6 impulse output, 2 / 3 data output, 8 / 9 data input.
- **Parameter values** by description type: 0 bool / 1 int / 2 float = 4 B; 3 string = u32 offset of a NUL-terminated string in `ParameterStrings` + 8 x `FF`; 4 vec3 = 3 floats.
- **Links**: `MeridianLinkPool` > `MLink`, per namespace that has links: `NameSpaceBegin` (Name ...), `LinksBuffer`, `NameSpaceEnd`, in node-pool order (one file has two namespaces of the same name).

  ```
  LinksBuffer.Data   u32 n, 00 03 00, n x (u16 code, u16 from, u16 to)
      from / to      position in the namespace (MNode index - Begin - 1)
      code           from pin << 10 | to pin << 4 | kind     kind: 1 impulse, 3 data, 2 surge
  ```
  - A surge is a held state: Switch, CatalystSurge, AndSurge ... into SoundEmitter, cameras, emitters.
- **Associations**: `MeridianNodeAssociationPool` (2 files) > `MNodeAssociation`: `Type Collection`, `Owner` / `Collector` = MNode indices.
- **`.merdebug`** adds per node:
  - `_DebugOnly_Uuid` and `_DebugOnly_AuthoredName`;
  - one `_DebugOnly_ParamValue = [name, value]` per description entry, in description order (a vec3 as the string `'%f, %f, %f'`).
- Verified (`mer --selftest`, 584 graphs: 130,221 nodes, 117,117 links, 428,387 parameters). Every invariant above holds in all of them:
  - The descriptions of 128,913 of 128,913 nodes line up 1:1 with their `.merdebug` names: each of the 428,387 names hashes to its entry, and each decoded value equals the debug value.
  - Every parameter block lies inside `ParameterValues`, and the blocks cover it completely (654 of 654 namespaces).
  - Every link endpoint is a node of its namespace, and the links per node equal `NumOutLinks` / `NumInLinks`.
  - No class mixes pins and ports.
- `mer show` prints one graph; `mer catalogue` lists every node type over the shipped graphs (parameters, pins, ports, links).

### 5.4 .mtc: the class manifest
Code: `mer.mtc`

```
u32 0xffff   u32 n node types   u32 m link types
(n + m) x (u32 count, u32 file offset of the name)
names, NUL-terminated
```
- **One ships beside every world `.mer`.** It is the engine's type manifest for that graph.
- The node types are the `.mer`'s MNode classes without `NameSpaceBegin`/`End`, sorted, each with its count. The `MLink` types follow the same way (`LinksBuffer`, in the graphs that have links).
- Verified: `mtc()` rebuilds 584/584 shipped `.mtc` byte-exact from their `.mer` (`mer --selftest`).
- A `.mtc` that does not match its `.mer` declares node classes the graph does not have. `native30` used to copy `out_intro2`'s (Catalyst, RealmLoader), which declares no PlayerStartPosNode or LocatorNode. It now writes `mer.mtc` of its own graph (README).

### 5.5 The start chain
Code: `native30.build_mer`, `native30._start_graph`; exe addresses from `native30.py` and README

- **How the exe picks the start** (`StartPositions::GetStartPosition`, 0x13a85d0). It tries, in order:
  1. the `PlayerStartPosNode`s registered for the **loading zone** (StartPositions keys a start by it, 0x1485830);
  2. those of the zone record's `AlternateStartPointZone` ([10.2](#102-zonelistlua-zone-records));
  3. those of any zone.

  Otherwise it returns the **origin**.
- **The `.mer` is parsed only when the `.oct` has `ExternalFilePool [Meridian]`** (0x77ca30). Every shipped world with a `.mer` has it, and none without ([4.1](#41-pools-and-their-order)). `native30` also names `AssetPool[0]` as a requirement; both come from the donor `out_intro2`.
- **The chain**: a `PlayerStartPosNode` takes its Transform from a `LocatorNode`. The LocatorNode binds by `LocatorNodeIndex` into `MeridianLocatorPool`. That locator's `MeridianLocator` association must name a Transform DataNode ([4.6](#46-associations)).
  - `LocatorNode -> PlayerStartPosNode` is the form ground levels ship. `TardisManifestAdv` only starts vehicles.
- **Start donor: `holohendy`**, the smallest shipped ground start with a `RealmCache` (5 nodes): `LocatorNode -> PlayerStartPosNode`, plus a connection-free RealmCache for lighting.
  - Its `.mer` chain, its `MeridianLocatorPool` and its `MeridianLocator` association are taken together.
  - Its LocatorNode is bound to `BoundObjectName = Layout|LOC_Player`, so the start slot is named `LOC_Player`.
- **Namespace**: `_start_graph` names the namespace as shipped graphs do. It does so in the `NameSpaceBegin` of both `MeridianNodePool` and `MeridianLinkPool`.
  - `Name` and `GraphFileName` = the zone (as in 164 of 207 zoned graphs).
  - `UserNamespace` = the world folder in lower case (as in 189 of 207).
- **Realm**: the RealmCache's `Name` parameter (a string) names the realm.
  - The new string is appended to `ParameterStrings` (`set_bytes`), and its offset is written at the parameter's slot in `ParameterValues` (`Mer_ParametersOffset` + the entry's offset). No other offset moves.
  - The `.merdebug` copy of that parameter (`_DebugOnly_ParamValue`) is changed too.
- **Output**: `<w>.mer`, `<w>.merdebug` and `<w>.mtc` (`mer.mtc` of the new graph). `build_mer` reloads the graph and asserts LocatorNode, PlayerStartPosNode and RealmCache, the realm name and `BoundObjectName = Layout|LOC_Player`.
- **Why earlier builds spawned at the origin** (under the debugger no start node was ever constructed, even with the unmodified shipped `emp_endor_details` graph):
  - they had no `ExternalFilePool [Meridian]` entry, so the `.mer` was never parsed;
  - they had a donor `.mtc` that declared other node classes.

  Both are now written as shipped. This is not yet tested in-game ([15](#15-open-questions)).
- `--translate` is the old workaround: it moves the world so the start lies 1 below the origin, applied only to world-space fields ([4.2](#42-scene-tree-nodes)). Without it the world stays native and the start graph moves the player.

### 5.6 Graph slots
Code: `native30.convert`, `graph.expand`

- **The 1.0 graph has no processor nodes** (3,480 Dilations in `in_city`). 3.0 resolves positions through processor chains, so `native30` generates a graph under the root `Layout` (`graph.expand`). It carries only:
  - the expendable sockets `SOCK_i`;
  - one slot per collision instance, `COL_i`;
  - the player start `LOC_Player` (last).
- **Slots** are the Dilation DataNodes with `BasisRef 1`, in order.
  - A socket or collision slot gets its `Translation` from its world matrix (the socket node's, or the instance's `Transform`; `[12:15]`). An `Orientation` quaternion (`conv.mat2quat`) is inserted after it.
  - The start slot gets the start position.
- **The start slot's processor outputs** (`In WorldBasis.Transform`, a Transform DataNode, and `In WorldBasis`, a Dilation) are pre-filled with the same position, because the Layout basis is identity. 125 shipped worlds likewise hold the position in that Transform.
- **1.0 locators**: a locator's world position is its `<name>|Transform` Dilation plus its basis chain (`native30.graph_locator`). `native30` starts at the 1.0 locator named `*StartPoint` unless `--start x,y,z` or `--start-locator` is given. Metroville uses `--start`, since `IN_City_StartPoint` lies outside the playable city.

---

## 6. Materials (.mtb) and textures (.tbody)
Code: `matp.py` (codec, from the engine's parser FUN_00472590), `autobundle.py` (1.0 -> 3.0 bundles), `slotcheck.py`, `mtb-retex.py`

### 6.1 Bundle layout
```
BNDL  u32 ver(1)  u32 fileSize  u32 nChunks
TEXB  u32 ver(3)  u32 chunkSize u32 nTex  u32 0   nTex x (8-byte .tbody name hash, u32 0xffffffff)
MATP  u32 ver(2)  u32 chunkSize u32 nMat
      align4  u32 nShaders, nShaders x NUL-terminated shader name     (FUN_0045c820 lookup)
      align4  u32 nMat,     nMat x NUL-terminated material name       (FUN_00459a90)
      align2  u16 shaderIndex[nMat]
      align4  u32 hash[nMat]                                          (FUN_00468500 material lookup)
      align16 per material: block A (shd u16 @0x6a bytes) + block B (shd u16 @0xa2 bytes)
```
- All alignment is file-relative; the bundle loads at a 16-aligned address.
- `chunkSize` includes the chunk header. `build()` pads TEXB to 16 bytes.
- These are PC bundles. PS4 TEXB entries are 16 bytes (`ps4mtbfix.py`, not in this repo).
- **Names**: material names are GUIDs (e.g. `66780fdf-5a90-4c77-9d67-0f326bc80cdc`). Shader names are 32 hex digits (e.g. `d0e71afc8ef520e49044db932c324993`).
- **The engine caches materials by `hash`** (FUN_00468500). That is why substitutes keep the 1.0 material's GUID and hash: a donor's hash could hand back the donor's already-loaded material (`autobundle.py`).
- **Block sizes come from the material's shader**, not from the bundle:

  | Game | A size | B size | slot count | slot stride |
  |---|---|---|---|---|
  | 3.0 | `.shd` u16 @`0x6a` | u16 @`0xa2` | u8 @`0xa1` | 8 B |
  | 1.0 | `.shd` u16 @`0x46` | u16 @`0x6a` | u8 @`0x69` | 4 B (bare u32 index) |

  The 3.0 values are from the exe. The 1.0 values were fitted to 350/350 single-material bundles (`matp.SHD_LAYOUT`), and the README reports 9,638/9,638 1.0 layouts predicted. They are the same fields: in 1.0, block A size sits 0x24 earlier, and block B size and slot count sit 0x38 earlier.
- **`matp.texb()`** reads the TEXB hashes in order without sizing any material, so no shaders are needed. `di3arc.pack_dir` orders texture members by it ([2](#2-archives-zip)).
- **Block A** holds the material parameters (first float4 = tint).
  - Over 421 GUIDs shipped in both games with the same shader, 3.0's A is 1.0's A followed by new trailing parameters in 401.
  - Across a shader change the first float4 still matches in 85%.
  - **Block B's data after the slots (samplers) changed layout between games in every pair**, so `autobundle` keeps the donor's (`autobundle._substitute`).
- Verified (`matp --selftest`): 48,057/48,057 layouts on a full extract (2026-10-03). On the stock-install extract, "7,218/7,218 + 6,545/6,545 rebuilt byte-exact" (README). `merge()` copies blocks verbatim and only rebases texture indices.

### 6.2 Texture slots and the slot resolver
- **3.0 slot**: block B opens with `shd[0xa1]` slots of 8 bytes, `(u32 TEXB index, u32)`. Shipped slots are **only** `(idx, 0)` or `(ffffffff, ffffffff)` (other forms: 0 of 25,052 shipped slots).
- **Resolver** (`Shader_ParseBindings`, 0x45D5C0, called from the `.mtb` parser): a slot is skipped only when `w0 & w1 == 0xffffffff`. Otherwise it reads `table[w0]`, drops the old pointer's refcount and bumps the new one.
  - **A `(ffffffff, 0)` slot reads `table[-1]`**: a garbage pointer plus a refcount write. That was the Metroville heap corruption at 0x403fc0 and the garbage SRV in the d3d11 AV.
  - An `(idx, ffffffff)` slot resolves normally.
  - So always write the whole 8-byte slot (`autobundle._substitute`). `slotcheck.py` flags mixed forms.
- **Binder** (FUN_0045a1c0; the call at 0x45a2c4 is where the d3d11 `PSSetShaderResources` crash happened):
  - The draw loop passes `shader + 0xa0` as the binder's descriptor: `desc[1] = shd[0xa1]` (slot count), `desc[0x2c] = shd[0xcc]` (PS stage).
  - The binder reads `blockB[i*8]` for `i <` the PS count. An index past TEXB, or a PS count above the slot count, hands D3D11 a garbage SRV.
  - `slotcheck`: 0/836 shaders read past block B.

### 6.3 Textures (.tbody)
- **A TEXB entry's 8-byte hash, as 16 hex digits, is the `.tbody` file stem.** The 4 bytes after it are `0xffffffff`; `mtb-retex.py` calls them "spare".
- **A `.tbody` is a DDS file** (`DDS ` magic; `conv.copy_texture`).
- **Where textures live**:
  - The global pool `textures/<first byte>.zip`, which holds `textures/<hash>.tbody` and where every world finds a texture (`autobundle.in_pool`).
  - Many assets keep theirs in their own `textures/` folder instead (`autobundle.tex_index`).
  - `native30`/`expl30` copy textures the pool lacks into `<assets>/textures/`.
- **D3D11 rejects block-compressed textures below 4x4** (`CreateTexture2D` `E_INVALIDARG`, the only 2 throws in Metroville). A 1x1 or 2x2 single-mip DXT file already holds one whole 4x4 block, so `copy_texture` only raises the header's height (+12) and width (+16) to 4. It checks the mip count at +28 and `DXT` at +84.

### 6.4 1.0 materials in 3.0 (autobundle tiers)
- Each material takes the first tier that applies:
  1. **real**: the same GUID ships in 3.0, so it is copied verbatim.
  2. **same-shader**: a 3.0 material with the same logical shader supplies the blocks, holding the 1.0 material's own textures.
  3. **generic**: a common 3.0 shader that accepts the vertex format. The 1.0 textures go in order into the slots whose role shipped materials fill, never into the engine-bound ones ([7.1](#71-shd)).
- Hand-made exceptions:
  - `GENERIC_SLOTS`: shader `d0e71afc...` is mask, colour A, normal, colour B, normal. Taken in order, the mask became the colour (Metroville's mottled walls).
  - `NOT_REAL`: `i_buildingrooftop`'s 3.0 copy is on the debris shader `a3399c16`, which made the roofs invisible.
- `--survey` (2026-10-03): 3,748/4,424 material slots resolve.

---

## 7. Shaders and vertex formats
Code: `shdfmt.py`, `vtxf.py`, `matp.py`, `slotcheck.py`

### 7.1 .shd
| Offset | Field |
|---|---|
| `+0x6a` | u16 block A size (3.0) |
| `+0xa0` | binder descriptor base: the draw loop passes `shader+0xa0` (`slotcheck.py`) |
| `+0xa1` | u8 block B texture-slot count |
| `+0xa2` | u16 block B size |
| `+0xcc` | PS stage byte: low nibble = first t-register, high nibble = number of slots read. `slotcheck.stages` reads 5 such bytes, `0xcc..0xd0`, and treats the first as the PS |
| `0xF8..` | pass records, 80 B each, to the end of the file: `+0` vformatCRC, `+0x10` VS file id (`<8hex>.vp`), `+0x18` PS file id (`<8hex>.fp`) |

- **Files**: `.vp`/`.fp` are DXBC with their reflection kept: `ISGN` (VS inputs) and `RDEF` (bound resources; resource type 2 = texture).
- **Input layout**: if the VS reads a semantic the vertex format lacks, `CreateInputLayout` throws (layout builder FUN_0044d430). `ISGN` sysval 0 = fed by the input layout.
- **Slot roles** (`shdfmt.slot_roles`): slot i feeds `t(first + i)`; first = 5 in 821 of 836 shaders.
  - A pass samples a subset of the slots, so roles are the union over passes. 834 of 836 shaders name each slot at most once.
  - **Material roles are always filled** over every shipped 3.0 bundle: diffuse0 14,621 of 14,621, normalMap0 12,976, emissive, specular0 ...
  - **Engine-bound roles are never filled**: dither_pattern 0 of 11,825, shadow, colorBuffer, normalGlossCopy, bink_*.
- **Shader names hash like vertex-format GUIDs** (FUN_0045c820 -> `Shader_LookupCrc`).

### 7.2 vformat.vtxf
- **File**: `shaders/vformat.vtxf` is a Lua table literal. Its `formats` table maps format GUID -> variants (`main`, ...) -> `streams` -> elements {`semantic`, `type`, `name`, optional `default`} (`vtxf.parse`, `vtxf.elements`).
  - Elements are packed back to back: an element's offset is the sum of the sizes before it, and the stream stride is the total (`vtxf.layout`).
- **`vformatCRC` (in `.oct`) = `name_hash(format GUID)`** (FUN_0044aa40). The hash is case-insensitive, with 4-byte chunks folded with `*0x25` (`vtxf.name_hash`). Test vector: `name_hash('8bc48a5716cd76811da52f7aee4c28b2') == 0x63ae710f`.
- **Registry**: FUN_004642f0 loads the file into a registry at `0x02170664`, 52 B per format, with the count at `0x0216f660`. At runtime it holds 41 entries: slot 0 is empty, and the rest are the 40 formats from the file.
- **An unknown CRC is not an error, it is a silent bad layout.** The primitive loader FUN_004553c0 stores `lookup(vformatCRC)` as a **byte**. An unknown CRC returns `0xbadddec1`, which truncates to slot `0xc1`. That slot is empty, so the input layout (FUN_0045c5c0 -> FUN_0044d430 -> `CreateInputLayout`) is built from nothing.
- **1.0 formats**: 1.0 has 44 formats, and 12 have an exact 3.0 twin (identical `main` declaration). 1.0's `ea1acc4c` (sumo, monolith) = 3.0's `63ae710f`; it was tested in-game on 2026-10-02 and renders.
- All 12 vformatCRCs used by shipped 3.0 `.oct` files hash from a 3.0 GUID (`vtxf --selftest`).

**Element types** (3.0 uses all nine; size = `vtxf.SIZE`):

| Type | Bytes | Kind |
|---|---|---|
| `FLOAT32_2` / `_3` / `_4` | 8 / 12 / 16 | real |
| `FLOAT16_2` / `_4` | 4 / 8 | real |
| `U8_4_NORMALIZED` | 4 | real (x / 255) |
| `S16_4_NORMALIZED` | 8 | real (x / 32767, clamped at -1) |
| `U8_4` | 4 | integer (BLENDINDICES) |
| `U16_2` | 4 | integer (packed texcoords) |

**Conversion** (`vtxf.plan`, `convert_vertex`, `targets_1to3`):
- **Matching**: a 3.0 element is filled from the 1.0 element with the same semantic.
  - It is copied when the type matches.
  - Otherwise it is converted between any two real types (normalised <-> float, float16 <-> float32, 3 <-> 4 components) or between integer types (clamped).
- **Missing data**: components the source lacks come from the 3.0 element's declared default. Without one, they come as shipped data has them (`vtxf.layout`):
  - **Tangent handedness**: `w = +1` for the `*_signed` tangents. Shipped w is +-1 in 477,578 of 477,578 sampled vertices (+1 in 94.4%) and never 0.
  - Everything else is 0. NORMAL w is 0 in 491,568 of 491,568.

  Only a semantic the source does not have at all is padded.
- **Ranking**: identical twins come first, then conversions ranked by (no non-colour element dropped, fewest padded, fewest dropped). A target that pads `POSITION` is never used.
- Per primitive, the target must be a format the material's shader declares a pass for (`autobundle.resolve`).

---

## 8. Collision (.btd, .btm)
Code: `btdconv.py`, `btdblocks.py`; also `btdscan.py`, `btd-info.ps1`, `btd-a-const.bin`

**A `.btd` is one block per TriangleList shape** (`SerializeIndex` = block ordinal). Blocks are packed back to back and each starts 16-byte aligned. A block ends where its per-triangle u32 array ends: the array offset and count are at `+0x28`/`+0x2c` in 1.0 and `+48`/`+52` in 3.0 (`btdblocks.blocks`). Block count = TriangleList shapes in 267/267 3.0 worlds. Metroville has 724 blocks, monolith 2.

**1.0 block header**: `+0` A offset, `+4` A length, `+8` B offset, `+12` B length, `+16` vertexOffset, `+20` vertexCount, `+24` indexOffset, `+28` triangleCount, then `+0x28`/`+0x2c` (above).

**3.0 block** (what `btdconv.convert` writes, matching stock 3.0):

| Off | Field |
|---|---|
| +0 | u64 vertexOffset |
| +8 | u64 indexOffset |
| +16 | u32 vertexCount |
| +20 | u32 triangleCount |
| +24 / +28 | section A offset, length |
| +32 / +36 | section B offset, length |
| +40 | end of the index array, rounded up to 4 |
| +44 | 1 |
| +48 / +52 | per-triangle array offset, count |
| +56..63 | `0c 00 06 00 02 00 00 00` in all 6,866 shipped blocks |
| +64 | section A, then B, vertices (f32x3), indices (u16 x3), an 8-byte gap, the per-triangle u32 array (zeros). The gap is written `0, ffffffff`; in 37% of shipped blocks its first word holds exporter junk |

- **Section A** is the tree. Its first 64 bytes hold the AABB min/max at block `+80`/`+96`: the root bounds, with a 1.0 margin.
  - **The tree is carried from 1.0, because it cannot be synthesised**: it depends on how the geometry is distributed. A donor's 480-triangle tree in front of 7,056 triangles gave no collision at all.
  - Only its descriptor changes:
    - `A[0..63]` carried;
    - `A[64..183]` = the 120-byte constant `btd-a-const.bin` (identical in all 322 pairs);
    - `A[184..199]` = 1.0 `A[64..79]` (tag `0x114`, node count, flags);
    - `A[200..203]` = 1.0 `A[180..183]`;
    - `A[204..]` = 1.0 `A[188..]` (the tree body, unchanged).
  - Net: A grows by 16 bytes.
- **Section B**: `4 x triangles + 72` bytes in 3.0 (`+56` in 1.0). Its four leading pointer fields widen from u32 to u64, so it is 32 zero bytes then 1.0 B from `+16`, with the size field (`+16`) = 72.
- `--offset` shifts vertices and the AABB together.
- `btd-info.ps1` notes that the region at block `+320` is undecoded and looks like a BVH.
- **`.btm`** is the `.btd`'s block table: `u32 0xc007d00d`, `u32 block count`, then `(u32 offset, u32 0x00010010)` per block (`btdconv.btm`).
  - **Without a matching `.btm` the game loads only the blocks it lists**: a copied 1-block `.btm` = no collision.
  - `btd-info.ps1`: the version word is `C001` in 1.0 and `C007` in 3.0.
  - Verified: `btdconv --selftest` rebuilds **every shipped `.btm` from its `.btd`** byte-exact, 317/317. This needs only the 3.0 extract, and the test asserts it.
- Verified: with the 1.0 extract, `btdconv --selftest` rebuilds shipped 3.0 `.btd` from their 1.0 twins and counts single- and multi-block pairs separately. 323/330 single-block pairs were byte-exact (2026-10-03); the comparison masks the gap's junk word. Collision works in-game.

---

## 9. Localisation (.dct)
Code: `dct.py`. Layout decoded from the 471 shipped files (all little-endian, the `_xbox360` ones too).

| Off | Field |
|---|---|
| 0x00 | `'DICT'` |
| 0x04 | u32 `0x2000` |
| 0x08 | u32 initval (hash seed) |
| 0x0C | rel -> slot table (`0x20`; stored `0x13`) |
| 0x10 | u32 slots |
| 0x14 | u32 1 |
| 0x18 | rel -> containers (= end of the slot table) |
| 0x1C | u32 0 containers |
| 0x20 | slots x 12 B: u32 hash, rel -> value, u32 0. An empty slot is 12 zero bytes |
| ... | string pool to EOF: `''` first (even when no key uses it), then each distinct value once, UTF-8 + NUL, in order of first use by slot |

- **rel**: target = field offset + 1 + stored value.
- **Keys are stored only as hashes**: Bob Jenkins' lookup2 of the name's Latin-1 bytes, seeded with initval.
  - A `stringids*.dct` ships beside each package's language files, and its values **are** the key names.
  - The hash reproduces 337,023 of 337,023 keys in the 37 stringids files. Hashing UTF-8 instead misses one name with an NBSP (3 files).
  - 8 keys hold `...` as U+2026 where the hash has `&`.
- **Placement**: slot = `hash % slots`, then a linear probe forward with wrap.
  - `slots = (keys + 1) * 6 // 5` for all 27 key counts that ship; `int(keys * 1.2) + 1` misses 67 files.
  - The insertion order cannot be recovered. `load()` lists keys in table order from the first empty slot, and re-inserting them in that order rebuilds the same table.
- **Containers** (24 B records in a third-party converter) never ship; `load()` rejects them.
- The `_xbox360` files have the same layout. 141 of 147 differ from `_win32` in initval, key set or text.
- Verified on 471 files (4,372,484 keys): load/dump exact 471, export->import exact 471. The stringids name 4,319,415 of the keys; the rest export as `#hash`.

---

## 10. Gamedb (.chd, zonelist.lua)

### 10.1 .chd: compiled gamedb tables
Code: `chd.py`. A `.chd` is a Lua table literal compiled to binary (inventory, locks, rumpusdata). 30 of the 33 shipped 3.0 files have their source `.lua` beside them.

| Off | Field |
|---|---|
| 0x00 | u32 `0x0123abcd` |
| 0x04 | u32 `0x3000` |
| 0x08 | u32 `0x20` -> key table |
| 0x0C | key table size |
| 0x10 | -> string table (= end of the key table) |
| 0x14 | string table size |
| 0x18 | -> data (end of the strings, zero-padded to 4) |
| 0x1C | data size (data runs to EOF) |

- **Name tables**:
  - Key table: each distinct key name once, sorted bytewise, as u16 length + bytes + NUL.
  - String table: each distinct string value once, in the same form and order. `''` appears only when some value is `''`.
- **Data**: a root entry (`00 00 00 d0`, 8), then every table in depth-first pre-order. A table is `u16 0xfeed`, `u16 n`, then n entries of 8 bytes: `u24 key`, `u8 type`, `4 B value`.
  - key = offset of the name in the key table, or `0xffffff` for an array item.
  - type: `0x00` i32, `0x20` f32, `0x30` string (offset in the string table), `0x40` bool (0/1), `0x50` table (offset in data); `+0x80` for an array item.
- **A table holds named entries sorted by key, or array items in Lua order, never both.**
- The key table also holds `true`/`false` when some bool is true/false (the compiler interns the Lua identifiers). All 11 `inventory__lua` files carry them unused.
- Types seen: `0x00 0x20 0x30 0x40 0x50 0x80 0xb0 0xd0`. Array items `0xa0`/`0xc0` (f32/bool) never ship; `dump()` writes them by the rule.
- Strings are bytes (`stateData` is a 16-byte blob). They load as UTF-8 with surrogateescape.
- Measured on 33 files (345,031 entries, 36,037 tables): load/dump exact 33, export->import exact 33. `lua()` + `dump()` compiles 29 of the 30 `.lua` sources to their `.chd` byte-exact; `empire/rumpusdata.lua` has `ID = 5` where its `.chd` has 4.
- An int/float/bool edit changes only its 4 value bytes.

### 10.2 zonelist.lua: zone records
Code: `zones.py`. The member `gamedb/core/zonelist.lua` of `assets/startup2.zip` is UTF-8 Lua text.

- **Records**: the entries of `Records.{GlobalData, InheritList, PlaysetProperties, Zones}` form one namespace. A zone's `_Inherits_` names an `InheritList` entry, and a child's own fields win over its parent's.
- **Zone name -> world folder**: the zone name in lower case is the world folder (`PSX_Arcade_01` loads `assets/worlds/psx_arcade_01/`).
- **The menu gate is three flags**, evaluated after `_Inherits_` resolution: `_ShowInMenu_`, `availableInConsumerBuild` and `availableInShippingBuild`. A parent may supply some and not others. `Inherit_PSX_Arcade_Parent` has no `availableInShippingBuild`, so its zones stay hidden in the retail build whatever the child sets.
- **Environment**: real zones inherit a parent carrying `MasterZone`/`PlaysetProperties`/`System`/`SaveZoneOverride`. A record without one gets no environment set up; every world loaded through such a record, stock 3.0 geometry included, rendered unlit.
- **Start points can come from another zone**: `PSX_L1_Planet_Main` has `AlternateStartPointZone = "PSX_L1_Planet_Details"`. The exe tries it after the loading zone's own starts ([5.5](#55-the-start-chain)).
- **The zone names the graph**: `native30 --zone` takes the record name as written in `zonelist.lua`. It becomes the `.mer` namespace `Name`/`GraphFileName`, because starts are keyed by the loading zone ([5.5](#55-the-start-chain)).
- **Text layout `zones.py` edits**:
  - Records sit at 3 tabs (`<Zone> = ` then `{`) and fields at 4 tabs.
  - `add` places a new record before `PSX_Intro` with `MasterZone = "PSX_Master"`, `PlaysetProperties = "PlaysetX"` (or `_Inherits_`), `SplitScreenAllowed = false` and the three flags.
- **Where it reads**: `--zonelist FILE`, the loose install copy, or straight from `startup2.zip`. Edits inside the archive go through `di3arc.add_files`, keeping `startup2.zip.orig`.
- Other zone keys the README names: `WorldIsIntro` (defaults to false), `OverrideInitialCreation` / `OverrideCreation` (choose the avatar creation). None of them places the player.

---

## 11. Lua bytecode and expendables packages
Code: `luabc.py`, `expack.py`, `expl30.models_of`

- **Bytecode**: the game's Lua chunks are **Lua 5.1, 32-bit, big-endian**: header `\x1bLua\x51`, byte 6 (endianness) = 0, byte 8 (size_t) = 4.
  - `luabc.to64` re-encodes them for a 64-bit little-endian Lua 5.1 (lupa). Only the endianness and the size_t fields (string lengths) change.
  - Per function: source string, 2 x i32, 4 bytes, code (i32 n + n words), constants (type byte: 1 bool, 3 number 8 B, 4 string), prototypes, line info, locals (string, i32, i32), upvalue names.
  - `luabc --selftest`: 17/17 3.0 packages re-encoded.
- **Packages**: 3.0 loads expendables packages **per playset**: `assets/expendables_data/<playset>.lua` (`playsetx.lua`, `toybox.lua`). 1.0 packages are named by type (`TerrainBuildingsExpendables`).
  - Each sets the global `ExpendablesPackage = {Expendables = {...}, Groups = {...}}`; the loader FUN_0160aa30 reads only that.
  - Entries name their model as `Model = 'exploders/...'` (`expl30.models_of`).
- **Write merged packages as plain Lua text.** A text wrapper that `loadstring()`s the embedded bytecode did **not** run in-game (0 of 34 playsetx-only definitions registered). `expack` writes the merged table as a plain Lua literal; all 198 1.0 definitions registered (README, memory dump).
- **Pop-in range is mandatory.** 1.0 entries have no pop-in range. 3.0 then stores 0 in the definition (`+0x9c`/`+0xa0` = squared distances), so the expendable never pops in and its model is never even requested. `expack` adds `PopinDistance 1000`, `PopoutDistance 1200`, a shipped 3.0 pair (used 75 times).
  - No rule derives them: model size predicts the shipped pair for 31.6% of 453 models, and a constant for 17.2%. So `expack.PORT_DEFAULTS` stays explicit (README).

---

## 12. Save files
Code: `savegame.py`. PC saves are `SavedGame/user0/<file>`. `selftest.default_saves` looks for them under `%APPDATA%/Disney Interactive/Disney Infinity 3.0`. Layout decoded from the 16 saves of one PC profile.

| Off | Field |
|---|---|
| 0x00 | u32 `0x207` |
| 0x04 | u32 file size |
| 0x08 | u32 1 |
| 0x0C | u32 text size |
| 0x10 | u32 file size - 0x40 |
| 0x14..0x3F | zero |
| 0x40 | `'CMP1'` |
| 0x44 | u32 text size |
| 0x48 | u32 packed size |
| 0x4C | u32 lookup2(text) |
| 0x50 | u32 lookup2(packed) |
| 0x54 | packed: a zlib stream (`78 01`) of the text, then zero bytes to the next multiple of 64 (none when it already ends on one) |

- **Checksums**: lookup2 is Bob Jenkins' hash with seed 0 (`dct.lookup2`). The text hash covers the final NUL.
- **Text**: the whole save, as a Lua-like tree (`Root =\r\n{\r\n\tKEY = value ...}`). It is CRLF only, ASCII only, with one NUL at the end and none before it. The text stays bytes, since no save shows how non-ASCII is encoded.
- **Files seen** (areas RR1, RO0, BA0); every kind has the same container:

  | File | Holds |
  |---|---|
  | `P0` | `CGameSettings` |
  | `S<area>A` | the Toy Box (`RumpusSaveGame`, `RR_SAVE_VERSION = 127`) |
  | `SH<area>A` | its `CSaveGameSlot` header |
  | `SC<area>A` | its screenshot (`SCREENSHOT = $` + hex rows) |
  | `E<area>` | `HelpManager` |
  | `EH<area>` | `CSaveGameSlot`, `INFINITY_VERSION = 3` |

- **Encryption**: PC saves are not encrypted. A big-endian `0x207` is a console save (Phelix-encrypted according to the third-party LiteralToyBoxDecryptor); `load()` rejects it.
- **Recompression**: zlib level 1 with default parameters reproduces all 16 stored streams (zlib 1.3.1). A zlib that packs level 1 differently (zlib-ng) still writes a valid stream, but not the same bytes.
- Verified on 16 files: every header word holds, both checksums reproduce, load/dump and export->import are exact.

---

## 13. Executable facts (DisneyInfinity3.exe)
Code: `arenapatch.py`, `arenas.py`, `mdmp.py`, `disx.py`, `dispfind.py`

- **Exe build**: TimeDateStamp `0x5851C5D1` (README).
- **Static vs runtime addresses**: static addresses assume base `0x400000` (`mdmp.read_static`).
- **ASLR**: the runtime base changes between runs: `0x00F60000` on the Aug-16 launches, `0x00D00000` on the Aug-18 ones. Read it from the **last** `EXE base = ` line of `pentane.log` (`disx.py`); Pentane prints it after config errors and plugin loading.

| Address | What | Source |
|---|---|---|
| FUN_012ea270 -> FUN_012e9e60 | archive index bsearch | `di3arc.py` |
| FUN_00472590 | `.mtb` MATP parser | `matp.py` |
| FUN_0045c820 -> `Shader_LookupCrc` | shader name lookup (name_hash) | `matp.py`, `vtxf.py` |
| FUN_00459a90 | MATP material names | `matp.py` |
| FUN_00468500 | material lookup by hash (materials cached by hash) | `matp.py`, `autobundle.py` |
| 0x45D5C0 `Shader_ParseBindings` | texture-slot resolver, called from the `.mtb` parser | `slotcheck.py`, README |
| FUN_0045a1c0 (call at 0x45a2c4) | PS texture binder; d3d11 `PSSetShaderResources` crash | `slotcheck.py`, README |
| FUN_0044aa40 | `name_hash` | `vtxf.py` |
| FUN_004642f0 | `vformat.vtxf` loader; registry `0x02170664` (52 B/format), count `0x0216f660` | `vtxf.py`, README |
| FUN_004553c0 | primitive loader: `lookup(vformatCRC)` stored as a byte; unknown = `0xbadddec1` -> slot `0xc1` | `vtxf.py`, README |
| FUN_0045c5c0 -> FUN_0044d430 -> `CreateInputLayout` | per-mesh input layout (FUN_0044d430 = layout builder) | README, `shdfmt.py` |
| FUN_01604820 | registers `ExpendablePool`; resolves `Expendable` NodeName + `'\|Transform'` as a graph DataNode | `native30.py` |
| 0x13a85d0 `StartPositions::GetStartPosition` | the starts registered for the loading zone, then `AlternateStartPointZone`, then any zone, else the origin | `native30.py`, README |
| 0x1485830 | StartPositions keys a start by the loading zone | `native30.py` |
| 0x77ca30 | parses the `.mer` only with `ExternalFilePool [Meridian]` in the `.oct` | `native30.py` |
| 0x6b6830 | `MeridianLocator` binding: requires a Transform DataNode, resolves a Dilation to null | `native30.py` |
| 0x147ff90 / 0x17640a0 | the locator transform (breakpoints for the start position) | README |
| 0x14397ae | `InitPlayersAtClosestCheckpoint` (set on `PSX_Master`) | README |
| FUN_0160aa30 | expendables package loader (reads only `ExpendablesPackage`) | `expack.py` |
| `OctanePlayerMain+0xdcddd7` | crash (eax = 0) from an undersized SubNetwork | `graph.py` |
| FUN_00aae3e0 (file offset `0x6ad7e0`) | render config: `+4` arena A = `0x140000`, `+8` arena B (draw commands) = `0x300000`, `+0x10` arena C = `0x4000` | `arenapatch.py` |
| FUN_0047e0e0 / FUN_0047e030 | arena bump allocators; return NULL when out (the retail build does not check) | `arenapatch.py`, `arenas.py` |
| FUN_0045ace0 | resets each arena's `cur = base` per frame | `arenas.py` |
| `[[0x02150d68] + 0x48]` | render context (contexts form a ring, `ctx[0]` -> next); arenas `{cur, end, base}` at ctx `+0x2520` (A), `+0x2530` (B, draw buckets), `+0x2548` (C) | `arenas.py` |
| 0x47d330 | draw loop: NULL draw list on arena overflow | README, `arenapatch.py` |
| 0x7bcc0c | physics body with a null shape | README |
| 0x403fc0 | heap block-list corruption (a GeometryNode's allocator record) | README |
| 0xDEA280 | avatar routine; waits on a ReaderMgr *hold* named "Intro" (a set at ReaderMgr+0xB4C that also holds "LevelLoad"; check 0xDCC5D0) | README |
| expendable definition `+0x9c` / `+0xa0` | squared pop-in / pop-out distances | `expack.py` |

**Arena patch** (`arenapatch.py`):
- Each of the 3 render contexts carves A|B|C from one block: 1280 KB / 3072 KB / 16 KB, contiguous (read live from the Aug-16 dumps).
- The patch raises them to 5 MB / 16 MB / 64 KB. The immediates sit at function `+0x07`, `+0x0e` and `+0x15`, after the prefix `8b 44 24 04 c7 40 04`.
- It verifies the original bytes first and backs up to `<exe>.prearena`.
- There is no fixed block-size constant in the exe.

---

## 14. 1.0 vs 3.0 differences

| Area | 1.0 | 3.0 | Source |
|---|---|---|---|
| Archives | plain zips | hash-index archives, AES-128-CTR (default / PSX / none), MD5 extra | `di3arc.py`, `extract.py` |
| `.shd` block offsets | A @`0x46`, B @`0x6a`, slots @`0x69` | A @`0x6a`, B @`0xa2`, slots @`0xa1` | `matp.SHD_LAYOUT` |
| Texture slot | 4 B: bare u32 index | 8 B: `(idx, 0)` or `(ffffffff, ffffffff)` | `matp.py`, `slotcheck.py` |
| Material block A | 1.0 params | same params + new trailing ones (401 of 421 same-shader GUIDs) | `autobundle._substitute` |
| Block B after the slots (samplers) | 1.0 layout | changed layout in every pair (not decoded) | `autobundle._substitute` |
| `Idata[1]` | start in indices | start in bytes | `meshexport.py` |
| Index values | relative to the primitive's own streams; `Idata[0]`, `Idata[2]` unused | node-global; `Idata[2]` = base vertex; `Idata[0]` 0 = 16-bit, 2 = 32-bit buffer | `conv.py`, `meshexport.py` |
| Vertex regions | each node its own region | stream-major within a node; formats mixed per node | `conv.build_buffers`, `streamlayout.py` |
| Vertex formats | 44 formats | 40 formats (+ empty slot 0); 12 1.0 formats have an exact twin; the rest are converted | `vtxf.py`, README |
| Primitive RenderCaps | `0x1000` on every drawn primitive; 0 is drawn | `0x200` (0x200-0x207); `0x1000` never ships; 0 is not drawn | `conv.rendercaps30` |
| Material RenderCaps | present | absent | `conv.prepare` |
| `LodGroup` node | present | never ships | `native30.py` |
| `Light` node in worlds / exploders | 1.0 light fields | `native30` turns it into a plain Transform (exploder point lights broke every VFX) | `native30.py`, `expl30.py` |
| `Light` node in realms | `LightType` name, `LightColor`, `LightIntensity` | `LightType` 6/7, `LightData`, `FeatureCaps 0` | `realm30.py` |
| Realm data | `RealmDataPool` | `RealmData` scene node at index 1 | `realm30.py` |
| `LightRig` | no `Uuid` | `Uuid` after `LightRefs` | `realm30.py` |
| Pools only 1.0 ships | `Realm*`, `SurfaceTypes` (+ a duplicate `LayoutPool` in in_city) | never | `conv.foreign_pools`, README |
| `AssetPool` | absent (`PrimaryAsset` association) | present | `realm30.py`, `expl30.py` |
| Graph | no processor nodes (3,480 Dilations in in_city) | positions through processor chains; extra SubNetwork counts | `native30.py`, `subnet.py` |
| DataNode link path | `Anchor\|X` | `Anchor\|X\|Transform\|In WorldBasis.Transform` | `expl30.py` |
| Exploder collision | no aggregate | per instance a `CollisionAggregate` with the DataNode link's `NodeName`; `AggregateCount` = instances | `expl30.py` |
| `Expendable` association / `ExpendablePool` | present | never ship; still registered and resolved (FUN_01604820) | `native30.py` |
| `.btd` header | 8 u32 (+ array @`0x28`/`0x2c`) | u64 offsets + u32 fields (array @48/52), `+56` constant | `btdconv.py`, `btdblocks.py` |
| `.btd` section A | 1.0 descriptor | descriptor rebuilt with a 120-byte constant (+16 B) | `btdconv.py` |
| `.btd` section B | `4t + 56` | `4t + 72` (pointer fields u32 -> u64) | `btdconv.py` |
| `.btm` version word | `C001` | `C007` (`0xc007d00d`) | `btd-info.ps1`, `btdconv.btm` |
| Expendables packages | named by type | per playset | `expack.py` |
| Expendable pop-in | no `PopinDistance`/`PopoutDistance` | required (0 = never pops in) | `expack.py` |
| Small DXT textures | 1x1/2x2 single-mip files occur (the only 2 `CreateTexture2D` throws in Metroville) | D3D11 rejects < 4x4 | `conv.copy_texture` |

---

## 15. Open questions

Things the repo marks as unknown, untested or guessed:

**Engine and assets**
- **Loose vs archived**: README says "the engine only sees assets inside archives". Yet `native30`/`expl30` write loose folders ("loose, like worlds"), and loose expendables packages and `zonelist.lua` are used once their archive is disabled. Which loose paths the engine reads is not recorded.
- **Loose `.dnax`**: CrabeLoader's character mods serve `.dnax` files loose, against this repo's "the dna lookup is archive-only". One launch would settle it (README, Next 4).
- **Non-empty `.ap`**: the format of the `.ap` beside a world with an `ActorParameters` entry is not documented. The converters write only empty ones.
- **World archives**: `pack_dir` rebuilds 576 of 579 shipped world archives (README). What differs in the other 3 is not recorded.

**Graph and spawn**
- **Start position in-game**: `native30` now writes `ExternalFilePool [Meridian]`, its own `.mtc` and holohendy's ground start ([5.5](#55-the-start-chain)), but this has not been launched yet (README, Next 1).
  - If the player still spawns at the origin: break on 0x13a85d0 (its result) and on 0x147ff90 / 0x17640a0 (the locator transform).
  - If a checkpoint wins: 0x14397ae (`InitPlayersAtClosestCheckpoint`, set on `PSX_Master`); try a record without it.
- **SubNetwork counts**: no rule yet for `ConstantHeaderCount` ... `ConstantLayoutCount` (`subnet.py` fitting stage).
- **Shared DataNode sizes**: `graph.plan` refuses a second shared DataNode other than `Dilation` (+0) or `Transform` (+32 B), because no other size has been measured.
- **`.mer` parts not decoded** (`mer.py`):
  - the names of pins a node does not list (867 of 36,805 link ends at nodes that list pins hit an unlisted one; the catalogue names pins from the instances that list them);
  - the internal name behind a `LeafNameHash` whose authored label differs (24,495 of 43,868);
  - what the connection type codes mean beyond their link direction.

**Geometry and materials**
- **RenderCaps**: `0x2000 -> 0x400` is a guess (`conv.rendercaps30`; README, Next 7).
- **Hand-written exceptions**: `GENERIC_SLOTS` (one entry) and `NOT_REAL` remain. They need the 1.0 shaders' resource tables (README, Next 7; `shdfmt.slot_roles` is the 3.0 side) or an in-game observation. The NRM-first shaders `030a6710`, `255f3180`, `1fe46d84` and `01ae42b4` would need entries too if seen.
- **Block B samplers**: the layout of block B after the texture slots is not decoded; the donor's is kept.

**Collision**
- **`.btd` tree**: section A's tree body is carried, not decoded ("looks like a BVH", `btd-info.ps1`).
- **Gap junk**: the first word of the 8-byte gap after each block's indices is exporter junk in 37% of shipped blocks, with no pattern (`btdconv`). `btdconv` writes 0, and its 1.0 -> 3.0 comparison masks that word.

**Gamedb, localisation, saves**
- **`.chd`**:
  - The meaning of the `0x3000` word at `0x04` is not recorded; `load()` rejects any other value.
  - Whether the game reads a key it does not know is untested.
  - Array items `0xa0`/`0xc0` never ship, and are written by the rule only.
- **`zonelist.lua` inside `startup2.zip`**: whether the game accepts an edit made there is untested in-game; `dna.zip` edits made the same way load.
- **Saves**:
  - Word `0x08` is 1 in all 16 measured saves; its meaning is not recorded.
  - Whether the game accepts an edited save is untested.
  - Console saves (big-endian `0x207`, encrypted) are not handled.
  - How non-ASCII text is encoded is unknown.
- **`.dct`**: containers never ship. Their 24 B record layout is known only from a third-party converter, and `load()` rejects them.

**Not yet checked**
- **2.0 Gold Edition** (none installed): whether it is closer to 3.0 than 1.0 (`vtxf` twins, `matp --ver`, `oct --selftest`) is unknown.
- **CrabeLoader mod** (`crabe/di3tools/`): untested in-game.

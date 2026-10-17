# Disney Infinity Tools

Run **Disney Infinity 1.0** content natively inside **Disney Infinity 3.0** (PC Gold Edition), plus a verified
toolkit for the games' file formats.

> **Disclaimer.** This is an unofficial fan project. It is not affiliated with, endorsed by or supported by Disney or
> Avalanche Software. The repository contains no game data: you need your own installed copies of both games.

## Contents

- [Overview](#overview)
- [Status](#status)
- [How di1compat works](#how-di1compat-works)
- [Requirements](#requirements)
- [Building](#building)
- [Installing](#installing)
- [Configuration](#configuration)
- [Testing](#testing)
- [Python toolkit](#python-toolkit)
- [Repository layout](#repository-layout)
- [Third-party code](#third-party-code)
- [License](#license)

## Overview

The project has two parts:

- **di1compat** (`native/di1compat`) is a DLL loaded into the game by the [Pentane](#requirements) plugin loader. When Disney Infinity 3.0 asks
  for a file it does not have, the plugin reads it from your 1.0 installation and translates it at load time into the
  form the matching 3.0 system expects. Nothing on disk is modified. 1.0 data goes into the 3.0 system that replaced
  its 1.0 counterpart, and new systems are built only where 3.0 has none (for example the 1.0 FMOD audio host).
- **The Python toolkit** (repository root) reads and writes the archive, scene, graph, material, collision,
  localisation, game-database and save formats, and adds analysis and debugging tools. A tool stays in this repository
  only when it has a check against shipped game data.

[FORMATS.md](FORMATS.md) is the format reference behind both parts. Each fact in it names the data it was measured on.

## Status

The first target is **Metroville** (*The Incredibles* playset, zone `IN_City`), served straight from the 1.0
installation. Last verified in-game in October 2026 on the development machine:

| Area | State |
|---|---|
| Loading | About 20 s from launch to the opening cinematic |
| Performance | About 150 fps during play |
| World | 1.0 geometry, materials, lighting, scene settings (fog, bloom, exposure, grading), collision and expendable buildings |
| Rendering | Each 1.0 material uses the identical 3.0 material where one ships. Otherwise the original 1.0 Direct3D 9 shaders are translated to Direct3D 11 bytecode |
| Missions | The player spawns at the 1.0 start point; the world and mission graphs (Meridian) run the opening mission chain |
| Cinematics | 1.0 choreographies play with their actors, cameras and body animation, plus scripted destruction such as the dock explosion |
| Audio | 1.0 FMOD sound banks, the music system with its cues, and dialogue banks, including multi-line dialogue sequences |

**Known issues**

- Some sound effects inside cutscenes are not audible (under investigation).
- Some 1.0 fog particle effects have been reported missing.
- A rare crash has been seen while the vault props are instanced.

## How di1compat works

The plugin hooks the engine's asset open. For a path 3.0 lacks, it reads the file from the 1.0 archives and returns
the translated result to the engine as an in-memory file. The translations by format:

| 1.0 data | Translation | Source |
|---|---|---|
| Scene and processor graphs (`.oct`) | Pool counts, header layouts and constants in 3.0's form | `octx.hpp`, `oct.hpp` |
| Choreographies | Clip blocks re-packed; motion-clip output ids renumbered to 3.0's skeleton ports; play ranges and layout attachments | `games/di1/cho.hpp`, `choconst.hpp`, `cholayout.hpp`, `ports10.hpp`, `convert/clips.hpp` |
| Meridian graphs (`.mer`, `.mtc`) | Parameter tables, pin tables, links; 1.0-only node classes registered natively | `formats/mer.hpp`, `games/di1/mer.hpp`, `engine/mernode.hpp` |
| Materials and shaders (`.mtb`, `.shd`) | Resolved to identical 3.0 materials, or 1.0 shaders translated at load (compiled once, then cached) | `render.hpp`, `gen30.hpp`, `sm3.hpp` |
| Entities (`.bent`) | Rewritten in 3.0's 4.1 layout | `bent.hpp` |
| Game database (DataMap Lua, `.chd`, zone list) | Paths and records in 3.0's form; CHD compiled from Lua | `datamap.hpp`, `chd.hpp`, `gameplay.hpp` |
| Scripts and AI (Slang, behaviour maps and trees, navmeshes) | 3.0 containers, with natives bound to their 1.0 semantics | `sx.hpp`, `npc.hpp`, `npcrt.hpp` |
| Missions and UI | Event enums, beacons, loading screens, menus | `games/di1/mission.hpp`, `ui.hpp` |
| Collision (`.btd`), localisation (`.dct`) | 3.0 block layout; 1.0 dictionaries | `btd.hpp`, `dct.hpp` |
| Audio (FMOD Ex events, music, dialogue) | A 1.0 FMOD event system runs beside 3.0's Wwise. Posts for 1.0 events, music cues and dialogue lines are routed to it | `audio.hpp` |

Every translation has a configuration switch, on by default, so a problem can be narrowed down one subsystem at a
time.

## Requirements

- *Disney Infinity 3.0 Gold Edition* for PC (Steam).
- *Disney Infinity 1.0 Gold Edition* for PC, installed.
- The Pentane plugin loader for Disney Infinity 3.0, included in [`third_party/pentane`](third_party/pentane). It
  only loads the plugin at startup, before the engine opens its first file; di1compat installs its own hooks with
  MinHook.
- To build: Visual Studio 2022 (MSVC, x86 toolset) and CMake 3.21 or newer.
- For the toolkit: Python 3.10 or newer.

## Building

The game is 32-bit, so the plugin is built for Win32:

```powershell
cmake -S native/di1compat -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
```

The build produces `di1compat.dll` (the plugin) and `octtest.exe` (the offline test driver). The plugin links the CRT
statically, so the game needs no Visual C++ runtime; `octtest.exe` uses the runtime DLL (Visual C++ Redistributable,
x86). Configuring for any architecture other than Win32 stops with an error. Our C++ compiles at `/W4`, as warnings.

## Installing

1. Install Pentane with `python pentane_install.py`. It patches the executable (with a backup), copies Pentane and
   its config, and enables di1compat ([details and Pentane's options](third_party/pentane/README.md)). Then copy
   `di1compat.dll` to `<3.0 install>\Pentane\Plugins\`, or run
   `cmake --install build --config Release --prefix "<3.0 install>"`.
2. Create `di1compat.cfg` next to it, starting from
   [`di1compat.cfg.example`](native/di1compat/di1compat.cfg.example), and set `install10` to your 1.0 installation.
3. Generate the 3.0-side tables next to the DLL:
   - `di1compat_render.txt`: run `python rentable.py`.
   - `di1compat_mer.txt`: run `mertable.py`. Its docstring lists the inputs.

   The shader cache `di1compat_shaders\` is created on first use.
4. Start the game. The plugin writes `di1compat.log` to the game folder. Nothing needs registering: as the zone list
   loads, di1compat adds every 1.0 playset of the 1.0 install to it (`zones10`) and lists them on the Play Sets menu.
   A zone list written earlier by `python zonereg.py` still works (its Incredibles records are kept as they are);
   `python zonereg.py --revert` restores the shipped one.

## Configuration

`di1compat.cfg` holds `key = value` lines. The main keys:

| Key | Default | Meaning |
|---|---|---|
| `install10` | (none) | Path of the 1.0 installation |
| `serve10` | `0` | Serve the files 3.0 lacks from the 1.0 archives. Set to `1` |
| `shaders10` | `0` | Translate the original 1.0 shaders instead of using 3.0 substitutes. Recommended: `1` |
| `trace` | `0` | Log every file open (verbose) and install the playset-flow probes. It also enables the `hkInst` guard against a dead SubNetwork root and the `hide_ps` experiment |

Diagnostics, all off by default and log-only:

| Key | Logs |
|---|---|
| `locktrace10` | Control locks, controller contexts, choreography play and events, messages, mission monitors, load screens, compass, beacons, mission progress |
| `fpslog10` | Frame time once a second, and how much of it the main thread waits on rendering |
| `loadprof10` | Per-file load timings and a sampling profile of the busiest threads (`di1compat_prof.tsv`) |
| `audiotrace10` | Each sound posted and the backend that played it, its position, its mix and its lifetime |
| `breaktrace` | Choreography clocks, clip setup, layout attachments and destructible hits |
| `missiontrace` | Mission context state changes |
| `spawntrace10` | Building lots registered, SpawnBuilding nodes firing, lot spawns, the player start lookups |
| `capsuletrace10` | SpawnExpendable nodes firing, their spawn requests, which branch each 1.0 expendable's spawn took, the async completions, and the actors `shutdown10` keeps drawn |
| `renderdoc10` | Captures one frame with RenderDoc that many seconds into the world |

The full list, including the per-subsystem switches, is the `Cfg` structure at the top of
`native/di1compat/di1compat.cpp`.

## Testing

Game data cannot be published, so the data tests run locally against your own extracts:

- `octtest.exe` runs the plugin's translation code offline. Its modes include:
  - a byte-exact round trip of every tuple file;
  - `--cho-corpus`: every choreography converted and checked;
  - `--render`: the material and geometry translation, compared with `renderoracle.py`;
  - `--fmod`: loading and playing 1.0 sound projects;
  - `--readable`: a self-test of the plugin's pointer checks.
- `python golden.py` is the refactor's safety net ([docs/audit](docs/audit/README.md) X-13). `record <label>` runs
  every deterministic octtest mode on your data (with the environment and argument variants, and with the plugin's
  arena slack through `OCTX_SLACK=4096`) plus three source checks that need no octtest (`static/*`: the logf format
  strings, the hook rows in install order, the config keys with their defaults). It stores exit codes, normalised
  output and every written file with its sha256 under the golden root. `check` re-runs them and exits 1 on any
  difference, with a short diff per case:

  ```powershell
  python golden.py check --golden <golden root> --octtest build\Release\octtest.exe [--quick] [case globs]
  ```

  `--quick` runs the fast subset; `python golden.py list` names the cases. The roots a baseline was recorded with are
  stored with it, so `check` needs no `--ext10`/`--ext30`/`--game`/`--inst10`.
- `python selftest.py` runs the toolkit's self-tests against the configured extracts.
- [CI](.github/workflows/check.yml) checks everything that needs no game data: lint, that every module imports with
  no roots set, that every tool answers `--help`, and that both native targets build (Win32 Release, on a Windows
  runner).

## Python toolkit

### Setup

```powershell
pip install -r requirements.txt
python extract.py "<3.0 install>" <3.0 extract folder>
python extract.py "<1.0 install>" <1.0 extract folder>
```

`lupa` is needed only for Lua packages (`luabc.run_package`) and `zonereg.py`, Pillow only for
`meshexport.py --textures`. `sm3.py`, `gen30.py` (except `--dump`), `shdiff.py` and `rendercmp.py` compile or
disassemble through the system's `d3dcompiler_47.dll`, so they run on Windows only. No tool assumes a
machine. It resolves three roots from the command line, else from the environment (`paths.py`):

| Root | Flag | Environment | What |
|---|---|---|---|
| Game | `--game` | `DI3_GAME` | The 3.0 installation; found through Steam when unset |
| 3.0 extract | `--ext30` | `DI_EXT30` | The folder holding `assets/`, as `extract.py` writes it |
| 1.0 extract | `--ext10` | `DI_EXT10` | The same for 1.0 |

`python paths.py` shows what resolves and from where. Corpus indexes (`matindex.json`, `texindex.json`,
`shippedkeys-*.json` and so on) are built on first use next to the scripts, with paths relative to the extract.
Delete them after extracting a different game build.

### Tools

Unless dated otherwise, each check was run on the stock Steam 3.0 Gold installation.

**Archives and core formats**

| Tool | Purpose | Verified by |
|---|---|---|
| `paths.py` | Root resolution shared by every tool | |
| `extract.py` | Installation to extract tree (3.0 indexed archives, 1.0 plain zips); long-path safe | 4,115 archives extracted |
| `di3arc.py` | 3.0 archive codec: `list`, `unpack`, `pack`; key detected from the header | 21,133 / 21,133 shipped archives re-packed byte-exact |
| `oct.py` | Tuple files (`.oct .mer .merdebug .bent .banm .bct`): read, modify, write | 5,079 / 5,079 tuple files byte-exact |
| `par.py`, `capped.ps1` | Parallel map over all cores; run a command under a process-tree memory cap | |

**Graphs and choreography**

| Tool | Purpose | Verified by |
|---|---|---|
| `mer.py` | Meridian graphs: `show`, `catalogue`, `mtc` (class manifest) | 428,387 parameters and 117,117 links over 584 graphs; 584 / 584 `.mtc` byte-exact |
| `graph.py` | Meridian graph sizing per geometry node | All 1,367 shipped SubNetworks of the generated shape |
| `portmap.py` | 1.0 to 3.0 port numbering for the skeleton templates both games own; writes `games/di1/ports10.hpp` | 105,708 motion-clip ids at the 3.0 twin's position; 5,963 / 5,963 model ports |
| `nodedump.py`, `streamlayout.py` | One node's records; vertex-buffer stream layout | 4,915 / 4,915 multi-primitive nodes stream-major |

**Materials, shaders and geometry**

| Tool | Purpose | Verified by |
|---|---|---|
| `matp.py` | `.mtb` material bundles (both games), from the engine's own parser; `merge()` | 48,057 / 48,057 layouts; bundles rebuilt byte-exact |
| `autobundle.py` | Material resolution: identical 3.0 material, then same-shader substitute, then generic | Survey: 3,748 / 4,424 material slots resolve |
| `vtxf.py` | `vformat.vtxf` parser and vertex conversion between any formats | All 12 vertex-format CRCs used by shipped `.oct` reproduced |
| `skinlayout.py` | Skinned versus rigid vertex layout | 75,895 rigid and 3,396 skinned shipped primitives |
| `shdfmt.py` | Shader passes, input signatures, and what each material texture slot feeds | 834 / 836 shaders name each slot unambiguously |
| `slotcheck.py` | Per-material texture-binder check | Flagged the 122 malformed Metroville slots; 0 after the fix |
| `meshexport.py` | World, exploder or realm geometry to OBJ, optionally with textures | Every primitive within its buffers (57,978); textured exports match the levels |
| `shd.py` | Shader descriptions (`.shd`) of both games, with names resolved | 208 shaders shipped in both games |
| `sm3.py` | 1.0 D3D9 SM3 programs: disassemble, translate to HLSL (the reference for `sm3.hpp`) | 1,773 / 1,773 programs translate and compile |
| `gen30.py` | 1.0 shader to 3.0 `.shd` and HLSL (the reference for `gen30.hpp`); `--dump` writes what `octtest --gen30` compares | 1,245 / 1,248 shaders clean (2026-10-16) |
| `ssf.py` | Scene-settings format (`.ssf`) and a bundle's SSET chunk, both games | |
| `particles.py`, `particles_val30.py` | Particle packages: both layouts and the 1.0 to 3.0 conversion (the reference for `vfx.hpp`); an independent check of a 3.0 package as 3.0's loader reads it | Every 1.0 package converts with 0 problems; every 3.0 package passes |
| `rentable.py`, `mertable.py` | Generate the plugin's 3.0-side tables | |

**Collision, game data and saves**

| Tool | Purpose | Verified by |
|---|---|---|
| `btdconv.py` | 1.0 to 3.0 collision (`.btd` and its `.btm` block table) | 323 / 330 single-block shipped pairs byte-exact |
| `btdblocks.py` | Collision block tiling | Block count = shapes in 267 / 267 worlds |
| `zones.py`, `zonereg.py` | Zone records: inspect, edit; register Incredibles offline (superseded by `zones10`, kept as its reference) | `zones.py` edits match the retired tools on 6 cases; `zones10` writes zonereg's Incredibles records byte for byte |
| `chd.py` | Compiled game-database tables: dump, export, import, compile from Lua | 33 / 33 files round-trip byte-exact |
| `dct.py` | Localisation dictionaries: list, export, import | 471 / 471 files round-trip; 337,023 keys hash correctly |
| `luabc.py` | Lua 5.1 bytecode re-encoding; runs expendables packages | 17 / 17 packages re-encoded |
| `savegame.py` | PC saves: show, export, import, checksums recomputed | 16 / 16 saves decoded and round-tripped |

**Conversion references and comparisons**

di1compat converts at load time. These tools stay because the plugin's code is checked against them, or because they
show how 1.0 and 3.0 files differ. The offline converters that wrote into the installation (`native30.py`, `expl30.py`,
`expack.py`, `variant.py`, `crabemod.py`) were removed; they remain in git history up to commit `684539b`.

| Tool | Purpose |
|---|---|
| `conv.py`, `realm30.py` | Shared conversion primitives; offline realm conversion (the reference for di1compat's `realmFile10`) |
| `diffworld.py`, `foreign.py`, `ranges.py` | Field-by-field file diff; record kinds 3.0 never ships; values above the shipped 3.0 maximum |
| `shdiff.py`, `rendercmp.py`, `mtbdiff.py` | Every shader and material shipped in both games: `gen30`'s translation of the 1.0 copy against the shipped 3.0 copy, field by field; what each pixel program writes; parameter values and texture slots |

**Oracles for octtest**

Each reads the 1.0 files on its own and checks one `octtest` mode's output. They take the 1.0 install first, as
`octtest` does.

| Tool | Checks |
|---|---|
| `fevcheck.py <1.0 install> <output>` | `--fmod`: every registered event is a string of its own `.fev`; where missing names live; the cue list |
| `fevdeps.py <1.0 install> <output>` | `--fevdeps`: each `.fev`'s `Load` list against a byte scan of the file |
| `mixercheck.py <1.0 install> <output>` | `--mixer`: the at-rest mixer level per group, recomputed from `Mixer.lua` |
| `vocheck.py <1.0 install> <output> <out dir> [<3.0 scripts>]` | `--vo`: every 1.0 dialog line and named entry through the transcoded `.dlg30`, and every `NAME` line |
| `renderoracle.py <1.0 world> <out dir>` | `--render`: the Python render pipeline's output for the same world, to diff against |

**Debugging and analysis**

| Tool | Purpose |
|---|---|
| `harness.ps1`, `lua.ps1` | Unattended in-game test: boot, enter a zone, run Lua probes, screenshot, quit (needs CrabeLoader built from tag v1.1.0 with `crabe/crabeloader-no-guards.patch`) |
| `crabe/di3tools/` | CrabeLoader mod: log the player position, dump zone records, force travel, spawn a model |
| `arenas.py`, `mdmp.py` | Render frame-arena sizes and static addresses from a minidump |
| `dispfind.py`, `disx.py` | Find operands or immediates in the executable; disassemble at runtime addresses |

## Repository layout

```
native/di1compat/    Pentane plugin (C++17) and the octtest driver
*.py, *.ps1          Python toolkit and test scripts
FORMATS.md           Format reference
crabe/               CrabeLoader mod and the patch the test harness uses
third_party/pentane/ Pentane plugin loader (Disney Infinity 3.0 build)
.github/workflows/   CI
```

## Third-party code

- [Pentane](https://github.com/high-octane-dev/pentane), Disney Infinity 3.0 build, `third_party/pentane/`
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD 2-Clause), `native/di1compat/minhook/`
- [miniz](https://github.com/richgel999/miniz) (MIT), `native/di1compat/third_party/miniz/`
- [RenderDoc](https://renderdoc.org/) in-application API header (MIT), `native/di1compat/renderdoc_app.h`

## License

This project is released under the [MIT License](LICENSE). The third-party components above keep their own licenses.

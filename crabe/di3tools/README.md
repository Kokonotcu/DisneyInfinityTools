# DI3Tools (CrabeLoader mod)

> **UNTESTED IN-GAME.** Written from CrabeLoader's Lua API source and from CrabeMenu's working code,
> never run. Every call in `main.lua` cites the file:line it comes from. Expect to fix things on the
> first run; the console (`Insert`) and `logs\mods\di3tools.log` show what happened.

A test bench for Disney Infinity 1.0 worlds ported into 3.0 with `native30.py`. It adds a **DI3Tools**
category to the `Crabe.Menu` registry:

| Entry | What it does |
|---|---|
| Log player position | Logs the avatar's position as `x,y,z`, plus a `--start=x,y,z` line ready for `native30.py`, and the camera eye (the free camera while it runs) |
| Dump zone record (current) | Logs the current world and zone names and the effective value of 12 zone record keys |
| Dump zone record: `<TRAVEL_ZONE>` | The same for the configured travel zone, without loading it |
| Travel (force): `<TRAVEL_ZONE>` | Force-loads the zone (default `OUT_Intro2`) |
| Sky > Sky: `<realm>` | Loads that realm as the sky and lighting (default `rlm_in_day`, `rlm_intro_day`), to see whether VFX problems follow the realm |
| Spawn DNA > `<path>` | Creates an actor from that DNA file 2 m in front of and 2 m above the player, to test converted exploders one at a time |

## Install

1. CrabeLoader 1.1.0 installed (`dinput8.dll` and `CrabeLoader.asi` next to `DisneyInfinity3.exe`, and a
   `mods\` folder). The harness uses a build of tag v1.1.0 with `crabe/crabeloader-no-guards.patch`.
2. Copy this folder to `<game>\mods\di3tools\` (`mod.json` and `main.lua`; the README is optional).
3. Install **CrabeMenu** as well (`<game>\mods\crabemenu\`). The loader keeps the `Crabe.Menu` registry,
   but in this CrabeLoader source nothing in its C++ overlay draws it. CrabeMenu does, under
   **F5 > Mods > DI3Tools** (`CrabeMenu/ui/pages/mods.lua`).
4. Without CrabeMenu, the same functions are exported for the `Insert` console (also untested):
   `Crabe.Exports.DI3Tools.position()`, `.dumpZone()`, `.dumpZone("OUT_Intro2")`, `.travel()`,
   `.sky("rlm_in_day")`, `.spawn("exploders/...dnax")`.

`mod.json` uses the same pre-schema (manifest v0) shape as CrabeMenu's, so the loader gives the mod the
id `local.di3tools`. If `<game>\Crabe\crabe.toml` uses a profile that lists mods by id, add that id.

## Configure

Edit the `CONFIG` table at the top of `main.lua`, save, then press **F4** in-game to hot-reload.
The menu entry is replaced by label on reload, so no stale rows are left behind.

```lua
TRAVEL_ZONE = "OUT_Intro2",
SKY_REALMS  = { "rlm_in_day", "rlm_intro_day" },
SPAWN_DNA   = { "exploders/bb/bb05_socket04/bb05_socket04.dnax" },
SPAWN_FORWARD = 2.0, SPAWN_UP = 2.0,
```

## Where the output goes

Each action sets the menu's status line. The full lines go to two places:

- **`<game>\logs\mods\di3tools.log`**, always. This uses `Crabe._fileLog`, written relative to the
  game's working directory (`src/application/lua_natives.cpp:833-854`).
- **`loader.log` and the `Insert` console, only when `logLevel = "debug"`**. `Crabe.write` lines are
  passed to the loader's logger at DEBUG (`src/application/loader.cpp:481`), and the default level is
  `"info"`. Set it in `<game>\Crabe\crabe.toml` under `[general]`.

## Notes per feature

**Player position.** The value comes from `Game.ActorPosition` on the host avatar's handle: the actor's
kinematic position, y up, read as the engine's `KinematicStateGetActualPosition` reads it. It is in the
**world coordinates of the loaded 3.0 zone**. A world ported **without `--translate`** keeps native 1.0
coordinates, so the value can go straight into `native30.py --start`. A world built **with
`--translate`** was shifted by `-(start) - (0, 1, 0)`, so the native value is
`logged + start + (0, 1, 0)`. Use the `--start=x,y,z` form the log prints. With a space,
`--start -87.85,...` is read by argparse as an option and refused, because the value does not look
like a single negative number. Start the free camera from the console with
`Crabe.Camera.StartFreeCam()` to log a position the avatar cannot reach. The camera eye is logged
with it.

**Zone record dump.** Keys are read through `Game.ZoneString` and `Game.ZoneBool`, which wrap the
engine's `UI_GetZoneMgrString` and `UI_GetZoneMgrBool`. Those return the zone manager's effective
values, which resolve `_Inherits_`; CrabeLoader's source does not document this. Compare against
`python zones.py info <name>`. Each key goes through the native of its type in the 3.0 `zonelist.lua`.
The 8 strings are `ZoneType`, `MasterZone`, `PlaysetProperties`, `OverrideInitialCreation`,
`OverrideCreation`, `AlternateStartPointZone`, `StartWorld` and `DefaultProxyAvatar`. The 4 booleans
are `WorldIsIntro`, `_ShowInMenu_`, `availableInConsumerBuild` and `availableInShippingBuild`. The
value an absent key returns has not been checked: strings are printed quoted, so `""` and `nil` can
be told apart. A failing call prints `error: ...` and the dump goes on.

**Travel.** This calls `Game.LoadLevel(zone, true)`, the loader's force path, which skips
`UI_CanTransitionToLevel` and releases the free camera first. As in CrabeMenu, a zone the game refuses
loads only on a **second press within 5 s**, because a forced transition can leave the session stuck
on a loading screen. Only one transition can start every 5 s. Close CrabeMenu (F5) once the load
starts: while it is open it keeps the keys and the pad away from the game.

**Sky.** This calls `Game.LoadSkyDome(realm)`, the engine's `RealmManager_LoadSkyDomeInZone`. The
realm must be one the engine can load, so `rlm_in_day` (1.0's realm) has to be ported and installed
first (`realm30.py`). The sky stays until the world reloads, and it is refused in the front end.

**Spawn.** This calls `Game.CreateActor("DNAFile=<path>", x, y, z, heading)`, the engine's
`CActorCreator::CreateActor`, which works outside the Toy Box. An entry that already contains `=` is
passed as a full parameter string. **The path must be one the game can open.** It is relative to the
game's `assets\` root, like the loader's own example `DNAFile=characters/X.dnax`. The `.dnax` and
everything it references must be where the game's file system finds them: installed under
`<game>\assets\` (where `native30.py --assets` writes by default) or provided as a CrabeLoader VFS
override. A path the game cannot open builds nothing. `loader.log` then shows
`EngineActors: the engine built no actor for '...'` at warning level, or `CreateActor faulted` if
the engine crashed inside. "Forward" is the camera-to-player direction, as in
`Game.SpawnActorNearPlayer`, and the actor faces the player. Assets load on the spot, so expect a
hitch.

## Not included

**Logging every Lua chunk name** is left out, because the Lua API does not expose it:
- `Crabe.Events` has no chunk event. The only events emitted are `init`, `tick`, `update`, `keyDown`,
  `reload`, `playerDeath`, `portalFigurePlaced`, `portalFigureRemoved` and `gatewayTagWrite`.
- `Crabe.Hooks.patchChunk` and `patchNamedChunk` need a non-empty match key. An empty key is dropped
  (`src/application/loader_content.cpp:31`, `:48`), so there is no catch-all. A patch runs fixed
  source and is not given the chunk's name.

The loader logs every chunk itself, though: `Lua: loadbuffer <name> (<n> bytes)` and
`Lua: loadfile <name>` at DEBUG (`src/infrastructure/lua_call.cpp:132`, `:124`). Set
`logLevel = "debug"` in `crabe.toml` and read `loader.log`.

Also not used: `Crabe.Menu.registerCategory`, `addItem`, `addToggle`, `addSlider`, `setStatus`,
`Crabe.Sandbox.import` and `Crabe.log`. They appear in `docs/crabe_api.def.lua` (and the
`mods/_template`), but `src/api` does not implement them. The menu uses `Crabe.Menu.register`
(`src/api/15_menu.lua:31`) instead.

## Crash bisection: do it WITHOUT CrabeLoader

Stock CrabeLoader always installs engine hooks that change how bad assets fail. No setting in
`crabe.toml` turns them off (`src/application/loader.cpp:658-659`):
- `:659` `EngineShaders::initialize()` hooks **`Shader_LookupCrc`**, which swaps in a fallback shader
  for an unknown CRC instead of the engine's `int 3`, and **`Shader_ParseBindings`**, a material
  binding guard (`src/infrastructure/engine_shaders.cpp:92-110`).
- `:658` `EngineMeridian::initialize()` guards `DynArray16::Append` against bad graph buffers.

A ported world that loads with stock CrabeLoader can therefore still crash vanilla 3.0, and a crash
seen with CrabeLoader may differ from the vanilla one. `crabe/crabeloader-no-guards.patch` turns both
guards off. 1.1.0 also serves a mod's `.tbody` textures over archive members (`EngineAssetLoader`); a
`crabe_disable_asset_override.txt` in the game folder turns that off
(`src/infrastructure/engine_asset_loader.cpp:30`, `:140-144`). To bisect crashes, uninstall
CrabeLoader: delete `dinput8.dll` and `CrabeLoader.asi` (the install changes no game file). Do not
use Steam's *Verify integrity* for this, because it also reverts the installed port.

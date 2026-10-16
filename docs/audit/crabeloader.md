# CrabeLoader: what changed, and what it means for this repository

> 2026-10-16: the owner moved to CrabeLoader v1.1.0 (tag `96f866b`) with the no-guards patch, now `crabe/crabeloader-no-guards.patch`.

Scope: CrabeLoader upstream ([LucasLhomme/CrabeLoader](https://github.com/LucasLhomme/CrabeLoader)) against every
place this repository depends on it:
- `native/di1compat` comments and design,
- `crabe/crabeloader-no-guards.patch`,
- `harness.ps1`, `lua.ps1`,
- `crabe/di3tools/`,
- `crabemod.py`,
- FORMATS.md.

The clone was read only; nothing was built or run from it.

## What "changed" means

| Ref | Commit | Date | Version |
|---|---|---|---|
| `main` | `2928d52` | 2026-10-04 | tag v1.0.0, unchanged since |
| `origin/dev` | `a780daa` | **pushed 2026-10-16** | VERSION `1.1.0`, CHANGELOG "[1.1.0] - 2026-10-16"; 79 commits ahead of main; no v1.1.0 tag yet |

This repository's patch targets v1.0.0. `crabe/crabeloader-no-guards.patch:3` has `index 479288b`, main's
`src/application/loader.cpp` blob. Everything below compares main with dev.

Headline changes in 1.1.0, from its CHANGELOG:
- It now ships as **`CrabeLoader.asi` plus a `dinput8.dll` bootstrap**, instead of a `bink2w32.dll` proxy. The .asi
  still exports the Bink functions, so it can still be deployed as `bink2w32.dll`.
- **New engine hooks:**
  - `EngineAssetLoader`: loose `.tbody` texture overrides at `Asset_LoadFile`.
  - `EngineGateway`: figure/portal support.
  - `EngineResolution`.
  - `DirectInputHook`.
  - An `.asi` plugin loader that also runs top-level `.asi` files in enabled mods.
- Vendored MinHook, ImGui and JSON were replaced by CMake FetchContent (MinHook v1.3.3).

## Does di1compat itself depend on CrabeLoader?

No. di1compat does not link, call or require CrabeLoader. It only **works around** CrabeLoader's crash reporter. That
reporter has not changed:
- It registers a vectored handler at priority 1 (dev `crash_reporter.cpp:1112`).
- It reports every exception code >= 0x80000000 except C++ and CLR exceptions (`:53-66`).
- It then returns `EXCEPTION_CONTINUE_SEARCH` (`:1059-1071`).

So it fires **before** any `__try/__except` in di1compat. Each report writes a `.txt` and a `.dmp`. Reports are capped at
3 per process (`crash_reporter.hpp:84`, `kMaxReports = 3`, checked), and an exact repeat of the last one is skipped.

Consequences:
1. **Keep the "check readability instead of faulting" design** (`sys::readable` before reading engine memory;
   di1compat.cpp:89-90, :1514, :2236-2237). It is still required.
2. **Handled AVs are not free.** Each one that still happens uses up one of the 3 report slots. After 3 of them, a real
   crash later in the session produces **no** `Crabe\crash-*` report, and harness.ps1:135 looks for exactly those reports.
   The reviewer listed the `__try` blocks that still read unchecked memory: sx.hpp:131, sx.hpp:690, di1compat.cpp:3167,
   :1261, :2056. The per-area sections verify them.

## Assumptions, one by one

| # | Assumption (where) | Status on 1.1.0 (dev) | Action |
|---|---|---|---|
| 1 | `crabe/crabeloader-no-guards.patch` applies | **Broken.** `git apply --check` fails at `loader.cpp:635` (re-checked). New lines in the hunk's context: `DirectInputHook` (dev loader.cpp:650), `EngineGateway` (:653). It still applies to v1.0.0 | Regenerate the hunk (below). It was checked with `git apply --check` against dev |
| 2 | Meridian and Shaders are the only guards that hide engine faults | **Changed.** `EngineAssetLoader` (dev loader.cpp:658) now serves loose `.tbody` files at `Asset_LoadFile` (0x0041FAF0) **without calling the original** (dev `engine_asset_loader.cpp:225-265`). That is a "loads with CrabeLoader but not in vanilla" difference like the two the patch removes | For vanilla-equivalent testing, create `crabe_disable_asset_override.txt` in the game folder (dev `engine_asset_loader.cpp:30, :140-144`; no rebuild), or comment out the line in the patch |
| 3 | First-chance AVs produce a dump even when handled | **Still true** (above) | Keep the design; note the 3-report cap |
| 4 | Remote command file `<game>\crabe_remote_cmd.txt`, polled 4x/s, an empty file deleted unrun (harness.ps1:22,31; lua.ps1:6,8) | **Valid** (dev loader.cpp:249-281: 250 ms interval, read, remove, skip if blank) | None |
| 5 | `loader.log` result lines match `^\[\d{4}-[^\]]*\] \[\w+\] [=!] ` (harness.ps1:39, lua.ps1:16) | **Valid** (dev logger.cpp:49, :86-100; results `info("= {}")` / `error("! {}")` at loader.cpp:386-388; default level "info") | None. A `logLevel` above info in `crabe.toml` hides the `= ...` lines, as before |
| 6 | `Embedded API loaded` marks boot (harness.ps1:111) | **Valid** (dev lua_runtime.cpp:106) | None |
| 7 | Crash reports in `<game>\Crabe\crash-*` (harness.ps1:135-136) | **Valid** (dev main.cpp:146; names `crash-YYYYMMDD-HHMMSS[-N].txt/.dmp`) | None, but see the 3-report cap |
| 8 | Remote commands run only while the game calls Lua (harness.ps1:6) | **Valid** (dev lua_call.cpp:148-155) | None |
| 9 | "Needs CrabeLoader (bink2w32.dll proxy ...)" (harness.ps1:4; crabe/di3tools/README.md install steps) | **Changed**: the intended install is now `dinput8.dll` + `CrabeLoader.asi` | Update both texts once you pick an install mode |
| 10 | Lua API used by harness.ps1 and `crabe/di3tools/main.lua` | **All present, same signatures** (diffs of 10_game, 14_world, 15_menu, 26_camera, 03_lifecycle are comment-only) | Update the file:line citations (list below) |
| 11 | crabemod.py VFS rules: id format, '.'/'_' folder skip, `assets/` drop, later wins | **Valid** (dev vfs_override_manager.cpp:52-57, :113-128, :155-162; mod_manifest.cpp:33) | None. Dev also skips `*.asi` in the VFS |
| 12 | crabemod.py:9 "needs no entry script (ModManager.cpp only runs one that exists)" | **Wrong in both versions.** `ModManager.cpp` is not compiled; the build uses `mod_manager.cpp`, where an asset-only mod logs `ERROR ... ran nothing` (dev mod_manager.cpp:141-149). The files are still served, so the effect is cosmetic | Moot if crabemod.py is removed (Python section); otherwise fix the docstring or write a stub `main.lua` |
| 13 | "Cannot change a member INSIDE an archive" (crabemod.py:9-12, FORMATS.md:76) | **Changed for `.tbody`**: EngineAssetLoader serves loose `.tbody` over archive members | Update FORMATS.md:76 |

## Regenerated patch hunk for 1.1.0

The full patch is [patches/crabeloader-1.1.0-no-guards.patch](patches/crabeloader-1.1.0-no-guards.patch). It was
checked with `git apply --check` against dev `a780daa`. The hunk:

```diff
@@ -651,8 +651,10 @@ bool Loader::initialize()
     crabe::infrastructure::MessageHook::get().initialize();
     crabe::infrastructure::EngineFreeCamera::get().initialize();
     crabe::infrastructure::EngineGateway::get().initialize();
-    crabe::infrastructure::EngineMeridian::get().initialize();
-    crabe::infrastructure::EngineShaders::get().initialize();
+    // DI3Tools build: the DynArray sanitizer and the fallback shader are off, so a world that loads here
+    // also loads in vanilla 3.0 (they hid int3 on unknown shaders and corrupt Meridian arrays).
+    // crabe::infrastructure::EngineMeridian::get().initialize();
+    // crabe::infrastructure::EngineShaders::get().initialize();
     crabe::infrastructure::VfsOverrideManager::get().initialize(std::filesystem::current_path());
     crabe::infrastructure::VfsHook::get().initialize();
     crabe::infrastructure::EngineAssetLoader::get().initialize();
```

Keep the current file for v1.0.0. Add this as a second patch, or replace the current one, once you move to 1.1.0.

Things the patch cannot reach, which also differ from vanilla on 1.1.0:
- **EngineResolution** is installed from DllMain (dev main.cpp:144), not from `Loader::initialize`. It hooks
  `0x004459B0`, `0x00A5A010` and `0x007F3C20`. It only widens accepted display modes and recentres the UI; it is not
  relevant to loading.
- **Embedded Lua that always runs:**
  - `src/api/29_multiplayer.lua:12-67` replaces game globals (`IsOnline`, `IsSignedIntoDisneyID`, `NETZ = true`, ...)
    even with multiplayer disabled, because `tools/embed_api.py:24` embeds every `src/api/*.lua`.
  - `12_virtualreader.lua:314-427` patches `VirtualReaderPC_Loadout.HandleCloseButton`, `ScreenDefinition.doLuaInput`
    and `PlaysetListWin32.GoBack`. These are screens that harness.ps1:98-101 drives.
  - Effect on the harness boot flow: **unverified**. Run the harness once on 1.1.0 before relying on it.

## Hook overlap with di1compat

di1compat installs 149 `hook()` calls on 147 exe addresses, all through MinHook, with no IAT, vtable or byte patches.
For every CrabeLoader hook target, the reviewer checked whether di1compat hooks the same bytes. Stolen-byte lengths were
measured on this machine's exe, which matches CrabeLoader's profile (TimeDateStamp 0x5851C5D1).

| CrabeLoader target | Since | di1compat hook at the same function? |
|---|---|---|
| `0x0041FAF0` Asset_LoadFile (EngineAssetLoader) | 1.1.0 | No. di1compat hooks AssetOpen `0x0041CB10`, `0x0041FC10` (loadprof10) and `0x0041FEF0`; none overlap |
| `0x0042A2A0..0x0042A5B0` Gateway::Write* (6 hooks) | 1.1.0 | No |
| `0x004459B0`, `0x00A5A010`, `0x007F3C20` (EngineResolution) | 1.1.0 | No |
| `0x004509D0` Shader_LookupCrc, `0x0045D5C0` Shader_ParseBindings | 1.0.0 (patched out) | No. di1compat's are `0x0045C5C0`, `0x0045C820`, `0x00472050`; `0x0045D5C0` and `0x0045C5C0` are different functions |
| `0x006A9E90` Meridian DynArrayAppend | 1.0.0 (patched out) | No |
| `0x006A45C0` CreateFreeCameras | 1.0.0 | No |
| `0x0130DF60`, `0x0130EBF0`, `0x0130EDE0` lua_pcall / luaL_loadfile / luaL_loadbuffer | 1.0.0 | No. di1compat calls Lua functions but hooks none |
| **`0x0068BBB0` message dispatcher (MessageHook)** | **1.0.0** | **Yes**: di1compat's "event post (trace)" (di1compat.cpp:4218, prologue `56578BF98B07`) |
| Exe entry point `0x012B9B2A` (dinput8 bootstrap, restored before load) | 1.1.0 | No |
| Multiplayer byte patches | 1.1.0, only with `[multiplayer].enabled = true` (default false) | No |

Outside the exe, CrabeLoader hooks kernel32 file APIs, XInput, DXGI, user32, the d3d11 export, dinput8 and WinHTTP.
di1compat hooks none of these.

**About the one shared target, `0x0068BBB0`.** CrabeLoader finds it by pattern, not by a fixed address
(`message_hook.cpp`). The resolver takes the 4th call inside the native that pushes `"System_StartButtonPushed"`. The
reviewer reproduced it against the exe and got `0x0068BBB0`.
- di1compat installs its hook only with `trace = 1`. That is the code default, but `di1compat.cfg.example` sets
  `trace = 0`, so in the recommended play config there is no overlap.
- If di1compat hooks first, which is the expected order (below): CrabeLoader's MinHook copies di1compat's jump
  into its trampoline, and both detours run.
- If CrabeLoader hooks first: di1compat refuses its hook ("prologue differs"), and only that trace is lost.
- Either way it is harmless. Document it.

**Load order.**
- Pentane loads di1compat, and `Pentane_Main` installs every hook before the exe's entry point runs. The repository
  states that this happens inside Pentane's DllMain, under the loader lock (audio.hpp:2138-2140); not re-checked by
  disassembly.
- CrabeLoader 1.1.0 installs most of its hooks from a detached thread (dev main.cpp:155 → loader.cpp:645-659). That
  thread cannot start before the loader lock is released.
- The few hooks CrabeLoader installs from DllMain touch no di1compat address.
- So the two never patch at the same moment.

## A latent crash this review exposed (not caused by CrabeLoader)

`hook()` leaves the original pointer **null** when it refuses a hook. That happens when the prologue differs, for
example because another loader patched the function first. Three call sites call the AssetOpen original `g_open`
**outside** the detour without checking it:
- `di1compat.cpp:506` (`read30`)
- `di1compat.cpp:3304` (the expendables-package hook)
- `npcrt.hpp:267`

All three were checked. CrabeLoader does not hook `0x0041CB10` today, but any tool that ever does would turn this into
a null call. The fix-pass entry is in the cross-cutting section (X-11).

## Stale citations in `crabe/di3tools/` (they were correct for v1.0.0)

| Where | Now (dev) |
|---|---|
| main.lua:36 | 00_core.lua:23 (was :25) |
| main.lua:37, README:52 | loader.cpp:480 (was :463) |
| main.lua:38, README:50 | lua_natives.cpp:786-807, :1121 (was :680-701, :765) |
| main.lua:61 | 14_world.lua:112 (was :154) |
| main.lua:65 | 26_camera.lua:178-179 (was :246-247) |
| main.lua:67 | 11_avatar.lua:43 (was :61) |
| main.lua:69 | 14_world.lua:35 (was :56) |
| main.lua:79 | 14_world.lua:128 (was :172) |
| main.lua:82, :146 | 26_camera.lua:164 (was :230) |
| main.lua:96 / :103 / :115 / :123 / :131 / :139 / :154 | 14_world.lua :132-138 / :141, :133 / :184 / :200 / :117 / :82-109 / :46 |
| main.lua:172 | 15_menu.lua:64-65, :31-44, :82-91 (the comment at :20-21 was removed) |
| main.lua:193 | 01_sandbox.lua:125-131 |
| main.lua:198 | 03_lifecycle.lua:56 |
| main.lua:12, :153 | the 14_world.lua:70-72 DNAFile comment was removed; the example now exists only at lua_natives.cpp:252 |
| README:106 | loader_content.cpp:31, :48 |
| README:110 | lua_call.cpp:132, :124 |
| README:116 | 15_menu.lua:31 |
| README:121-125 | loader.cpp:654-655 |
| README:124 | engine_shaders.cpp:92-110 |

The README's "not implemented" list (`Crabe.Menu.registerCategory`, `addItem`, `addToggle`, `addSlider`, `setStatus`,
`Crabe.Sandbox.import`, `Crabe.log`) is still not implemented in 1.1.0. Nothing in C++ draws `Crabe.Menu` yet.
`mod.json` (manifest v0, synthesised id `local.di3tools`) is still valid.

## Ideas worth borrowing for the di1compat refactor

- **A hook table instead of 149 hand-written `hook(...)` lines.** A static array of
  `{name, staticVa, prologueHex, detour, &original, gate}` fed to the **existing** `hook()` helper (di1compat.cpp:110-128),
  like CrabeLoader's `targets[]` loop (dev `vfs_hook.cpp:169-199`). Keep the helper; don't write a new mechanism. Keep
  every name string and the install order, because both appear in the log.
- **Name the hook in crash logs.** CrabeLoader names the hook that covers a faulting address (`hook_registry.cpp:299-358`,
  `crash_reporter.cpp:897-910`). With the table, `avHandler` could log "in hook X" the same way. This adds new log text,
  so it is a deliberate change.
- **Not worth copying:** owner tracking and RAII removal (di1compat never removes hooks), a self-collision check
  (di1compat's hooks don't overlap each other), and HDE stolen-byte measuring (the analysed prologues already cover it).

## Still unverified

- When Pentane loads its plugins (inside its DllMain or not), and when it LoadLibrary's a renamed `bink2w32.dll`.
- Whether the queued `Asset_LoadFile` request later reaches `0x0041CB10` through an indirect call.
  - If it does, a `.tbody` served by CrabeLoader wins over anything di1compat serves for that path.
  - Seven levels of direct calls were searched; none found.
- The in-game effect of 1.1.0's always-on Lua overrides on harness.ps1's boot flow.
- Which CrabeLoader build is installed on the development machine. harness.ps1 and lua.ps1 default to `D:\SteamLibrary\...`,
  which does not exist on this machine. The 3.0 install here is vanilla.

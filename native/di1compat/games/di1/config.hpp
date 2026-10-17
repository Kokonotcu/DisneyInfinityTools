// The plugin's config keys, defaults and fields; stable names (B1). Windows, core config and log. Read once (S4).
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include "../../core/config.hpp"
#include "../../core/log.hpp"

namespace di1 {

struct Config {
    char install10[MAX_PATH] = "";   // the 1.0 install (its assets\ holds plain zips)
    bool serve10 = false;            // serve paths 3.0 lacks from the 1.0 zips
    bool trace = false;              // diagnostic: log every open, 26 probe hooks (incl. hkInst, DR0 watch)
    bool missionTrace = false;       // log 1.0 playset mission context state changes (one cheap hook)
    bool teardownTrace = false;      // probe: Layout dtor placements, node dtor + hierarchy entry release inside it
    bool breakTrace = false;         // probe: clip evaluation clocks, exploder hits and explodes (log only)
    bool shaders10 = false;          // real 1.0 shaders: translated at load (gen30.hpp) instead of 3.0 substitutes
    bool translate10 = true;         // shaders10's materials (0: its engine-side changes only, for bisecting)
    bool lazyLink10 = true;          // shaders10: 3.0's shaders link on first use too (0: the boot linker runs)
    bool skinLayout10 = true;        // skinned nodes' converted primitives in their 3.0 'skin' layout, bones kept
    bool skinTr10 = true;            // skinned materials take their translated 1.0 shader too (identity bones; gen30)
    bool sceneSettings10 = true;     // a 1.0 bundle's scene settings (SSET: fog, bloom, grading) re-laid into 3.0's
    bool magenta10 = false;          // test: every translated pixel shader draws solid magenta
    int bufview10 = 0;               // test: translated final passes show 1 light buffer (t3), 2 view depth (t0), 3 t4
    int renderdoc10 = 0;             // >0: load RenderDoc (x86) and capture one frame this many seconds into the world
    bool opacityLog10 = false;       // test: log primitives the opacity test drops from the opaque list
    int fadeProbe10 = 0;             // test: 1 log faded objects' fade inputs (FUN_0044b8f0); 2 also draw them unfaded
    bool fpsLog10 = false;           // test: each second, frame time and the main thread's wait on rendering
    bool loadProf10 = false;         // diagnostic: per-open timings + a sampling profile of the busiest threads
    char memtest[600] = "";          // comma list of path substrings served from a memory copy
    bool hidePs = false;             // experiment: actor registry denies <name>_PS, so avatars fall back
    char block10[600] = "";          // experiment: comma list of path prefixes never served from 1.0
    bool events10 = true;            // mission presentation Event ints: 1.0 enum -> 3.0 (mission.hpp; .lua and .mer)
    bool progress10 = true;          // mission .lua: 1.0's generated objective line -> ProgressString "<default>"
    bool aliases10 = true;           // register dropped 1.0 Meridian classes with their 3.0 template (mission.hpp)
    bool lockTrace10 = false;        // diagnostic: log control locks, contexts, choreography play/end, messages
    bool portRefs10 = true;          // .mer: owner fields naming port sub-nodes stay plain ints (mer::Rules::portRefs)
    bool ports10 = true;             // .mer: port names from owner fields, DataFetch constants under them
    bool pins10 = true;              // .mer: complete pin tables on every instance (mer::Rules::pins10)
    bool gate10 = true;              // .mer: MasterNetSync Link_Out chains fire locally as in 1.0 (mer::Rules::gate10)
    bool toggleType10 = true;        // .mer: Collision/VisibilityToggle keep their 1.0 mode (mer::Rules::toggleType10)
    bool beacon10 = true;            // zonelist.lua: 1.0 BeaconMap rows in 3.0's row form (mission::beaconMap30)
    bool behav10 = true;            // mission lines: MissionBehavior 2 -> 3 story / 4 side (mission.hpp behaviour30)
    bool native10 = true;            // native Meridian classes (mernode.hpp): registration after the class registry
    bool sx10 = true;                // 1.0 Slang scripts served in the 3.0 container (slang.hpp SX-1; 0: raw, rejected)
    bool sxNatives10 = true;         // sx10's imports bound to 1.0-semantics natives by a linker hook (SX-3/4)
    bool sxPorts10 = true;           // sxNatives10 with the ported natives (0: stack-balanced stubs only)
    bool sxBeh10 = true;             // sxPorts10 + 1.0 behavior-type numbers into 1.0 scripts' SetInitialParams
    bool bmx10 = true;               // 1.0 behavior maps in 3.0's form (aitables.hpp BMX-1; 0: raw, 3.0 drops them)
    bool btx10 = true;               // 1.0 behavior trees in 3.0's form (aitables.hpp BTX-1; 0: raw, 3.0 drops them)
    bool root10 = true;              // sys: read10 also searches the 1.0 root archives (startup*.zip)
    bool locks10 = true;             // sys: 1.0 lock records the Locks DB lacks, after the playset locks (PS-5)
    bool popin10 = true;             // sys: 1.0 expendable packages' entries pop in as in 1.0 (PS-8)
    bool chest10 = true;             // sys: 1.0 ChestInfo.bin through 3.0's chestInfo loader (GLB-1)
    bool bits10 = true;              // sys: the 1.0 core bitmasks after the playset's (PS-5)
    bool econ10 = true;              // sys: .ap SpawnEconomyOnDeath read as in 1.0 (NPC-8)
    bool perc10 = true;              // sys: the 1.0 perceptions 3.0 lacks through 3.0's perceptions loader (GLB-1)
    bool deliv10 = true;             // sys: 1.0 RewardType Delivery registered with 3.0's Multi reward (SYS-12)
    bool navmesh10 = true;           // 1.0 navmeshes in the 3.0 layout (navmesh.hpp, OCT-8); 0: NavMeshPool dropped
    bool gamedb30 = true;            // the 3.0 GameDB tables a 1.0 DataMap cannot name (npcrt.hpp; the E-on-NPC crash)
    bool playset30 = true;           // gamedb30 on: the 1.0 playset's Playset record (3.0 includes) as a DataMap
    bool hudMarker10 = true;         // playset30 on: + Include.hudmarker_inc, 3.0's mission compass (supplement.hpp)
    bool rewards10 = true;           // 1.0 expendable groups in 3.0's keys + the 1.0 core packages (groups.hpp)
    bool currency10 = true;          // a 1.0 playset's currency is its DefaultRewardType (gameplay.hpp)
    bool skin10 = true;              // 1.0 InfluenceRemap palettes applied to skinned vertices (skin.hpp, REN-3)
    bool psdna30 = true;             // a 1.0 avatar <x>_ps.dnax 3.0 lacks: 3.0's <x>.dnax naming the _PS model
    bool avperc10 = true;            // gamedb30 on: the 1.0 avatar's perception policy in a 1.0 playset (npcrt.hpp)
    bool spawner10 = true;           // 1.0 SpawnerQuarterback groups: 3.0's QB factory lacks type 10 (npcrt.hpp)
    bool choTime10 = true;           // cho.hpp: SubNetwork MinTime/MaxTime (choreography play range)
    bool choAnim10 = true;           // cho.hpp: Float 'Global.Time' clip/event input -> AnimState 'AnimState'
    bool choScene10 = true;          // cho.hpp: SceneTreeNodeRef on PlacementNode Associations
    bool variant10 = true;           // octx.hpp: VariantGroup for the variant processors skin30 / locator30 add
    bool choConst10 = true;          // choconst.hpp: choreography Static nodes stay Static under constant headers
    bool camWorld10 = true;          // octx.hpp: a camera's world-basis twin (Transform/In WorldBasis) as 3.0 ships it
    bool realmFile10 = true;         // octx.hpp: a 1.0 realm file's RealmDataPool as 3.0's RealmData node
    bool choLayout10 = true;         // cholayout.hpp: ContextNodes -> InstanceNodes (actors and clips instanced)
    bool clip10 = true;              // cho.hpp: ClipDataBlock output tables in the 3.0 layout (scene clips)
    bool clip10Motions = true;       // cho.hpp: the same for motion clips on templates 3.0 lacks (ids kept)
    bool clip10Ids = true;           // cho.hpp: 3.0 port ids on motion clips of templates 3.0 owns; 1.0 model links
    bool choAlias10 = true;          // cho.hpp: Cho DataMap paths outside choreographies/<n>/<n> (vault bolts, door)
    bool banm10 = true;              // cho.hpp: .banm 1.0f -> 1.0g (version gate, full track paths)
    bool loadScreen10 = true;        // ui: 1.0's loading-screen choice for zones with 1.0's LoadingScreen key
    bool uiScope10 = true;           // ui: a flash image is served from 1.0 only to a movie served from 1.0
    bool fmv10 = true;               // ui: FMVs 3.0 lacks play from the 1.0 install's fmv\PC1
    bool text10 = true;              // ui: a 1.0 playset table also carries 1.0 lang/main keys no 3.0 table has
    bool tips10 = true;              // ui: 3.0's data/tips.lua + the 1.0 tip records it lacks
    bool resident10 = true;          // the 1.0 startup*.zip members are served by their full paths (resident in 1.0)
    bool tutorial10 = true;          // ui: tutorial element 61 (1.0's SelectMenu ribbon) is answered by 3.0's ribbon
    bool context10 = true;           // ui: a 1.0 controller-context list keeps 3.0's extra gates of its actions
    bool audio10 = true;             // 1.0 FMOD runtime hosts the 1.0 sound data Wwise lacks (audio.hpp)
    bool music10 = true;             // audio10: 1.0 Music_<X>.fev through 3.0's MusicStack
    bool vo10 = true;                // audio10: 1.0 dialog banks (.fsb/.name) and .dlg scripts in 3.0's dialog system
    bool audioTrace10 = false;       // audio10: log each posted sound name once with the backend that took it
    bool mixer10 = true;             // audio10: 1.0's Mixer presets as the 'Mixer' volume source of the 1.0 categories
    bool cineSync10 = true;          // audio10: an audio-synced choreography starts the 1.0 sound event of its name
    bool vfx10 = true;               // 1.0 particle packages and .p3d meshes in 3.0 form (vfx.hpp)
    bool vfxUniversal10 = true;      // 1.0's universal particle package beside 3.0's (vfxrt.hpp; needs vfx10)
    bool vfxChan10 = true;           // emitter curve channels re-laid for the shader of their material (chan.hpp)
    bool vfxTrace = false;           // diagnostic: log effect/emitter spawns with their placement (vfxrt.hpp)
    bool curves10 = true;            // gp: ledge/rail curves bound in 3.0 form (gameplay::curves30)
    bool surfaces10 = true;          // gp: collision shape SurfaceType mapped by name onto the 3.0 table (surfaces30)
    bool assetBufs10 = true;         // gp+npc: read10 finds <stem>_<n>.* members in <dir>/<stem>.zip
    bool challenge10 = true;         // gp: 1.0 activities into 3.0's Challenge system (gameplay.hpp)
    bool selectMenu10 = true;        // gp: Tab opens 1.0's SelectMenu (Mission Log / Toy Store) in 1.0 zones (gprt.hpp)
    bool actionMap10 = true;         // ui: a 1.0 movie's UsesActionMap sets its screen's action map (gprt.hpp)
    bool focus10 = true;             // ui: a 3.0 Screen on a 1.0 movie gets 3.0's non-FSM focus (gprt.hpp)
    bool keys10 = true;              // ui: a 1.0 movie's 3.0 script: mapped actions reach the movie as keys (gprt.hpp)
    bool accept10 = true;            // gp: a 1.0 mission activated in a 1.0 zone posts 'mission accepted' (gprt.hpp)
    bool tools10 = true;             // gp: in a 1.0 zone a tool 3.0 refuses is judged by its 1.0 list (gprt.hpp)
    bool playsetMenu10 = true;       // menu: a Play Sets entry per registered 1.0 playset (ui::playsetMenu10)
    bool zones10 = true;             // zonelist.lua: every 1.0 playset registered as it loads (gameplay::playsets10)
    bool building10 = true;          // a 1.0 SpawnBuilding node builds its building on the lot as in 1.0 (bldrt.hpp)
    bool spawnTrace10 = false;       // probe: lots, SpawnBuilding firing, lot spawns, player start lookups (log only)
    bool capsuleTrace10 = false;     // probe: 1.0 expendable spawns, branches, completions (caprt.hpp, log only)
    bool shutdown10 = true;          // a 1.0 expendable's actor stays drawn while shut down (caprt.hpp; needs popin10)
};

// Every key as the cfg file spells it. Not derivable from the field names (hide_ps, vfx_universal10, vfx_trace).
inline constexpr core::ConfigKey kConfigKeys[] = {
    {"install10", core::ConfigKind::Text, offsetof(Config, install10), sizeof(Config::install10)},
    {"serve10", core::ConfigKind::Flag, offsetof(Config, serve10)},
    {"trace", core::ConfigKind::Flag, offsetof(Config, trace)},
    {"missiontrace", core::ConfigKind::Flag, offsetof(Config, missionTrace)},
    {"teardowntrace", core::ConfigKind::Flag, offsetof(Config, teardownTrace)},
    {"breaktrace", core::ConfigKind::Flag, offsetof(Config, breakTrace)},
    {"shaders10", core::ConfigKind::Flag, offsetof(Config, shaders10)},
    {"translate10", core::ConfigKind::Flag, offsetof(Config, translate10)},
    {"lazylink10", core::ConfigKind::Flag, offsetof(Config, lazyLink10)},
    {"magenta10", core::ConfigKind::Flag, offsetof(Config, magenta10)},
    {"skinlayout10", core::ConfigKind::Flag, offsetof(Config, skinLayout10)},
    {"skintr10", core::ConfigKind::Flag, offsetof(Config, skinTr10)},
    {"scenesettings10", core::ConfigKind::Flag, offsetof(Config, sceneSettings10)},
    {"bufview10", core::ConfigKind::Int, offsetof(Config, bufview10)},
    {"renderdoc10", core::ConfigKind::Int, offsetof(Config, renderdoc10)},
    {"opacitylog10", core::ConfigKind::Flag, offsetof(Config, opacityLog10)},
    {"fadeprobe10", core::ConfigKind::Int, offsetof(Config, fadeProbe10)},
    {"fpslog10", core::ConfigKind::Flag, offsetof(Config, fpsLog10)},
    {"loadprof10", core::ConfigKind::Flag, offsetof(Config, loadProf10)},
    {"memtest", core::ConfigKind::Text, offsetof(Config, memtest), sizeof(Config::memtest)},
    {"hide_ps", core::ConfigKind::Flag, offsetof(Config, hidePs)},
    {"block10", core::ConfigKind::Text, offsetof(Config, block10), sizeof(Config::block10)},
    {"events10", core::ConfigKind::Flag, offsetof(Config, events10)},
    {"progress10", core::ConfigKind::Flag, offsetof(Config, progress10)},
    {"aliases10", core::ConfigKind::Flag, offsetof(Config, aliases10)},
    {"locktrace10", core::ConfigKind::Flag, offsetof(Config, lockTrace10)},
    {"portrefs10", core::ConfigKind::Flag, offsetof(Config, portRefs10)},
    {"ports10", core::ConfigKind::Flag, offsetof(Config, ports10)},
    {"pins10", core::ConfigKind::Flag, offsetof(Config, pins10)},
    {"gate10", core::ConfigKind::Flag, offsetof(Config, gate10)},
    {"toggletype10", core::ConfigKind::Flag, offsetof(Config, toggleType10)},
    {"beacon10", core::ConfigKind::Flag, offsetof(Config, beacon10)},
    {"behav10", core::ConfigKind::Flag, offsetof(Config, behav10)},
    {"native10", core::ConfigKind::Flag, offsetof(Config, native10)},
    {"sx10", core::ConfigKind::Flag, offsetof(Config, sx10)},
    {"sxnatives10", core::ConfigKind::Flag, offsetof(Config, sxNatives10)},
    {"sxports10", core::ConfigKind::Flag, offsetof(Config, sxPorts10)},
    {"sxbeh10", core::ConfigKind::Flag, offsetof(Config, sxBeh10)},
    {"bmx10", core::ConfigKind::Flag, offsetof(Config, bmx10)},
    {"btx10", core::ConfigKind::Flag, offsetof(Config, btx10)},
    {"root10", core::ConfigKind::Flag, offsetof(Config, root10)},
    {"locks10", core::ConfigKind::Flag, offsetof(Config, locks10)},
    {"popin10", core::ConfigKind::Flag, offsetof(Config, popin10)},
    {"chest10", core::ConfigKind::Flag, offsetof(Config, chest10)},
    {"bits10", core::ConfigKind::Flag, offsetof(Config, bits10)},
    {"econ10", core::ConfigKind::Flag, offsetof(Config, econ10)},
    {"perc10", core::ConfigKind::Flag, offsetof(Config, perc10)},
    {"deliv10", core::ConfigKind::Flag, offsetof(Config, deliv10)},
    {"navmesh10", core::ConfigKind::Flag, offsetof(Config, navmesh10)},
    {"gamedb30", core::ConfigKind::Flag, offsetof(Config, gamedb30)},
    {"skin10", core::ConfigKind::Flag, offsetof(Config, skin10)},
    {"playset30", core::ConfigKind::Flag, offsetof(Config, playset30)},
    {"hudmarker10", core::ConfigKind::Flag, offsetof(Config, hudMarker10)},
    {"rewards10", core::ConfigKind::Flag, offsetof(Config, rewards10)},
    {"currency10", core::ConfigKind::Flag, offsetof(Config, currency10)},
    {"psdna30", core::ConfigKind::Flag, offsetof(Config, psdna30)},
    {"avperc10", core::ConfigKind::Flag, offsetof(Config, avperc10)},
    {"spawner10", core::ConfigKind::Flag, offsetof(Config, spawner10)},
    {"chotime10", core::ConfigKind::Flag, offsetof(Config, choTime10)},
    {"choanim10", core::ConfigKind::Flag, offsetof(Config, choAnim10)},
    {"choscene10", core::ConfigKind::Flag, offsetof(Config, choScene10)},
    {"variant10", core::ConfigKind::Flag, offsetof(Config, variant10)},
    {"choconst10", core::ConfigKind::Flag, offsetof(Config, choConst10)},
    {"camworld10", core::ConfigKind::Flag, offsetof(Config, camWorld10)},
    {"realmfile10", core::ConfigKind::Flag, offsetof(Config, realmFile10)},
    {"cholayout10", core::ConfigKind::Flag, offsetof(Config, choLayout10)},
    {"clip10", core::ConfigKind::Flag, offsetof(Config, clip10)},
    {"clip10motions", core::ConfigKind::Flag, offsetof(Config, clip10Motions)},
    {"clip10ids", core::ConfigKind::Flag, offsetof(Config, clip10Ids)},
    {"choalias10", core::ConfigKind::Flag, offsetof(Config, choAlias10)},
    {"banm10", core::ConfigKind::Flag, offsetof(Config, banm10)},
    {"loadscreen10", core::ConfigKind::Flag, offsetof(Config, loadScreen10)},
    {"uiscope10", core::ConfigKind::Flag, offsetof(Config, uiScope10)},
    {"fmv10", core::ConfigKind::Flag, offsetof(Config, fmv10)},
    {"text10", core::ConfigKind::Flag, offsetof(Config, text10)},
    {"tips10", core::ConfigKind::Flag, offsetof(Config, tips10)},
    {"resident10", core::ConfigKind::Flag, offsetof(Config, resident10)},
    {"tutorial10", core::ConfigKind::Flag, offsetof(Config, tutorial10)},
    {"context10", core::ConfigKind::Flag, offsetof(Config, context10)},
    {"audio10", core::ConfigKind::Flag, offsetof(Config, audio10)},
    {"music10", core::ConfigKind::Flag, offsetof(Config, music10)},
    {"vo10", core::ConfigKind::Flag, offsetof(Config, vo10)},
    {"audiotrace10", core::ConfigKind::Flag, offsetof(Config, audioTrace10)},
    {"cinesync10", core::ConfigKind::Flag, offsetof(Config, cineSync10)},
    {"mixer10", core::ConfigKind::Flag, offsetof(Config, mixer10)},
    {"vfx10", core::ConfigKind::Flag, offsetof(Config, vfx10)},
    {"vfx_universal10", core::ConfigKind::Flag, offsetof(Config, vfxUniversal10)},
    {"vfxchan10", core::ConfigKind::Flag, offsetof(Config, vfxChan10)},
    {"vfx_trace", core::ConfigKind::Flag, offsetof(Config, vfxTrace)},
    {"curves10", core::ConfigKind::Flag, offsetof(Config, curves10)},
    {"surfaces10", core::ConfigKind::Flag, offsetof(Config, surfaces10)},
    {"assetbufs10", core::ConfigKind::Flag, offsetof(Config, assetBufs10)},
    {"challenge10", core::ConfigKind::Flag, offsetof(Config, challenge10)},
    {"selectmenu10", core::ConfigKind::Flag, offsetof(Config, selectMenu10)},
    {"actionmap10", core::ConfigKind::Flag, offsetof(Config, actionMap10)},
    {"focus10", core::ConfigKind::Flag, offsetof(Config, focus10)},
    {"keys10", core::ConfigKind::Flag, offsetof(Config, keys10)},
    {"accept10", core::ConfigKind::Flag, offsetof(Config, accept10)},
    {"tools10", core::ConfigKind::Flag, offsetof(Config, tools10)},
    {"playsetmenu10", core::ConfigKind::Flag, offsetof(Config, playsetMenu10)},
    {"zones10", core::ConfigKind::Flag, offsetof(Config, zones10)},
    {"building10", core::ConfigKind::Flag, offsetof(Config, building10)},
    {"spawntrace10", core::ConfigKind::Flag, offsetof(Config, spawnTrace10)},
    {"capsuletrace10", core::ConfigKind::Flag, offsetof(Config, capsuleTrace10)},
    {"shutdown10", core::ConfigKind::Flag, offsetof(Config, shutdown10)},
};

// The config lines of the log; the other keys are logged by their features or not at all.
inline void logConfig(const Config& c)
{
    core::logf("config: install10=%s serve10=%d trace=%d memtest=%s fpslog10=%d loadprof10=%d", c.install10, c.serve10,
               c.trace, c.memtest, c.fpsLog10, c.loadProf10);
    core::logf("config cho: chotime10=%d choanim10=%d choscene10=%d choconst10=%d cholayout10=%d clip10=%d "
               "clip10motions=%d clip10ids=%d choalias10=%d banm10=%d variant10=%d camworld10=%d teardowntrace=%d "
               "breaktrace=%d", c.choTime10, c.choAnim10, c.choScene10, c.choConst10, c.choLayout10, c.clip10,
               c.clip10Motions, c.clip10Ids, c.choAlias10, c.banm10, c.variant10, c.camWorld10, c.teardownTrace,
               c.breakTrace);
    core::logf("config menu: playsetmenu10=%d zones10=%d realmfile10=%d building10=%d spawntrace10=%d shutdown10=%d "
               "capsuletrace10=%d", c.playsetMenu10, c.zones10, c.realmFile10, c.building10, c.spawnTrace10,
               c.shutdown10, c.capsuleTrace10);
    core::logf("config mis: gate10=%d toggletype10=%d beacon10=%d locktrace10=%d", c.gate10, c.toggleType10, c.beacon10,
               c.lockTrace10);
}

// Reads <dir>\di1compat.cfg into cfg; without the file cfg keeps its defaults and one line says so.
inline void loadConfig(const char* dir, Config& cfg)
{
    char path[MAX_PATH];
    // sprintf_s kept (N21): a plugin dir of 246+ characters fail-fasts, where snprintf would cut the path (A-08)
    sprintf_s(path, "%s\\di1compat.cfg", dir);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") || !f) {
        core::logf("no %s: defaults (serve10 off)", path);
        return;
    }
    core::readConfig(f, cfg, kConfigKeys);
    fclose(f);
    logConfig(cfg);
}

}  // namespace di1

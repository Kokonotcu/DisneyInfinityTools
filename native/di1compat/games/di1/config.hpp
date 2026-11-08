// The plugin's config keys, defaults and fields; stable names (B1). Windows, core config and log. Read once (S4).
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <string>
#include "../../core/config.hpp"
#include "../../core/log.hpp"

namespace di1 {

struct Config {
    char install10[MAX_PATH] = "";   // the 1.0 install (its assets\ holds plain zips)
    bool serve10 = false;            // serve paths 3.0 lacks from the 1.0 zips
    bool shaders10 = false;          // real 1.0 shaders: translated at load (gen30.hpp) instead of 3.0 substitutes
    bool translate10 = true;         // shaders10's materials (0: its engine-side changes only, for bisecting)
    bool lazyLink10 = true;          // shaders10: 3.0's shaders link on first use too (0: the boot linker runs)
    bool skinLayout10 = true;        // skinned nodes' converted primitives in their 3.0 'skin' layout, bones kept
    bool skinTr10 = true;            // skinned materials take their translated 1.0 shader too (identity bones; gen30)
    bool sceneSettings10 = true;     // a 1.0 bundle's scene settings (SSET: fog, bloom, grading) re-laid into 3.0's
    bool events10 = true;            // mission presentation Event ints: 1.0 enum -> 3.0 (mission.hpp; .lua and .mer)
    bool progress10 = true;          // mission .lua: 1.0's generated objective line -> ProgressString "<default>"
    bool aliases10 = true;           // register dropped 1.0 Meridian classes with their 3.0 template (mission.hpp)
    bool mana10 = true;              // aliases10 on: SpawnManaNode as SpawnGroupKiller KeepAlive (mission.hpp)
    bool portRefs10 = true;          // .mer: owner fields naming port sub-nodes stay plain ints (mer::Rules::portRefs)
    bool ports10 = true;             // .mer: port names from owner fields, DataFetch constants under them
    bool pins10 = true;              // .mer: complete pin tables on every instance (mer::Rules::pins10)
    bool gate10 = true;              // .mer: MasterNetSync Link_Out chains fire locally as in 1.0 (mer::Rules::gate10)
    bool toggleType10 = true;        // .mer: Collision/VisibilityToggle keep their 1.0 mode (mer::Rules::toggleType10)
    bool cacheEnd10 = true;          // .mer: a cached, bound, never uncached Choreography uncaches at its end
    bool realmStart10 = true;        // .mer: a RealmCache starts at the weight its graph's Catalyst sets at once
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
    bool gamedb30 = true;            // 3.0 GameDB tables a 1.0 DataMap cannot name (gamedb30.hpp; the E-on-NPC crash)
    bool playset30 = true;           // gamedb30 on: the 1.0 playset's Playset record (3.0 includes) as a DataMap
    bool hudMarker10 = true;         // playset30 on: + Include.hudmarker_inc, 3.0's mission compass (supplement.hpp)
    bool rewards10 = true;           // 1.0 expendable groups in 3.0's keys + the 1.0 core packages (groups.hpp)
    bool currency10 = true;          // a 1.0 playset's currency is its DefaultRewardType (zones.hpp)
    bool skin10 = true;              // 1.0 InfluenceRemap palettes applied to skinned vertices (skin.hpp, REN-3)
    bool lod10 = true;               // a 1.0 LodGroup draws its nearest level only (3.0 draws them all: z-fighting)
    bool dnaChos10 = true;           // a 1.0 DNA's choreography effects name their scene in 3.0's Chos form
    bool scourgeHp10 = true;         // a 1.0 pod DNA's ScourgePlant health as 3.0's Health gadget (scourgehp.hpp)
    bool dnaNest10 = true;           // a 1.0 gadget's flat cho / DNA names under <gadget>.Chos / .DNAs (dnachos.hpp)
    bool psdna30 = true;             // a 1.0 avatar <x>_ps.dnax 3.0 lacks: 3.0's <x>.dnax naming the _PS model
    bool avperc10 = true;            // gamedb30 on: the 1.0 avatar's perception policy in a 1.0 playset (gamedb30.hpp)
    bool spawner10 = true;           // 1.0 SpawnerQuarterback groups: 3.0's QB factory lacks type 10 (spawner.hpp)
    bool spawnFrom10 = true;         // spawner10 on: SpawnFromActorNode + the group's spawn timer (spawner.hpp)
    bool scourge10 = true;           // spawnfrom10 on: ScourgeWave pods, Died / Empty (scourge.hpp)
    bool delivery10 = true;          // native10 on: DeliveryNode keeps its pins and links (inertnodes.hpp)
    bool decoSockets10 = true;       // native10 on: Spawn/ActivateDecorationSockets keep theirs (inertnodes.hpp)
    bool prank10 = true;             // native10 on: RegisterPranks, SpawnPrankSocket, RegisterPrankCombo keep theirs
    bool prankCombo10 = true;        // native10 on: PrankComboEnded keeps its pins and links (inertnodes.hpp)
    bool rcDelivery10 = true;        // a purchased 1.0 catalog lock goes DELIVERED, AVAILABLE at once (deliveries.hpp)
    bool rcBox10 = true;             // rcdelivery10 on: its Delivery expendable spawns at the avatar (deliveries.hpp)
    bool twin10 = true;              // a 1.0 DNA's model 3.0 retired names 3.0's '<model>2' re-export (transcode)
    bool choTime10 = true;           // cho.hpp: SubNetwork MinTime/MaxTime (choreography play range)
    bool choAnim10 = true;           // cho.hpp: Float 'Global.Time' clip/event input -> AnimState 'AnimState'
    bool choScene10 = true;          // cho.hpp: SceneTreeNodeRef on PlacementNode Associations
    bool variant10 = true;           // snedit.hpp: VariantGroup for the variant processors skin30 / locator30 add
    bool choConst10 = true;          // choconst.hpp: choreography Static nodes stay Static under constant headers
    bool camWorld10 = true;          // associations.hpp: a camera's world-basis twin (Transform/In WorldBasis)
    bool realmFile10 = true;         // realms.hpp: a 1.0 realm file's RealmDataPool as 3.0's RealmData node
    bool zoneVolume10 = true;        // realms.hpp: a RealmVolume naming no realm names its zone's realm
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
    bool audio10 = true;             // 1.0 FMOD runtime hosts the 1.0 sound data Wwise lacks (games/di1/audio.hpp)
    bool music10 = true;             // audio10: 1.0 Music_<X>.fev through 3.0's MusicStack
    bool vo10 = true;                // audio10: 1.0 dialog banks (.fsb/.name) and .dlg scripts in 3.0's dialog system
    bool mixer10 = true;             // audio10: 1.0's Mixer presets as the 'Mixer' volume source of the 1.0 categories
    bool cineSync10 = true;          // audio10: an audio-synced choreography starts the 1.0 sound event of its name
    bool voSync10 = true;            // vo10: a type 4 dialog run follows its audio-synced choreography's clock
    bool voFirst10 = true;           // vo10: a dialog sequence's line 2 waits item 0's delay, as 1.0 FUN_00718650
    bool vfx10 = true;               // 1.0 particle packages and .p3d meshes in 3.0 form (vfx.hpp)
    bool vfxUniversal10 = true;      // 1.0's universal particle package beside 3.0's (particles.hpp; needs vfx10)
    bool vfxChan10 = true;           // emitter curve channels re-laid for the shader of their material (chan.hpp)
    bool curves10 = true;            // gp: ledge/rail curves bound in 3.0 form (curves::curves30)
    bool surfaces10 = true;          // gp: collision shape SurfaceType mapped by name onto the 3.0 table (surfaces30)
    bool assetBufs10 = true;         // gp+npc: read10 finds <stem>_<n>.* members in <dir>/<stem>.zip
    bool challenge10 = true;         // gp: 1.0 activities into 3.0's Challenge system (challenges.hpp, activities.hpp)
    bool selectMenu10 = true;        // gp: Tab opens 1.0's SelectMenu (Mission Log, Toy Store) in 1.0 zones
    bool actionMap10 = true;         // ui: a 1.0 movie's UsesActionMap sets its screen's action map (movies.hpp)
    bool focus10 = true;             // ui: a 3.0 Screen on a 1.0 movie gets 3.0's non-FSM focus (movies.hpp)
    bool keys10 = true;              // ui: a 1.0 movie's 3.0 script: mapped actions reach the movie as keys (movies)
    bool accept10 = true;            // gp: a 1.0 mission activated in a 1.0 zone posts 'mission accepted' (accept.hpp)
    bool tools10 = true;             // gp: in a 1.0 zone a tool 3.0 refuses is judged by its 1.0 list (tools.hpp)
    bool playsetMenu10 = true;       // menu: a Play Sets entry per registered 1.0 playset (ui::playsetMenu10)
    bool zones10 = true;             // zonelist.lua: every 1.0 playset registered as it loads (playsets10, zones.hpp)
    bool building10 = true;          // a 1.0 SpawnBuilding node builds its building on the lot as in 1.0 (buildings)
    bool shutdown10 = true;          // a 1.0 expendable's actor stays drawn while shut down (shutdown; needs popin10)
    bool debris10 = true;            // a 1.0 breakable explodes into its DebrisModel (shutdown; needs popin10)
    bool rootGraph10 = true;         // 3.0-shipped 1.0-era models: Root takes the lone graph (edits30, octx)
    bool pause10 = true;             // ui: a 1.0 screen's pause is released at its exit unless handed on (selectmenu)
    bool bhteam10 = true;            // ai: a team check of an actor without the team component is 'not on it' (bhteam)
    bool rcplace10 = true;           // ui: the Toy Store card list skips an item without a place record (rcplace)
    bool rcType10 = true;            // ui: a 1.0 playset's 3.0 RC catalog keeps 1.0's item types (edits30)
    bool statReward10 = true;        // missions: a 1.0 Stat reward carries its overrides' amount (edits30)
    bool carBody10 = true;           // Cars townsfolk car bodies: playsets get 1.0's tinted models (carbody)
    bool rowboat10 = true;           // 1.0 player rowboat DNA as a 3.0 boat vehicle a Tardis rule can mount (rowboat)
    bool oceanBoat10 = true;         // a 3.0 boat floats on a 1.0 zone's OceanSystem sea level (oceanboat)
    bool playerShip10 = true;        // 1.0 player ship deck DNA + its rigid body as one 3.0 boat vehicle (playership)
};

// Every key as the cfg file spells it. Not derivable from the field names (hide_ps, vfx_universal10, vfx_trace).
inline constexpr core::ConfigKey kConfigKeys[] = {
    {"install10", core::ConfigKind::Text, offsetof(Config, install10), sizeof(Config::install10)},
    {"serve10", core::ConfigKind::Flag, offsetof(Config, serve10)},
    {"shaders10", core::ConfigKind::Flag, offsetof(Config, shaders10)},
    {"translate10", core::ConfigKind::Flag, offsetof(Config, translate10)},
    {"lazylink10", core::ConfigKind::Flag, offsetof(Config, lazyLink10)},
    {"skinlayout10", core::ConfigKind::Flag, offsetof(Config, skinLayout10)},
    {"skintr10", core::ConfigKind::Flag, offsetof(Config, skinTr10)},
    {"scenesettings10", core::ConfigKind::Flag, offsetof(Config, sceneSettings10)},
    {"events10", core::ConfigKind::Flag, offsetof(Config, events10)},
    {"progress10", core::ConfigKind::Flag, offsetof(Config, progress10)},
    {"aliases10", core::ConfigKind::Flag, offsetof(Config, aliases10)},
    {"mana10", core::ConfigKind::Flag, offsetof(Config, mana10)},
    {"portrefs10", core::ConfigKind::Flag, offsetof(Config, portRefs10)},
    {"ports10", core::ConfigKind::Flag, offsetof(Config, ports10)},
    {"pins10", core::ConfigKind::Flag, offsetof(Config, pins10)},
    {"gate10", core::ConfigKind::Flag, offsetof(Config, gate10)},
    {"toggletype10", core::ConfigKind::Flag, offsetof(Config, toggleType10)},
    {"cacheend10", core::ConfigKind::Flag, offsetof(Config, cacheEnd10)},
    {"realmstart10", core::ConfigKind::Flag, offsetof(Config, realmStart10)},
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
    {"dnachos10", core::ConfigKind::Flag, offsetof(Config, dnaChos10)},
    {"scourgehp10", core::ConfigKind::Flag, offsetof(Config, scourgeHp10)},
    {"dnanest10", core::ConfigKind::Flag, offsetof(Config, dnaNest10)},
    {"lod10", core::ConfigKind::Flag, offsetof(Config, lod10)},
    {"avperc10", core::ConfigKind::Flag, offsetof(Config, avperc10)},
    {"spawner10", core::ConfigKind::Flag, offsetof(Config, spawner10)},
    {"spawnfrom10", core::ConfigKind::Flag, offsetof(Config, spawnFrom10)},
    {"scourge10", core::ConfigKind::Flag, offsetof(Config, scourge10)},
    {"delivery10", core::ConfigKind::Flag, offsetof(Config, delivery10)},
    {"decosockets10", core::ConfigKind::Flag, offsetof(Config, decoSockets10)},
    {"prank10", core::ConfigKind::Flag, offsetof(Config, prank10)},
    {"prankcombo10", core::ConfigKind::Flag, offsetof(Config, prankCombo10)},
    {"rcdelivery10", core::ConfigKind::Flag, offsetof(Config, rcDelivery10)},
    {"rcbox10", core::ConfigKind::Flag, offsetof(Config, rcBox10)},
    {"twin10", core::ConfigKind::Flag, offsetof(Config, twin10)},
    {"chotime10", core::ConfigKind::Flag, offsetof(Config, choTime10)},
    {"choanim10", core::ConfigKind::Flag, offsetof(Config, choAnim10)},
    {"choscene10", core::ConfigKind::Flag, offsetof(Config, choScene10)},
    {"variant10", core::ConfigKind::Flag, offsetof(Config, variant10)},
    {"choconst10", core::ConfigKind::Flag, offsetof(Config, choConst10)},
    {"camworld10", core::ConfigKind::Flag, offsetof(Config, camWorld10)},
    {"realmfile10", core::ConfigKind::Flag, offsetof(Config, realmFile10)},
    {"zonevolume10", core::ConfigKind::Flag, offsetof(Config, zoneVolume10)},
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
    {"cinesync10", core::ConfigKind::Flag, offsetof(Config, cineSync10)},
    {"vosync10", core::ConfigKind::Flag, offsetof(Config, voSync10)},
    {"vofirst10", core::ConfigKind::Flag, offsetof(Config, voFirst10)},
    {"mixer10", core::ConfigKind::Flag, offsetof(Config, mixer10)},
    {"vfx10", core::ConfigKind::Flag, offsetof(Config, vfx10)},
    {"vfx_universal10", core::ConfigKind::Flag, offsetof(Config, vfxUniversal10)},
    {"vfxchan10", core::ConfigKind::Flag, offsetof(Config, vfxChan10)},
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
    {"shutdown10", core::ConfigKind::Flag, offsetof(Config, shutdown10)},
    {"debris10", core::ConfigKind::Flag, offsetof(Config, debris10)},
    {"rootgraph10", core::ConfigKind::Flag, offsetof(Config, rootGraph10)},
    {"pause10", core::ConfigKind::Flag, offsetof(Config, pause10)},
    {"bhteam10", core::ConfigKind::Flag, offsetof(Config, bhteam10)},
    {"rcplace10", core::ConfigKind::Flag, offsetof(Config, rcplace10)},
    {"rctype10", core::ConfigKind::Flag, offsetof(Config, rcType10)},
    {"statreward10", core::ConfigKind::Flag, offsetof(Config, statReward10)},
    {"carbody10", core::ConfigKind::Flag, offsetof(Config, carBody10)},
    {"rowboat10", core::ConfigKind::Flag, offsetof(Config, rowboat10)},
    {"oceanboat10", core::ConfigKind::Flag, offsetof(Config, oceanBoat10)},
    {"playership10", core::ConfigKind::Flag, offsetof(Config, playerShip10)},
};

// The config lines of the log; the other keys are logged by their features or not at all.
inline void logConfig(const Config& c)
{
    core::logf("config: install10=%s serve10=%d", c.install10, c.serve10);
    core::logf("config cho: chotime10=%d choanim10=%d choscene10=%d choconst10=%d cholayout10=%d clip10=%d "
               "clip10motions=%d clip10ids=%d choalias10=%d banm10=%d variant10=%d camworld10=%d", c.choTime10,
               c.choAnim10, c.choScene10, c.choConst10, c.choLayout10, c.clip10, c.clip10Motions, c.clip10Ids,
               c.choAlias10, c.banm10, c.variant10, c.camWorld10);
    core::logf("config menu: playsetmenu10=%d zones10=%d realmfile10=%d building10=%d shutdown10=%d debris10=%d",
               c.playsetMenu10, c.zones10, c.realmFile10, c.building10, c.shutdown10, c.debris10);
    core::logf("config mis: gate10=%d toggletype10=%d beacon10=%d", c.gate10, c.toggleType10, c.beacon10);
}

// Reads <dir>\di1compat.cfg into cfg; without the file cfg keeps its defaults and one line says so.
inline void loadConfig(const char* dir, Config& cfg)
{
    // a whole path, never a cut one that could name another file: too long fails to open (N21, A-08)
    const std::string path = std::string(dir) + "\\di1compat.cfg";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "r") || !f) {
        core::logf("no %s: defaults (serve10 off)", path.c_str());
        return;
    }
    core::readConfig(f, cfg, kConfigKeys);
    fclose(f);
    logConfig(cfg);
}

}  // namespace di1

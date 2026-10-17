// di1compat: a Pentane plugin that serves Disney Infinity 1.0 data, read in place from its zips, to the 3.0 engine.

// Files reach the engine as Genie::MemFile streams through its asset open, FUN_0041cb10.

// Log: <game dir>\di1compat.log, not Pentane's (its calling convention is unverified). Config: games/di1/config.hpp.
#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <string>
#include "minhook/MinHook.h"
#include "base/format.hpp"
#include "base/hash.hpp"
#include "base/text.hpp"
#include "core/engine_file.hpp"
#include "core/hooks.hpp"
#include "core/log.hpp"
#include "core/memcheck.hpp"
#include "core/readonly_file.hpp"
#include "core/stack.hpp"
#include "core/veh.hpp"
#include "core/zip.hpp"
#include "engine/assets.hpp"
#include "engine/choreography.hpp"
#include "engine/crt.hpp"
#include "engine/langkeys.hpp"
#include "engine/playsetslot.hpp"
#include "engine/shaderlinks.hpp"
#include "engine/shaders.hpp"
#include "engine/zones.hpp"
#include "games/di1/archives.hpp"
#include "games/di1/assetdefs.hpp"
#include "games/di1/assetopen.hpp"
#include "games/di1/choalias.hpp"
#include "games/di1/config.hpp"
#include "games/di1/currency.hpp"
#include "games/di1/econ.hpp"
#include "games/di1/expendables.hpp"
#include "games/di1/gamedb.hpp"
#include "games/di1/hideps.hpp"
#include "games/di1/meridian.hpp"
#include "games/di1/octsteps.hpp"
#include "games/di1/options.hpp"
#include "games/di1/playsets.hpp"
#include "games/di1/popin.hpp"
#include "games/di1/renderio.hpp"
#include "games/di1/render10.hpp"
#include "games/di1/screens.hpp"
#include "games/di1/session.hpp"
#include "games/di1/shaders10.hpp"
#include "games/di1/tables.hpp"
#include "games/di1/transcode.hpp"
#include "games/di1/transcodeio.hpp"
#include "games/di1/vfs.hpp"
#include "games/di1/zones.hpp"
#include "formats/oct.hpp"
#include "formats/chd.hpp"
#include "trace/breaktrace.hpp"
#include "trace/fade.hpp"
#include "trace/loadprof.hpp"
#include "trace/locktrace.hpp"
#include "trace/mission.hpp"
#include "trace/opacity.hpp"
#include "trace/renderdoc.hpp"
#include "trace/spawn.hpp"
#include "trace/spawntrace.hpp"
#include "trace/teardown.hpp"
#include "mer.hpp"
#include "mernode.hpp"
#include "games/di1/renderoct.hpp"
#include "formats/arc30.hpp"
#include "convert/datamap.hpp"
#include "formats/dct.hpp"
#include "formats/luachunk.hpp"
#include "sx.hpp"
#include "sys.hpp"
#include "npc.hpp"
#include "cho.hpp"
#include "ui.hpp"
#include "audio.hpp"
#include "games/di1/vfx.hpp"
#include "games/di1/chan.hpp"
#include "gameplay.hpp"
#include "npcrt.hpp"
#include "vfxrt.hpp"
#include "gprt.hpp"
#include "bldrt.hpp"
#include "caprt.hpp"
#include "third_party/miniz/miniz.h"

// ---- Pentane plugin ABI (upstream source/plugin.hpp; DI3G strings match) ----
struct SemVer { uint16_t major, minor, patch; };
struct PluginInformation {
    char name[256];
    char author[256];
    uint8_t uuid[16];
    SemVer version;
    SemVer minimum_pentane_version;
    char reserved[484];
};
static_assert(sizeof(PluginInformation) == 0x400, "Pentane expects 0x400 bytes");

extern "C" PluginInformation Pentane_PluginInformation = {
    "di1compat", "DI3Tools",
    {0x6d, 0x1c, 0x0e, 0x5a, 0x3f, 0x41, 0x4b, 0x8e, 0x9a, 0x21, 0xd1, 0x7c, 0x0a, 0x03, 0x30, 0x10},
    {0, 1, 0}, {0, 0, 0}, {}};
extern "C" size_t Pentane_PluginDependencyCount = 0;

// The services by their old names: this file's code calls them unqualified
using core::g_base;
using core::hook;
using core::installFeature;
using core::logf;
using core::rt;
using core::cstr;
using core::safeCopy;
using core::afterCall;
using core::avHandler;
using core::stackOf;
using core::copyToMemFile;
using core::memFile;
using core::readStream;
using text::endsWith;
using engine::assets::read30;
using engine::assets::read30x;
using text::norm;
using trace::loadprof::frameNo;
using trace::loadprof::noteOpenThread;
using trace::loadprof::nowMs;
using di1::vfs::read10;
using di1::vfs::read10s;

static di1::Config g_cfg;   // the cfg file's values (games/di1/config.hpp): set once in Pentane_Main, before any hook

static std::string g_gameDir;   // the 3.0 install; set once in Pentane_Main, before any hook

// The choreography start hook: cinesync10 starts 1.0's audio-sync sound event (audio.hpp); breaktrace logs.
static bool installChoStart(const di1::Config& c)
{
    engine::choreography::Config cho;
    if (c.breakTrace) cho.starting = trace::breaktrace::choreographyStart;
    const bool cineSync = c.audio10 && c.cineSync10;
    if (cineSync)
        cho.started = [](const uint8_t*, const char* name, uint32_t flags) { audio::choreographyStarted(name, flags); };
    return engine::choreography::install(cho);
}

// missiontrace's config: 1.0 playset missions by zones10's prefixes; renderdoc10 arms on the first one's tick,
// missiontrace or not.
static trace::mission::Config missionTraceOf(const di1::Config& c)
{
    trace::mission::Config m;
    m.on = c.missionTrace;
    m.ownMission = di1::playsets::ownMission;
    if (trace::renderdoc::loaded()) m.ownMissionTick = trace::renderdoc::arm;
    return m;
}

// sxnatives10's hooks (H1). sx::g_log is set in every config: BMX-1 and BTX-1 log unpaired key ids through it.
static bool installSx(const di1::Config& c)
{
    sx::g_log = logf;
    if (!c.sx10 || !c.sxNatives10) return true;
    sx::g_exe = g_base;
    sx::init(c.sxPorts10);
    std::vector<core::HookSpec> natives = {
        {"Slang linker (1.0 natives)", 0x00736DA0, "51538B5C240C568BF18BCBE8", (void*)&sx::hkLink,
         (void**)&sx::g_link}};
    if (c.sxPorts10 && c.sxBeh10)
        natives.push_back({"Slang call by name (1.0 behavior type)", 0x00739090, "56578B7C240C8BF18B4E04",
                           (void*)&sx::hkCallByName, (void**)&sx::g_callByName});
    return installFeature("sxnatives10", natives);
}

// audio10's hooks (audio::install) need the 1.0 install; its log goes in first.
static bool installAudio(const di1::Config& c)
{
    if (!c.audio10 || !c.install10[0]) return true;
    audio::logfn = logf;
    audio::Cfg a;
    a.music = c.music10;
    a.vo = c.vo10;
    a.trace = c.audioTrace10;
    a.mixer = c.mixer10;
    a.install10 = c.install10;
    a.game30 = g_gameDir;
    a.installFeature = installFeature;
    a.rt = rt;
    if (audio::install(a)) return true;
    logf("audio10: off (FMOD host failed)");
    return false;
}

// Before any hook, in this order: RenderDoc, 1.0 archives, resident archives, tables, shaders10, playsets.
static void prepare(const di1::Config& c)
{
    trace::renderdoc::install({c.renderdoc10, g_gameDir + "\\di3cap"});
    di1::vfs::install(c);
    di1::choalias::install(c);
    di1::session::install(c);
    di1::tables::install(c);
    di1::render10::install(c);
    di1::playsets::install(c);
    engine::langkeys::install({g_gameDir, read30x});
    di1::transcodeio::install(c);
}

// The first-AV handler, then MinHook; false: no hook can be installed.
static bool startHooks()
{
    AddVectoredExceptionHandler(1, avHandler);
    if (MH_Initialize() == MH_OK) return true;
    logf("MinHook init failed; no hooks");
    return false;
}

extern "C" void __stdcall Pentane_Main();
extern "C" void __stdcall Pentane_Main()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) format::fitOrFailFast(slash + 1, path + MAX_PATH - (slash + 1), "%s", "di1compat.log");
    {   // the last 5 runs' logs stay as di1compat.1.log (newest) .. di1compat.5.log: a launch keeps the evidence
        std::string base(path, strlen(path) - 4);
        for (int i = 4; i >= 0; --i)
            MoveFileExA(i ? (base + "." + std::to_string(i) + ".log").c_str() : path,
                        (base + "." + std::to_string(i + 1) + ".log").c_str(), MOVEFILE_REPLACE_EXISTING);
    }
    core::openLog(path);
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    g_gameDir.assign(path, slash ? slash - path : 0);
    logf("di1compat 0.3.0 loaded; exe base %08X", (unsigned)g_base);
    char dir[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&Pentane_Main, &self);
    GetModuleFileNameA(self, dir, MAX_PATH);
    if (char* s = strrchr(dir, '\\')) *s = 0;
    di1::loadConfig(dir, g_cfg);
    prepare(g_cfg);

    // the modules' hooks in install order (B3): a hook is live once enabled
    if (!startHooks()) return;
    di1::assetopen::install(g_cfg);
    if (!engine::assets::install({(void*)&di1::assetopen::hkOpen, g_gameDir})) return;
    di1::gamedb::install(g_cfg);
    di1::econ::install(g_cfg);
    di1::expendables::install(g_cfg);
    di1::currency::install(g_cfg);
    di1::popin::install(g_cfg);
    engine::playsetslot::install({});   // always on: no cfg key (B-20)
    trace::spawn::install({g_cfg.trace, g_cfg.hidePs});
    di1::hideps::install(g_cfg);
    di1::shaders10::install(g_cfg);
    trace::mission::install(missionTraceOf(g_cfg));
    installChoStart(g_cfg);
    trace::breaktrace::install({g_cfg.breakTrace});
    trace::teardown::install({g_cfg.teardownTrace});
    di1::meridian::install({g_cfg.aliases10, g_cfg.native10, di1::tables::mer()});
    trace::locktrace::install({g_cfg.lockTrace10});
    trace::opacity::install({g_cfg.opacityLog10});
    trace::fade::install({g_cfg.fadeProbe10});
    trace::loadprof::install({g_cfg.fpsLog10, g_cfg.loadProf10, g_gameDir});
    di1::assetdefs::install(g_cfg);
    engine::crt::install({});
    installSx(g_cfg);
    npcrt::install(g_cfg);
    di1::screens::install(g_cfg);
    installAudio(g_cfg);
    vfxrt::install(g_cfg);
    gprt::install(g_cfg);
    bldrt::install(g_cfg);
    caprt::install(g_cfg);
    trace::spawntrace::install({g_cfg.spawnTrace10 && !g_cfg.trace});
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

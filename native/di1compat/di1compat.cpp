// di1compat: a Pentane plugin that serves Disney Infinity 1.0 data, read in place from its zips, to the 3.0 engine.

// Files reach the engine as Genie::MemFile streams through its asset open, FUN_0041cb10.

// Log: <game dir>\di1compat.log, not Pentane's (its calling convention is unverified). Config: games/di1/config.hpp.
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include "core/hooks.hpp"
#include "core/log.hpp"
#include "core/paths.hpp"
#include "core/veh.hpp"
#include "engine/assets.hpp"
#include "engine/crt.hpp"
#include "engine/langkeys.hpp"
#include "engine/playsetslot.hpp"
#include "games/di1/assetdefs.hpp"
#include "games/di1/assetopen.hpp"
#include "games/di1/audio10.hpp"
#include "games/di1/choalias.hpp"
#include "games/di1/chostart.hpp"
#include "games/di1/config.hpp"
#include "games/di1/currency.hpp"
#include "games/di1/econ.hpp"
#include "games/di1/expendables.hpp"
#include "games/di1/gamedb.hpp"
#include "games/di1/hideps.hpp"
#include "games/di1/meridian.hpp"
#include "games/di1/missiontrace.hpp"
#include "games/di1/playsets.hpp"
#include "games/di1/popin.hpp"
#include "games/di1/render10.hpp"
#include "games/di1/screens.hpp"
#include "games/di1/session.hpp"
#include "games/di1/shaders10.hpp"
#include "games/di1/sxkeys.hpp"
#include "games/di1/sxnatives.hpp"
#include "games/di1/tables.hpp"
#include "games/di1/transcodeio.hpp"
#include "games/di1/vfs.hpp"
#include "trace/breaktrace.hpp"
#include "trace/fade.hpp"
#include "trace/loadprof.hpp"
#include "trace/locktrace.hpp"
#include "trace/opacity.hpp"
#include "trace/renderdoc.hpp"
#include "trace/spawn.hpp"
#include "trace/spawntrace.hpp"
#include "trace/teardown.hpp"
#include "npcrt.hpp"
#include "vfxrt.hpp"
#include "gprt.hpp"
#include "bldrt.hpp"
#include "caprt.hpp"

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

// Pentane's entry, under the loader lock (B-N1): the log and the cfg, then each module's install() in order.
extern "C" void __stdcall Pentane_Main();
extern "C" void __stdcall Pentane_Main()
{
    const std::string gameDir = core::openExeLog("di1compat.log");
    core::g_base = (uintptr_t)GetModuleHandleA(nullptr);
    core::logf("di1compat 0.3.0 loaded; exe base %08X", (unsigned)core::g_base);
    di1::Config cfg;
    di1::loadConfig(core::pluginDir().c_str(), cfg);

    // before any hook, in this order: RenderDoc, 1.0 archives, resident archives, tables, shaders10, playsets
    trace::renderdoc::install({cfg.renderdoc10, gameDir + "\\di3cap"});
    di1::vfs::install(cfg);
    di1::choalias::install(cfg);
    di1::session::install(cfg);
    di1::tables::install(cfg);
    di1::render10::install(cfg);
    di1::playsets::install(cfg);
    engine::langkeys::install({gameDir, engine::assets::read30x});
    di1::transcodeio::install(cfg);
    di1::assetopen::install(cfg);

    // the modules' hooks in install order (B3): a hook is live once enabled
    if (!core::startHooks()) return;
    if (!engine::assets::install({(void*)&di1::assetopen::hkOpen, gameDir})) return;
    di1::gamedb::install(cfg);
    di1::econ::install(cfg);
    di1::expendables::install(cfg);
    di1::currency::install(cfg);
    di1::popin::install(cfg);
    engine::playsetslot::install({});   // always on: no cfg key (B-20)
    trace::spawn::install({cfg.trace, cfg.hidePs});
    di1::hideps::install(cfg);
    di1::shaders10::install(cfg);
    di1::missiontrace::install(cfg);
    di1::chostart::install(cfg);
    trace::breaktrace::install({cfg.breakTrace});
    trace::teardown::install({cfg.teardownTrace});
    di1::meridian::install({cfg.aliases10, cfg.native10, di1::tables::mer()});
    trace::locktrace::install({cfg.lockTrace10});
    trace::opacity::install({cfg.opacityLog10});
    trace::fade::install({cfg.fadeProbe10});
    trace::loadprof::install({cfg.fpsLog10, cfg.loadProf10, gameDir});
    di1::assetdefs::install(cfg);
    engine::crt::install({});
    di1::sxkeys::install(cfg);
    di1::sxnatives::install(cfg);
    npcrt::install(cfg);
    di1::screens::install(cfg);
    di1::audio10::install(cfg);
    vfxrt::install(cfg);
    gprt::install(cfg);
    bldrt::install(cfg);
    caprt::install(cfg);
    trace::spawntrace::install({cfg.spawnTrace10 && !cfg.trace});
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

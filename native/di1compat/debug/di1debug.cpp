// di1debug: a Pentane plugin that only watches. It counts what 1.0 content asks of the engine and writes di1debug.txt.

// Deleting the DLL removes it; nothing in di1compat knows it exists. It changes no call's arguments or results.

// Pentane runs it after di1compat when config.toml lists it above di1compat.dll; it refuses to run earlier.
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../minhook/MinHook.h"
#include "chain.hpp"
#include "files.hpp"
#include "flash.hpp"
#include "lua.hpp"
#include "report.hpp"
#include "sx.hpp"

// ---- Pentane plugin ABI (upstream source/plugin.hpp), as in di1compat.cpp ----
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
    "di1debug", "DI3Tools",
    {0x8c, 0xf2, 0xaa, 0x76, 0x54, 0x20, 0x48, 0xc3, 0xb9, 0x45, 0xc5, 0x4c, 0x15, 0xcd, 0x91, 0x67},
    {0, 1, 0}, {0, 0, 0}, {}};
extern "C" size_t Pentane_PluginDependencyCount = 0;

// di1compat always hooks the asset open first (engine/assets.hpp); without its jump there it has not run yet.
static bool compatRanFirst(const dbg::chain::Module& compat)
{
    if (!compat.lo) return true;
    if (dbg::chain::jumpsInto(dbg::files::eng::kAssetOpen, compat)) return true;
    core::logf("di1debug off: di1compat.dll has not run yet; list di1debug.dll above it in Pentane\\config.toml");
    return false;
}

// Pentane's entry, under the loader lock (H10): the log, the order check, the watches, the writer thread.
extern "C" void __stdcall Pentane_Main();
extern "C" void __stdcall Pentane_Main()
{
    const std::string gameDir = core::openExeLog("di1debug.log");
    core::g_base = (uintptr_t)GetModuleHandleA(nullptr);
    core::logf("di1debug 0.1.0 loaded; exe base %08X", (unsigned)core::g_base);
    const dbg::chain::Module compat = dbg::chain::module("di1compat.dll");
    core::logf("di1compat.dll %s", compat.lo ? "loaded" : "not loaded: every answer counts as 3.0's");
    if (!compatRanFirst(compat)) return;
    if (MH_Initialize() != MH_OK) {
        core::logf("di1debug off: MinHook init failed");
        return;
    }
    const bool filesOn = dbg::files::install(compat);
    const bool luaOn = dbg::lua::install({dbg::chain::module(nullptr), compat, &dbg::flash::noteError});
    const bool sxOn = dbg::sx::install(compat);
    const bool flashOn = dbg::flash::install(compat);
    core::logf("watches: files=%d lua=%d sx=%d flash=%d", filesOn, luaOn, sxOn, flashOn);
    dbg::report::start(gameDir);
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

// Audio converters for the Python oracles: the 1.0 mixer applied through FMOD (mixercheck.py), FEV deps (fevdeps.py).
#include <climits>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include "../games/di1/audio.hpp"
#include "run.hpp"
#include "fmodtools.hpp"
#include "modes.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

// The categories --mixer reads back: 1.0's groups, 3.0's ui ones, IN_City's ambience, the master.
constexpr const char* kMixerCategories[] = {"sfx", "music", "dialog", "ui/sfx", "ui/songs", "ui/dialog", "sfx_sfx2d",
                                            "sfx_sfx3d_ambience", "sfx_sfx3d_ship",
                                            "sfx_sfx3d_ambience_waterfall_virtual", "master"};
constexpr float kOptionFader = 0.5f;   // a 3.0 option fader on top: music categories at 0.5 x mixer

// The 'Mixer' entry of 1.0's core datamap, its Lua table from the install's root archives, the at-rest source.
bool readMixer(const std::string& inst, di1::mixer::MixerRest& rest)
{
    std::string core, lua;
    const di1::mixer::RootArchives zips(inst);
    if (!zips.read("GameDB/Core/CoreDataMap.lua", core)) {
        printf("MIXER no core datamap\n");
        return false;
    }
    std::vector<std::string> paths = di1::mixer::mixerPaths(core);
    for (auto& p : paths) printf("MIXER datamap entry %s\n", p.c_str());
    if (paths.empty() || !zips.read(paths[0], lua)) {
        printf("MIXER no mixer file\n");
        return false;
    }
    std::string err;
    if (!di1::mixer::mixerRest(lua, rest, &err)) {
        printf("MIXER parse failed: %s\n", err.c_str());
        return false;
    }
    for (auto& a : rest.active) printf("MIXER active %s\n", a.c_str());
    for (auto& v : rest.vol) printf("MIXER group %s %.6f\n", v.first.c_str(), v.second);
    printf("MIXER pitch sum %.6f\n", rest.pitchSum);
    return true;
}

void printCategories()
{
    for (const char* c : kMixerCategories) {
        void* cat = nullptr;
        float v = -1;
        bool has = !g_sys.f.EventSystem_GetCategory(g_sys.es, c, &cat) && cat;
        if (has) g_sys.f.EventCategory_GetVolume(cat, &v);
        printf("CAT %s %s %.6f\n", c, has ? "exists" : "absent", v);
    }
}

std::string nameLabel(const char* name, void*) { return name; }

// Where the music system's own category sits, and whether it is 'music'.
void printMusicCategory()
{
    void* mc = nullptr;
    if (g_sys.f.EventSystem_GetMusicCategory(g_sys.es, &mc) || !mc) {
        printf("MUSICCAT none\n");
        return;
    }
    const std::string chain = categoryChain(mc, INT_MAX, nameLabel);
    void* music = nullptr;
    g_sys.f.EventSystem_GetCategory(g_sys.es, "music", &music);
    float v = -1;
    g_sys.f.EventCategory_GetVolume(mc, &v);
    printf("MUSICCAT %s (same object as 'music': %s) volume %.6f\n", chain.c_str(), mc == music ? "yes" : "no", v);
}

}  // namespace

// mixer10: the at-rest source per group, applied through the real FMOD DLLs with Initialize, IN_City, Music_Inc.
int convertMixer(const Args& a)
{
    ea::setLog(stdoutLog);
    std::string inst = a.argv[2];
    di1::mixer::MixerRest rest;
    if (!readMixer(inst, rest)) return 1;
    if (!offlineHost(offlineAudio(inst))) return 1;
    ea::host::loadTree("Initialize.fev", "Permanent");
    ea::host::loadTree("IN_City.fev", nullptr);
    ea::music::ensure(kPlayset10);
    {
        std::lock_guard<std::recursive_mutex> l(g_sys.mx);
        g_host.mixer = rest.vol;
        g_host.faderValues["music"]["Options"] = kOptionFader;
        ea::host::applyVolumes();
    }
    printCategories();
    printMusicCategory();
    return 0;
}

// Every 1.0 FEV's 'Load' list as the plugin reads it through FMOD (1.0 FUN_011348e0): DEPS lines.
int convertFevDeps(const Args& a)
{
    ea::setLog(stdoutLog);
    if (!offlineHost(offlineAudio(a.argv[2]))) return 1;
    for (auto& e : fs::directory_iterator(g_sys.cfg.soundDir)) {
        if (e.path().extension() != ".fev") continue;
        std::string file = e.path().filename().string();
        void* p = nullptr;
        if (g_sys.f.EventSystem_Load(g_sys.es, file.c_str(), nullptr, &p) || !p) {
            printf("DEPS %s LOADFAIL\n", file.c_str());
            continue;
        }
        ea::host::Project pr;
        pr.p = p;
        std::string list;
        for (auto& d : ea::host::depsOf(pr, e.path().stem().string())) list += (list.empty() ? "" : "|") + d;
        printf("DEPS %s %s\n", file.c_str(), list.c_str());
        g_sys.f.EventProject_Release(p);
    }
    return 0;
}

}  // namespace octtest

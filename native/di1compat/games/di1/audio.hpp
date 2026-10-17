// 1.0's sound data in 3.0: install layout, FMOD init, registration and mixer rules for the FMOD host. engine/audio.
#pragma once
#include <windows.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../base/text.hpp"
#include "../../core/hooks.hpp"
#include "../../core/wholefile.hpp"
#include "../../engine/audio/dialogue.hpp"
#include "../../engine/audio/host.hpp"
#include "../../engine/audio/log.hpp"
#include "../../engine/audio/music.hpp"
#include "../../engine/audio/system.hpp"
#include "../../engine/audio/wwise.hpp"
#include "dlg.hpp"
#include "mixer.hpp"

namespace di1::audio {

struct Cfg {
    bool music = true;    // music10: 1.0 music through the MusicStack
    bool vo = true;       // vo10: 1.0 dialog banks and scripts in 3.0's dialog system
    bool trace = false;   // audiotrace10: log each posted name once with the backend that took it
    bool mixer = true;    // mixer10: 1.0's Mixer presets as the 'Mixer' source of the 1.0 categories
    std::string install10;   // the 1.0 install: FMOD DLLs and sound data
    std::string game30;      // the 3.0 install
    core::InstallFeatureFn installFeature = nullptr;   // core::installFeature; octtest passes stand-ins for both
    uintptr_t (*rt)(uint32_t) = nullptr;               // core::rt
    engine::audio::LogFn log = nullptr;                // core::logf; octtest's stdout
};

// The engine audio feature configured with 1.0's facts.
inline engine::audio::wwise::Config config(const Cfg& c);
// The host thread started and the audio hooks installed; false only when the thread did not start.
inline bool install(const Cfg& c);
// 1.0 FUN_01134440: a 'Load' value split at ' ', ',' and ';'; no extension means '.fev'; ':Group' kept.
inline void splitLoad(const char* v, std::vector<std::string>& out);
// A 1.0 line's lipsync (3.0 FUN_014c5550's path) -> the 1.0 character zip and member (1.0 FUN_00fb8e30).
inline bool annoSource(const char* path, std::string& zipRel, std::string& member);

namespace detail {

inline std::string soundDir(const std::string& install10) { return install10 + "\\assets\\sound"; }
inline std::string dialogDir(const std::string& install10) { return install10 + "\\assets\\sound\\dialog"; }

// 1.0 FUN_011372c0's FMOD init values (init flags 2: FMOD_INIT_3D_RIGHTHANDED; event flags 0xa).
inline engine::audio::system::InitValues initValues()
{
    engine::audio::system::InitValues v;
    v.doppler = v.distance = v.rolloff = 1.0f;
    v.softwareOutputs = 0;
    v.streamBufferMs = 1000;
    v.advancedField = 0x40;
    v.advancedValue = 750;
    v.maxChannels = 64;
    v.initFlags = 2;
    v.eventFlags = 10;
    return v;
}

// 1.0 0x011384a0: the category under 'master', or 'ui' over one; 'virtual' events must loop with behaviour 5.
inline engine::audio::host::RegisterRule registerRule()
{
    engine::audio::host::RegisterRule r;
    r.top = {"sfx", "music", "songs", "dialog"};
    r.ui = "ui";
    r.virtualCategory = "virtual";
    r.virtualBehavior = engine::audio::fmod::kJustFailIfQuietest;
    r.releaseProperty = "Release";
    return r;
}

// 3.0's faders ('<src>_Volume_Fader_<cat>', FUN_0182c460) onto the 1.0 categories of the same use (table 0x0148d240).
inline std::vector<engine::audio::host::FaderTarget> faders()
{
    return {{"sfx", {"sfx", "ui/sfx"}}, {"music", {"music", "ui/songs"}}, {"dialog", {"dialog", "ui/dialog"}}};
}

// 1.0's music project of a playset: 'Music_<X>.fev'.
inline std::string musicProject(const std::string& playset)
{
    return engine::audio::system::installedName("Music_" + playset + ".fev");
}

// mixer10: 1.0 loads it through its core datamap at boot, before any zone (FUN_006efd10 case 7).
inline void loadMixer(const std::string& install10)
{
    const mixer::RootArchives zips(install10);
    std::string core, lua, err;
    std::vector<std::string> paths;
    mixer::MixerRest rest;
    const bool haveCore = zips.read("GameDB/Core/CoreDataMap.lua", core);
    if (haveCore) paths = mixer::mixerPaths(core);
    const bool haveLua = !paths.empty() && zips.read(paths[0], lua);
    const bool parsed = haveLua && mixer::mixerRest(lua, rest, &err);
    if (!parsed) {
        engine::audio::log("mixer10: no 1.0 mixer (%s)", core.empty() ? "no core datamap" : paths.empty() ?
                           "no Mixer entry" : lua.empty() ? "file missing" : err.c_str());
        return;
    }
    std::string a, v;
    for (auto& s : rest.active) a += (a.empty() ? "" : ", ") + s;
    char b[64];
    for (auto& x : rest.vol) {
        snprintf(b, sizeof b, "%s%s %.3f", v.empty() ? "" : ", ", x.first.c_str(), x.second);
        v += b;
    }
    engine::audio::log("mixer10: %s: active %s; at rest %s%s", paths[0].c_str(), a.c_str(), v.c_str(),
                       rest.pitchSum ? " (a pitch offset is set: not applied)" : "");
    engine::audio::host::setMixer(rest.vol);
}

// 3.0's DialogueManager reads 'dialog/<script>' (0x00740a37); a script 1.0 has as v11 comes from 1.0's folder.
inline bool readScript(const std::string& dir, const char* path, std::vector<uint8_t>& in)
{
    constexpr size_t kPrefix = 7;   // "dialog/"
    constexpr size_t kExt = 4;      // ".dlg"
    const size_t n = path ? strlen(path) : 0;
    const bool named = n > kPrefix + kExt && !_strnicmp(path, "dialog/", kPrefix);
    if (!named || _stricmp(path + n - kExt, ".dlg")) return false;
    if (const bool up = engine::audio::system::ready(); !up) return false;
    const bool read = core::readFile(dir + "\\" + (path + kPrefix), in);
    if (!read) return false;
    return in.size() >= 4 && bytes::u32At(in.data()) == dlg::kVersion11;
}

inline engine::audio::dialogue::Config dialogueConfig(const Cfg& c)
{
    engine::audio::dialogue::Config d;
    d.banksDir = dialogDir(c.install10);
    d.projectsDir = soundDir(c.install10);
    d.defaultLanguage = "ENU";
    d.streamSuffix = "_str";        // 1.0 FUN_01137bb0: '_str' banks stream
    d.streamMode = 0x20002c1;
    d.sampleMode = 0x2000241;
    d.slotSuffix = ".fsb10";
    const std::string dir = d.banksDir;
    d.readScript = [dir](const char* path, std::vector<uint8_t>& in) { return readScript(dir, path, in); };
    d.transcode = dlg::dlgTranscode;
    d.installFeature = c.installFeature;
    d.rt = c.rt;
    return d;
}

inline engine::audio::music::Config musicConfig(const Cfg& c)
{
    engine::audio::music::Config m;
    const std::string sound30 = c.game30 + "\\assets\\sound";
    m.project = musicProject;
    // 3.0 ships no '<x>_music.bnk' for it and 1.0 has 'Music_<X>.fev'
    m.hosted = [sound30](const std::string& x) {
        const std::string bnk = sound30 + "\\" + text::lower(x) + "_music.bnk";
        if (x.empty() || GetFileAttributesA(bnk.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
        return !musicProject(x).empty();
    };
    m.installFeature = c.installFeature;
    m.rt = c.rt;
    return m;
}

// FMOD from the 1.0 install's root, its sound folder as the media path; mixer and boot projects first.
inline engine::audio::system::Config systemConfig(const Cfg& c)
{
    engine::audio::system::Config y;
    y.dllDir = c.install10;
    y.soundDir = soundDir(c.install10);
    y.init = initValues();
    const std::string install10 = c.install10;
    const bool mixer = c.mixer;
    y.boot = [install10, mixer]() {
        if (mixer) loadMixer(install10);
        engine::audio::host::loadTree("Initialize.fev", "Permanent");   // 1.0 FUN_01138210 at audio init
    };
    y.tick = [] { engine::audio::wwise::tick(); };
    return y;
}

inline engine::audio::host::Config hostConfig(const Cfg& c)
{
    engine::audio::host::Config h;
    h.rule = registerRule();
    h.depsProperty = "Load";
    h.splitDeps = splitLoad;
    h.faders = faders();
    h.rt = c.rt;
    return h;
}

}  // namespace detail

inline engine::audio::wwise::Config config(const Cfg& c)
{
    engine::audio::wwise::Config w;
    w.music = c.music;
    w.vo = c.vo;
    w.trace = c.trace;
    w.log = c.log;
    w.systemCfg = detail::systemConfig(c);
    w.hostCfg = detail::hostConfig(c);
    w.musicCfg = detail::musicConfig(c);
    w.voCfg = detail::dialogueConfig(c);
    w.installFeature = c.installFeature;
    return w;
}

inline bool install(const Cfg& c) { return engine::audio::wwise::install(config(c)); }

inline void splitLoad(const char* v, std::vector<std::string>& out)
{
    std::string cur;
    for (const char* c = v;; ++c) {
        const bool end = *c == '\0';
        if (!end && !strchr(" ,;", *c)) {
            cur += *c;
            continue;
        }
        if (!cur.empty()) {
            const size_t colon = cur.find(':');   // an item may name a group: 'mu_campus.fev:mu_exploders'
            std::string file = cur.substr(0, colon);
            if (file.find('.') == std::string::npos) file += ".fev";
            out.push_back(colon == std::string::npos ? file : file + cur.substr(colon));
            cur.clear();
        }
        if (end) break;
    }
}

// 3.0 opens 'sound/dialog/<lang>/<letters>/<media>.anno'; 1.0 keeps it in sound/dialog/ENU/<Character>.zip.
inline bool annoSource(const char* path, std::string& zipRel, std::string& member)
{
    constexpr size_t kDir = 13;      // "sound/dialog/"
    constexpr size_t kAnno = 5;      // ".anno"
    constexpr size_t kShortest = 18;   // "sound/dialog/" and a 5-char tail
    constexpr size_t kLetters = 3;   // a folder of two letters, with its leading '/'
    std::string p = path ? text::lower(path) : "";
    for (char& c : p)
        if (c == '\\') c = '/';
    const bool shaped = p.compare(0, kDir, "sound/dialog/") == 0 && p.size() >= kShortest;
    if (!shaped || p.compare(p.size() - kAnno, kAnno, ".anno")) return false;
    const size_t a = p.find('/', kDir), b = a == std::string::npos ? a : p.find('/', a + 1);
    if (b == std::string::npos || b - a < kLetters || p.find('/', b + 1) != std::string::npos) return false;
    const uint16_t prefix = (uint16_t)(toupper((uint8_t)p[a + 1]) | toupper((uint8_t)p[a + 2]) << 8);
    // the character is the speaker's first 1.0 bank's (Edna.zip holds ed019_02_kpr-zo55_01-02.anno)
    return engine::audio::dialogue::withSpeaker(prefix, [&](const std::string& bankFile, const std::string& dir) {
        std::string c = bankFile.substr(0, bankFile.rfind('.'));
        if (text::endsWithPastStem(c, "_str") || text::endsWithPastStem(c, "_ram")) c.resize(c.size() - 4);
        zipRel = "sound/dialog/" + dir.substr(dir.rfind('\\') + 1) + "/" + c + ".zip";
        member = p.substr(b + 1);
        return true;
    });
}

}  // namespace di1::audio

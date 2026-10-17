// mixer10: 1.0's Mixer presets at rest, the 'Mixer' fader source of its sound categories. formats, miniz. Once.
#pragma once
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../../core/zip.hpp"
#include "../../formats/luatable.hpp"

namespace di1::mixer {

// The 1.0 install's root archives (assets\*.zip, mounted at the root: a member path is the engine path), opened once.
class RootArchives {
public:
    explicit RootArchives(const std::string& install10);
    ~RootArchives();
    RootArchives(const RootArchives&) = delete;
    RootArchives& operator=(const RootArchives&) = delete;
    // The member from the first archive in FindFirstFile order that has it (names ignore case); false: out untouched.
    bool read(const std::string& path, std::string& out) const;

private:
    std::vector<std::unique_ptr<mz_zip_archive>> zips_;
};

// The 'Mixer|<path>' entries of 1.0's core datamap (CoreDataMap.lua bytecode, 1.0 string 0x01350a24).
inline std::vector<std::string> mixerPaths(const std::string& coreDataMap);

// 1.0's mixer at rest: per group (an FMOD category) the 'Mixer' source, the presets active from the start.
struct MixerRest {
    // ponytail: no preset fade (FadeInTime); 1.0 switches them at start. Run FUN_00713490's fade if a node drives one.
    std::map<std::string, float> vol;
    std::vector<std::string> active;
    double pitchSum = 0;
};

// false: out untouched, err says why.
inline bool mixerRest(const std::string& lua, MixerRest& out, std::string* err = nullptr);

namespace detail {

constexpr double kDbRange = 60;   // FUN_007141d0 and FUN_00713490 clamp dB sums and values to +-60
constexpr double kMinGain = 1e-5, kMaxGain = 1e4;

// 10^(dB / 20)
inline double fromDb(double db) { return pow(10.0, db / 20); }

// A Volume entry's dB (FUN_00713490): 20 log10(clamp(1 - (1 - 10^(v/20)) * gain, 1e-5, 1e4)), clamped.
inline double volumeDb(double v, double gain)
{
    const double x = std::clamp(1 - (1 - fromDb(std::clamp(v, -kDbRange, kDbRange))) * gain, kMinGain, kMaxGain);
    return std::clamp(20 * log10(x), -kDbRange, kDbRange);
}

// One preset entry of the group: its Volume dB into sum, its Pitch into pitchSum.
inline void addEntry(const luatable::Value& entry, double gain, double& sum, double& pitchSum)
{
    for (auto& par : entry.arr) {
        const std::string kind = par.str("Parameter");
        const double v = par.num("Value", 0);
        if (kind == "Volume") sum += volumeDb(v, gain);
        else if (kind == "Pitch") pitchSum += fabs(v * gain);
    }
}

// The dB sum the presets active from the start (FUN_00714a00, gain clamped to 0..1) give one group.
inline double groupDb(const std::string& name, const luatable::Value& presets, double& pitchSum)
{
    double sum = 0;
    for (auto& p : presets.arr) {
        if (!p.num("ActivateOnStart", 0)) continue;
        const double gain = std::clamp(p.num("Gain", 1), 0.0, 1.0);
        for (auto& entry : p.arr)
            if (entry.str("Name") == name) addEntry(entry, gain, sum, pitchSum);
    }
    return sum;
}

}  // namespace detail

inline RootArchives::RootArchives(const std::string& install10)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((install10 + "\\assets\\*.zip").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    for (BOOL more = TRUE; more; more = FindNextFileA(h, &fd)) {
        auto z = std::make_unique<mz_zip_archive>();
        const mz_bool opened = mz_zip_reader_init_file(z.get(), (install10 + "\\assets\\" + fd.cFileName).c_str(), 0);
        if (opened) zips_.push_back(std::move(z));
    }
    FindClose(h);
}

inline RootArchives::~RootArchives()
{
    for (auto& z : zips_) mz_zip_reader_end(z.get());
}

inline bool RootArchives::read(const std::string& path, std::string& out) const
{
    for (auto& z : zips_) {
        size_t n = 0;
        void* d = core::extractZipMember(z.get(), path.c_str(), &n);
        if (!d) continue;
        out.assign((const char*)d, n);
        mz_free(d);
        return true;
    }
    return false;
}

inline std::vector<std::string> mixerPaths(const std::string& coreDataMap)
{
    constexpr size_t kTag = 6;   // "Mixer|"
    std::vector<std::string> out;
    for (size_t at = coreDataMap.find("Mixer|"); at != std::string::npos; at = coreDataMap.find("Mixer|", at + kTag)) {
        const size_t b = at + kTag;
        size_t e = b;
        while (e < coreDataMap.size() && isprint((uint8_t)coreDataMap[e]) && coreDataMap[e] != '|') ++e;
        if (e > b) out.push_back(coreDataMap.substr(b, e - b));
    }
    return out;
}

// FUN_007141d0's tick at rest: v = clamp(10^(clamp(sum) / 20) * MasterVolume, 0, 1), via FUN_00713360's fader.
inline bool mixerRest(const std::string& lua, MixerRest& out, std::string* err)
{
    auto fail = [&](const char* why) {
        if (err) *err = why;
        return false;
    };
    const size_t b = lua.find('{');
    if (b == std::string::npos) return fail("no table");
    luatable::Value m;
    luatable::Parser ps(lua.data() + b, lua.data() + lua.size());
    if (!ps.value(m) || m.t != luatable::Kind::Table) return fail("not a Lua table");
    const luatable::Value* gs = m.get("GlobalSettings");
    const luatable::Value* groups = m.get("Groups");
    const luatable::Value* presets = m.get("Presets");
    const bool groupsOk = groups && groups->t == luatable::Kind::Table;
    const bool presetsOk = presets && presets->t == luatable::Kind::Table;
    if (!groupsOk || !presetsOk) return fail("no Groups or Presets");
    const double master = detail::fromDb(std::clamp(gs ? gs->num("MasterVolume", 0) : 0, -detail::kDbRange,
                                                    detail::kDbRange));   // FUN_00713290
    for (auto& p : presets->arr)
        if (p.num("ActivateOnStart", 0)) out.active.push_back(p.str("Name"));
    for (auto& g : groups->arr) {
        const std::string name = g.str("Name");
        if (name.empty()) continue;
        const double sum = detail::groupDb(name, *presets, out.pitchSum);
        const double v = detail::fromDb(std::clamp(sum, -detail::kDbRange, detail::kDbRange)) * master;
        out.vol[name] = (float)std::clamp(v, 0.0, 1.0);
    }
    if (gs) out.pitchSum += fabs(gs->num("MasterPitch", 0));
    return true;
}

}  // namespace di1::mixer

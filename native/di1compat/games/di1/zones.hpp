// zones10: 1.0's playsets in a 3.0 zone list, their currencies and zone prefixes. Windows, codec headers. Any thread.
#pragma once
#include <windows.h>
#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../../gameplay.hpp"
#include "../../luachunk.hpp"

namespace di1 {

// A 1.0 playset's start world is in the 1.0 install: assets\worlds\<zone>.zip or the unpacked folder.
inline bool worldInstalled(const char* install10, const std::string& zone)
{
    std::string w = std::string(install10) + "\\assets\\worlds\\" + zone;
    const bool zip = GetFileAttributesA((w + ".zip").c_str()) != INVALID_FILE_ATTRIBUTES;
    return zip || GetFileAttributesA(w.c_str()) != INVALID_FILE_ATTRIBUTES;
}

struct Zones10Options {
    bool add = false;          // zones10: register the 1.0 playsets 3.0's list lacks (off: report them only)
    bool currencies = false;   // currency10: also each playset's currency
    std::function<bool(const std::string& zone)> world;   // a zone's world is installed
};

struct Zones10 {
    std::vector<gameplay::Playset10> playsets;
    std::map<std::string, std::string> currencies;   // currency10 only
};

// t: the 3.0 zone list (records added to it); zoneList10: 1.0's chunk. Throws when the chunk does not evaluate.
inline Zones10 zones10(std::string& t, const std::string& zoneList10, const Zones10Options& o)
{
    Zones10 z;
    auto g = luachunk::eval(zoneList10);
    z.playsets = gameplay::playsets10(t, g["ZoneList"], o.add, o.world);
    if (o.currencies) z.currencies = gameplay::currencies10(g["ZoneList"]);
    return z;
}

// The playsets' zone name prefixes ("IN_", "MU_" ...: their missions' names start so), first seen first.
inline std::vector<std::string> zonePrefixes(const std::vector<gameplay::Playset10>& playsets)
{
    std::vector<std::string> pre;
    for (auto& x : playsets)
        for (auto& zone : x.zones) {
            size_t us = zone.find('_');
            if (us == std::string::npos) continue;
            std::string p = zone.substr(0, us + 1);
            if (std::find(pre.begin(), pre.end(), p) == pre.end()) pre.push_back(p);
        }
    return pre;
}

}  // namespace di1

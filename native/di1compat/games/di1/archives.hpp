// The 1.0 install's zips: where a file is and the search order (M8). Strings only, no engine, log or config.
#pragma once
#include <array>
#include <cstddef>
#include <string>

namespace di1 {

// A 1.0 archive by its path under assets (worlds/in_city.zip) as a file path.
inline std::string archivePath(const char* install10, const std::string& rel)
{
    std::string full = std::string(install10) + "\\assets\\" + rel;
    for (char& c : full) if (c == '/') c = '\\';
    return full;
}

enum class AssetBuffers { Skip, Search };   // assetbufs10: a buffer member is also looked for in its asset's archive

namespace detail {

// 1.0 shards choreographies and exploders by name: choreographies\bhs_beam1 is in choreographies/bh/bhs_beam1.zip
inline std::string shardArchive(const std::string& p, const std::string& dir, const std::string& stem)
{
    size_t top = p.find('/'), last = dir.rfind('/');
    std::string asset = last == std::string::npos ? stem : dir.substr(last + 1);
    // a top-level buffer's shard is its asset's: exploders\inc_rubble_0.vbuf is in exploders/in/inc_rubble.zip
    const bool buffer0 = asset.size() > 2 && asset.compare(asset.size() - 2, 2, "_0") == 0;
    if (buffer0 && last == std::string::npos) asset.resize(asset.size() - 2);
    if (asset.size() < 2) return dir + ".zip";
    return p.substr(0, top) + "/" + asset.substr(0, 2) + "/" + asset + ".zip";
}

// A buffer's asset: kiosk_in_0.vbuf is in kiosk_in.zip (read10sim.py: 3,772 of 3,772 characters/ buffers found).
inline std::string bufferAsset(const std::string& stem)
{
    size_t us = stem.find_last_of('_');
    const bool digits = us != std::string::npos && us + 1 < stem.size() &&
                        stem.find_first_not_of("0123456789", us + 1) == std::string::npos;
    return digits ? stem.substr(0, us) : stem;
}

}  // namespace detail

// Archives that may hold p's member after the '/' at cut, in search order (first hit wins); "" is none.
inline std::array<std::string, 5> candidateArchives(const std::string& p, size_t cut, AssetBuffers buffers)
{
    std::string dir = p.substr(0, cut), member = p.substr(cut + 1);
    std::string stem = member.substr(0, member.find('.'));
    std::string shard = detail::shardArchive(p, dir, stem);
    std::string bufAsset = detail::bufferAsset(stem);
    // <dir>.zip, the folder-named one (gamedb/core/core.zip), the member's (inc_bob/inc_bob_ps.zip), the shard
    return {dir + ".zip", dir + "/" + dir.substr(dir.rfind('/') + 1) + ".zip", dir + "/" + stem + ".zip", shard,
            buffers == AssetBuffers::Search ? dir + "/" + bufAsset + ".zip" : std::string()};
}

// 1.0's root archives (members under full paths), searched in this order after every split.
inline constexpr const char* kRootArchives[] = {"startup.zip", "startup2.zip", "startup3.zip", "startup4.zip",
                                                "startup5.zip"};

}  // namespace di1

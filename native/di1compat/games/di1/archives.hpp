// Where the 1.0 install keeps a file (CLAUDE.md M8): its zips under <install>\assets, in extract.py's layout, and
// the order they are searched in. Strings in, strings out: no engine, log or config. Standard library only.
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

// The archives that may hold member p.substr(cut + 1) of path p split at the '/' at cut, in search order: the first
// that holds it wins; an empty name is no candidate. read10 tries every split of p, the deepest first.
inline std::array<std::string, 5> candidateArchives(const std::string& p, size_t cut, AssetBuffers buffers)
{
    std::string dir = p.substr(0, cut), member = p.substr(cut + 1);
    // <dir>.zip (worlds/in_city.zip), a folder-named archive inside <dir> (gamedb/core/core.zip,
    // shaders/shaders.zip: the engine path leaves the archive's own name out), or a per-asset archive inside
    // <dir> named after the member (characters/inc_bob/inc_bob_ps.zip holds inc_bob_ps.oct)
    std::string stem = member.substr(0, member.find('.'));
    // 1.0 shards choreographies and exploders by the name's first two letters, one flat archive per asset:
    // choreographies\bhs_beam1\bhs_beam1.oct is choreographies/bh/bhs_beam1.zip, exploders\inc_rubble_0.vbuf
    // is exploders/in/inc_rubble.zip (in-game 2026-10-13: the avatar's DNA pulls 1.0 choreographies)
    size_t top = p.find('/'), last = dir.rfind('/');
    std::string asset = last == std::string::npos ? stem : dir.substr(last + 1);
    if (asset.size() > 2 && asset.compare(asset.size() - 2, 2, "_0") == 0 && last == std::string::npos) asset.resize(asset.size() - 2);
    std::string shard = asset.size() >= 2 ? p.substr(0, top) + "/" + asset.substr(0, 2) + "/" + asset + ".zip" : dir + ".zip";
    // a buffer member (<asset>_0.vbuf) lives in its asset's archive in a shared folder too: characters/omnidroid/
    // omnidroid_pieces.zip holds omnidroid_pieces_0.vbuf, characters/kiosk/kiosk_in_0.vbuf is in kiosk_in.zip
    // (in-game 2026-10-13: 7 1.0 models served without geometry, all 14 MISSING buffers had this form;
    // read10sim.py: 3,772 of 3,772 characters/ buffers found, 630 before; bufpaths.py: 5,234 such buffers).
    // npc and gp each added this candidate: one candidate, npc's rule (a superset of gp's .vbuf/.ibuf rule), gp's key.
    size_t us = stem.find_last_of('_');
    std::string bufAsset = us != std::string::npos && us + 1 < stem.size() &&
                                   stem.find_first_not_of("0123456789", us + 1) == std::string::npos ? stem.substr(0, us) : stem;
    return {dir + ".zip", dir + "/" + dir.substr(dir.rfind('/') + 1) + ".zip", dir + "/" + stem + ".zip", shard,
            buffers == AssetBuffers::Search ? dir + "/" + bufAsset + ".zip" : std::string()};
}

// 1.0 mounts its root archives at assets\ (members under full paths), searched in this order after every split:
// startup3.zip holds expendables_data/terrainbuildingsexpendables.lua (in-game 2026-10-13: MISSING), startup2.zip the
// core GameDB tables, startup5.zip var/*.bin
inline constexpr const char* kRootArchives[] = {"startup.zip", "startup2.zip", "startup3.zip", "startup4.zip", "startup5.zip"};

}  // namespace di1

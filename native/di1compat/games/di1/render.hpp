// The 1.0 render transcode's inputs and results (rendermtb.hpp, renderoct.hpp). Base and formats only; any thread.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../../base/hash.hpp"
#include "../../formats/rentable.hpp"
#include "../../formats/ssf.hpp"
#include "../../formats/vtxf.hpp"
#include "gen30.hpp"

namespace render {

// 1.0's scenesettingsformat.ssf (FUN_004af580): 4-byte slots, 1.0 .shd post passes, a shader name after each
inline constexpr ssf::Layout kSsfLayout10 = {0x64, 0x1c, 0x40, 4, 4, gen30::kShd10, true};

// The scene-settings slot 3.0 renamed: 1.0's g_colorgrade_lut is 3.0's g_colorgrade_lut_sampler
inline std::map<uint32_t, uint32_t> ssfRenamedSlots10()
{
    return {{hash::ehash("g_colorgrade_lut"), hash::ehash("g_colorgrade_lut_sampler")}};
}

// What the transcode reads and how: readers, cfg policy, the served formats
struct IO {
    std::function<bool(const std::string& path, std::string& out)> read30;   // a 3.0 asset by extract path ('3:...')
    std::function<bool(const std::string& path, std::string& out)> read10;   // a 1.0 asset by path
    std::function<void(const std::string&)> log;
    bool verbose = false;   // log every substituted material's slot assignment
    // Real 1.0 shaders: a material whose shader read10 finds keeps it as 'x'<shader>, with its 1.0 blocks and formats
    bool translate = false;
    // The format has bone indices: 3.0 skins on the GPU first, and no 3.0 layout matches it (FUN_0047c300)
    std::function<bool(uint32_t vformat)> skinned;
    // the shader translates (and is registered): only then may a material take it
    std::function<bool(const std::string& shader)> translatable;
    // Skinned primitives go to their target's 'skin' layout with 1.0 bones (R plans); off: 'main', bones dropped
    bool skinLayout = false;
    // cfg scenesettings10: both games' scenesettingsformat.ssf; a 1.0 SSET chunk is re-laid into 3.0's and kept
    const ssf::Format* ssf10 = nullptr;
    const ssf::Format* ssf30 = nullptr;
    // Re-laid 1.0 formats of the served vformat.vtxf: a translated primitive's trailing colour gets its own stream
    const std::map<uint32_t, vtxf::ColourTail>* tails10 = nullptr;
    // cfg skintr10: skinned-only materials take their translated shader too; vertices still go to skinTarget30
    bool translateSkinned = false;
};

// A skinned 1.0 format's 3.0 target, for shaders that declare none: its first with an R plan (0: none)
inline uint32_t skinTarget30(const rentable::Table& T, uint32_t crc10)
{
    auto t = T.targets.find(crc10);
    if (t != T.targets.end())
        for (uint32_t c : t->second)
            if (T.skinPlans.count({crc10, c})) return c;
    return 0;
}

// mtb, vbuf, ibuf: the 3.0 files; ibuf2: the 32-bit indices (IndexBuffer 2); tiers: materials per tier
struct Result {
    std::string mtb, vbuf, ibuf, ibuf2;
    std::vector<std::string> problems;
    std::map<std::string, int> tiers;
    std::set<std::string> textures10;   // 1.0 texture hashes (hex) the bundle uses
    std::set<int> translated;           // materials on their real (translated) 1.0 shader
    int skinned = 0;                    // primitives written in their target's 'skin' layout (io.skinLayout)
    int sceneSettings = 0;              // SSET blocks re-laid into 3.0's format (io.ssf10/ssf30)
    std::vector<std::pair<std::string, std::string>> shaders;   // per 1.0 material: (its 1.0 shader, the shader served)
};

}  // namespace render

// The render IO 1.0 materials transcode with, one builder for plugin and octtest (OT-11). Readers given (M2).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include "../../gen30.hpp"
#include "../../render.hpp"

namespace di1 {

using Read = std::function<bool(const std::string& path, std::string& out)>;

// A material bundle outside any asset (.mtb, a particle package's): only the readers.
inline render::IO bundleIO(const Read& read10, const Read& read30)
{
    render::IO io;
    io.read30 = read30;
    io.read10 = read10;
    return io;
}

// What a world or model render takes beside the readers; vf10, vf30 and tails10 must outlive the IO.
struct WorldRender {
    bool translate = false;     // shaders10 && translate10: real 1.0 shaders where they translate
    bool skinLayout = false;    // skinlayout10
    bool skinTr = false;        // skintr10: only with skinLayout and 3.0's vertex formats
    const gen30::VFormats* vf10 = nullptr;   // 1.0's vertex formats: those with bone indices are skinned
    const gen30::VFormats* vf30 = nullptr;   // 3.0's, from the served vformat.vtxf
    const std::map<uint32_t, gen30::ColourTail>* tails10 = nullptr;   // the served vtxf's re-laid 1.0 formats
};

inline render::IO worldIO(const WorldRender& w, const Read& read10, const Read& read30)
{
    render::IO io = bundleIO(read10, read30);
    io.translate = w.translate;
    const gen30::VFormats* vf10 = w.vf10;
    io.skinned = [vf10](uint32_t vf) {
        auto f = vf10->find(vf);
        return f != vf10->end() && f->second.count("BLENDINDICES0");
    };
    io.skinLayout = w.skinLayout;
    const bool formats30 = w.vf30 && !w.vf30->empty();
    io.translateSkinned = w.skinTr && w.skinLayout && formats30;
    io.tails10 = w.tails10;
    return io;
}

// Both games' scenesettingsformat.ssf, read and parsed (render.hpp ssfBlock30); false if either is missing or bad.
inline bool sceneSettings(const Read& read10, const Read& read30, render::Ssf& f10, render::Ssf& f30)
{
    std::string a, b;
    if (!read10("shaders/shaders/scenesettingsformat.ssf", a)) return false;
    if (!read30("3:shaders/shaders/scenesettingsformat.ssf", b)) return false;
    return render::parseSsf(a, false, f10) && render::parseSsf(b, true, f30);
}

}  // namespace di1

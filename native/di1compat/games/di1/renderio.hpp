// The render IO 1.0 materials transcode with, one builder for plugin and octtest (OT-11). Readers given (M2).
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include "../../formats/ssf.hpp"
#include "../../formats/vtxf.hpp"
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
    const vtxf::VFormats* vf10 = nullptr;   // 1.0's vertex formats: those with bone indices are skinned
    const vtxf::VFormats* vf30 = nullptr;   // 3.0's, from the served vformat.vtxf
    const std::map<uint32_t, vtxf::ColourTail>* tails10 = nullptr;   // the served vtxf's re-laid 1.0 formats
};

inline render::IO worldIO(const WorldRender& w, const Read& read10, const Read& read30)
{
    render::IO io = bundleIO(read10, read30);
    io.translate = w.translate;
    const vtxf::VFormats* vf10 = w.vf10;
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

// Both games' scenesettingsformat.ssf, read and parsed (ssf::relayBlock); false if either is missing or bad.
inline bool sceneSettings(const Read& read10, const Read& read30, ssf::Format& f10, ssf::Format& f30)
{
    std::string a, b;
    if (!read10("shaders/shaders/scenesettingsformat.ssf", a)) return false;
    if (!read30("3:shaders/shaders/scenesettingsformat.ssf", b)) return false;
    return ssf::parse(a, render::kSsfLayout10, f10) && ssf::parse(b, ssf::kLayout30, f30);
}

}  // namespace di1

// 1.0 rendering: vertex and scene formats, shaders10's compiler, world and particle renders. games/di1, engine.
// Loader threads.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include "../../core/log.hpp"
#include "../../core/paths.hpp"
#include "../../engine/assets.hpp"
#include "../../engine/shaders.hpp"
#include "../../formats/oct.hpp"
#include "../../formats/ssf.hpp"
#include "../../formats/vtxf.hpp"
#include "config.hpp"
#include "gen30.hpp"
#include "options.hpp"
#include "render.hpp"
#include "renderio.hpp"
#include "renderoct.hpp"
#include "session.hpp"
#include "skin.hpp"
#include "tables.hpp"
#include "transcode.hpp"
#include "vfs.hpp"

namespace di1::render10 {

// Keeps c; with shaders10, reads 1.0's vertex formats and loads the HLSL compiler, before any hook.
inline bool install(const Config& c);
// shaders10's compiler and DXBC cache folder; empty without shaders10.
inline const engine::shaders::Compiler& compiler();
// vformat.vtxf: 3.0's formats plus the 1.0 formats 3.0 lacks (vtxf::merge). Read at boot, before any transcode.
inline std::string mergeVtxf(const std::string& t30, const std::string& t10);
// The render half of a 1.0 asset with a material bundle: materials onto 3.0 shaders, vertices into their formats.
inline bool transcodeAssetRender(const std::string& p, oct::File& f, std::string& note);
// vfxchan10: a particle package's shaders; its served .mtb is kept for the engine's request right after.
inline Shaders particleShaders(const std::string& pd, std::string& note);
// The 1.0 shader after name's 'x' as a 3.0 one (gen30: render table V/W/Q lines); false when it has problems.
inline bool generateShader10(const char* name, const engine::shaders::Compile& compile,
                             engine::shaders::Generated& out);

namespace detail {

constexpr size_t kOctSuffix = sizeof(".oct") - 1;

struct State {
    Config cfg;                                  // set once in install()
    engine::shaders::Compiler compiler;          // set once in install()
    vtxf::VFormats vf10;                         // 1.0's vertex formats: set once in install()
    vtxf::VFormats vf30;                         // 3.0's: set once by mergeVtxf at renderer init (A-17)
    std::map<uint32_t, vtxf::ColourTail> tails10;   // the served vtxf's re-laid 1.0 formats: set with vf30
    std::mutex sceneMx;                          // guards sceneDone, sceneOk, ssf10 and ssf30
    bool sceneDone = false;                      // the formats were read (once, at the first render)
    bool sceneOk = false;                        // both parsed
    ssf::Format ssf10, ssf30;                    // both games' scenesettingsformat.ssf
    std::once_flag skinOnce;                     // skin10: skinLayouts filled at the first skinned render
    skin::SkinLayouts skinLayouts;
};
inline State g_state;

// skin10: a 1.0 asset's InfluenceRemap palettes on its vertex buffer (419 models: skincheck2.py). -> the note.
inline std::string skin10(oct::File& f, std::string& vbuf, const std::string& ibuf)
{
    State& s = g_state;
    std::call_once(s.skinOnce, [&s] {
        std::string t;
        if (vfs::read10s("shaders/vformat.vtxf", t)) s.skinLayouts = skin::skinLayouts(t);
        core::logf("skin10: %u skinned 1.0 vertex formats", (unsigned)s.skinLayouts.size());
    });
    if (vbuf.empty() || ibuf.empty() || s.skinLayouts.empty()) return "";
    std::string why;
    int n = skin::skinRemap(f, vbuf, ibuf, s.skinLayouts, &why);
    if (!n && why.empty()) return "";
    return "+skinremap(" + std::to_string(n) + (why.empty() ? "" : ": " + why) + ") ";
}

// Both games' scenesettingsformat.ssf (ssf::relayBlock), parsed at the first render; both or neither (scenesettings10).
inline void sceneSettingsIO(render::IO& io)
{
    State& s = g_state;
    std::lock_guard<std::mutex> lock(s.sceneMx);
    if (!s.sceneDone) {
        s.sceneDone = true;
        s.sceneOk = sceneSettings(vfs::read10s, engine::assets::read30x, s.ssf10, s.ssf30);
        core::logf("scene settings formats: %s (1.0 block %d B, 3.0 block %d B)", s.sceneOk ? "ok" : "MISSING",
                   s.ssf10.size, s.ssf30.size);
    }
    if (!s.sceneOk || !s.cfg.sceneSettings10) return;
    io.ssf10 = &s.ssf10;
    io.ssf30 = &s.ssf30;
}

// A world render's IO: c's switches, the formats, the readers, its log and the translatable test.
inline render::IO worldRenderIO()
{
    State& s = g_state;
    WorldRender w = worldRenderOf(s.cfg);
    w.vf10 = &s.vf10;
    w.vf30 = &s.vf30;
    w.tails10 = &s.tails10;
    render::IO io = worldIO(w, vfs::read10s, engine::assets::read30x);
    io.log = [](const std::string& line) { core::logf("%s", line.c_str()); };
    io.translatable = [](const std::string& sh) { return engine::shaders::make(("x" + sh).c_str()) != nullptr; };
    sceneSettingsIO(io);
    return io;
}

// The rendered bundle's files for the engine's next opens: .mtb always, the buffers when written.
inline void keepRendered(const std::string& base, render::Result& r)
{
    session::Files files = {{base + ".mtb", std::move(r.mtb)}};
    if (!r.vbuf.empty()) files.push_back({base + "_0.vbuf", std::move(r.vbuf)});
    if (!r.ibuf.empty()) files.push_back({base + "_0.ibuf", std::move(r.ibuf)});
    if (!r.ibuf2.empty()) files.push_back({base + "_2.ibuf", std::move(r.ibuf2)});
    session::keep(std::move(files));
}

}  // namespace detail

inline const engine::shaders::Compiler& compiler() { return detail::g_state.compiler; }

inline std::string mergeVtxf(const std::string& t30, const std::string& t10)
{
    detail::State& s = detail::g_state;
    int n = 0;
    std::string m = vtxf::merge(t30, t10, n, s.tails10);
    s.vf30 = vtxf::formats(t30);
    core::logf("shaders10: vformat.vtxf + %d 1.0 formats, %u with their vertex colour in its own stream", n,
               (unsigned)s.tails10.size());
    return m;
}

inline bool transcodeAssetRender(const std::string& p, oct::File& f, std::string& note)
{
    if (!tables::renderOk()) {
        note += "render table missing ";
        return false;
    }
    size_t b = p.rfind('/');
    if (b == std::string::npos) return false;
    std::string dir = p.substr(0, b), stem = p.substr(b + 1, p.size() - b - 1 - detail::kOctSuffix);
    std::string mtb, vbuf, ibuf;
    if (!vfs::read10s(dir + "/" + stem + ".mtb", mtb)) return false;   // nothing to render
    vfs::read10s(dir + "/" + stem + "_0.vbuf", vbuf);
    vfs::read10s(dir + "/" + stem + "_0.ibuf", ibuf);
    if (detail::g_state.cfg.skin10) note += detail::skin10(f, vbuf, ibuf);
    render::IO io = detail::worldRenderIO();
    render::Result r;
    if (!render::transcodeWorld(tables::render(), io, f, {stem, mtb, vbuf, ibuf}, r)) {
        for (auto& pr : r.problems) core::logf("render problem %s: %s", p.c_str(), pr.c_str());
        note += "render FAILED ";
        return false;
    }
    detail::keepRendered(dir + "/" + stem, r);
    // at most 179 characters (60 fixed, five numbers, both tails): never cut
    char buf[200];
    snprintf(buf, sizeof(buf), "+render(translated %d real %d same-sh %d generic %d, %u 1.0 textures%s%s) ",
             r.tiers["translated"], r.tiers["real"], r.tiers["same-sh"], r.tiers["generic"],
             (unsigned)r.textures10.size(),
             r.skinned ? (", " + std::to_string(r.skinned) + " skinned in skin layout").c_str() : "",
             r.sceneSettings ? (", " + std::to_string(r.sceneSettings) + " scene settings").c_str() : "");
    note += buf;
    return true;
}

inline Shaders particleShaders(const std::string& pd, std::string& note)
{
    if (!tables::renderOk()) return {};
    std::string mp = pd.substr(0, pd.size() - strlen(".particledata")) + ".mtb";
    // the .mtb beside it, or the overlay member the alias names
    const std::string over = session::overlayMember(mp);
    std::string mtb;
    if (!vfs::read10s(over.empty() ? mp : over, mtb)) return {};
    ParticleBundle b = particleBundle(tables::render(), bundleIO(vfs::read10s, engine::assets::read30x), mp, mtb, note);
    if (!b.ok) return {};
    session::keepFirst(mp, std::move(b.mtb));
    return b.shaders;
}

inline bool generateShader10(const char* name, const engine::shaders::Compile& compile,
                             engine::shaders::Generated& out)
{
    detail::State& s = detail::g_state;
    gen30::Generated g;
    std::function<uint32_t(uint32_t)> skinTarget;
    if (s.cfg.skinTr10 && s.cfg.skinLayout10 && !s.vf30.empty())
        skinTarget = [](uint32_t vf) { return render::skinTarget30(tables::render(), vf); };
    gen30::generate(tables::render().shaderRules, {vfs::read10s, s.vf10, compile, skinTarget, &s.vf30},
                    std::string(name + 1), g);
    for (auto& pr : g.problems) core::logf("shaders10 %s: %s", name, pr.c_str());
    if (g.shd.empty() || !g.problems.empty()) return false;
    out.shd = std::move(g.shd);
    for (gen30::Program& p : g.programs) out.programs.push_back({p.id, p.ps, std::move(p.code)});
    return true;
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.cfg = c;
    if (!c.shaders10) return true;
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    s.compiler.compile = dc ? (engine::shaders::D3DCompileFn)GetProcAddress(dc, "D3DCompile") : nullptr;
    s.compiler.cacheDir = core::pluginDir() + "\\di1compat_shaders";
    CreateDirectoryA(s.compiler.cacheDir.c_str(), nullptr);
    std::string v10;
    if (vfs::read10s("shaders/vformat.vtxf", v10)) s.vf10 = vtxf::formats(v10);
    core::logf("shaders10: compiler %s, %u 1.0 vertex formats, %u 3.0 globals, cache %s",
               s.compiler.compile ? "ok" : "MISSING", (unsigned)s.vf10.size(),
               (unsigned)tables::render().shaderRules.globals.size(), s.compiler.cacheDir.c_str());
    return true;
}

}  // namespace di1::render10

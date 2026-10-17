// Render converters for the Python oracles and the golden files: worlds (renderoracle.py), bundles, particles.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "../base/hash.hpp"
#include "../formats/rentable.hpp"
#include "../formats/ssf.hpp"
#include "../formats/vtxf.hpp"
#include "../games/di1/chan.hpp"
#include "../games/di1/gen30.hpp"
#include "../games/di1/render.hpp"
#include "../games/di1/renderio.hpp"
#include "../games/di1/renderoct.hpp"
#include "../games/di1/transcode.hpp"
#include "../games/di1/vfx.hpp"
#include "files.hpp"
#include "modes.hpp"
#include "run.hpp"
#include "shadercompile.hpp"
#include "tuples.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

bool loadTable(rentable::Table& t, const char* path)
{
    if (t.load(slurp(path))) return true;
    printf("table load failed\n");
    return false;
}

void printLine(const std::string& l) { printf("%s\n", l.c_str()); }

// D3DCompile's code and message blobs released once read (OT-24).
void releaseBlobs(void* code, void* msg)
{
    if (code) releaseBlob(code);
    if (msg) releaseBlob(msg);
}

// ---- --render ----

// The 3.0 donors: from the extract, or with ARC30_GAME=<3.0 install> from its archives as the plugin reads them.
struct Reads30 {
    const char* game;
    fs::path assets30;
    int reads = 0;
    double readMs = 0;
    Clock::time_point start;   // the render's, for its time
    bool read(const std::string& p, std::string& o)   // p: '3:<extract path>'
    {
        if (!game) {
            o = slurp(assets30 / p.substr(kPrefix30));
            return !o.empty();
        }
        auto t = Clock::now();
        std::string rel = p.substr(kPrefix30);
        size_t cut = rel.rfind('/');
        bool ok = cut != std::string::npos &&
                  readArc30(std::string(game) + "/assets/" + rel.substr(0, cut) + ".zip", rel.substr(cut + 1), o);
        readMs += msSince(t);
        ++reads;
        return ok;
    }
};

// RENDER_GENCHECK=<cache dir>: a shader translates only if the plugin's generator makes it and every program compiles.
struct GenCheck {
    const rentable::Table& t;
    const render::IO& io;
    const vtxf::VFormats& vf10;
    const vtxf::VFormats& vf30;
    fs::path cacheDir;
    CompileFn compile = nullptr;
    std::map<std::string, bool> made;   // shader -> translatable, each generated once

    bool compiled(const std::string& hlsl, const char* target, std::string& code, std::string& msg)
    {
        fs::path path = cacheDir / hash::shaderCacheName(target, hlsl);   // the plugin's cache key
        code = slurp(path);
        if (code.size() > kCachedMin) return true;
        void *c = nullptr, *e = nullptr;
        const bool failed = !compile || FAILED(compile(hlsl.data(), hlsl.size(), "di1compat", nullptr, nullptr, "main",
                                                       target, 0, 0, &c, &e));
        if (failed || !c) {
            if (e) msg.assign(blobPointer(e));
            releaseBlobs(c, e);
            return false;
        }
        code.assign(blobPointer(c), blobSize(c));
        releaseBlobs(c, e);
        fs::create_directories(cacheDir);
        writeFile(path, code);
        return true;
    }

    bool translatable(const std::string& sh)
    {
        auto m = made.find(sh);
        if (m != made.end()) return m->second;
        gen30::Read rd = [this](const std::string& p, std::string& o) { return io.read10(p, o); };
        std::function<uint32_t(uint32_t)> st;
        if (io.translateSkinned) st = [this](uint32_t vf) { return render::skinTarget30(t, vf); };
        gen30::Compile comp = [this](const std::string& hlsl, const char* target, std::string& code, std::string& msg) {
            return compiled(hlsl, target, code, msg);
        };
        gen30::Generated g;
        gen30::generate(t.shaderRules, {rd, vf10, comp, st, &vf30}, sh, g);
        for (auto& p : g.problems) printf("shaders10 x%s: %s\n", sh.c_str(), p.c_str());
        return made[sh] = !g.shd.empty() && g.problems.empty();
    }
};

// The world's files written to <out>/<stem>*, then what the transcode did.
void writeWorld(const fs::path& out, const std::string& stem, const oct::File& f, const render::Result& r)
{
    fs::create_directories(out);
    writeFile(out / (stem + ".oct"), f.write());
    writeFile(out / (stem + ".mtb"), r.mtb);
    writeFile(out / (stem + "_0.vbuf"), r.vbuf);
    writeFile(out / (stem + "_0.ibuf"), r.ibuf);
    if (!r.ibuf2.empty()) writeFile(out / (stem + "_2.ibuf"), r.ibuf2);
    for (auto& [t, n] : r.tiers) printf("tier %s %d\n", t.c_str(), n);
    if (r.skinned) printf("skinned in skin layout %d\n", r.skinned);
    if (r.sceneSettings) printf("scene settings %d\n", r.sceneSettings);
    printf("wrote %s: oct %zu, mtb %zu, vbuf %zu, ibuf %zu, ibuf2 %zu, 1.0 textures %zu\n", out.string().c_str(),
           f.write().size(), r.mtb.size(), r.vbuf.size(), r.ibuf.size(), r.ibuf2.size(), r.textures10.size());
}

// The world's .oct and buffers through render::transcodeWorld; problems printed, false when it refused.
bool renderWorld(const rentable::Table& T, const render::IO& io, const fs::path& world, const Reads30& reads,
                 const fs::path& out)
{
    const std::string stem = world.filename().string();
    std::string ob = slurp(world / (stem + ".oct"));
    oct::File f = parse(ob);
    render::Result r;
    bool ok = render::transcodeWorld(T, io, f, {stem, slurp(world / (stem + ".mtb")), slurp(world / (stem + "_0.vbuf")),
                                                slurp(world / (stem + "_0.ibuf"))}, r);
    printf("render %.0f ms, %d 3.0 reads %.0f ms\n", msSince(reads.start), reads.reads, reads.readMs);
    for (auto& p : r.problems) printf("problem: %s\n", p.c_str());
    if (!ok) return false;
    writeWorld(out, stem, f, r);
    return true;
}

// ---- --pd ----

// The plugin's vfxchan10 inputs: the package's .mtb through transcodeBundle, chan::layout per material.
struct PdChannels {
    chan::Read r10, r30;
    std::vector<std::pair<std::string, std::string>> shaders;   // per material: 1.0 and 3.0 shader
    chan::Roles roles;
};

// argv[4..7]: render table, extract roots, the package's engine path. 0, or the mode's exit code.
int pdBundle(const Args& a, PdChannels& pc)
{
    rentable::Table T;
    if (!loadTable(T, a.argv[4])) return 2;
    constexpr char kSuffix[] = ".particledata";
    constexpr size_t kSuffixLen = sizeof kSuffix - 1;
    std::string p = a.argv[7], note;
    std::string mtbFile = std::string(a.argv[2]).substr(0, strlen(a.argv[2]) - kSuffixLen) + ".mtb";
    std::string mp = p.substr(0, p.size() - kSuffixLen) + ".mtb";
    di1::ParticleBundle pb = di1::particleBundle(T, di1::bundleIO(pc.r10, pc.r30), mp, slurp(mtbFile), note);
    if (!pb.ok) {
        printf("bundle FAILED\n");
        return 1;
    }
    pc.shaders = pb.shaders;
    return 0;
}

}  // namespace

// The C++ render transcode of one 1.0 world over the extracts, its cfg keys from the environment (OT-11).
int convertRender(const Args& a)
{
    fs::path world = a.argv[2], out = a.argv[6];
    rentable::Table T;
    if (!loadTable(T, a.argv[3])) return 2;
    fs::path ext30 = a.argv[4], ext10 = a.argv[5];
    Reads30 reads{env("ARC30_GAME"), ext30 / "assets"};
    di1::Read read30 = [&reads](const std::string& p, std::string& o) { return reads.read(p, o); };
    di1::Read read10 = readerAt(ext10 / "assets");
    vtxf::VFormats vf10 = vtxf::formats(vformatFile(ext10)), vf30 = vtxf::formats(vformatFile(ext30));
    std::map<uint32_t, vtxf::ColourTail> tails;   // as the plugin serves vformat.vtxf
    int added = 0;
    vtxf::merge(vformatFile(ext30), vformatFile(ext10), added, tails);
    di1::WorldRender w;
    w.translate = env("RENDER_TRANSLATE") != nullptr;   // real 1.0 shaders (read10 finds shaders/shaders/<x>.shd)
    w.skinLayout = env("RENDER_SKIN") != nullptr;       // cfg skinlayout10 (the Python oracle keeps 'main')
    w.skinTr = env("RENDER_SKINTR") != nullptr;         // cfg skintr10
    w.vf10 = &vf10;
    w.vf30 = &vf30;
    w.tails10 = &tails;
    render::IO io = di1::worldIO(w, read10, read30);
    io.verbose = env("RENDER_VERBOSE") != nullptr;
    io.log = printLine;
    ssf::Format ssf10, ssf30;   // cfg scenesettings10 (RENDER_SSF): SSET re-laid and kept
    if (env("RENDER_SSF") && di1::sceneSettings(read10, read30, ssf10, ssf30)) {
        io.ssf10 = &ssf10;
        io.ssf30 = &ssf30;
    }
    reads.start = Clock::now();
    GenCheck gen{T, io, vf10, vf30, env("RENDER_GENCHECK") ? env("RENDER_GENCHECK") : ""};
    if (env("RENDER_GENCHECK")) {
        gen.compile = d3dCompiler();
        io.translatable = [&gen](const std::string& sh) { return gen.translatable(sh); };
    }
    return renderWorld(T, io, world, reads, out) ? 0 : 1;
}

// A stand-alone bundle as the plugin serves it (transcode() .mtb: donors from the engine path's 3.0 folder).
int convertMtb(const Args& a)
{
    rentable::Table T;
    if (!loadTable(T, a.argv[2])) return 2;
    fs::path ext30 = a.argv[3], ext10 = a.argv[4];
    std::string p = a.argv[6];
    render::IO io = di1::bundleIO(readerAt(ext10 / "assets"), reader30At(ext30 / "assets"));
    io.verbose = env("RENDER_VERBOSE") != nullptr;
    io.log = printLine;
    render::Result r;
    bool ok = render::transcodeBundle(T, io, slurp(a.argv[5]), "3:" + p.substr(0, p.find('/') + 1), r);
    for (auto& pr : r.problems) printf("problem: %s\n", pr.c_str());
    int n = 0;
    for (auto& [t, c] : r.tiers) {
        printf("tier %s %d\n", t.c_str(), c);
        n += c;
    }
    printf("%s: %d materials, %zu 1.0 textures, %zu B\n", ok ? "ok" : "FAILED", n, r.textures10.size(), r.mtb.size());
    if (ok && a.argc > 7) writeFile(a.argv[7], r.mtb);   // [out .mtb]
    return ok ? 0 : 1;
}

// vfx.hpp's transcode of a package or mesh (particles.py checks the bytes); with argv[4..7], vfxchan10 too.
int convertPd(const Args& a)
{
    std::string in = slurp(a.argv[2]), out;
    vfx::Stats st;
    vfx::ChanMap cm;
    std::string ext30 = a.argc > 5 ? a.argv[5] : "", ext10 = a.argc > 6 ? a.argv[6] : "";
    PdChannels pc{readerAt(fs::path(ext10) / "assets"), reader30At(fs::path(ext30) / "assets"), {}};
    if (a.argc > 7 && vfx::isPd10(in)) {
        if (int e = pdBundle(a, pc)) return e;
        cm = [&pc](uint16_t mat, int n10, std::vector<int>& chans) {
            return mat < pc.shaders.size() && chan::layout(pc.roles, {pc.r10, pc.r30}, pc.shaders[mat], n10, chans);
        };
    }
    bool ok = vfx::isPd10(in) ? vfx::transcode(in, out, &st, &cm) : vfx::p3d30(in, out);
    if (!ok) {
        printf("not a 1.0 particle package or mesh\n");
        return 1;
    }
    printf("%u -> %u B, %d emitters %d effects %d params %d keys %d mesh emitters, %d re-channeled (%d constant "
           "channels)\n", (unsigned)in.size(), (unsigned)out.size(), st.emitters, st.effects, st.params, st.keys,
           st.meshes, st.rechanneled, st.constChans);
    writeFile(a.argv[3], out);
    return 0;
}

}  // namespace octtest

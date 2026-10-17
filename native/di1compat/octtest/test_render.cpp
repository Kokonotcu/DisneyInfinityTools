// Render acceptance tests: gen30 against gen30.py, the shader cache warm-up, arc30 reads, chan::layout against 3.0.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"
#include "../base/hash.hpp"
#include "../formats/bndl.hpp"
#include "../formats/particledata.hpp"
#include "../formats/rentable.hpp"
#include "../formats/vtxf.hpp"
#include "../games/di1/chan.hpp"
#include "../games/di1/gen30.hpp"
#include "../games/di1/render.hpp"
#include "../games/di1/vfx.hpp"
#include "files.hpp"
#include "modes.hpp"
#include "run.hpp"
#include "shadercompile.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

bool loadTable(rentable::Table& t, const char* path)
{
    if (t.load(slurp(path))) return true;
    printf("table load failed\n");
    return false;
}

// ---- --gen30 ----

constexpr size_t kShownHlsl = 3, kShownShaders = 5;

struct Gen30Run {
    const rentable::Table& t;
    gen30::Read read10;
    const vtxf::VFormats& vf;
    fs::path dump;
    size_t ok = 0, bad = 0, progs = 0;
};

std::string programFile(const std::string& sh, uint32_t id)
{
    char hex[16];
    snprintf(hex, sizeof hex, "%08x", id);
    return sh + "." + hex + ".hlsl";
}

// One shader gen30.py dumped: the .shd and each program's HLSL must match.
void gen30Shader(Gen30Run& r, const fs::path& shd)
{
    std::string sh = shd.stem().string();
    gen30::Generated g;
    gen30::generate(r.t.shaderRules, {r.read10, r.vf, nullptr}, sh, g);
    bool same = g.shd == slurp(shd);
    for (auto& p : g.programs) {
        const std::string nm = programFile(sh, p.id);
        ++r.progs;
        if (const char* d = env("GEN30_DUMP")) writeFile(fs::path(d) / nm, p.hlsl);
        if (p.hlsl == slurp(r.dump / nm)) continue;
        if (same && r.bad < kShownHlsl) printf("HLSL differs: %s\n", nm.c_str());
        same = false;
    }
    if (same) {
        ++r.ok;
        return;
    }
    if (const size_t n = ++r.bad; n <= kShownShaders)
        printf("MISMATCH %s (shd %s)\n", sh.c_str(), g.shd == slurp(shd) ? "same" : "differs");
}

// ---- --warm ----

constexpr size_t kShownProblems = 10;

// The plugin's shader cache filled through D3DCompile: cached, compiled and failed programs counted.
struct Warm {
    CompileFn compile;
    fs::path cache;
    size_t compiled = 0, cached = 0, failed = 0;
};

std::string takeBlob(void* b)
{
    std::string out(blobPointer(b), blobSize(b));
    releaseBlob(b);
    return out;
}

bool warmCompile(Warm& w, const std::string& hlsl, const char* target, std::string& code, std::string& msg)
{
    fs::path path = w.cache / hash::shaderCacheName(target, hlsl);   // the plugin's cache key
    code = slurp(path);
    if (code.size() > kCachedMin) {
        ++w.cached;
        return true;
    }
    void *c = nullptr, *e = nullptr;
    HRESULT hr = w.compile(hlsl.data(), hlsl.size(), "di1compat", nullptr, nullptr, "main", target, 0, 0, &c, &e);
    if (e) msg = takeBlob(e);
    if (FAILED(hr) || !c) {
        ++w.failed;
        return false;
    }
    code = takeBlob(c);
    writeFile(path, code);
    ++w.compiled;
    return true;
}

// The shader list of the MATP chunk at o.
void matpShaders(const std::string& d, size_t o, std::set<std::string>& shaders)
{
    size_t p = bytes::alignUp(o + bndl::detail::kHeader, 4);
    uint32_t ns = bytes::u32OrZero(d, p);
    p += sizeof ns;
    for (uint32_t k = 0; k < ns; ++k) {
        size_t e = d.find('\0', p);
        shaders.insert(d.substr(p, e - p));
        p = e + 1;
    }
}

// The shader names of each bundle's MATP chunk.
std::set<std::string> bundleShaders(const Args& a, int first)
{
    namespace b = bndl::detail;
    std::set<std::string> shaders;
    for (int i = first; i < a.argc; ++i) {
        std::string d = slurp(a.argv[i]);
        size_t o = b::kHeader, nc = d.size() >= b::kHeader ? bytes::u32OrZero(d, b::kCountAt) : 0;
        for (size_t c = 0; c < nc && o + b::kHeader <= d.size(); ++c) {
            if (!d.compare(o, 4, "MATP")) matpShaders(d, o, shaders);
            o += bytes::u32OrZero(d, o + b::kSizeAt);
        }
    }
    return shaders;
}

// ---- --arc ----

constexpr size_t kShownArcDiffs = 10;

struct ArcRun {
    const char* game;
    const char* ext30;
    std::set<std::string> seen;
    size_t ok = 0, bad = 0, skip = 0, n = 0;
};

// One table bundle ('3:<path>') read from the install's archive must equal the extract's file.
void arcCheck(ArcRun& r, const std::string& x)
{
    if (!r.seen.insert(x).second) return;
    if (const size_t i = r.n++; i < r.skip) return;
    std::string rel = x.substr(kPrefix30), got;
    size_t cut = rel.rfind('/');
    fprintf(stderr, "%s\n", x.c_str());
    std::string arc = std::string(r.game) + "/assets/" + rel.substr(0, cut) + ".zip";
    bool ok = readArc30(arc, rel.substr(cut + 1), got);
    fprintf(stderr, "  read %d %zu\n", (int)ok, got.size());
    std::string ext = slurp(fs::path(r.ext30) / "assets" / rel);
    fprintf(stderr, "  extract %zu\n", ext.size());
    if (ok && got == ext) {
        ++r.ok;
        return;
    }
    ++r.bad;
    if (r.bad <= kShownArcDiffs) printf("MISMATCH %s (%s)\n", x.c_str(), ok ? "differs" : "unreadable");
}

// ---- --vfxchan ----

struct PdEmitter {
    std::vector<std::string> keys;   // per channel: its curve key
    std::string shader;              // its material's
};
using Emitters = std::map<uint32_t, PdEmitter>;   // name hash -> emitter

// 3.0's trailer adds two index kinds: counts at 0x14 (12-byte entries) and 0x16 (8-byte entries).
constexpr size_t kKindACount30 = 0x14, kKindBCount30 = 0x16, kKindAEntry30 = 12, kKindBEntry30 = 8;

// Per material its shader name: the MATP shader list, then a u16 index per material (matp.Bundle._matp).
std::vector<std::string> materialShaders(const std::string& mtb)
{
    namespace b = bndl::detail;
    std::vector<std::string> shaders;
    size_t nc = mtb.size() >= b::kHeader ? vfx::rd32(mtb, b::kCountAt) : 0;
    for (size_t o = b::kHeader, c = 0; c < nc && o + b::kHeader <= mtb.size(); ++c) {
        uint32_t cs = vfx::rd32(mtb, o + b::kSizeAt);
        if (!mtb.compare(o, 4, "MATP")) {
            uint32_t nmat = vfx::rd32(mtb, o + b::kCountAt);
            size_t p = bytes::alignUp(o + b::kHeader, 4);
            uint32_t nsh = vfx::rd32(mtb, p);
            p += sizeof nsh;
            std::vector<std::string> sh;
            for (uint32_t i = 0; i < nsh && p < mtb.size(); ++i) {
                sh.push_back(mtb.c_str() + p);
                p += sh.back().size() + 1;
            }
            p = bytes::alignUp(p, 4) + 4;
            for (uint32_t i = 0; i < nmat && p < mtb.size(); ++i) p += strlen(mtb.c_str() + p) + 1;
            p = bytes::alignUp(p, 2);
            for (uint32_t i = 0; i < nmat; ++i) {
                uint16_t k = vfx::rd16(mtb, p + 2 * i);
                shaders.push_back(k < sh.size() ? sh[k] : "");
            }
        }
        if (!cs) break;
        o += cs;
    }
    return shaders;
}

// Every emitter of a package, keyed by name hash: its channel keys and its material's shader.
Emitters pdEmitters(const std::string& pd, const std::string& mtb, bool is30)
{
    Emitters out;
    const size_t tl = is30 ? particledata::kTrailer : vfx::kTrailer10;
    if (pd.size() < tl) return out;
    const size_t t = pd.size() - tl, n = t + vfx::kTrailerCounts;
    uint32_t keys = vfx::rd32(pd, t + vfx::kTrailerKeys);
    uint16_t nkeys = vfx::rd16(pd, n), ne = vfx::rd16(pd, n + 2), nf = vfx::rd16(pd, n + 4), np = vfx::rd16(pd, n + 6);
    size_t extra = is30 ? kKindBEntry30 * (size_t)vfx::rd16(pd, t + kKindBCount30) +
                              kKindAEntry30 * (size_t)vfx::rd16(pd, t + kKindACount30)
                        : 0;
    size_t pe = t - extra - particledata::kEntry * ((size_t)np + nf) - particledata::kEmitterEntry * (size_t)ne;
    const std::vector<std::string> shaders = materialShaders(mtb);
    const size_t cnt = is30 ? particledata::kChannelCount : vfx::kChannelCount10;
    const size_t at = is30 ? particledata::kEmitterRecord : vfx::kEmitter10;
    for (uint16_t i = 0; i < ne; ++i) {
        size_t e = pe + particledata::kEmitterEntry * (size_t)i;
        uint32_t off = vfx::rd32(pd, e), h = vfx::rd32(pd, e + vfx::kEntryHash);
        uint16_t mat = vfx::rd16(pd, e + vfx::kEntryMaterial);
        PdEmitter m;
        m.shader = mat < shaders.size() ? shaders[mat] : "";
        for (int k = 0; k < (uint8_t)pd[off + cnt]; ++k) {
            uint16_t s = vfx::rd16(pd, off + at + 2 * k);
            const size_t key = keys + particledata::kKeyBytes * (size_t)s;
            m.keys.push_back(s < nkeys ? pd.substr(key, particledata::kKeyBytes) : "");
        }
        out.emplace(h, m);
    }
    return out;
}

// Every emitter of an extract's particle packages.
Emitters extractEmitters(const fs::path& assets, bool is30)
{
    Emitters all;
    for (auto& d : fs::directory_iterator(assets / "particles")) {
        const std::string name = d.path().filename().string();
        fs::path pd = d.path() / (name + ".particledata"), mtb = d.path() / (name + ".mtb");
        if (!fs::exists(pd) || !fs::exists(mtb)) continue;
        for (auto& [h, m] : pdEmitters(slurp(pd), slurp(mtb), is30)) all.emplace(h, m);
    }
    return all;
}

// A layout's keys (1.0 keys, Disney's constant key for -1) against the 3.0 emitter's.
void scoreLayout(Tally& c, const std::string& name, const PdEmitter& x, const std::vector<int>& m,
                 const std::vector<std::string>& kb)
{
    std::vector<std::string> got;
    const std::string constant(particledata::kConstantKey, particledata::kKeyBytes);
    for (int s : m) got.push_back(s >= 0 && s < (int)x.keys.size() ? x.keys[s] : constant);
    ++c[name + ": emitters"];
    c[name + ": count = Disney's"] += got.size() == kb.size();
    c[name + ": whole emitter = Disney's"] += got == kb;
    for (size_t j = 0; j < kb.size(); ++j) {
        ++c[name + ": channels"];
        c[name + ": channel key = Disney's"] += j < got.size() && got[j] == kb[j];
    }
}

// One emitter whose shader differs between the games: chan::layout's channels and the 1.0 ones kept.
void vfxEmitter(Tally& c, chan::Roles& roles, const chan::Reads& io, const PdEmitter& x, const PdEmitter& y)
{
    std::vector<int> ours, verb;
    for (int i = 0; i < (int)x.keys.size(); ++i) verb.push_back(i);
    if (!chan::layout(roles, io, {x.shader, y.shader}, (int)x.keys.size(), ours)) {
        std::vector<char> ra = chan::shaderRoles(roles, io.read10, x.shader, chan::kGame10);
        std::vector<char> rb = chan::shaderRoles(roles, io.read30, y.shader, chan::kGame30);
        if (ra.empty() || rb.empty()) {
            ++c["  no channel roles (layout kept)"];
            return;
        }
        ours = verb;   // same layout
    }
    scoreLayout(c, "roles", x, ours, y.keys);
    scoreLayout(c, "1.0 as is", x, verb, y.keys);
}

}  // namespace

// Every shader gen30.py dumped (.shd and <shader>.<program id>.hlsl, compile off): the C++ port must match.
int testGen30(const Args& a)
{
    rentable::Table T;
    if (!loadTable(T, a.argv[2])) return 2;
    fs::path ext10 = fs::path(a.argv[3]) / "assets";
    const vtxf::VFormats vf = vtxf::formats(vformatFile(a.argv[3]));
    Gen30Run r{T, readerAt(ext10), vf, a.argv[4]};
    for (auto& e : fs::directory_iterator(r.dump))
        if (e.path().extension() == ".shd") gen30Shader(r, e.path());
    printf("%zu shaders identical to gen30.py, %zu differ (%zu programs)\n", r.ok, r.bad, r.progs);
    return r.bad ? 1 : 0;
}

// The translated shaders these bundles name, compiled into the plugin's cache (same compiler, same keys).
int testWarm(const Args& a)
{
    rentable::Table T;
    if (!loadTable(T, a.argv[2])) return 2;
    fs::path ext10 = fs::path(a.argv[3]) / "assets";
    Warm w{nullptr, a.argv[4]};
    fs::create_directories(w.cache);
    gen30::Read read10 = readerAt(ext10);
    vtxf::VFormats vf = vtxf::formats(vformatFile(a.argv[3]));
    w.compile = d3dCompiler();
    if (!w.compile) {
        printf("no d3dcompiler_47\n");
        return 2;
    }
    gen30::Compile comp = [&w](const std::string& hlsl, const char* target, std::string& code, std::string& msg) {
        return warmCompile(w, hlsl, target, code, msg);
    };
    const std::set<std::string> shaders = bundleShaders(a, 5);
    size_t probs = 0;
    vtxf::VFormats vf30;   // WARM_EXT30=<3.0 extract root>: cfg skintr10 on, skinned passes take their 3.0 target
    if (const char* e30 = env("WARM_EXT30")) vf30 = vtxf::formats(vformatFile(e30));
    std::function<uint32_t(uint32_t)> st;
    if (!vf30.empty()) st = [&T](uint32_t v) { return render::skinTarget30(T, v); };
    for (auto& sh : shaders) {
        gen30::Generated g;
        gen30::generate(T.shaderRules, {read10, vf, comp, st, &vf30}, sh, g);
        for (auto& pr : g.problems)
            if (const size_t n = ++probs; n <= kShownProblems) printf("  %s: %s\n", sh.c_str(), pr.c_str());
    }
    printf("%zu shaders: %zu programs compiled, %zu already cached, %zu failed, %zu problems\n", shaders.size(),
           w.compiled, w.cached, w.failed, probs);
    return w.failed ? 1 : 0;
}

// Every table bundle read from the install's archive (arc30) must equal the extract's file.
int testArc(const Args& a)
{
    rentable::Table T;
    if (!T.load(slurp(a.argv[2]))) return 2;
    ArcRun r{a.argv[3], a.argv[4]};
    r.skip = env("ARC_SKIP") ? (size_t)atoll(env("ARC_SKIP")) : 0;
    for (auto& [k, x] : T.donors) arcCheck(r, x.extract);
    for (auto& [k, x] : T.matindex) arcCheck(r, x.extract);
    printf("archive reads: %zu equal the extract, %zu not\n", r.ok, r.bad);
    return r.bad ? 1 : 0;
}

// chan::layout against Disney's port: each emitter whose shader differs, its keys against the 3.0 emitter's.
int testVfxChan(const Args& a)
{
    fs::path e10 = fs::path(a.argv[2]) / "assets", e30 = fs::path(a.argv[3]) / "assets";
    chan::Read r10 = readerAt(e10), r30 = reader30At(e30);
    const Emitters A = extractEmitters(e10, false), B = extractEmitters(e30, true);
    Tally c;
    chan::Roles roles;
    for (auto& [h, x] : A) {
        auto y = B.find(h);
        if (y == B.end() || x.shader.empty()) continue;
        const bool sameShader = y->second.shader.empty() || x.shader == y->second.shader;
        if (!sameShader) vfxEmitter(c, roles, {r10, r30}, x, y->second);
    }
    printTally(c, 7);
    return 0;
}

}  // namespace octtest

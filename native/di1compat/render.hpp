// Load-time render transcode of a 1.0 world (plan REN-5 + the geometry half of native30): the 1.0 materials are
// redirected onto 3.0 shaders and the 1.0 vertices into vertex formats those shaders take, in memory. C++ port of
// DI3Tools autobundle.resolve + conv.convert_geometry/build_buffers; octtest --render checks it byte for byte against
// them. The 3.0-side knowledge comes from di1compat_render.txt (rentable.py); the 1.0 data from the 1.0 files.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include "base/bytes.hpp"
#include "base/hash.hpp"
#include "oct.hpp"
#include "gen30.hpp"
#include "formats/bndl.hpp"
#include "formats/rentable.hpp"
#include "formats/ssf.hpp"
#include "formats/vtxf.hpp"

namespace render {

inline double halfToDouble(uint16_t h)
{
    int s = h >> 15, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    double v = e == 0 ? std::ldexp(m, -24) : e == 31 ? (m ? NAN : INFINITY) : std::ldexp(m + 1024, e - 25);
    return s ? -v : v;
}

// CPython PyFloat_Pack2 (struct 'e'): round half to even straight from the double.
inline uint16_t doubleToHalf(double x)
{
    int sign = std::signbit(x) ? 1 : 0;
    if (x == 0.0) return (uint16_t)(sign << 15);
    if (std::isinf(x)) return (uint16_t)((sign << 15) | 0x7c00);
    if (std::isnan(x)) return (uint16_t)((sign << 15) | 0x7e00);
    int e;
    double f = std::frexp(std::fabs(x), &e);
    f *= 2.0; e--;
    if (e >= 16) return (uint16_t)((sign << 15) | 0x7c00);     // overflow (CPython raises; never hit by real data)
    if (e < -25) { f = 0.0; e = 0; }
    else if (e < -14) { f = std::ldexp(f, 14 + e); e = 0; }
    else { e += 15; f -= 1.0; }
    f *= 1024.0;
    unsigned bits = (unsigned)f;
    if ((f - bits > 0.5) || ((f - bits == 0.5) && (bits % 2 == 1))) {
        if (++bits == 1024) { bits = 0; ++e; }
    }
    return (uint16_t)(bits | (e << 10) | (sign << 15));
}

// One element of type v at b -> out[0..v.n).
inline void decode(const vtxf::ElementType& v, const uint8_t* b, double* out)
{
    for (int i = 0; i < v.n; ++i) {
        const uint8_t* p = b + i * v.csize;
        double x;
        switch (v.code) {
        case 'f': { float f; memcpy(&f, p, 4); x = f; break; }
        case 'e': x = halfToDouble((uint16_t)(p[0] | p[1] << 8)); break;
        case 'B': x = p[0]; break;
        case 'h': x = (int16_t)(p[0] | p[1] << 8); break;
        default: x = (uint16_t)(p[0] | p[1] << 8); break;
        }
        if (v.norm) { x /= v.norm; if (x < -1.0) x = -1.0; }
        out[i] = x;
    }
}

// vals[0..v.n) as one element of type v at out (v.n * v.csize bytes).
inline void encode(const vtxf::ElementType& v, const double* vals, char* out)
{
    for (int i = 0; i < v.n; ++i) {
        double x = vals[i];
        if (v.norm) {
            double lo = v.code == 'B' ? 0.0 : -1.0;
            x = std::nearbyint((x < lo ? lo : x > 1.0 ? 1.0 : x) * v.norm);   // Python round(): half to even
        } else if (v.code != 'f' && v.code != 'e') {
            double hi = v.code == 'B' ? 255.0 : 65535.0;
            x = std::trunc(x);
            x = x < 0 ? 0 : x > hi ? hi : x;
        }
        switch (v.code) {
        case 'f': { float f = (float)x; memcpy(out, &f, 4); out += 4; break; }
        case 'e': { uint16_t h = doubleToHalf(x); memcpy(out, &h, 2); out += 2; break; }
        case 'B': *out++ = (char)(int)x; break;
        default: { int16_t s = (int16_t)(int)x; memcpy(out, &s, 2); out += 2; break; }
        }
    }
}

// One vertex through plan p, appended to the destination streams per[0..p.ds.size()). Per vertex of a 52 MB
// world, so no allocation here (the string/vector form cost in_city ~1.5 s of load).
inline void convertVertex(const std::vector<const uint8_t*>& src, const rentable::Plan& p,
                          std::vector<std::string>& per)
{
    for (size_t s = 0; s < p.ds.size(); ++s) per[s].append(p.ds[s], '\0');
    for (const rentable::Op& o : p.ops) {
        std::string& ds = per[o.dsi];
        char* d = &ds[ds.size() - p.ds[o.dsi] + o.doff];
        if (o.kind == 'K') memcpy(d, o.bytes.data(), o.size < (int)o.bytes.size() ? o.size : o.bytes.size());
        else if (o.kind == 'C') memcpy(d, src[o.ssi] + o.soff, o.size);
        else {
            double vals[4] = {0, 0, 0, 0};   // source values, then the default's tail, then zeros (to dv.n)
            decode(o.sv, src[o.ssi] + o.soff, vals);
            for (int i = o.sv.n; i < o.dv.n; ++i) vals[i] = o.dflt.empty() ? 0.0 : i < (int)o.dflt.size() ? o.dflt[i] : 0.0;
            char e[16];
            encode(o.dv, vals, e);
            int n = o.dv.n * o.dv.csize;
            memcpy(d, e, o.size < n ? o.size : n);
        }
    }
}

// 1.0's scenesettingsformat.ssf (FUN_004af580): 4-byte slots, 1.0 .shd post passes, a shader name after each
inline constexpr ssf::Layout kSsfLayout10 = {0x64, 0x1c, 0x40, 4, 4, gen30::kShd10, true};

// The scene-settings slot 3.0 renamed: 1.0's g_colorgrade_lut is 3.0's g_colorgrade_lut_sampler
inline std::map<uint32_t, uint32_t> ssfRenamedSlots10()
{
    return {{hash::ehash("g_colorgrade_lut"), hash::ehash("g_colorgrade_lut_sampler")}};
}

// ---- the world transcode ----
struct IO {
    std::function<bool(const std::string& path, std::string& out)> read30;   // a 3.0 asset by extract path ('3:...')
    std::function<bool(const std::string& path, std::string& out)> read10;   // a 1.0 asset by path
    std::function<void(const std::string&)> log;
    bool verbose = false;   // log every substituted material's slot assignment
    // real 1.0 shaders (gen30.hpp): a material whose 1.0 shader read10 finds keeps it, as 'x'<shader>, with its own
    // blocks (B re-laid for 8-byte slots) and its primitives keep their 1.0 vertex formats
    bool translate = false;
    // a 1.0 vertex format with bone indices: 3.0 skins on the GPU before the material shader (MemExport, six
    // layouts; a raw 1.0 skinned layout matches none -> its job list spins on purpose, FUN_0047c300, in-game
    // 2026-10-13), so materials drawn with one keep the converted path for now
    std::function<bool(uint32_t vformat)> skinned;
    // the shader translates (and is registered): only then may a material take it
    std::function<bool(const std::string& shader)> translatable;
    // a skinned node's converted primitives go into their target's 'skin' layout with the 1.0 bone indices/weights
    // (R plans), as every shipped skinned primitive is stored (3,396 of 3,396; rigid ones 'main', 75,895 of 75,895).
    // Off: the 'main' layout, bones dropped, which the GPU skinning pass then reads as bones (cfg skinlayout10)
    bool skinLayout = false;
    // cfg scenesettings10: both games' scenesettingsformat.ssf; a 1.0 SSET chunk is re-laid into 3.0's and kept
    const ssf::Format* ssf10 = nullptr;
    const ssf::Format* ssf30 = nullptr;
    // the served vformat.vtxf's re-laid 1.0 formats (gen30::mergeVtxf): a translated primitive in one has its
    // trailing vertex colour split into a stream of its own, as that layout says
    const std::map<uint32_t, vtxf::ColourTail>* tails10 = nullptr;
    // cfg skintr10: a material drawn only with skinned formats may take its translated 1.0 shader too. Its vertices
    // still go to their 3.0 'skin' layout target (skinTarget30: 3.0's GPU skinning reads that and nothing else), and
    // the translated shader takes that target's skinned output (gen30: skin passes, identity bones)
    bool translateSkinned = false;
};

// The 3.0 format a skinned 1.0 format's vertices go to: its first target with an R plan (into the target's 'skin'
// layout) -- the pick transcodeWorld makes for a material whose shader declares no format (0: none).
inline uint32_t skinTarget30(const rentable::Table& T, uint32_t crc10)
{
    auto t = T.targets.find(crc10);
    if (t != T.targets.end())
        for (uint32_t c : t->second)
            if (T.skinPlans.count({crc10, c})) return c;
    return 0;
}

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

struct Src { bndl::Bundle b; int i; };   // one material of a source bundle (merge input)

namespace detail {

// The table's material index inside the bundle read from the install; another 3.0 build moves it (RE-18)
inline bool hasMaterial(const bndl::Bundle& b, const rentable::Ref& ref, std::vector<std::string>& problems)
{
    if (ref.idx >= 0 && (size_t)ref.idx < b.mats.size()) return true;
    problems.push_back("3.0 bundle " + ref.extract + ": no material " + std::to_string(ref.idx));
    return false;
}

// Every slot's texture index lies inside its bundle's TEXB; a material block may name any index (RE-18)
inline bool texturesInRange(const std::vector<Src>& srcs, std::vector<std::string>& problems)
{
    for (const Src& s : srcs) {
        const bndl::Material& m = s.b.mats[s.i];
        for (int k = 0; k < m.slots; ++k) {
            const uint32_t t = s.b.slot(m, k);
            if (t == bndl::kUnset || t < s.b.textures.size()) continue;
            problems.push_back(m.name + ": texture index " + std::to_string(t) + " past the bundle's TEXB");
            return false;
        }
    }
    return true;
}

}  // namespace detail

inline std::string hex8(const std::string& h)
{
    static const char* d = "0123456789abcdef";
    std::string s;
    for (unsigned char c : h) { s.push_back(d[c >> 4]); s.push_back(d[c & 15]); }
    return s;
}

// RenderCaps 1.0 -> 3.0 (conv.rendercaps30).
inline int32_t rendercaps30(int32_t v)
{
    if (v == 0) return 0x200;
    return (v & 0xfff) | ((v & 0x1000) ? 0x200 : 0) | ((v & 0x2000) ? 0x400 : 0);
}

// The material half's inputs
struct MaterialInput {
    const std::string& mtb10;
    const std::map<int, std::set<uint32_t>>& drawn;   // material -> the 1.0 vformats it is drawn with
    const std::string& donorPrefix;                   // generic donors' extract path starts with it ("" = any)
};

namespace detail {

constexpr int kSlotStride10 = 4, kSlotStride30 = 8;   // a texture slot's bytes in block B (FORMATS.md 6.1)
// own hash space: MATP load (FUN_00472590) reuses a material registered under the hash (FUN_00468500)
constexpr uint32_t kTranslatedHashXor = 0x9E3779B9u;
constexpr size_t kParamBytes = 16;   // one float4 parameter of block A

// One transcodeMaterials call: inputs, the 1.0 bundle, the 3.0 bundles read so far, the merge sources
struct MaterialRun {
    const rentable::Table& T;
    const IO& io;
    const MaterialInput& in;
    Result& r;
    bndl::Bundle b10;
    std::map<std::string, bndl::Bundle> cache30;   // by extract path, for this call only
    std::vector<Src> srcs;
    std::string err;                          // the last bundle parse failure
};

inline bool blockSizes30(const rentable::Table& T, const std::string& sh, int& A, int& B, int& slots)
{
    auto it = T.shaders.find(sh);
    if (it == T.shaders.end()) return false;
    A = it->second.A;
    B = it->second.B;
    slots = it->second.slots;
    return true;
}

// A 1.0 shader's block sizes and B's slot count from its .shd descriptors, read once per call
struct BlockSizes10 {
    const IO& io;
    std::map<std::string, std::array<int, 3>> memo;
    bool get(const std::string& sh, int& A, int& B, int& slots);
};

inline bool BlockSizes10::get(const std::string& sh, int& A, int& B, int& slots)
{
    auto it = memo.find(sh);
    if (it == memo.end()) {
        std::string b;
        const shd::Layout& L = gen30::kShd10;   // block descriptors: u8 params, u8 slots, u16 size
        if (!io.read10("shaders/shaders/" + sh + ".shd", b) || b.size() < L.blockB + 4) return false;
        const uint8_t* p = (const uint8_t*)b.data();
        const int sizeA = (int)oct::u(p + L.blockA + 2, 2), sizeB = (int)oct::u(p + L.blockB + 2, 2);
        it = memo.emplace(sh, std::array<int, 3>{sizeA, sizeB, (int)p[L.blockB + 1]}).first;
    }
    A = it->second[0];
    B = it->second[1];
    slots = it->second[2];
    return true;
}

// The 3.0 bundle at ref.extract, read once per call; null (and a problem) when it does not read
inline bndl::Bundle* bundle30(MaterialRun& run, const rentable::Ref& ref)
{
    auto it = run.cache30.find(ref.extract);
    if (it != run.cache30.end()) return &it->second;
    std::string raw;
    bndl::Bundle b;
    const auto sizes3 = [&](const std::string& sh, int& A, int& B, int& slots) {
        return blockSizes30(run.T, sh, A, B, slots);
    };
    if (!run.io.read30(ref.extract, raw) || !bndl::parse(raw, b, kSlotStride30, sizes3, &run.err)) {
        const std::string why = raw.empty() ? std::string("unreadable") : run.err;
        run.r.problems.push_back("3.0 bundle " + ref.extract + ": " + why);
        return nullptr;
    }
    return &run.cache30.emplace(ref.extract, std::move(b)).first->second;
}

// Disney ships this 1.0 material in 3.0 (the real tier)
inline bool isReal(const rentable::Table& T, const std::string& name)
{
    return !name.empty() && T.matindex.count(name) && !T.notReal.count(name);
}

// skin: drawn with a skinned format; rigid: with an unskinned one
struct SkinUse { bool skin = false, rigid = false; };

inline SkinUse skinUse(const IO& io, const std::set<uint32_t>* formats)
{
    SkinUse u;
    if (!formats || !io.skinned) return u;
    for (uint32_t c : *formats) (io.skinned(c) ? u.skin : u.rigid) = true;
    return u;
}

// skintr10: only when drawn skinned alone, each format with a planned 3.0 skin target, and not real
inline bool skinTranslates(const MaterialRun& run, const bndl::Material& m10, const std::set<uint32_t>& formats,
                           SkinUse u)
{
    if (!run.io.translateSkinned || u.rigid || isReal(run.T, m10.name)) return false;
    for (uint32_t c : formats) {
        const uint32_t target = skinTarget30(run.T, c);
        if (target == 0 || !run.T.plans.count({c, target})) return false;
    }
    return true;
}

// dm, holding the 1.0 bundle's textures, as a merge source
inline void addOwnSource(MaterialRun& run, const bndl::Material& dm)
{
    bndl::Bundle one;
    one.textures = run.b10.textures;
    one.mats.push_back(dm);
    run.srcs.push_back({one, 0});
}

inline void placeTranslated(MaterialRun& run, int k, const bndl::Material& m10, const gen30::Shd10& s10, SkinUse u)
{
    gen30::Layout L = gen30::layout(s10);
    bndl::Material dm;
    dm.name = m10.name;
    dm.shader = "x" + m10.shader;
    dm.hash = m10.hash ^ kTranslatedHashXor;
    dm.stride = kSlotStride30;
    dm.slots = L.slots;
    dm.A = m10.A;
    dm.A.resize(L.sizeA, '\0');
    dm.B = gen30::blockB30(m10.B, L);
    addOwnSource(run, dm);
    run.r.tiers["translated"]++;
    if (u.skin) run.r.tiers["translated skinned"]++;
    if (run.io.verbose) run.io.log("translated " + m10.name + " [" + m10.shader + "]" + (u.skin ? " skinned" : ""));
    run.r.translated.insert(k);
    run.r.shaders.push_back({m10.shader, dm.shader});
}

// The translated tier: the material keeps its 1.0 shader as 'x'<shader>, with its own blocks. false: another tier
inline bool tryTranslated(MaterialRun& run, int k, const bndl::Material& m10, const std::set<uint32_t>* formats)
{
    const IO& io = run.io;
    const SkinUse u = skinUse(io, formats);
    if (!io.translate) return false;
    if (u.skin && !skinTranslates(run, m10, *formats, u)) return false;
    std::string raw10;
    gen30::Shd10 s10;
    if (!io.read10("shaders/shaders/" + m10.shader + ".shd", raw10) || !gen30::parse10(raw10, s10)) return false;
    if (io.translatable && !io.translatable(m10.shader)) return false;   // generates and registers the shader
    placeTranslated(run, k, m10, s10, u);
    return true;
}

inline std::string slotTextures(const bndl::Bundle& b, const bndl::Material& m)
{
    std::string l;
    for (int s = 0; s < m.slots; ++s) {
        uint32_t t = b.slot(m, s);
        l += " " + (t < b.textures.size() ? hex8(b.textures[t]) : std::string("-"));
    }
    return l;
}

// The real tier: the 3.0 material as it ships. false: a problem says why
inline bool addReal(MaterialRun& run, const bndl::Material& m10, const rentable::Ref& ref)
{
    bndl::Bundle* bnd = bundle30(run, ref);
    if (!bnd || !hasMaterial(*bnd, ref, run.r.problems)) return false;
    if (run.io.verbose) {   // the textures side by side: a GUID 3.0 reused shows as a mismatch
        std::string l = "real " + m10.name + " " + ref.extract + " 1.0:" + slotTextures(run.b10, m10);
        run.io.log(l + " 3.0:" + slotTextures(*bnd, bnd->mats[ref.idx]));
    }
    run.srcs.push_back({*bnd, ref.idx});
    run.r.tiers["real"]++;
    return true;
}

// The 3.0 targets of each format the material is drawn with
inline std::vector<std::set<uint32_t>> twinSets(const rentable::Table& T, const std::set<uint32_t>* formats)
{
    std::vector<std::set<uint32_t>> need;
    if (!formats) return need;
    for (uint32_t c : *formats) {
        auto t = T.targets.find(c);
        need.emplace_back();
        if (t != T.targets.end()) need.back().insert(t->second.begin(), t->second.end());
    }
    return need;
}

// The 1.0 shader's block B slot names
inline std::set<uint32_t> slotNames10(const IO& io, const std::string& shader)
{
    std::set<uint32_t> names;
    std::string raw;
    gen30::Shd10 s;
    if (io.read10("shaders/shaders/" + shader + ".shd", raw) && gen30::parse10(raw, s))
        names.insert(s.slotsB.begin(), s.slotsB.end());
    return names;
}

inline bool readsRole(const std::set<uint32_t>& slots10, const std::string& role)
{
    return slots10.count(hash::ehash("g_" + role + "_sampler")) != 0;
}

// A generic donor candidate and its slot roles in common with the 1.0 shader
struct Candidate { int common = 0; const std::string* shader = nullptr; const rentable::Ref* ref = nullptr; };

// Python max over (roles in common, count, shader, extract path, index)
inline bool better(const Candidate& a, const Candidate& b)
{
    return std::tie(a.common, a.ref->count, *a.shader, a.ref->extract, a.ref->idx) >
           std::tie(b.common, b.ref->count, *b.shader, b.ref->extract, b.ref->idx);
}

// The donor's shader has slots, its bundle the prefix, and a twin of every format the material is drawn with
inline bool donorFits(const MaterialRun& run, const Candidate& c, const std::vector<std::set<uint32_t>>& need)
{
    auto si = run.T.shaders.find(*c.shader);
    if (si == run.T.shaders.end() || si->second.slots < 1) return false;
    if (c.ref->extract.compare(0, run.in.donorPrefix.size(), run.in.donorPrefix)) return false;
    const std::set<uint32_t>& vf = si->second.vf;
    const auto declared = [&](uint32_t x) { return vf.count(x) != 0; };
    for (auto& twins : need)
        if (std::none_of(twins.begin(), twins.end(), declared)) return false;
    return true;
}

// A colour-buffer reader (refraction, distortion) keeps a 3.0 one -> the slot roles they share; nullopt: no reader
inline std::optional<int> colourBufferRoles(const rentable::Table& T, const std::set<uint32_t>& slots10,
                                            const std::string& shader)
{
    const std::vector<std::string>& roles = T.shaders.at(shader).roles;
    if (std::find(roles.begin(), roles.end(), "colorBuffer") == roles.end()) return std::nullopt;
    int common = 0;
    for (auto& role : roles) common += !role.empty() && readsRole(slots10, role);
    return common;
}

// The generic tier: the most common 3.0 shader taking a twin of every drawn format; colour-buffer readers first
inline const rentable::Ref* genericDonor(const MaterialRun& run, const bndl::Material& m10,
                                        const std::set<uint32_t>* formats)
{
    const std::vector<std::set<uint32_t>> need = twinSets(run.T, formats);
    const std::set<uint32_t> slots10 = slotNames10(run.io, m10.shader);
    Candidate best;
    for (int pass = readsRole(slots10, "colorBuffer") ? 0 : 1; pass < 2 && !best.ref; ++pass)
        for (auto& [sh, ref] : run.T.donors) {
            Candidate c{0, &sh, &ref};
            if (!donorFits(run, c, need)) continue;
            const std::optional<int> common = pass == 0 ? colourBufferRoles(run.T, slots10, sh) : 0;
            if (!common) continue;
            c.common = *common;
            if (!best.ref || better(c, best)) best = c;
        }
    return best.ref;
}

struct DonorPick { const rentable::Ref* ref = nullptr; const char* tier = "same-sh"; };

// same-sh: the table's donor for the 1.0 shader; else generic. No donor: a problem
inline DonorPick pickDonor(MaterialRun& run, const bndl::Material& m10, const std::set<uint32_t>* formats)
{
    DonorPick p;
    auto di = run.T.donors.find(m10.shader);
    if (di != run.T.donors.end()) {
        p.ref = &di->second;
        return p;
    }
    p.tier = "generic";
    p.ref = genericDonor(run, m10, formats);
    if (!p.ref) run.r.problems.push_back(m10.name + ": no 3.0 shader accepts its vertex format");
    return p;
}

// On another shader the 1.0 textures go in order into the slots whose role materials fill (FORMATS.md 6.4)
inline std::vector<uint32_t> genericSlots(const rentable::Table& T, const std::string& shader10,
                                          const bndl::Material& dm, std::vector<uint32_t> used)
{
    auto g = T.genericSlots.find(shader10);
    if (g != T.genericSlots.end()) {
        std::vector<uint32_t> pick;
        for (int i : g->second) pick.push_back(i < (int)used.size() ? used[i] : bndl::kUnset);
        used = pick;
    }
    std::vector<uint32_t> set;
    for (uint32_t t : used)
        if (t != bndl::kUnset) set.push_back(t);
    std::vector<int> targets;
    const auto& roles = T.shaders.at(dm.shader).roles;
    for (size_t s = 0; s < roles.size(); ++s)
        if (!roles[s].empty() && T.filled.count(roles[s])) targets.push_back((int)s);
    used.assign(dm.slots, bndl::kUnset);
    for (size_t ti = 0; ti < targets.size() && ti < set.size(); ++ti)   // targets ascend: each slot once
        if (targets[ti] < dm.slots) used[targets[ti]] = set[ti];
    return used;
}

// The 1.0 material's textures into the donor's slots (whole 8-byte slots, FORMATS.md 6.2) -> the slot assignment
inline std::vector<uint32_t> substituteSlots(const rentable::Table& T, const bndl::Bundle& b10,
                                             const bndl::Material& m10, bndl::Material& dm)
{
    std::vector<uint32_t> used;
    for (int s = 0; s < m10.slots; ++s) used.push_back(b10.slot(m10, s));
    if (dm.shader != m10.shader) used = genericSlots(T, m10.shader, dm, used);
    for (int s = 0; s < dm.slots; ++s) {
        uint32_t t = s < (int)used.size() ? used[s] : bndl::kUnset, w2 = t == bndl::kUnset ? bndl::kUnset : 0;
        memcpy(&dm.B[dm.stride * s], &t, sizeof t);
        memcpy(&dm.B[dm.stride * s + sizeof t], &w2, sizeof w2);
    }
    return used;
}

// The donor's 3.0 shader's block A, when it reads and fits the donor material's block
inline std::optional<shd::BlockA> donorBlockA(const IO& io, const bndl::Material& dm)
{
    if (!io.read30) return std::nullopt;
    std::string raw30;
    shd::BlockA a30;
    if (!io.read30("3:shaders/shaders/" + dm.shader + ".shd", raw30)) return std::nullopt;
    if (!shd::parseBlockA(raw30, a30) || (int)dm.A.size() != a30.sizeA) return std::nullopt;
    return a30;
}

// 3.0's defaults, then each 1.0 block A parameter at 3.0's offset for its name
inline std::string blockAByName(const IO& io, const bndl::Material& m10, const shd::BlockA& a30)
{
    const std::string& a10 = m10.A;
    std::string A = a30.defA, raw10;
    gen30::Shd10 s10;
    if (!io.read10("shaders/shaders/" + m10.shader + ".shd", raw10) || !gen30::parse10(raw10, s10)) return A;
    for (auto& p : s10.params) {
        auto q = a30.off.find(p.hash);
        if (!p.A || q == a30.off.end()) continue;
        const size_t left10 = a10.size() - (std::min)(a10.size(), (size_t)p.offset);
        const size_t n = (std::min)({kParamBytes, left10, A.size() - (size_t)q->second});
        A.replace(q->second, n, a10, p.offset, n);
    }
    return A;
}

// Block A by parameter name: copied raw, 1.0's layout put parameters 3.0 moved at the wrong offsets
inline void relayBlockA(const IO& io, const bndl::Material& m10, bndl::Material& dm)
{
    const std::string& a10 = m10.A;
    const bool firstParam = a10.size() >= kParamBytes && dm.A.size() >= kParamBytes;
    if (const std::optional<shd::BlockA> a30 = donorBlockA(io, dm)) dm.A = blockAByName(io, m10, *a30);
    else if (dm.shader == m10.shader && a10.size() <= dm.A.size()) dm.A = a10 + dm.A.substr(a10.size());
    else if (firstParam) dm.A = a10.substr(0, kParamBytes) + dm.A.substr(kParamBytes);
}

// octtest --render with RENDER_VERBOSE: which 1.0 texture went to which donor slot
inline void logSubstitute(const MaterialRun& run, const char* tier, const bndl::Material& m10, const bndl::Material& dm,
                          const std::vector<uint32_t>& used)
{
    std::string l = std::string(tier) + " " + m10.name + " [" + m10.shader + " -> " + dm.shader + "]";
    const auto& roles = run.T.shaders.at(dm.shader).roles;
    for (int s = 0; s < dm.slots; ++s) {
        uint32_t t = s < (int)used.size() ? used[s] : bndl::kUnset;
        const bool named = s < (int)roles.size() && !roles[s].empty();
        const bool none = t == bndl::kUnset || t >= run.b10.textures.size();
        const std::string texture = none ? std::string("none") : hex8(run.b10.textures[t]);
        l += " " + (named ? roles[s] : std::string("-")) + "=" + texture;
    }
    run.io.log(l);
}

// autobundle._substitute: the donor's blocks holding the 1.0 material's own textures. false: a problem says why
inline bool addSubstitute(MaterialRun& run, const bndl::Material& m10, const DonorPick& pick)
{
    bndl::Bundle* bnd = bundle30(run, *pick.ref);
    if (!bnd || !hasMaterial(*bnd, *pick.ref, run.r.problems)) return false;
    bndl::Material dm = bnd->mats[pick.ref->idx];
    const std::vector<uint32_t> used = substituteSlots(run.T, run.b10, m10, dm);
    relayBlockA(run.io, m10, dm);
    if (run.io.verbose) logSubstitute(run, pick.tier, m10, dm, used);
    dm.name = m10.name;
    dm.hash = m10.hash;
    addOwnSource(run, dm);
    run.r.tiers[pick.tier]++;
    return true;
}

// The real tier, else a donor's (same-sh, generic). false: a problem says why
inline bool placeOnDonor(MaterialRun& run, const bndl::Material& m10, const std::set<uint32_t>* formats)
{
    if (isReal(run.T, m10.name)) return addReal(run, m10, run.T.matindex.at(m10.name));
    const DonorPick pick = pickDonor(run, m10, formats);
    return pick.ref && addSubstitute(run, m10, pick);
}

// Material k onto its first tier; shaderVf gets the formats its final shader declares (not for translated ones)
inline void placeMaterial(MaterialRun& run, int k, std::map<int, const std::set<uint32_t>*>& shaderVf)
{
    const bndl::Material& m10 = run.b10.mats[k];
    auto dk = run.in.drawn.find(k);
    const std::set<uint32_t>* formats = dk != run.in.drawn.end() ? &dk->second : nullptr;
    if (tryTranslated(run, k, m10, formats)) return;
    if (!placeOnDonor(run, m10, formats)) return;
    const bndl::Material& fin = run.srcs.back().b.mats[run.srcs.back().i];
    run.r.shaders.push_back({m10.shader, fin.shader});
    shaderVf[k] = &run.T.shaders.at(fin.shader).vf;
}

// tex's index of texture h, appended on first use
inline uint32_t textureIndex(std::vector<std::string>& tex, const std::string& h)
{
    uint32_t at = 0;
    while (at < tex.size() && tex[at] != h) ++at;
    if (at == tex.size()) tex.push_back(h);
    return at;
}

// matp.merge: TEXB union in first-use order, slot indices rebased (index word only)
inline std::vector<bndl::Material> mergeSources(const std::vector<Src>& srcs, std::vector<std::string>& tex)
{
    std::vector<bndl::Material> mats;
    for (auto& s : srcs) {
        bndl::Material m = s.b.mats[s.i];
        for (int k = 0; k < m.slots; ++k) {
            uint32_t t = s.b.slot(m, k);
            if (t == bndl::kUnset) continue;
            uint32_t at = textureIndex(tex, s.b.textures[t]);
            memcpy(&m.B[m.stride * k], &at, sizeof at);
        }
        mats.push_back(m);
    }
    return mats;
}

// Scene settings re-laid into 3.0's format; their textures (LUT, environment map) join the merged TEXB
inline std::vector<std::string> relaySceneSettings(const IO& io, const bndl::Bundle& b10, std::vector<std::string>& tex)
{
    std::vector<std::string> sset;
    if (!io.ssf10 || !io.ssf30) return sset;
    const auto index = [&](uint32_t t) {
        return t < b10.textures.size() ? textureIndex(tex, b10.textures[t]) : bndl::kUnset;
    };
    const std::map<uint32_t, uint32_t> renamed = ssfRenamedSlots10();
    for (auto& blk : b10.ssetBlocks) sset.push_back(ssf::relayBlock(*io.ssf10, *io.ssf30, blk, index, renamed));
    return sset;
}

}  // namespace detail

// autobundle.resolve: the 1.0 materials onto 3.0 shaders -> r.mtb; shaderVf: material -> its shader's vformats
inline bool transcodeMaterials(const rentable::Table& T, const IO& io, const MaterialInput& in, Result& r,
                               std::map<int, const std::set<uint32_t>*>& shaderVf)
{
    detail::MaterialRun run{T, io, in, r, {}, {}, {}, {}};
    detail::BlockSizes10 sizes10{io, {}};
    const auto sizes1 = [&](const std::string& sh, int& A, int& B, int& slots) { return sizes10.get(sh, A, B, slots); };
    if (!bndl::parse(in.mtb10, run.b10, detail::kSlotStride10, sizes1, &run.err)) {
        r.problems.push_back("1.0 bundle: " + run.err);
        return false;
    }
    for (size_t k = 0; k < run.b10.mats.size(); ++k) detail::placeMaterial(run, (int)k, shaderVf);
    if (!r.problems.empty() || !detail::texturesInRange(run.srcs, r.problems)) return false;
    std::vector<std::string> tex;
    const std::vector<bndl::Material> mats = detail::mergeSources(run.srcs, tex);
    const std::vector<std::string> sset = detail::relaySceneSettings(io, run.b10, tex);
    r.sceneSettings = (int)sset.size();
    for (auto& t : tex) r.textures10.insert(hex8(t));
    r.mtb = bndl::build(tex, mats, sset.empty() ? std::vector<std::string>() : run.b10.ssetNames, sset);
    return true;
}

// A material bundle without geometry of its own (particles/<x>.mtb): materials only, generic donors from donorPrefix.
inline bool transcodeBundle(const rentable::Table& T, const IO& io, const std::string& mtb10,
                            const std::string& donorPrefix, Result& r)
{
    const std::map<int, std::set<uint32_t>> drawn;
    std::map<int, const std::set<uint32_t>*> vf;
    return transcodeMaterials(T, io, {mtb10, drawn, donorPrefix}, r, vf);
}

namespace detail {

// (MaterialReference, vformatCRC) per primitive in record order (autobundle.prim_pairs). -> nullptr or the problem
inline const char* primPairs(const oct::File& f, std::map<uint32_t, std::set<int>>& users,
                             std::map<int, std::set<uint32_t>>& drawn)
{
    int ref = -1;
    for (auto& rec : f.recs) {
        const std::string& n = f.name(rec);
        const bool isRef = n == "MaterialReference";
        if (!isRef && n != "vformatCRC") continue;
        const std::vector<int32_t> v = f.ints(rec);
        if (v.empty()) return isRef ? "MaterialReference without a value" : "vformatCRC without a value";
        if (isRef) {
            ref = v[0];
            continue;
        }
        users[(uint32_t)v[0]].insert(ref);
        drawn[ref].insert((uint32_t)v[0]);
    }
    return nullptr;
}

// A primitive's records by name: the last of each in its subtree; RenderCaps is optional
inline constexpr const char* kPrimFields[] = {"Idata", "Vdata", "vformatCRC", "MaterialReference", "RenderCaps"};
constexpr size_t kIdata = 0, kVdata = 1, kCrc = 2, kMatRef = 3, kCaps = 4, kPrimFieldCount = 5;
struct PrimRecs { size_t at[kPrimFieldCount] = {}; bool has[kPrimFieldCount] = {}; };

// index of nm in kPrimFields, or kPrimFieldCount
inline size_t primField(const std::string& nm)
{
    for (size_t k = 0; k < kPrimFieldCount; ++k)
        if (nm == kPrimFields[k]) return k;
    return kPrimFieldCount;
}

// false: Idata, Vdata, vformatCRC or MaterialReference missing (RE-19: record 0 stood in for it)
inline bool primRecords(const oct::File& f, size_t prim, PrimRecs& p)
{
    for (size_t j = prim + 1, pe = f.blockEnd(prim); j < pe; ++j) {
        const size_t k = primField(f.name(f.recs[j]));
        if (k == kPrimFieldCount) continue;
        p.at[k] = j;
        p.has[k] = true;
    }
    const bool buffers = p.has[kIdata] && p.has[kVdata];
    return buffers && p.has[kCrc] && p.has[kMatRef];
}

// Idata: [width, first index, base, count]; Vdata: [streams, vertices, (0, offset, stride) per stream]
struct PrimData { std::vector<int32_t> ida, vda; uint32_t src = 0; int ref = 0; };

// vda holds the (0, offset, stride) triples of its first `streams` streams
inline bool vdataHolds(const std::vector<int32_t>& vda, size_t streams)
{
    return vda.size() >= 2 && streams <= (vda.size() - 2) / 3;
}

// -> nullptr, or the problem: a value missing, or a list shorter than its counts (RE-19)
inline const char* readPrim(const oct::File& f, const PrimRecs& p, PrimData& d)
{
    const std::vector<int32_t> crc = f.ints(f.recs[p.at[kCrc]]), ref = f.ints(f.recs[p.at[kMatRef]]);
    if (crc.empty() || ref.empty()) return "primitive without a vformatCRC or MaterialReference value";
    d.ida = f.ints(f.recs[p.at[kIdata]]);
    d.vda = f.ints(f.recs[p.at[kVdata]]);
    d.src = (uint32_t)crc[0];
    d.ref = ref[0];
    if (d.ida.size() < 4) return "Idata too short";
    const bool counted = !d.vda.empty() && d.vda[0] >= 0;
    if (!counted || !vdataHolds(d.vda, (size_t)d.vda[0])) return "Vdata too short";
    return nullptr;
}

inline rentable::Op copyOp(int dsi, int size, int ssi, int soff)
{
    rentable::Op o{};
    o.dsi = dsi;
    o.doff = 0;
    o.size = size;
    o.kind = 'C';
    o.ssi = ssi;
    o.soff = soff;
    return o;
}

// the primitive's last stream ends in the re-laid format's colour tail
inline bool tailFits(const vtxf::ColourTail& ct, const std::vector<int32_t>& vda)
{
    return vda[0] == ct.stream + 1 && vda[4 + 3 * ct.stream] == ct.offset + ct.size;
}

// A translated material's primitive: every stream copied as it is; a re-laid format's colour tail to its own stream
inline const char* samePlan(const IO& io, uint32_t src, const std::vector<int32_t>& vda, rentable::Plan& same)
{
    const vtxf::ColourTail* ct = nullptr;
    if (io.tails10)
        if (auto t = io.tails10->find(src); t != io.tails10->end()) ct = &t->second;
    if (ct && !tailFits(*ct, vda)) return "vertex colour tail: strides differ from the format";
    for (int s = 0; s < vda[0]; ++s) {
        same.ss.push_back(vda[4 + 3 * s]);
        same.ds.push_back(ct && s == ct->stream ? ct->offset : vda[4 + 3 * s]);
        same.ops.push_back(copyOp(s, same.ds.back(), s, 0));
    }
    if (ct) {
        same.ds.push_back(ct->size);
        same.ops.push_back(copyOp(vda[0], ct->size, ct->stream, ct->offset));
    }
    return nullptr;
}

// 16-bit index -> local index, stamped per primitive: a flat table, not a hash map per index (in_city: ~4M indices)
struct Remap { std::vector<uint32_t> stamp, to; uint32_t gen = 0; };

// The primitive's indices made local: order = the vertices it uses, first use first; sub = its local indices
inline const char* remapIndices(const std::vector<int32_t>& ida, const std::string& ibuf10, Remap& m,
                                std::vector<uint32_t>& order, std::vector<uint32_t>& sub)
{
    const int32_t first = ida[1], count = ida[3];
    const bool inside = first >= 0 && bytes::fits(ibuf10.size(), 2 * (size_t)first, 2 * (size_t)count);
    if (count > 0 && !inside) return "index past the ibuf";
    const uint8_t* ib = (const uint8_t*)ibuf10.data();
    sub.reserve(count > 0 ? (size_t)count : 0);
    ++m.gen;
    for (int32_t q = 0; q < count; ++q) {
        const size_t at = 2 * ((size_t)first + q);
        const uint32_t v = (uint32_t)(ib[at] | ib[at + 1] << 8);
        if (m.stamp[v] != m.gen) {
            m.stamp[v] = m.gen;
            m.to[v] = (uint32_t)order.size();
            order.push_back(v);
        }
        sub.push_back(m.to[v]);
    }
    return nullptr;
}

// [off, off + (last + 1) * stride) lies inside the vbuf: every vertex up to index last
inline bool vertexFits(int32_t off, int32_t stride, uint32_t last, size_t size)
{
    if (off < 0 || stride < 0) return false;
    const uint64_t span = ((uint64_t)last + 1) * (uint32_t)stride;
    return span <= size && bytes::fits(size, (size_t)off, (size_t)span);
}

// Every vertex of order through plan into per (one string per destination stream); strides and ranges checked first
inline const char* convertPrim(const rentable::Plan& plan, const std::vector<int32_t>& vda,
                               const std::vector<uint32_t>& order, const std::string& vbuf10,
                               std::vector<std::string>& per)
{
    for (int s = 0; s < vda[0]; ++s)
        if (s >= (int)plan.ss.size() || vda[4 + 3 * s] != plan.ss[s]) return "stride mismatch";
    if (!vdataHolds(vda, plan.ss.size())) return "Vdata shorter than the plan";
    const uint32_t last = order.empty() ? 0 : *std::max_element(order.begin(), order.end());
    for (size_t s = 0; s < plan.ss.size() && !order.empty(); ++s)
        if (!vertexFits(vda[3 + 3 * s], plan.ss[s], last, vbuf10.size())) return "vertex past the vbuf";
    per.assign(plan.ds.size(), std::string());
    for (size_t s = 0; s < plan.ds.size(); ++s) per[s].reserve(order.size() * (size_t)plan.ds[s]);
    std::vector<const uint8_t*> srcp(plan.ss.size());
    const uint8_t* vb = (const uint8_t*)vbuf10.data();
    for (uint32_t v : order) {
        for (size_t s = 0; s < plan.ss.size(); ++s) srcp[s] = vb + (size_t)vda[3 + 3 * s] + (size_t)v * plan.ss[s];
        convertVertex(srcp, plan, per);
    }
    return nullptr;
}

// Record edits held until every check passed, so a false return leaves f as it was (RE-05)
struct RecEdits {
    std::map<size_t, oct::Rec> recs;   // record index -> its edited copy
    const oct::Rec& get(const oct::File& f, size_t i) const
    {
        auto it = recs.find(i);
        return it != recs.end() ? it->second : f.recs[i];
    }
    void setInts(oct::File& f, size_t i, const std::vector<int32_t>& v)
    {
        oct::Rec r = get(f, i);
        f.setInts(r, v);
        recs[i] = std::move(r);
    }
    void setInt(oct::File& f, size_t i, int64_t v)
    {
        oct::Rec r = get(f, i);
        f.setInt(r, v);
        recs[i] = std::move(r);
    }
    void commit(oct::File& f)
    {
        for (auto& [i, r] : recs) f.recs[i] = std::move(r);
        recs.clear();
    }
};

// RenderCaps 1.0 -> 3.0; false: the record has no value
inline bool caps30(oct::File& f, RecEdits& e, size_t rec)
{
    const std::vector<int32_t> v = f.ints(e.get(f, rec));
    if (v.empty()) return false;
    e.setInt(f, rec, rendercaps30(v[0]));
    return true;
}

// the first record named nm in block blk's subtree, or -1
inline long fieldOf(const oct::File& f, size_t blk, const char* nm)
{
    for (size_t j = blk + 1, end = f.blockEnd(blk); j < end; ++j)
        if (f.name(f.recs[j]) == nm) return (long)j;
    return -1;
}

// VertexBufferPool: its first non-zero Size becomes the new vbuf size. false: a Size without a value
inline bool vertexPoolSize(oct::File& f, RecEdits& e, size_t size)
{
    const long vbp = f.pool("VertexBufferPool");
    const size_t end = vbp >= 0 ? f.blockEnd(vbp) : 0;
    for (size_t j = vbp + 1; vbp >= 0 && j < end; ++j) {
        if (f.name(f.recs[j]) != "Size") continue;
        const std::vector<int32_t> v = f.ints(e.get(f, j));
        if (v.empty()) return false;
        if (!v[0]) continue;
        e.setInt(f, j, (int64_t)size);
        break;
    }
    return true;
}

// Buffer sizes into the pools; IndexBuffer 2 gets its own file. Commits e once every pool record is found.
inline const char* patchBufferPools(oct::File& f, RecEdits& e, const std::string& stem, const Result& r)
{
    if (!vertexPoolSize(f, e, r.vbuf.size())) return "VertexBufferPool Size without a value";
    const long ibp = f.pool("IndexBufferPool");
    const size_t ibEnd = ibp >= 0 ? f.blockEnd(ibp) : 0;
    std::vector<size_t> ibs;
    for (size_t j = ibp + 1; ibp >= 0 && j < ibEnd; ++j)
        if (f.recs[j].level == 2 && f.name(f.recs[j]) == "IndexBuffer") ibs.push_back(j);
    if (ibs.size() < 3) return "IndexBufferPool shape";
    const long size0 = fieldOf(f, ibs[0], "Size"), size2 = fieldOf(f, ibs[2], "Size");
    const bool ownFile = !r.ibuf2.empty() && fieldOf(f, ibs[2], "FileName") < 0;
    const long name0 = ownFile ? fieldOf(f, ibs[0], "FileName") : 0;
    if (size0 < 0 || size2 < 0 || name0 < 0) return "IndexBufferPool shape";
    e.commit(f);
    f.setInt(f.recs[size0], (int64_t)r.ibuf.size());
    f.setInt(f.recs[size2], (int64_t)r.ibuf2.size());
    if (!ownFile) return nullptr;
    const oct::Rec& fn = f.recs[name0];
    f.recs.insert(f.recs.begin() + f.blockEnd(ibs[2]), f.nameScalar(fn.level, fn.name, stem + "_2.ibuf"));
    return nullptr;
}

}  // namespace detail

// The 1.0 world's files: its stem ('in_city'), .mtb, _0.vbuf and _0.ibuf
struct World10 {
    const std::string& stem;
    const std::string& mtb;
    const std::string& vbuf;
    const std::string& ibuf;
};

namespace detail {

constexpr uint32_t kIndexValues16 = 0x10000;   // every value a 16-bit index takes
constexpr uint64_t kMaxIndex16 = 0xffff;       // a node past it gets 32-bit indices (FORMATS.md 4.4)
constexpr int32_t kIndexBuffer16 = 0, kIndexBuffer32 = 2;   // Idata[0] in 3.0 (FORMATS.md 4.3)
constexpr int kIndexBytes16 = 2, kIndexBytes32 = 4;
constexpr int kNodeLevel = 2;   // a pool's entries (Node, Material); their fields one level deeper (FORMATS.md 4.1)

using FormatMap = std::map<std::pair<int, uint32_t>, uint32_t>;    // (material, 1.0 vformat) -> 3.0 vformat
using ShaderFormats = std::map<int, const std::set<uint32_t>*>;   // material -> vformats its final shader declares

// One transcodeWorld call: its inputs and what the passes before the geometry found
struct WorldRun {
    const rentable::Table& T;
    const IO& io;
    Result& r;
    std::map<uint32_t, std::set<int>> users;   // 1.0 vformat -> materials drawn with it
    std::map<int, std::set<uint32_t>> drawn;   // material -> 1.0 vformats it is drawn with
    ShaderFormats shaderVf;
    FormatMap vmap;   // the first 3.0 target the material's shader takes
    FormatMap smap;   // for skinned nodes: the first target with an R plan the shader takes
};

inline bool fail(Result& r, const std::string& why)
{
    r.problems.push_back(why);
    return false;
}

// Material ref's final shader declares vformat c; no entry: any format
inline bool shaderTakes(const ShaderFormats& shaderVf, int ref, uint32_t c)
{
    auto sv = shaderVf.find(ref);
    return sv == shaderVf.end() || sv->second->count(c);
}

inline bool skinnedFormat(const IO& io, uint32_t c) { return io.skinned && io.skinned(c); }

// Translated materials keep the 1.0 format, a skinned one its 3.0 skin target (skintr10). true: all users are
inline bool mapTranslated(WorldRun& w, uint32_t crc, const std::set<int>& refs)
{
    bool all = true;
    const bool skinnedFmt = skinnedFormat(w.io, crc);
    for (int ref : refs) {
        if (w.r.translated.count(ref)) w.vmap[{ref, crc}] = skinnedFmt ? skinTarget30(w.T, crc) : crc;
        else all = false;
    }
    return all;
}

inline void mapConverted(WorldRun& w, int ref, uint32_t crc, const std::vector<uint32_t>& targets)
{
    if (w.r.translated.count(ref)) return;
    const auto takes = [&](uint32_t c) { return shaderTakes(w.shaderVf, ref, c); };
    auto c = std::find_if(targets.begin(), targets.end(), takes);
    if (c != targets.end()) w.vmap[{ref, crc}] = *c;
    else w.r.problems.push_back("vformat: no 3.0 format accepted by material " + std::to_string(ref));
}

inline void mapFormats(WorldRun& w)
{
    for (auto& [crc, refs] : w.users) {
        if (mapTranslated(w, crc, refs)) continue;
        auto t = w.T.targets.find(crc);
        if (t == w.T.targets.end() || t->second.empty()) {
            w.r.problems.push_back("vformat " + std::to_string(crc) + ": no 3.0 target");
            continue;
        }
        for (int ref : refs) mapConverted(w, ref, crc, t->second);
    }
}

inline void mapSkin(WorldRun& w, uint32_t crc, int ref)
{
    auto t = w.T.targets.find(crc);
    if (!w.io.skinLayout || t == w.T.targets.end()) return;
    if (w.r.translated.count(ref) && !skinnedFormat(w.io, crc)) return;   // translated rigid: 1.0 format
    const auto takes = [&](uint32_t c) { return w.T.skinPlans.count({crc, c}) && shaderTakes(w.shaderVf, ref, c); };
    auto c = std::find_if(t->second.begin(), t->second.end(), takes);
    if (c != t->second.end()) w.smap[{ref, crc}] = *c;
}

inline void mapSkinFormats(WorldRun& w)
{
    for (auto& [crc, refs] : w.users)
        for (int ref : refs) mapSkin(w, crc, ref);
}

// One geometry pass: the 1.0 buffers, the index remap, the record edits patchBufferPools commits
struct Geometry {
    const std::string& vbuf10;
    const std::string& ibuf10;
    Remap remap;
    RecEdits edits;
};

// A scene node: whether it is geometry, skinned, and its primitive records
struct NodeScan {
    bool geometry = false, skinned = false;
    std::vector<size_t> prims;
};

inline NodeScan scanNode(const oct::File& f, size_t n)
{
    NodeScan s;
    for (size_t j = n + 1, end = f.blockEnd(n); j < end; ++j) {
        const std::string& nm = f.name(f.recs[j]);
        const bool field = f.recs[j].level == kNodeLevel + 1;
        if (field && nm == "Type") {
            auto v = f.nameValues(f.recs[j]);
            s.geometry = !v.empty() && v[0] == "Geometry";
        }
        if (field && nm == "NumInfluences") s.skinned = true;
        if (nm == "Primitive") s.prims.push_back(j);
    }
    return s;
}

// A primitive's 3.0 vformat and plan; a null plan means same, the 1.0 format kept
struct PrimPlan {
    uint32_t dst = 0;
    rentable::Plan same;
    const rentable::Plan* plan = nullptr;
    const rentable::Plan& get() const { return plan ? *plan : same; }
};

// A skinned node's primitive goes to its 'skin' layout target (R plan); a translated one keeps its format
inline std::optional<PrimPlan> planFor(WorldRun& w, const PrimData& d, const NodeScan& node)
{
    PrimPlan p;
    auto vm = w.vmap.find({d.ref, d.src});
    const bool noTarget = vm == w.vmap.end() && w.T.targets.at(d.src).empty();
    if (noTarget) {
        fail(w.r, "vformat " + std::to_string(d.src) + ": no 3.0 target");
        return std::nullopt;
    }
    p.dst = vm != w.vmap.end() ? vm->second : w.T.targets.at(d.src)[0];
    const rentable::Plan* skinPlan = nullptr;
    auto s = w.smap.find({d.ref, d.src});
    if (node.skinned && p.dst != d.src && s != w.smap.end()) {
        p.dst = s->second;
        skinPlan = &w.T.skinPlans.at({d.src, p.dst});
        ++w.r.skinned;
    }
    if (p.dst != d.src) {
        p.plan = skinPlan ? skinPlan : &w.T.plans.at({d.src, p.dst});
        return p;
    }
    if (const char* why = samePlan(w.io, d.src, d.vda, p.same)) {
        fail(w.r, why);
        return std::nullopt;
    }
    return p;
}

// One converted primitive: destination streams and strides, local indices, vertex count, 3.0 vformat
struct PrimOut {
    std::vector<std::string> streams;
    std::vector<int> strides;
    std::vector<uint32_t> sub;
    uint32_t count = 0, dst = 0;
};

// The primitive's vertices through its plan, its indices made local; nullopt: a problem says why
inline std::optional<PrimOut> convertPrimitive(WorldRun& w, Geometry& g, const PrimData& d, const NodeScan& node)
{
    const std::optional<PrimPlan> p = planFor(w, d, node);
    if (!p) return std::nullopt;
    const rentable::Plan& plan = p->get();
    PrimOut out;
    std::vector<uint32_t> order;
    const char* why = remapIndices(d.ida, g.ibuf10, g.remap, order, out.sub);
    if (!why) why = convertPrim(plan, d.vda, order, g.vbuf10, out.streams);
    if (why) {
        fail(w.r, why);
        return std::nullopt;
    }
    out.strides = plan.ds;
    out.count = (uint32_t)order.size();
    out.dst = p->dst;
    return out;
}

// The node's vertices stream-major (FORMATS.md 4.4) -> each primitive's stream offsets in nv
inline std::vector<std::vector<uint32_t>> appendStreams(const std::vector<PrimOut>& outs, std::string& nv)
{
    std::vector<std::vector<uint32_t>> off(outs.size());
    size_t maxS = 0;
    for (auto& o : outs) maxS = o.strides.size() > maxS ? o.strides.size() : maxS;
    for (size_t i = 0; i < outs.size(); ++i) off[i].assign(outs[i].strides.size(), 0);
    for (size_t s = 0; s < maxS; ++s)
        for (size_t i = 0; i < outs.size(); ++i) {
            if (s >= outs[i].strides.size()) continue;
            off[i][s] = (uint32_t)nv.size();
            nv += outs[i].streams[s];
        }
    return off;
}

// sub + vbase, indexBytes each, appended to buf -> where they start
inline uint32_t appendIndices(const std::vector<uint32_t>& sub, uint32_t vbase, int indexBytes, std::string& buf)
{
    const uint32_t at = (uint32_t)buf.size();
    for (uint32_t x : sub) {
        uint32_t v = x + vbase;
        buf.append((const char*)&v, indexBytes);
    }
    return at;
}

// Vdata: [streams, vertices, (0, offset, stride) per stream]
inline std::vector<int32_t> vdataOf(const PrimOut& o, const std::vector<uint32_t>& off)
{
    std::vector<int32_t> v = {(int32_t)o.strides.size(), (int32_t)o.count};
    for (size_t s = 0; s < o.strides.size(); ++s) {
        v.push_back(0);
        v.push_back((int32_t)off[s]);
        v.push_back(o.strides[s]);
    }
    return v;
}

// The node's buffers and each primitive's new Idata, Vdata, vformatCRC and RenderCaps, queued in g.edits
inline bool layoutNode(WorldRun& w, oct::File& f, Geometry& g, const std::vector<PrimRecs>& ps,
                       const std::vector<PrimOut>& outs)
{
    const std::vector<std::vector<uint32_t>> off = appendStreams(outs, w.r.vbuf);
    uint64_t total = 0;
    for (auto& o : outs) total += o.count;
    const bool wide = total > kMaxIndex16;
    uint32_t vbase = 0;
    for (size_t i = 0; i < ps.size(); ++i) {
        const PrimOut& o = outs[i];
        const int indexBytes = wide ? kIndexBytes32 : kIndexBytes16;
        const uint32_t at = appendIndices(o.sub, vbase, indexBytes, wide ? w.r.ibuf2 : w.r.ibuf);
        const int32_t width = wide ? kIndexBuffer32 : kIndexBuffer16;
        g.edits.setInts(f, ps[i].at[kIdata], {width, (int32_t)at, (int32_t)vbase, (int32_t)o.sub.size()});
        g.edits.setInts(f, ps[i].at[kVdata], vdataOf(o, off[i]));
        g.edits.setInt(f, ps[i].at[kCrc], o.dst);
        const bool caps = ps[i].has[kCaps];
        if (caps && !caps30(f, g.edits, ps[i].at[kCaps])) return fail(w.r, "RenderCaps without a value");
        vbase += o.count;
    }
    return true;
}

inline bool convertNode(WorldRun& w, oct::File& f, Geometry& g, size_t n)
{
    const NodeScan node = scanNode(f, n);
    if (!node.geometry || node.prims.empty()) return true;
    std::vector<PrimRecs> ps(node.prims.size());
    for (size_t i = 0; i < node.prims.size(); ++i)
        if (!primRecords(f, node.prims[i], ps[i]))
            return fail(w.r, "primitive without Idata, Vdata, vformatCRC or MaterialReference");
    std::vector<PrimOut> outs;
    outs.reserve(ps.size());
    for (auto& p : ps) {
        PrimData d;
        if (const char* why = readPrim(f, p, d)) return fail(w.r, why);
        std::optional<PrimOut> o = convertPrimitive(w, g, d, node);
        if (!o) return false;
        outs.push_back(std::move(*o));
    }
    return layoutNode(w, f, g, ps, outs);
}

inline bool convertNodes(WorldRun& w, oct::File& f, Geometry& g)
{
    const long stp = f.pool("SceneTreeNodePool");
    const size_t stpEnd = stp >= 0 ? f.blockEnd(stp) : 0;   // reads only: the edits wait in g.edits
    for (size_t n = stp + 1; stp >= 0 && n < stpEnd; n = f.blockEnd(n)) {
        const bool node = f.recs[n].level == kNodeLevel && f.name(f.recs[n]) == "Node";
        if (node && !convertNode(w, f, g, n)) return false;
    }
    return true;
}

// conv.convert_geometry + build_buffers; the 32-bit index buffer gets its own file (IndexBuffer 2)
inline bool convertGeometry(WorldRun& w, oct::File& f, const World10& w10)
{
    Geometry g{w10.vbuf, w10.ibuf, {}, {}};
    g.remap.stamp.assign(kIndexValues16, 0);
    g.remap.to.assign(kIndexValues16, 0);
    if (!convertNodes(w, f, g)) return false;
    if (const char* why = patchBufferPools(f, g.edits, w10.stem, w.r)) return fail(w.r, why);
    return true;
}

// Material RenderCaps is 1.0-only (FORMATS.md 4.5, conv.prepare)
inline void dropMaterialRenderCaps(oct::File& f)
{
    const long mp = f.pool("MaterialPool");
    if (mp < 0) return;
    auto b = f.recs.begin() + mp + 1, e = f.recs.begin() + f.blockEnd(mp);
    const auto caps = [&](const oct::Rec& x) { return x.level > kNodeLevel && f.name(x) == "RenderCaps"; };
    f.recs.erase(std::remove_if(b, e, caps), e);
}

}  // namespace detail

// Materials, vertices and .oct primitive records of one 1.0 world, f edited in place. false: r.problems, f unchanged
inline bool transcodeWorld(const rentable::Table& T, const IO& io, oct::File& f, const World10& w10, Result& r)
{
    detail::WorldRun w{T, io, r};
    if (const char* why = detail::primPairs(f, w.users, w.drawn)) return detail::fail(r, why);
    const std::string anyDonor;   // generic donors from any 3.0 bundle
    if (!transcodeMaterials(T, io, {w10.mtb, w.drawn, anyDonor}, r, w.shaderVf)) return false;
    detail::mapFormats(w);
    if (!r.problems.empty()) return false;
    detail::mapSkinFormats(w);
    // assets without buffers (most mission graphs) have no geometry
    const bool geometry = !w10.vbuf.empty() && !w10.ibuf.empty();
    if (geometry && !detail::convertGeometry(w, f, w10)) return false;
    detail::dropMaterialRenderCaps(f);
    return true;
}

}  // namespace render

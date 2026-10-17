// 1.0 materials onto 3.0 shaders (autobundle.resolve): real, translated, same-shader and generic tiers.
// Base and formats only; any thread.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../base/hash.hpp"
#include "../../formats/bndl.hpp"
#include "../../formats/rentable.hpp"
#include "../../formats/shd.hpp"
#include "../../formats/ssf.hpp"
#include "gen30.hpp"
#include "render.hpp"

namespace render {

// The material half's inputs
struct MaterialInput {
    const std::string& mtb10;
    const std::map<int, std::set<uint32_t>>& drawn;   // material -> the 1.0 vformats it is drawn with
    const std::string& donorPrefix;                   // generic donors' extract path starts with it ("" = any)
};

namespace detail {

struct Src { bndl::Bundle b; int i; };   // one material of a source bundle (merge input)

// The bytes of h as lower-case hex, high nibble first
inline std::string hex8(const std::string& h)
{
    constexpr char kDigits[] = "0123456789abcdef";
    std::string s;
    for (unsigned char c : h) {
        s.push_back(kDigits[c >> 4]);
        s.push_back(kDigits[c & 15]);
    }
    return s;
}

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

// a texture slot's bytes in block B (FORMATS.md 6.1)
constexpr int kSlotStride10 = (int)gen30::kSlot10Bytes, kSlotStride30 = (int)gen30::kSlot30Bytes;
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
        const shd::Layout& L = gen30::kShd10;
        const size_t descriptor = shd::kBlockSize + sizeof(uint16_t);
        const bool read = io.read10("shaders/shaders/" + sh + ".shd", b);
        if (!read || !bytes::fits(b.size(), L.blockB, descriptor)) return false;
        const int sizeA = bytes::u16OrZero(b, L.blockA + shd::kBlockSize);
        const int sizeB = bytes::u16OrZero(b, L.blockB + shd::kBlockSize);
        const int slotsB = (uint8_t)b[L.blockB + shd::kBlockSlots];
        it = memo.emplace(sh, std::array<int, 3>{sizeA, sizeB, slotsB}).first;
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
        const uint32_t t = s < (int)used.size() ? used[s] : bndl::kUnset, w2 = t == bndl::kUnset ? bndl::kUnset : 0;
        bytes::put32(dm.B, dm.stride * s, t);
        bytes::put32(dm.B, dm.stride * s + sizeof t, w2);
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
            bytes::put32(m.B, m.stride * k, textureIndex(tex, s.b.textures[t]));
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
    for (auto& t : tex) r.textures10.insert(detail::hex8(t));
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

}  // namespace render

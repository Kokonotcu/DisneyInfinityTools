// The 1.0 transcode dispatch: which served files a codec rewrites, in which order (M8). No engine, log or config.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../../base/text.hpp"
#include "../../convert/dispatch.hpp"
#include "../../chan.hpp"
#include "cho.hpp"
#include "../../convert/datamap.hpp"
#include "../../gameplay.hpp"
#include "../../formats/btd.hpp"
#include "../../formats/dct.hpp"
#include "../../formats/luachunk.hpp"
#include "../../formats/oct.hpp"
#include "mer.hpp"
#include "mission.hpp"
#include "../../npc.hpp"
#include "../../render.hpp"
#include "../../sx.hpp"
#include "../../sys.hpp"
#include "../../ui.hpp"
#include "../../vfx.hpp"
#include "bent.hpp"
#include "btd.hpp"
#include "octsteps.hpp"
#include "renderio.hpp"

namespace di1 {

// The cfg keys the dispatch reads (S4); the keys of TranscodeIO's steps decide whether their function objects are set.
struct TranscodeOptions {
    bool challenge = false;   // challenge10
    bool behaviour = false;   // behav10
    bool events = false;      // events10
    bool progress = false;    // progress10
    bool text = false;        // text10
    bool banm = false;        // banm10
    bool psdna = false;       // psdna30
    bool vfx = false;         // vfx10
    bool rewards = false;     // rewards10
    bool sx = false;          // sx10
    bool bmx = false;         // bmx10
    bool btx = false;         // btx10
    mer::Rules merRules;      // .mer: events10, portrefs10, ports10, pins10, gate10, toggletype10
};

using Shaders = std::vector<std::pair<std::string, std::string>>;   // per material: (1.0 shader, served shader)

// What the dispatch reads through and keeps into; an empty function object or a null table switches its step off.
struct TranscodeIO {
    Read read10;     // a 1.0 file by engine path (read10s)
    Read read30x;    // a 3.0 archive member by extract path ('3:dir/name/member')
    Read read30;     // a 3.0 asset through the engine's own open (psdna30)
    OctSteps oct;    // the .oct steps
    const mer::Table* mer = nullptr;          // the Meridian table; null: .mer and .mtc are served raw
    const render::Table* render = nullptr;    // the render table; null: "render table missing"
    std::function<const std::unordered_set<uint32_t>&(const std::string& lang)> lang30;   // text10: 3.0's key hashes
    std::function<void(const std::string& path, const std::string& bytes)> keep;   // the engine reopens bundles
    std::function<void(const std::string& path, const std::string& problem)> renderProblem;   // logged per problem
    std::function<Shaders(const std::string& path, std::string& note)> particleShaders;   // vfxchan10
    std::function<bool(const std::string& dnaFile)> isSpark;   // currency10: does a 1.0 DNA pay sparks?
    std::function<void(const std::string& entry)> dataMapEntry;   // each DataMap entry before it is normalised
};

// currency10: the engine path of a 1.0 expendable's DNA ('FastCoin_Spark', 'X.dnax', a path); "" for none.
inline std::string dnaPath(const std::string& dnaFile)
{
    std::string p = dnaFile;
    for (char& c : p) c = c == '\\' ? '/' : (char)tolower((unsigned char)c);
    if (p.empty() || p == "none") return "";
    if (p.find('/') == std::string::npos) p = "expendables_data/dna/" + p;
    if (!text::endsWith(p.c_str(), ".dnax")) p += ".dnax";
    return p;
}

// currency10: does a 1.0 DNA chunk set DNA.AgentGadgets.FastCoin.IsSpark? One that does not evaluate does not.
inline bool paysSparks(const std::string& dnaChunk)
{
    try {
        auto G = luachunk::eval(dnaChunk);
        const luachunk::Value* d = G.count("DNA") ? &G["DNA"] : nullptr;
        const luachunk::Value* ag = d ? d->get("AgentGadgets") : nullptr;
        const luachunk::Value* fc = ag ? ag->get("FastCoin") : nullptr;
        const luachunk::Value* is = fc ? fc->get("IsSpark") : nullptr;
        return is && is->t == luachunk::Value::BOOL && is->b;
    } catch (std::exception&) {
        return false;
    }
}

struct ParticleBundle {
    bool ok = false;
    Shaders shaders;    // per material: (1.0 shader, served shader)
    std::string mtb;    // the served bundle, for the engine's .mtb request right after
};

// vfxchan10: a particle package's material shaders, from its .mtb (mp) through the .mtb rule's bundle transcode.
inline ParticleBundle particleBundle(const render::Table& T, const render::IO& io, const std::string& mp,
                                     const std::string& mtb10, std::string& note)
{
    ParticleBundle b;
    if (mtb10.size() < 4 || memcmp(mtb10.data(), "BNDL", 4)) return b;
    render::Result r;
    if (!render::transcodeBundle(T, io, mtb10, "3:" + mp.substr(0, mp.find('/') + 1), r)) {
        note += "channels: " + mp + " did not transcode ";
        return b;
    }
    note += "(" + mp + ": real " + std::to_string(r.tiers["real"]) + " same-sh " + std::to_string(r.tiers["same-sh"]) +
            " generic " + std::to_string(r.tiers["generic"]) + ", served from the session) ";
    b.ok = true;
    b.shaders = std::move(r.shaders);
    b.mtb = std::move(r.mtb);
    return b;
}

// A served 1.0 file: its engine path as opened and its bytes.
struct File {
    const char* path;
    const void* data;
    size_t n;
};

// Where the dispatch writes: served bytes, log note, a .mer's .mtc (asked next: 48 of 48 in a load).
struct Output {
    std::string& out;
    std::string& note;
    std::string* mtc;
};

// One served 1.0 file on its way through the rules.
struct Request {
    const char* path;      // the engine path as opened
    std::string np;        // norm(path)
    const void* data;      // the 1.0 bytes
    size_t n;
    std::string& out;
    std::string& note;
    std::string* mtc;
    std::string actLua;    // a mission table activityTasks30 changed: the mission rule goes on from it
    const TranscodeOptions& o;
    const TranscodeIO& io;
};

using Verdict = dispatch::Answer<const char*>;
inline constexpr Verdict kRaw{nullptr};   // serve the 1.0 bytes unchanged; no later rule runs

// Notes why a file is served raw, and serves it raw.
inline Verdict raw(Request& r, const std::string& why)
{
    r.note += why;
    return kRaw;
}

namespace detail {

inline bool startsLua(const Request& r) { return r.n >= 4 && !memcmp(r.data, "\x1bLua", 4); }
inline bool startsTuple(const Request& r) { return r.n >= 4 && oct::u((const uint8_t*)r.data, 4) == oct::MAGIC; }
inline bool luaSource(const Request& r) { return r.n && ((const char*)r.data)[0] != 0x1b; }
inline bool ends(const Request& r, const char* suffix) { return text::endsWith(r.path, suffix); }

// np without a suffix the path is known to end with
inline std::string dropSuffix(const Request& r, const char* suffix)
{
    return r.np.substr(0, r.np.size() - strlen(suffix));
}

// A mission table (Mission_Data/<playset>/<line>/<name>.lua), not its line's __missiongraph.lua
inline bool missionLua(const Request& r)
{
    const bool lua = ends(r, ".lua") && r.np.rfind("mission_data/", 0) == 0;
    return lua && !ends(r, "__missiongraph.lua");
}

enum class Challenge { None, Types, Tasks, Givers, Gadgets };

// gp: which 1.0 activity file of 3.0's Challenge system this is (gameplay.hpp), checked in this order.
inline Challenge challengeKind(const Request& r)
{
    const bool lua = ((const char*)r.data)[0] != 0x1b;
    if (!gameplay::challengeTypePath(r.np).empty() && lua) return Challenge::Types;
    if (missionLua(r) && lua) return Challenge::Tasks;
    if (ends(r, ".ap") && std::string((const char*)r.data, r.n).find("\nActivityIcon=") != std::string::npos)
        return Challenge::Givers;
    if (ends(r, ".dnax") && !lua) return Challenge::Gadgets;
    return Challenge::None;
}

// The givers of the missions activityTasks30 converts (the missiongraph beside the .ap names them).
inline int apGivers(const Request& r, std::string& t)
{
    std::string graph, dir = r.np.substr(0, r.np.rfind('/') + 1);
    std::set<std::string> ok;
    if (r.io.read10(dropSuffix(r, ".ap") + ".lua", graph))
        ok = gameplay::activityGivers(graph, [&](const std::string& name, std::string& o) {
            std::string m = dir + name + ".lua";
            for (char& c : m) c = (char)tolower((unsigned char)c);
            return r.io.read10(m, o);
        });
    return gameplay::apGivers30(t, &ok);
}

// giver DNA: the converted chunk replaces t; a chunk that does not convert gives 0
inline int gadgets(std::string& t)
{
    std::string o;
    int k = 0;
    try {
        k = gameplay::dnaGadgets30(t, o);
    } catch (std::exception&) {
        k = 0;
    }
    if (k) t.swap(o);
    return k;
}

inline bool challengeMatches(const Request& r) { return r.o.challenge && r.n && challengeKind(r) != Challenge::None; }

// gp: 1.0 activities into 3.0's Challenge system. A changed mission table goes on to the mission rule.
inline Verdict challenge(Request& r)
{
    std::string t((const char*)r.data, r.n);
    int k = 0;
    const char* codec = nullptr;
    switch (challengeKind(r)) {
    case Challenge::Types:
        k = gameplay::challengeTypes30(t);
        codec = "challengetypes";
        break;
    case Challenge::Tasks:
        k = gameplay::activityTasks30(t);
        codec = "activitytasks";
        break;
    case Challenge::Givers:
        k = apGivers(r, t);
        codec = "apgivers";
        break;
    case Challenge::Gadgets:
        k = gadgets(t);
        codec = "dnagadgets";
        break;
    case Challenge::None: break;
    }
    if (!k) return dispatch::kNext;
    r.note += std::string("+") + codec + "(" + std::to_string(k) + ") ";
    if (missionLua(r)) {
        r.actLua.swap(t);
        return dispatch::kNext;
    }
    r.out.swap(t);
    return codec;
}

// A mission line's story records with empty Unlocks get ForceActivateFirstTime = true. Returns the records changed.
inline int missionGraph30(std::string& t)
{
    // exact whitespace, as 1.0 writes it (A-25)
    static const std::string emptyUnlocks = "\t\t\tUnlocks = \n\t\t\t{\n\t\t\t},\n\t\t\tline = \"";
    // as 3.0's own openers MBA_Side_Tutorial_Quest_01 and PSX_L1_Main04 have it
    static const std::string add = "\t\t\t\tForceActivateFirstTime = true,\n";
    int n = 0;
    for (size_t pos = t.find(emptyUnlocks); pos != std::string::npos; pos = t.find(emptyUnlocks, pos + 1)) {
        size_t q = pos + emptyUnlocks.size(), e = t.find('"', q);
        if (e == std::string::npos) break;
        std::string line = t.substr(q, e - q);
        for (char& c : line) c = (char)tolower((unsigned char)c);
        size_t start = t.rfind("\n\t\t{\n", pos);
        // 1.0 opened story lines itself (IN_City_DockFire's giver spawns ~550 m away); 3.0 waits for a named giver
        if (start == std::string::npos || line.find("story") == std::string::npos) continue;
        std::string rec = t.substr(start, pos - start);
        size_t ar = rec.find("\t\t\t\tAvatarReady = ");
        const bool context = rec.find("Type = \"MissionContext\"") != std::string::npos;
        const bool forced = rec.find("ForceActivateFirstTime") != std::string::npos;
        if (!context || forced || ar == std::string::npos) continue;
        t.insert(start + ar, add);
        pos += add.size();
        ++n;
    }
    return n;
}

inline bool missionGraphMatches(const Request& r) { return ends(r, "__missiongraph.lua") && luaSource(r); }

// A mission line (<line>__missiongraph.lua): story openers force-activated, behaviours 2 -> story/side.
inline Verdict missionGraph(Request& r)
{
    std::string t((const char*)r.data, r.n);
    int k = missionGraph30(t), b = r.o.behaviour ? mission::behaviour30(t) : 0;
    if (!k && !b) return kRaw;
    r.note += "+missiongraph(" + std::to_string(k) + " opening missions force-activated, " + std::to_string(b) +
              " behaviours 2 -> story/side) ";
    r.out.swap(t);
    return "missiongraph";
}

inline bool missionMatches(const Request& r)
{
    const bool wanted = r.o.events || r.o.progress || !r.actLua.empty();
    const bool lua = ends(r, ".lua") && luaSource(r);
    const bool mission = r.np.rfind("mission_data/", 0) == 0 && !strstr(r.path, "__");
    return wanted && lua && mission;
}

// A mission table: presentation events in the 3.0 enum, objective and eval lines as ProgressString (mission.hpp).
inline Verdict missionTable(Request& r)
{
    static constexpr char kMissionHead[] = "Mission =";
    std::string t = r.actLua.empty() ? std::string((const char*)r.data, r.n) : r.actLua;
    bool m = t.compare(0, sizeof(kMissionHead) - 1, kMissionHead) == 0;
    int k = r.o.events && m ? mission::luaEvents30(t) : 0, pr = r.o.progress && m ? mission::progress30(t) : 0;
    if (k) r.note += "+events(" + std::to_string(k) + " presentation events to the 3.0 enum) ";
    if (pr) r.note += "+progress(objective line <MissionName>_Desc) ";
    int tl = r.o.progress && m ? mission::taskLines30(t) : 0;
    if (tl) r.note += "+tasklines(" + std::to_string(tl) + " 1.0 eval lines as ProgressString) ";
    const bool changed = k || pr || tl;
    if (!changed && r.actLua.empty()) return kRaw;
    r.out.swap(t);
    return changed ? "mission" : "activitytasks";
}

inline bool tasksMatches(const Request& r) { return !r.actLua.empty(); }

// A mission table only activityTasks30 changed
inline Verdict tasks(Request& r)
{
    r.out.swap(r.actLua);
    return "activitytasks";
}

inline bool octMatches(const Request& r) { return ends(r, ".oct") && startsTuple(r); }

// .oct (worlds, missions, choreographies, exploders ...): octsteps.hpp. A file that does not parse is served raw.
inline Verdict octFile(Request& r)
{
    try {
        oct::File f = oct::File::parse(r.data, r.n);
        if (!transcodeOct(r.np, f, r.io.oct, r.note)) return kRaw;
        r.out = f.write();
        return "oct";
    } catch (std::exception& e) {
        r.note = std::string("oct parse failed: ") + e.what();
        return kRaw;
    }
}

inline bool merMatches(const Request& r)
{
    const bool graph = ends(r, ".mer") || ends(r, ".mtc");
    return graph && r.io.mer;
}

// A Meridian graph (.mer) and its type manifest (.mtc, rebuilt from the transcoded graph: stand-ins change counts).
inline Verdict merGraph(Request& r)
{
    bool isMtc = ends(r, ".mtc");
    std::string graph, why;
    if (isMtc) {
        if (!r.io.read10(dropSuffix(r, ".mtc") + ".mer", graph)) return kRaw;
    } else graph.assign((const char*)r.data, r.n);
    if (graph.size() < 4 || oct::u((const uint8_t*)graph.data(), 4) != oct::MAGIC) return kRaw;
    oct::File f = oct::File::parse(graph.data(), graph.size());
    int actRe = 0, actDrop = 0, act = r.o.challenge ? gameplay::activityGraph30(f, &actRe, &actDrop) : 0;   // gp
    if (act)
        r.note += "+activity->challenge(" + std::to_string(act) + " nodes, " + std::to_string(actRe) +
                  " participant links, " + std::to_string(actDrop) + " dropped) ";
    mer::Stats st;
    if (!mer::transcode(*r.io.mer, r.o.merRules, f, &st, &why)) return raw(r, "mer: " + why + " ");
    // never cut: 136 characters and 11 numbers of at most 11
    char buf[300];
    snprintf(buf, sizeof(buf), "+mer(%d namespaces, %d nodes, %d params, %d pin tables copied, %d stand-ins, %d links, "
             "%d dropped, %d events, %d fetches, %d net-sync links, %d toggle types) ", st.namespaces, st.nodes,
             st.params, st.pinsCopied, st.stubs, st.links, st.linksDropped, st.events, st.fetches, st.netSync,
             st.typed);
    r.note += buf;
    r.out = isMtc ? ::mer::mtc(f) : f.write();
    if (!isMtc && r.mtc) *r.mtc = ::mer::mtc(f);
    return isMtc ? "mtc" : "mer";
}

inline bool dctMatches(const Request& r) { return ends(r, ".dct") && r.n >= 4 && !memcmp(r.data, "DICT", 4); }

// A localisation table: keys re-hashed under 3.0's seed, named by stringids beside it; text10 adds lang/main keys.
inline Verdict dctTable(Request& r)
{
    static constexpr char kPcCon[] = "_pc_con.dct";
    const std::string& p = r.np;
    std::string ids;
    size_t us = p.rfind('_'), sl = p.rfind('/');
    if (us == std::string::npos || sl == std::string::npos || us < sl) return kRaw;
    const bool pcCon = ends(r, kPcCon);
    std::string plat = pcCon ? "_win32.dct" : p.substr(us);   // its stand-in (hkOpen)
    if (!r.io.read10(p.substr(0, sl + 1) + "stringids" + plat, ids)) return raw(r, "dct: no stringids beside it ");
    std::string t((const char*)r.data, r.n);
    int dropped = 0, kept = dct::reseed(t, ids, dct::SEED30, &dropped);
    if (kept < 0) return kRaw;
    r.note += "+dct(" + std::to_string(kept) + " keys re-hashed, " + std::to_string(dropped) + " unnamed dropped) ";
    std::string m10, mids, lang = p.substr(sl + 1, (pcCon ? p.size() - (sizeof(kPcCon) - 1) : us) - sl - 1);
    const bool main10 = r.o.text && r.io.read10("lang/main/" + lang + "_win32.dct", m10);
    if (main10 && r.io.read10("lang/main/stringids_win32.dct", mids)) {
        int k = dct::supplement(t, m10, mids, r.io.lang30(lang));
        r.note += "+text10(" + std::to_string(k) + " 1.0 lang/main keys no 3.0 table has) ";
    }
    r.out.swap(t);
    return "dct";
}

inline bool btdMatches(const Request& r) { return ends(r, ".btd") || ends(r, ".btm"); }

// Collision blocks in the 3.0 layout and the .btm rebuilt from them: raw 1.0 collision gave a world without ground.
inline Verdict collision(Request& r)
{
    bool isBtm = ends(r, ".btm");
    std::string src, conv;
    if (isBtm) {
        if (!r.io.read10(dropSuffix(r, ".btm") + ".btd", src)) return kRaw;
    } else src.assign((const char*)r.data, r.n);
    if (!di1::btd::convert(src, conv)) return raw(r, "btd: not the 1.0 block layout ");
    // never cut: 14 characters and a number of at most 10
    char buf[64];
    snprintf(buf, sizeof(buf), "+btd(%u blocks) ", (unsigned)::btd::blocks(conv, ::btd::kLayout30).size());
    r.note += buf;
    r.out = isBtm ? ::btd::btm(conv) : conv;
    return isBtm ? "btm" : "btd";
}

inline bool bentMatches(const Request& r) { return ends(r, ".bent") && startsTuple(r); }

// An entity description (.bent 4.1 -> 4.1f, bent.hpp): characters, playset avatars, choreographies
inline Verdict entity(Request& r)
{
    oct::File f = oct::File::parse(r.data, r.n);
    std::string why;
    if (!di1::bent::transcode(f, r.np, &why)) return raw(r, "bent: " + why + " ");
    r.out = f.write();
    return "bent";
}

inline bool banmMatches(const Request& r) { return r.o.banm && ends(r, ".banm") && startsTuple(r); }

// A blended animation (.banm 1.0f -> 1.0g, cho.hpp): 3.0 dropped every 1.0 one at its version gate
inline Verdict blend(Request& r)
{
    oct::File f = oct::File::parse(r.data, r.n);
    if (!cho::banm30(f, r.np)) return raw(r, "banm: not 1.0f ");
    r.out = f.write();
    return "banm";
}

inline bool mtbMatches(const Request& r) { return ends(r, ".mtb") && r.n >= 4 && !memcmp(r.data, "BNDL", 4); }

// BNDL layout (render.hpp's bundle reader): chunk count at +12, chunks from +16, a chunk's size at its +8
inline constexpr size_t kBndlCount = 12;
inline constexpr size_t kBndlChunks = 16;
inline constexpr size_t kChunkHeader = 16;
inline constexpr size_t kChunkSize = 8;

// Does a bundle hold a MATP chunk? A chunk past the end ends the walk; o never wraps (A-26).
inline bool hasMaterials(const void* data, size_t n)
{
    bool hasMatp = false;
    const size_t nc = n >= kBndlChunks ? oct::u((const uint8_t*)data + kBndlCount, 4) : 0;
    for (size_t o = kBndlChunks, c = 0; c < nc && bytes::fits(n, o, kChunkHeader); ++c) {
        if (!memcmp((const char*)data + o, "MATP", 4)) hasMatp = true;
        uint32_t cs = oct::u((const uint8_t*)data + o + kChunkSize, 4);
        if (!cs || cs > n - o) break;
        o += cs;
    }
    return hasMatp;
}

// A texture-only bundle (UI images) is laid out as in 3.0: served raw; the _holo stand-in gets the asked name.
inline Verdict textureBundle(Request& r)
{
    std::string stem = r.np.substr(r.np.rfind('/') + 1);
    stem.resize(stem.size() - strlen(".mtb"));
    const bool holo = r.np.rfind("ui/charactericons/", 0) == 0 && ends(r, "_holo.mtb");
    if (!holo) return kRaw;
    r.out = ui::texBundleAs(std::string((const char*)r.data, r.n), stem);
    if (r.out.empty()) return kRaw;
    r.note += "+texname(" + stem + ") ";
    return "mtb";
}

// A material bundle outside any asset (particles/<x>.mtb) onto 3.0 shaders: raw, it hit int3 in Shader_LookupCrc.
inline Verdict materialBundle(Request& r)
{
    if (!hasMaterials(r.data, r.n)) return textureBundle(r);
    if (!r.io.render) return raw(r, "render table missing ");
    const std::string& p = r.np;
    render::IO io = bundleIO(r.io.read10, r.io.read30x);
    render::Result res;
    // generic donors from the same 3.0 asset family
    const std::string donors = "3:" + p.substr(0, p.find('/') + 1);
    if (!render::transcodeBundle(*r.io.render, io, std::string((const char*)r.data, r.n), donors, res)) {
        for (auto& pr : res.problems)
            if (r.io.renderProblem) r.io.renderProblem(p, pr);
        r.note += "render FAILED ";
        return kRaw;
    }
    if (r.io.keep) r.io.keep(p, res.mtb);   // the engine reopens bundles; transcode once
    // never cut: 51 characters and 4 numbers of at most 11
    char buf[160];
    snprintf(buf, sizeof(buf), "+materials(real %d same-sh %d generic %d, %u 1.0 textures) ", res.tiers["real"],
             res.tiers["same-sh"], res.tiers["generic"], (unsigned)res.textures10.size());
    r.note += buf;
    r.out = res.mtb;
    return "mtb";
}

inline bool psDnaMatches(const Request& r) { return r.o.psdna && ends(r, "_ps.dnax"); }

// A 1.0 playset avatar DNA 3.0 ships only as its base: the base, naming the 1.0 _PS model (npc::psDna30)
inline Verdict playsetDna(Request& r)
{
    std::string base, why;
    if (!r.io.read30(dropSuffix(r, "_ps.dnax") + ".dnax", base)) return kRaw;
    if (!npc::psDna30(std::string((const char*)r.data, r.n), base, r.out, &why)) return raw(r, "psdna30: " + why + " ");
    r.note += "+psdna30(3.0 base, " + why + ") ";
    return "psdna30";
}

inline bool particlesMatches(const Request& r) { return r.o.vfx && ends(r, ".particledata"); }

// A particle package, trailer v12 -> v47 (3.0 rejects 1.0's); vfxchan10 re-lays channels for each material's shader.
inline Verdict particles(Request& r)
{
    vfx::Stats st;
    vfx::ChanMap cm;
    Shaders shaders = r.io.particleShaders ? r.io.particleShaders(r.np, r.note) : Shaders();
    const TranscodeIO& io = r.io;
    if (!shaders.empty())
        cm = [&shaders, &io](uint16_t mat, int n10, std::vector<int>& chans) {
            if (mat >= shaders.size()) return false;
            return chan::layout(io.read10, io.read30x, shaders[mat].first, shaders[mat].second, n10, chans);
        };
    if (!vfx::transcode(std::string((const char*)r.data, r.n), r.out, &st, &cm)) return kRaw;
    r.note += "+particles(" + std::to_string(st.emitters) + " emitters, " + std::to_string(st.effects) + " effects, " +
              std::to_string(st.params) + " params, " + std::to_string(st.keys) + " keys, " +
              std::to_string(st.meshes) + " mesh emitters, " + std::to_string(st.rechanneled) +
              " re-channeled for their 3.0 shader, " + std::to_string(st.constChans) + " constant channels) ";
    return "particledata";
}

inline bool meshMatches(const Request& r) { return r.o.vfx && ends(r, ".p3d"); }

// A particle mesh (.p3d)
inline Verdict mesh(Request& r)
{
    return vfx::p3d30(std::string((const char*)r.data, r.n), r.out) ? "p3d" : nullptr;
}

inline bool groupsMatches(const Request& r)
{
    const bool package = r.np.rfind("expendables_data/", 0) == 0 && ends(r, ".lua");
    return r.o.rewards && package && startsLua(r);
}

// An expendables package: its 1.0 groups in the keys 3.0's group reader takes (sys::groups30); none: passed on.
inline Verdict groups(Request& r)
{
    int over = 0, sparks = 0;
    int k = sys::groups30(std::string((const char*)r.data, r.n), r.out, &over, r.io.isSpark, &sparks);
    if (!k) return dispatch::kNext;
    std::string dropped = over ? ", " + std::to_string(over) + " spawns past the 4th dropped" : "";
    std::string paying = sparks ? ", " + std::to_string(sparks) + " spark pickups paying INV_SPARKS" : "";
    r.note += "+groups30(" + std::to_string(k - sparks) + " groups" + dropped + paying + ") ";
    return "expgroups";
}

inline bool dataMapMatches(const Request& r) { return ends(r, "datamap.lua") && startsLua(r); }

// A playset DataMap (Lua chunk): its entries into the path forms 3.0's handlers take (datamap.hpp)
inline Verdict dataMap(Request& r)
{
    const TranscodeIO& io = r.io;
    int k = luachunk::rewrite(std::string((const char*)r.data, r.n), r.out, [&io](std::string& s) {
        if (io.dataMapEntry) io.dataMapEntry(s);
        return datamap::normalise(s);
    });
    r.note += std::to_string(k) + " entries normalised ";
    return k ? "datamap" : nullptr;
}

inline bool slangMatches(const Request& r) { return r.o.sx && ends(r, ".sx") && sx::is10(r.data, r.n); }

// A 1.0 Slang script (sx.hpp): 3.0's fix-up FUN_007366f0 takes only 0x100d containers
inline Verdict slang(Request& r)
{
    std::string why;
    int k = sx::transcode(r.data, r.n, r.np, r.out, &why);
    if (k < 0) return raw(r, "sx: " + why + " ");
    r.note += "+sx(" + std::to_string(k) + " imports) ";
    return "sx";
}

inline bool isBmx(const Request& r) { return r.o.bmx && ends(r, ".bmx"); }
inline bool behaviourMatches(const Request& r)
{
    const bool isBtx = r.o.btx && ends(r, ".btx");
    const bool table = isBmx(r) || isBtx;
    return table && startsLua(r);
}

// A 1.0 behavior map / behavior tree (sx.hpp): 3.0's loaders drop tables without Version 763
inline Verdict behaviour(Request& r)
{
    std::string why;
    const bool bmx = isBmx(r);
    const TranscodeIO& io = r.io;
    auto exists = [&io](const std::string& p) {
        std::string s;
        return io.read10(p, s);
    };
    int k = bmx ? sx::ai::bmx(r.data, r.n, exists, r.out, &why) : sx::ai::btx(r.data, r.n, exists, r.out, &why);
    if (k < 0) return raw(r, std::string(bmx ? "bmx: " : "btx: ") + why + " ");
    r.note += (bmx ? "+bmx(" : "+btx(") + std::to_string(k) + (bmx ? " behaviors) " : " tables) ");
    return bmx ? "bmx" : "btx";
}

using Rule = dispatch::Rule<Request, const char*>;

// First match wins, in this order (C3, B3). Only challenge (a changed mission table) and groups pass a file on.
inline constexpr Rule kRules[] = {
    {challengeMatches, challenge},
    {missionGraphMatches, missionGraph},
    {missionMatches, missionTable},
    {tasksMatches, tasks},
    {octMatches, octFile},
    {merMatches, merGraph},
    {dctMatches, dctTable},
    {btdMatches, collision},
    {bentMatches, entity},
    {banmMatches, blend},
    {mtbMatches, materialBundle},
    {psDnaMatches, playsetDna},
    {particlesMatches, particles},
    {meshMatches, mesh},
    {groupsMatches, groups},
    {dataMapMatches, dataMap},
    {slangMatches, slang},
    {behaviourMatches, behaviour},
};

}  // namespace detail

// A served 1.0 file in 3.0's form: the codec when out holds the bytes to serve; nullptr: the 1.0 bytes.
inline const char* transcode(const File& f, const Output& w, const TranscodeOptions& o, const TranscodeIO& io)
{
    Request r{f.path, text::norm(f.path), f.data, f.n, w.out, w.note, w.mtc, std::string(), o, io};
    return dispatch::firstMatch(detail::kRules, r, (const char*)nullptr);
}

}  // namespace di1

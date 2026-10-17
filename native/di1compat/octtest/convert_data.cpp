// Data converters: GameDB, DataMap, Lua and script transforms written or printed for the oracles and golden files.
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "../games/di1/cho.hpp"
#include "../convert/datamap.hpp"
#include "../formats/chd.hpp"
#include "../base/text.hpp"
#include "../formats/datamap.hpp"
#include "../games/di1/chestinfo.hpp"
#include "../games/di1/gamedb.hpp"
#include "../games/di1/groups.hpp"
#include "../games/di1/perceptions.hpp"
#include "../games/di1/psdna.hpp"
#include "../games/di1/supplement.hpp"
#include "../formats/luachunk.hpp"
#include "../games/di1/zones.hpp"
#include "../formats/slang.hpp"
#include "../games/di1/aitables.hpp"
#include "../games/di1/avperc.hpp"
#include "../games/di1/sxkeys.hpp"
#include "../games/di1/sxnatives.hpp"
#include "../games/di1/transcode.hpp"
#include "files.hpp"
#include "modes.hpp"
#include "run.hpp"
#include "tables.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

constexpr int kTriggerEventTypes = 14;   // TriggerEvent's type map: 1..14 (sxcheck.py: the exes' tables)
constexpr char kPsDna[] = "_ps.dnax";
constexpr size_t kPsDnaLen = sizeof kPsDna - 1;

// GROUPS30_DNA=<1.0 extract assets dir>: currency10 too, the plugin's rule (dnaPath, paysSparks).
bool paysSparksIn(const char* dnaRoot, const std::string& d)
{
    std::string p = di1::dnaPath(d);
    if (p.empty()) return false;
    const std::string dnaDir = "expendables_data/dna/";   // the extract unpacks dna/dna.zip one folder deeper
    if (p.rfind(dnaDir, 0) == 0) p.insert(dnaDir.size(), "dna/");
    return di1::paysSparks(slurp(fs::path(dnaRoot) / p));
}

// One line of table sizes: each section of c and its entry count.
template <size_t N>
void printSections(const chd::V* c, const char* const (&keys)[N])
{
    for (const char* k : keys) printf("%s %zu  ", k, c && field(*c, k) ? field(*c, k)->named.size() : 0);
}

// sx's embedded tables beside the scripts: natives, keys, 1.0 behaviours, the TriggerEvent type map.
void writeSxTables(const fs::path& out)
{
    std::ofstream nt(out / "natives.tsv"), kt(out / "keys.tsv");
    for (auto& n : di1::sxnatives::g_natives) nt << n.name << '\t' << (int)n.args << '\t' << (int)n.results << '\n';
    for (auto& n : di1::sxnatives::g_ports)
        nt << "port:" << n.name << '\t' << (int)n.args << '\t' << (int)n.results << '\n';
    for (auto& k : di1::sxkeys::KEYS30) kt << k.name << '\t' << k.id << '\n';
    std::ofstream bt(out / "beh10.tsv");
    for (auto* n : di1::sxnatives::BEH10) bt << n << '\n';
    std::ofstream et(out / "events.tsv");
    for (int t = 1; t <= kTriggerEventTypes; ++t) et << t << '\t' << di1::sxnatives::eventTo30(t) << '\n';
}

// FlipMotionName on its edge cases; the wrong ones.
int checkFlips()
{
    const char* flips[][2] = {{"HIT_BACK_HEAVY", "HIT_FRONT_HEAVY"}, {"FRONT", "BACK"}, {"BACK", "FRONT"},
                              {"FRONTBACK", "BACKFRONT"}, {"FRON", "FRON"}, {"BAC", "BAC"}, {"", ""}};
    int bad = 0;
    for (auto& f : flips) {
        char b[64];
        di1::sxnatives::flipMotionName(f[0], b, sizeof(b));
        if (!strcmp(b, f[1])) continue;
        printf("FlipMotionName(%s) = %s, want %s\n", f[0], b, f[1]);
        ++bad;
    }
    return bad;
}

struct SxRun {
    size_t ok = 0, bad = 0, imports = 0;
};

// Every .sx of dir through slang::upgrade into <out>/<dir name>/ (characters/ and scripts/ share names).
void transcodeScripts(const fs::path& out, const char* dir, SxRun& r)
{
    fs::path sub = out / fs::path(dir).filename();
    fs::create_directories(sub);
    for (auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".sx") continue;
        std::string b = slurp(e.path()), o, why;
        std::string name = e.path().parent_path().filename().string() + "/" + e.path().filename().string();
        int k = slang::upgrade(b.data(), b.size(), name, o, &why);
        if (k < 0) {
            ++r.bad;
            printf("REFUSED %s: %s\n", e.path().string().c_str(), why.c_str());
            continue;
        }
        ++r.ok;
        r.imports += k;
        writeFile(sub / e.path().filename(), o);
    }
}

// The comma-joined lower-case perception names of a list.
std::string percNames(const di1::avperc::PercList& l)
{
    std::set<std::string> s;
    for (auto& e : l) {
        if (!e.on.empty()) s.insert(text::lower(e.on));
        if (!e.off.empty()) s.insert(text::lower(e.off));
    }
    std::string o;
    for (auto& n : s) o += (o.empty() ? "" : ",") + n;
    return o;
}

// The perceptions added to one 3.0 list, sorted by name: 'name|0' or 'name|1'.
template <class Adds>
std::string percAdded(const Adds& adds)
{
    std::map<std::string, std::string> a;
    for (auto& [n, h] : adds) a[text::lower(n)] = n + (h ? "|1" : "|0");
    std::string s;
    for (auto& [k, v] : a) s += (s.empty() ? "" : ",") + v;
    return s;
}

bool dataMapOf(const char* path, datamap::DataMap& m) { return datamap::parse(slurp(path), m); }

}  // namespace

// di1::groups::groups30 over 1.0 packages: their groups with 3.0 keys, into <out>. A throw fails it (OT-04).
int convertGroups30(const Args& a)
{
    int groups = 0, over = 0, files = 0, sparks = 0, failed = 0;
    const char* dnaRoot = env("GROUPS30_DNA");
    std::function<bool(const std::string&)> isSpark;
    if (dnaRoot) isSpark = [dnaRoot](const std::string& d) { return paysSparksIn(dnaRoot, d); };
    for (int i = 3; i < a.argc; ++i) {
        std::string out;
        int k = 0, s = 0;
        try {
            k = di1::groups::groups30(slurp(a.argv[i]), out, &over, isSpark, &s);
        } catch (std::exception& e) {
            ++failed;
            printf("%s: %s\n", a.argv[i], e.what());
        }
        if (!k) continue;
        ++files;
        groups += k - s;
        sparks += s;
        writeFile(fs::path(a.argv[2]) / fs::path(a.argv[i]).filename(), out);
    }
    printf("%d packages with 1.0 groups, %d groups given 3.0 keys, %d spawns past the 4th, %d spark pickups paying "
           "INV_SPARKS\n", files, groups, over, sparks);
    return failed ? 1 : 0;
}

int convertCurrency10(const Args& a)
{
    auto g = luachunk::eval(slurp(a.argv[2]));
    for (auto& [f, c] : di1::currencies10(g["ZoneList"])) printf("%s -> %s\n", f.c_str(), c.c_str());
    return 0;
}

// di1::sxkeys::keyId of every line (keycheck.py recomputes the ids on its own).
int convertSxKeys(const Args& a)
{
    std::ifstream in(a.argv[2]);
    std::ofstream o(a.argv[3]);
    for (std::string s; std::getline(in, s);)
        if (!s.empty()) o << s << '\t' << di1::sxkeys::keyId(s.c_str()) << '\n';
    return 0;
}

// BMX-1 / BTX-1 over every .bmx and .btx -> <out>/<file>.lua (aicheck.py).
int convertAi(const Args& a)
{
    fs::path out = a.argv[2], scripts = a.argv[4];
    fs::create_directories(out);
    size_t ok = 0, bad = 0;
    auto exists = [&scripts](const std::string& p) { return fs::exists(scripts / fs::path(p).filename()); };
    for (auto& e : fs::directory_iterator(a.argv[3])) {
        std::string ext = e.path().extension().string();
        if (ext != ".bmx" && ext != ".btx") continue;
        std::string b = slurp(e.path()), o, why;
        int k = ext == ".bmx" ? di1::aitables::bmx(b.data(), b.size(), exists, o, &why)
                              : di1::aitables::btx(b.data(), b.size(), exists, o, &why);
        if (k < 0) {
            ++bad;
            printf("REFUSED %s: %s\n", e.path().string().c_str(), why.c_str());
            continue;
        }
        ++ok;
        writeFile(out / (e.path().filename().string() + ".lua"), o);
    }
    printf("ai tables transcoded %zu, refused %zu\n", ok, bad);
    return bad ? 1 : 0;
}

// SX-1 over every .sx of the dirs (sxcheck.py checks the files), and the embedded tables.
int convertSx(const Args& a)
{
    fs::path out = a.argv[2];
    fs::create_directories(out);
    SxRun r;
    for (int i = 3; i < a.argc; ++i) transcodeScripts(out, a.argv[i], r);
    writeSxTables(out);
    r.bad += checkFlips();
    printf("sx transcoded %zu (%zu imports), refused %zu; %zu natives, %zu ports, %zu keys\n", r.ok, r.imports, r.bad,
           std::size(di1::sxnatives::g_natives), std::size(di1::sxnatives::g_ports), std::size(di1::sxkeys::KEYS30));
    return r.bad ? 1 : 0;
}

// GLB-1: 1.0 chestinfo.bin -> Lua; an independent parse counts its three tables.
int convertChest(const Args& a)
{
    std::string out, why;
    if (!di1::chestinfo::chestInfoLua(slurp(a.argv[2]), out, &why)) {
        printf("FAILED: %s\n", why.c_str());
        return 1;
    }
    writeFile(a.argv[3], out);
    chd::V t = chd::lua(out);
    constexpr const char* kSections[] = {"Resource", "Choreography", "Chests"};
    printSections(field(t, "ChestInfo"), kSections);
    printf("(%zu B)\n", out.size());
    return 0;
}

// 1.0 perceptioninfo.bin -> Lua against 3.0's names and groups; an independent parse counts its sections.
int convertPerc(const Args& a)
{
    std::set<std::string> names;
    std::map<std::string, std::string> groups;
    std::istringstream nl(slurp(a.argv[3])), gl(slurp(a.argv[4]));
    for (std::string l; std::getline(nl, l);)
        if (!l.empty()) names.insert(l);
    for (std::string l; std::getline(gl, l);)
        if (l.find('\t') != std::string::npos) groups.emplace(l.substr(0, l.find('\t')), l.substr(l.find('\t') + 1));
    di1::perceptions::Perc30 p30{[&](const std::string& s) { return names.count(s) != 0; },
                                 [&](const std::string& s) {
                                     auto it = groups.find(s);
                                     return it == groups.end() ? std::string() : it->second;
                                 }};
    std::string out, why;
    int added = 0;
    if (!di1::perceptions::perceptionInfoLua(slurp(a.argv[2]), p30, out, added, &why)) {
        printf("FAILED: %s\n", why.c_str());
        return 1;
    }
    writeFile(a.argv[5], out);
    chd::V t = chd::lua(out);
    constexpr const char* kSections[] = {"StatePriority", "TypePriority", "Detect", "TargetMode",
                                         "Reticule",      "Ricochet",     "PartnerCheck", "InteractionCheck",
                                         "Groups",        "UIText",       "Perceptions"};
    printSections(field(t, "PerceptionInfo"), kSections);
    printf("(added %d, %zu B)\n", added, out.size());
    return 0;
}

// PS-2: a 1.0 DataMap's constants normalised; the kinds rewritten.
int convertDataMap(const Args& a)
{
    std::string in = slurp(a.argv[2]), out;
    std::map<std::string, int> kinds;
    int n = luachunk::rewrite(in, out, [&](std::string& s) {
        std::string before = s;
        if (!datamap::normalise(s)) return false;
        kinds[before.substr(0, before.find('|'))]++;
        return true;
    });
    for (auto& [k, c] : kinds) printf("%s %d\n", k.c_str(), c);
    printf("%d constants rewritten, %zu -> %zu B\n", n, in.size(), out.size());
    if (a.argc > 3) writeFile(a.argv[3], out);
    return 0;
}

// The locks / bitmasks triggers the 1.0 DataMaps register.
int convertDbTriggers(const Args& a)
{
    const auto add = [](std::string& s) {
        di1::gamedb::dbTrigger10Add(s);
        return datamap::normalise(s);
    };
    for (int i = 2; i < a.argc; ++i) {
        std::string in = slurp(a.argv[i]), out;
        luachunk::rewrite(in, out, add);
    }
    for (auto& p : di1::gamedb::dbTriggers10())
        if (di1::gamedb::dbTrigger10(p.c_str(), "locks__lua.lua") ||
            di1::gamedb::dbTrigger10(p.c_str(), "bitmasks.lua"))
            printf("%s\n", p.c_str());
    const bool match = di1::gamedb::dbTrigger10("GameDB/Incredibles/locks__lua.lua", "locks__lua.lua") &&
                       di1::gamedb::dbTrigger10("GameDB/Incredibles/bitmasks.lua", "bitmasks.lua");
    printf("%s\n", match ? "the former Incredibles triggers match" : "the former Incredibles triggers DO NOT match");
    return 0;
}

// di1::cho::Aliases as the plugin feeds it from a DataMap, then each engine path resolved.
int convertChoAlias(const Args& a)
{
    std::string in = slurp(a.argv[2]), out;
    di1::cho::Aliases al;
    luachunk::rewrite(in, out, [&](std::string& s) {
        al.add(s);
        return datamap::normalise(s);
    });
    int hit = 0;
    for (int i = 3; i < a.argc; ++i) {
        std::string r = al.resolve(a.argv[i]);
        hit += !r.empty();
        printf("%s -> %s\n", a.argv[i], r.empty() ? "(none)" : r.c_str());
    }
    printf("%zu aliases, %d of %d paths resolved\n", al.map.size(), hit, a.argc - 3);
    return 0;
}

// datamap::parse of each DataMap: one line per file and include.
int convertDmEval(const Args& a)
{
    for (int k = 2; k < a.argc; ++k) {
        datamap::DataMap m;
        std::string why;
        if (!datamap::parse(slurp(a.argv[k]), m, &why)) {
            printf("%s: FAILED %s\n", a.argv[k], why.c_str());
            return 1;
        }
        for (auto& [key, r] : m) {
            for (auto& f : r.files) printf("%s|%s|F|%s\n", key.first.c_str(), key.second.c_str(), f.c_str());
            for (auto& i : r.includes) printf("%s|%s|I|%s\n", key.first.c_str(), key.second.c_str(), i.c_str());
        }
    }
    return 0;
}

// The playset's GameDB text; with a 1.0 CoreDataMap, its core expendables packages too.
int convertPlayset(const Args& a)
{
    datamap::DataMap m10, tb, other, core10;
    bool ok = dataMapOf(a.argv[2], m10) && dataMapOf(a.argv[3], tb) && dataMapOf(a.argv[4], other);
    if (ok && a.argc > 6) ok = dataMapOf(a.argv[6], core10);
    if (!ok) {
        printf("DataMap eval failed\n");
        return 1;
    }
    di1::supplement::PlaysetRecord rec{a.argv[5], di1::supplement::gamedb30(m10, tb).playset,
                                       di1::supplement::coreExpendables10(core10), true};
    std::string t = di1::supplement::playsetText(rec, tb, other);
    fwrite(t.data(), 1, t.size(), stdout);
    return 0;
}

// di1::avperc::percAdds: the perceptions each 3.0 avatar list gains from its 1.0 list in a zone.
int convertAvPerc(const Args& a)
{
    std::vector<di1::avperc::PercList> l10, l30;
    std::string why;
    if (!di1::avperc::percLists(slurp(a.argv[2]), l10, &why) || !di1::avperc::percLists(slurp(a.argv[3]), l30, &why)) {
        printf("btx eval failed: %s\n", why.c_str());
        return 1;
    }
    auto adds = di1::avperc::percAdds(l10, l30, a.argv[4]);
    std::set<std::string> lines;
    for (size_t j = 0; j < l30.size(); ++j) {
        if (adds[j].first < 0) continue;
        lines.insert("L " + std::to_string(l30[j].size()) + " [" + percNames(l30[j]) + "] <- " +
                     std::to_string(l10[adds[j].first].size()) + ": " + percAdded(adds[j].second));
    }
    printf("%zu 1.0 lists, %zu 3.0 lists\n", l10.size(), l30.size());
    for (auto& l : lines) printf("%s\n", l.c_str());
    return 0;
}

// di1::supplement::gamedb30: the 1.0 DataMap's playset and actor entries against 3.0's ToyBox.
int convertGameDb(const Args& a)
{
    datamap::DataMap m10, m30;
    if (!dataMapOf(a.argv[2], m10) || !dataMapOf(a.argv[3], m30)) {
        printf("DataMap eval failed\n");
        return 1;
    }
    di1::supplement::GameDb g = di1::supplement::gamedb30(m10, m30);
    std::set<std::string> lines;
    for (auto& [t, p] : g.playset) lines.insert("P " + t + " " + p);
    for (auto& [actor, v] : g.actor)
        for (auto& [t, p] : v) lines.insert("A " + actor + " " + t + " " + p);
    for (auto& l : lines) printf("%s\n", l.c_str());
    return 0;
}

// Each 1.0 *_ps.dnax 3.0 lacks, served from its 3.0 base DNA when psDna30 can.
int convertPsDna(const Args& a)
{
    int ok = 0, no = 0;
    for (auto& e : fs::directory_iterator(a.argv[2])) {
        std::string n = e.path().filename().string();
        const bool ps = n.size() > kPsDnaLen && n.compare(n.size() - kPsDnaLen, kPsDnaLen, kPsDna) == 0;
        if (!ps || fs::exists(fs::path(a.argv[3]) / n)) continue;
        fs::path base = fs::path(a.argv[3]) / (n.substr(0, n.size() - kPsDnaLen) + ".dnax");
        if (!fs::exists(base)) continue;
        std::string out, why;
        bool r = di1::psdna::psDna30(slurp(e.path()), slurp(base), out, &why);
        printf("%s: %s %s\n", n.c_str(), r ? "ok" : "kept 1.0", why.c_str());
        if (r) writeFile(fs::path(a.argv[4]) / n, out);
        (r ? ok : no)++;
    }
    printf("psdna: %d served from the 3.0 base, %d kept\n", ok, no);
    return 0;
}

}  // namespace octtest

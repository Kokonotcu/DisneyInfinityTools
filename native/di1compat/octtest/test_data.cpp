// Data acceptance tests: PS-5 keep, the chd compiler, a .dct table, the mission chain order, gp load scans.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include "../formats/chd.hpp"
#include "../formats/dct.hpp"
#include "../formats/gamedblua.hpp"
#include "../gameplay.hpp"
#include "../games/di1/mission.hpp"
#include "../base/text.hpp"
#include "../third_party/miniz/miniz.h"
#include "files.hpp"
#include "modes.hpp"
#include "run.hpp"
#include "tables.hpp"
#include "tuples.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

// ---- --keep ----

// chd::V trees equal (the chd parser is independent of gamedblua::keepRecords' scanner).
bool sameTree(const chd::V& a, const chd::V& b)
{
    const bool sameScalar = a.t == b.t && a.i == b.i && !memcmp(&a.f, &b.f, sizeof a.f);
    const bool sameSizes = a.named.size() == b.named.size() && a.items.size() == b.items.size();
    if (!sameScalar || a.s != b.s || !sameSizes) return false;
    for (size_t k = 0; k < a.named.size(); ++k)
        if (a.named[k].first != b.named[k].first || !sameTree(a.named[k].second, b.named[k].second)) return false;
    for (size_t k = 0; k < a.items.size(); ++k)
        if (!sameTree(a.items[k], b.items[k])) return false;
    return true;
}

// A GameDB table's Data.Records, or null.
const chd::V* records(const chd::V& t)
{
    const chd::V* data = field(t, "Data");
    return data ? field(*data, "Records") : nullptr;
}

// The UIDs of the 3.0 tables argv[3..]; false (printed) when one has no Records.
bool uids30(const Args& a, std::set<uint32_t>& excl)
{
    for (int i = 3; i < a.argc; ++i) {
        chd::V t = chd::lua(slurp(a.argv[i]));
        const chd::V* r = records(t);
        if (!r) {
            printf("no Records in %s\n", a.argv[i]);
            return false;
        }
        for (auto& x : r->items)
            if (auto u = field(x, "UID")) excl.insert((uint32_t)u->i);
    }
    return true;
}

// The kept records in order against the 1.0 records no 3.0 table has; want gets their count.
int recordMismatches(const chd::V& ri, const chd::V& ro, const std::set<uint32_t>& excl, int& want)
{
    int bad = 0;
    size_t o = 0;
    for (auto& x : ri.items) {
        const chd::V* u = field(x, "UID");
        if (!u || excl.count((uint32_t)u->i)) continue;
        ++want;
        if (o >= ro.items.size()) {
            ++bad;
            continue;
        }
        const bool same = sameTree(x, ro.items[o]);
        ++o;
        if (!same) ++bad;
    }
    return bad;
}

// Both fields missing, or both there and equal.
bool sameField(const chd::V* a, const chd::V* b)
{
    if (!a || !b) return !a && !b;
    return sameTree(*a, *b);
}

int headerMismatches(const chd::V& in, const chd::V& got)
{
    int bad = 0;
    for (const char* k : {"DBName", "Type", "Version", "ID", "TableName"}) {
        const chd::V *a = field(*field(in, "Data"), k), *b = field(*field(got, "Data"), k);
        if (sameField(a, b)) continue;
        ++bad;
        printf("header %s differs\n", k);
    }
    return bad;
}

// ---- --mischain ----

bool isMission(const std::string& t)
{
    constexpr char kMission[] = "Mission =";   // the plugin's gate for the mission rules
    return t.compare(0, sizeof kMission - 1, kMission) == 0;
}

// ---- --gpscan ----

constexpr size_t kMemberNameMax = 400;
constexpr char kLuaChunk = 0x1b;   // a compiled Lua chunk starts with ESC

struct GpScan {
    std::vector<std::string> names30;   // 3.0 surfacetypes, as --surf reads them
    std::map<std::string, int> seen, changed, failed;
};

bool endsWith(const std::string& m, const char* s)
{
    size_t k = strlen(s);
    return m.size() >= k && !m.compare(m.size() - k, k, s);
}

// Archive member suffix -> gp transform kind; a missiongraph .lua is none.
constexpr const char* kMemberKinds[][2] = {{".oct", "oct"}, {".mer", "mer"}, {".ap", "ap"}, {".dnax", "dnax"}};

const char* memberKind(const std::string& m)
{
    for (auto& k : kMemberKinds)
        if (endsWith(m, k[0])) return k[1];
    return endsWith(m, ".lua") && !endsWith(m, "__missiongraph.lua") ? "lua" : nullptr;
}

// What the gp transforms of kind change in t, or "".
std::string gpChange(const char* kind, std::string& t, const GpScan& s)
{
    const bool chunk = !t.empty() && t[0] == kLuaChunk;
    if (!strcmp(kind, "oct") && isTuple(t)) {
        oct::File f = parse(t);
        int fl = 0, cv = gameplay::curves30(f, &fl), sf = gameplay::surfaces30(f, s.names30);
        if (cv || fl || sf)
            return "curves " + std::to_string(cv) + " flags " + std::to_string(fl) + " surf " + std::to_string(sf);
    } else if (!strcmp(kind, "mer") && isTuple(t)) {
        oct::File f = parse(t);
        if (int k = gameplay::activityGraph30(f)) return "activity nodes " + std::to_string(k);
    } else if (!strcmp(kind, "ap")) {
        if (int k = gameplay::apGivers30(t)) return "givers " + std::to_string(k);
    } else if (!strcmp(kind, "dnax") && chunk) {
        std::string o;
        if (int k = gameplay::dnaGadgets30(t, o)) return "gadgets " + std::to_string(k);
    } else if (!strcmp(kind, "lua") && !t.empty() && !chunk) {
        if (int k = gameplay::activityTasks30(t)) return "tasks " + std::to_string(k);
    }
    return "";
}

// One member of an archive through its transforms: FAIL on a throw, else what changed.
void scanMember(mz_zip_archive& z, mz_uint i, const std::string& zipName, GpScan& s)
{
    char nm[kMemberNameMax];
    mz_zip_reader_get_filename(&z, i, nm, sizeof(nm));
    const char* kind = memberKind(text::lower(nm));
    if (!kind) return;
    size_t n = 0;
    void* d = mz_zip_reader_extract_to_heap(&z, i, &n, 0);
    if (!d) return;
    std::string t((const char*)d, n), what;
    mz_free(d);
    ++s.seen[kind];
    try {
        what = gpChange(kind, t, s);
    } catch (std::exception& x) {
        ++s.failed[kind];
        printf("FAIL %s %s|%s: %s\n", kind, zipName.c_str(), nm, x.what());
        return;
    }
    if (what.empty()) return;
    ++s.changed[kind];
    printf("%-4s %s|%s: %s\n", kind, zipName.c_str(), nm, what.c_str());
}

}  // namespace

// PS-5: the supplement is the 1.0 records whose UID no 3.0 table has.
int testKeep(const Args& a)
{
    std::set<uint32_t> excl;
    if (!uids30(a, excl)) return 2;
    std::string src = slurp(a.argv[2]), out;
    int kept = 0, total = 0;
    if (!gamedblua::keepRecords(src, [&](uint32_t u) { return !excl.count(u); }, out, kept, total)) {
        printf("layout not found\n");
        return 1;
    }
    chd::V in = chd::lua(src), got = chd::lua(out);
    const chd::V* ri = records(in);
    const chd::V* ro = records(got);
    if (!ri) {   // a table without Records fails cleanly (OT-24)
        printf("input has no Records\n");
        return 1;
    }
    if (!ro) {
        printf("output has no Records\n");
        return 1;
    }
    int want = 0;
    int bad = recordMismatches(*ri, *ro, excl, want);
    bad += headerMismatches(in, got);
    const bool rightCount = ro->items.size() == (size_t)want, allRead = total == (int)ri->items.size();
    printf("%s: %d records (chd parse %zu), kept %d (expected %d, output parse %zu), 3.0 UIDs %zu, mismatches %d\n",
           a.argv[2], total, ri->items.size(), kept, want, ro->items.size(), excl.size(),
           bad + (int)!rightCount + (int)!allRead);
    if (const char* ko = env("KEEP_OUT")) writeFile(ko, out);
    const bool counted = kept == want && rightCount && allRead;
    return bad || !counted;
}

// Every .chd under the root with its .lua beside it, compiled: must equal the shipped .chd. A throw fails (OT-04).
int testChd(const Args& a)
{
    size_t ok = 0, bad = 0, failed = 0;
    for (auto& e : fs::recursive_directory_iterator(a.argv[2])) {
        if (e.path().extension() != ".chd") continue;
        fs::path src = e.path();
        src.replace_extension(".lua");
        if (!fs::exists(src)) continue;
        std::string got;
        try {
            got = chd::compile(chd::lua(slurp(src)));
        } catch (std::exception& x) {
            ++failed;
            printf("FAIL %s: %s\n", src.string().c_str(), x.what());
        }
        if (got == slurp(e.path())) {
            ++ok;
            continue;
        }
        ++bad;
        printf("MISMATCH %s (%zu vs %zu B)\n", e.path().string().c_str(), got.size(), (size_t)fs::file_size(e.path()));
    }
    printf("chd compiled exact: %zu, mismatch %zu\n", ok, bad);
    return failed ? 1 : 0;
}

// A 1.0 table round trip, its re-seed for 3.0, and the engine's probe for one key.
int testDct(const Args& a)
{
    std::string raw = slurp(a.argv[2]), ids = slurp(a.argv[3]);
    dct::Dict d;
    if (!dct::load(raw, d) || dct::dump(d) != raw) {
        printf("round trip FAILED\n");
        return 1;
    }
    int dropped = 0, kept = dct::reseed(raw, ids, dct::SEED30, &dropped);
    printf("reseeded: %d keys kept, %d unnamed dropped\n", kept, dropped);
    std::string value;
    if (dctLookup(raw, a.argv[4], value)) {
        printf("%s = \"%s\"\n", a.argv[4], value.c_str());
        return 0;
    }
    printf("%s not found\n", a.argv[4]);
    return 1;
}

// transcode() chains activityTasks30 into luaEvents30: disjoint keys, so both orders give the same bytes.
int testMissionChain(const Args& a)
{
    int both = 0, bad = 0;
    for (int i = 2; i < a.argc; ++i) {
        std::string t = slurp(a.argv[i]), x = t, y = t;
        int a1 = gameplay::activityTasks30(x), e1 = isMission(x) ? di1::mission::luaEvents30(x) : 0;
        int e2 = isMission(y) ? di1::mission::luaEvents30(y) : 0, a2 = gameplay::activityTasks30(y);
        if (x != y || a1 != a2 || e1 != e2) {
            ++bad;
            printf("ORDER MATTERS %s\n", a.argv[i]);
        }
        if (!a1 || !e1) continue;
        ++both;
        printf("both %s: %d activity tables, %d events\n", a.argv[i], a1, e1);
    }
    printf("%d files, %d changed by both, %d order-dependent\n", a.argc - 2, both, bad);
    return bad ? 1 : 0;
}

// Every gp load-time transform over every 1.0 archive member it would see: no parse may throw (OT-04).
int testGpScan(const Args& a)
{
    GpScan s;
    s.names30 = surfaceNames30(slurp(a.argv[2]));
    for (auto& e : fs::recursive_directory_iterator(a.argv[3])) {
        if (e.path().extension() != ".zip") continue;
        mz_zip_archive z{};
        if (!mz_zip_reader_init_file(&z, e.path().string().c_str(), 0)) continue;
        const std::string zipName = e.path().filename().string();
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); ++i) scanMember(z, i, zipName, s);
        mz_zip_reader_end(&z);
    }
    for (auto& [k, c] : s.seen)
        printf("summary %-4s seen %d changed %d failed %d\n", k.c_str(), c, s.changed[k], s.failed[k]);
    bool failed = false;
    for (auto& [k, n] : s.failed) failed |= n > 0;
    return failed ? 1 : 0;
}

}  // namespace octtest

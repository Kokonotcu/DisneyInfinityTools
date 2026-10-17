// Tuple acceptance tests: the codec round trip (oct.py --selftest's oracle), OCT-1 counts, the served vformat.
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include "../convert/counts.hpp"
#include "../formats/vtxf.hpp"
#include "../npc.hpp"
#include "files.hpp"
#include "modes.hpp"
#include "run.hpp"
#include "tuples.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

constexpr const char* kTupleExts[] = {".oct", ".mer", ".merdebug", ".bent", ".banm", ".bct"};

// fn(path, lower-case extension, bytes) for every tuple file with a tuple extension under argv[first..].
template <class Fn>
void forEachTupleFile(const Args& a, int first, Fn fn)
{
    for (int i = first; i < a.argc; ++i)
        for (auto& e : fs::recursive_directory_iterator(a.argv[i], fs::directory_options::skip_permission_denied)) {
            if (!e.is_regular_file()) continue;
            const std::string ext = npc::lower(e.path().extension().string());
            bool want = false;
            for (auto x : kTupleExts) want |= ext == x;
            if (!want) continue;
            const std::string b = slurp(e.path());
            if (isTuple(b)) fn(e.path(), ext, b);   // other formats share the extensions
        }
}

// True when the file parses and writes back byte-identical, with no tail.
bool roundTrips(const fs::path& p, const std::string& b)
{
    try {
        oct::File f = parse(b);
        if (f.write() == b && f.tail.empty()) return true;
        printf("MISMATCH %s\n", p.string().c_str());
    } catch (std::exception& x) {
        printf("FAIL %s: %s\n", p.string().c_str(), x.what());
    }
    return false;
}

// ---- --counts ----

using Fields = std::map<std::string, long>;
constexpr size_t kShownMismatches = 10, kShownFields = 3;

struct CountsRun {
    size_t ok = 0, bad = 0, skip = 0;
    std::map<std::string, std::pair<size_t, size_t>> fieldTally;   // per field of the inexact graphs: hits, misses
    std::map<std::string, size_t> skipWhy;
    octx::Options octx;
};

// The count records addCounts writes besides kCountsA and kCountsB.
constexpr const char* kCountSizes[] = {"ConstantLayoutCount",       "VariableAccumulatedSize32",
                                       "VariableAccumulatedSize64", "ConstantAccumulatedSize32",
                                       "ConstantAccumulatedSize64", "DefaultDataSize",
                                       "BaseDataNodeLinksAdditionalSize", "LayerBindAdditionalSize"};

bool isCountRecord(const std::string& n)
{
    for (auto c : octx::kCountsA)
        if (n == c) return true;
    for (auto c : octx::kCountsB)
        if (n == c) return true;
    for (auto c : kCountSizes)
        if (n == c) return true;
    return false;
}

// Level-3 scalar counts and the all-keys arena total (FUN_004b4970 with every capability flag) of one SubNetwork.
Fields countFields(const oct::File& f, size_t sn)
{
    Fields m;
    std::string in;
    for (size_t j = sn + 1, e = f.blockEnd(sn); j < e; ++j) {
        const auto& r = f.recs[j];
        const std::string& n = f.name(r);
        if (r.level == 3) {
            in = n;
            if (isCountRecord(n) && n.find("AccumulatedSize") == std::string::npos) m[n] = f.ints(r)[0];
        } else if (r.level == 4 && in.find("AccumulatedSize32") != std::string::npos) {
            auto v = f.ints(r);
            for (size_t k = 0; k + 2 < v.size(); k += 3) m[in] += (long)v[k + 2] * (v[k] - 1) + v[k + 1];
        }
    }
    return m;
}

// The count records of SubNetwork i stripped, last first.
void stripCounts(oct::File& g, size_t i)
{
    size_t end = g.blockEnd(i);
    for (size_t j = end; j-- > i + 1;)
        if (g.recs[j].level == 3 && isCountRecord(g.name(g.recs[j])))
            g.recs.erase(g.recs.begin() + j, g.recs.begin() + g.blockEnd(j));
}

// One field of a graph with processors or constant data: exact, or the 3.0 rule's bound or variable form.
bool fieldHit(const std::string& k, long v, Fields& shipped, Fields& ours)
{
    const bool bound = k.find("AccumulatedSize") != std::string::npos || k == "LayerBindAdditionalSize";
    bool hit = bound ? ours[k] >= v : ours[k] == v;
    const bool header = k == "ConstantHeaderCount" || k == "VariableHeaderCount" || k == "ConstantLayoutCount";
    if (!hit && shipped["ConstantHeaderCount"] && header)   // all variable by design
        hit = k != "VariableHeaderCount" ? ours[k] == 0 : ours[k] == v + shipped["ConstantHeaderCount"];
    const bool node = k == "ConstantDataNodeCount" || k == "VariableDataNodeCount";
    if (!hit && shipped["ConstantDataNodeCount"] && node)   // static -> variable
        hit = k == "ConstantDataNodeCount" ? ours[k] == 0 : ours[k] == v + shipped["ConstantDataNodeCount"];
    if (!hit && k == "ConstantAccumulatedSize32" && !ours.count(k)) hit = true;   // no constant nodes left
    return hit;
}

// Every shipped field against ours, tallied per field; arena totals as bound >= shipped.
void compareFields(Fields& shipped, Fields& ours, const std::string& path, CountsRun& run)
{
    bool all = true;
    for (auto& [k, v] : shipped) {
        const bool hit = fieldHit(k, v, shipped, ours);
        auto& t = run.fieldTally[k];
        (hit ? t.first : t.second)++;
        if (!hit && t.second <= kShownFields)
            printf("FIELD %s ours %ld shipped %ld  %s\n", k.c_str(), ours[k], v, path.c_str());
        all &= hit;
    }
    ++(all ? run.ok : run.bad);
}

// A processor-free graph without constant data comes back byte-identical.
void compareFile(const oct::File& g, const std::string& b, const std::string& path, size_t i, CountsRun& run)
{
    if (g.write() == b) {
        ++run.ok;
        return;
    }
    ++run.bad;
    if (run.bad <= kShownMismatches) printf("MISMATCH %s subnetwork @%zu\n", path.c_str(), i);
    if (run.bad == 1 && env("OCTTEST_DUMP")) writeFile(env("OCTTEST_DUMP"), g.write());
}

// SubNetwork i: its counts stripped and regenerated by octx::addCounts, then compared.
void countsSubNetwork(const oct::File& f, size_t i, const std::string& b, const std::string& path, CountsRun& run)
{
    Fields shipped = countFields(f, i);
    bool fits = !shipped["ProcessorNodeCount"] && !shipped["ConstantDataNodeCount"];
    oct::File g = f;
    stripCounts(g, i);
    std::string why;
    if (!octx::addCounts(g, i, run.octx, &why)) {
        ++run.skip;
        ++run.skipWhy[why];
        return;
    }
    if (fits) {
        compareFile(g, b, path, i, run);
        return;
    }
    Fields ours = countFields(g, i);
    compareFields(shipped, ours, path, run);
}

void countsFile(const std::string& b, const std::string& path, CountsRun& run)
{
    oct::File f = parse(b);
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return;
    for (size_t i = pool + 1, end = f.blockEnd(pool); i < end; i = f.blockEnd(i))
        if (f.recs[i].level == 2) countsSubNetwork(f, i, b, path, run);
}

// ---- --vtxf ----

// The engine's vformat parser: 8 descriptor slots per element name and 32 names (vtxf::merge).
constexpr size_t kMaxNames = 32;
constexpr int kMaxDescriptors = 8;
using Descriptor = std::tuple<int, std::string, std::string, int, int>;   // size, type, semantic, offset, stream
using Descriptors = std::map<std::string, std::set<Descriptor>>;          // element name -> its descriptors

// The descriptors of one format's 'main' streams.
void entryDescriptors(const std::string& t, const vtxf::Entry& e, std::map<std::string, int>& sizes, Descriptors& desc)
{
    size_t mn = t.find("main", e.start), st = mn < e.end ? t.find("streams", mn) : std::string::npos;
    if (st == std::string::npos || st > e.end) return;
    auto streams = vtxf::children(t, t.find('{', st));
    for (size_t s = 0; s < streams.size(); ++s) {
        int off = 0;
        for (auto [x, y] : vtxf::children(t, streams[s].first)) {
            std::string ty = vtxf::field(t, x, y, "type");
            desc[vtxf::field(t, x, y, "name")].insert({sizes[ty], ty, vtxf::field(t, x, y, "semantic"), off, (int)s});
            off += sizes[ty];
        }
    }
}

// One vformat.vtxf within the engine's limits; true when it is.
bool withinLimits(const std::string& t, const char* label)
{
    auto sizes = vtxf::typeSizes(t);
    Descriptors desc;
    auto ents = vtxf::entries(t);
    for (auto& e : ents) entryDescriptors(t, e, sizes, desc);
    int worst = 0;
    std::string who;
    for (auto& [n, d] : desc) {
        if ((int)d.size() <= worst) continue;
        worst = (int)d.size();
        who = n;
    }
    bool ok = desc.size() <= kMaxNames && worst <= kMaxDescriptors;
    printf("%s: %zu formats, %zu element names (32 max), most descriptors %d (%s; 8 max) -> %s\n", label, ents.size(),
           desc.size(), worst, who.c_str(), ok ? "ok" : "OVER");
    return ok;
}

}  // namespace

int testRoundTrip(const Args& a)
{
    size_t files = 0, bad = 0;
    forEachTupleFile(a, 1, [&](const fs::path& p, const std::string&, const std::string& b) {
        ++files;
        bad += !roundTrips(p, b);
    });
    printf("round-trip %zu/%zu exact\n", files - bad, files);
    return bad ? 1 : 0;
}

// OCT-1: in every shipped processor-free SubNetwork, counts stripped and regenerated must come back byte-identical.
int testCounts(const Args& a)
{
    CountsRun run;
    run.octx = a.octx;
    forEachTupleFile(a, 2, [&run](const fs::path& p, const std::string& ext, const std::string& b) {
        if (ext != ".oct") return;
        try {
            countsFile(b, p.string(), run);
        } catch (std::exception& x) {
            printf("FAIL %s: %s\n", p.string().c_str(), x.what());
        }
    });
    printf("SubNetwork counts regenerated: %zu exact, %zu mismatch, %zu outside the rule\n", run.ok, run.bad, run.skip);
    for (auto& [k, n] : run.skipWhy) printf("  skipped: %-40s %zu\n", k.c_str(), n);
    for (auto& [k, t] : run.fieldTally)
        printf("  per-field (processor / constant graphs) %-32s %zu ok %zu miss\n", k.c_str(), t.first, t.second);
    return run.bad ? 1 : 0;
}

// The served vformat.vtxf (vtxf::merge of 3.0's and 1.0's) stays within the engine's limits, as 3.0's does.
int testVtxf(const Args& a)
{
    std::string t30 = vformatFile(a.argv[2]), t10 = vformatFile(a.argv[3]);
    std::map<uint32_t, vtxf::ColourTail> tails;
    int added = 0;
    std::string m = vtxf::merge(t30, t10, added, tails);
    if (a.argc > 4) writeFile(a.argv[4], m);
    int fails = !withinLimits(t30, "3.0 shipped");
    fails += !withinLimits(m, "served");
    printf("%d 1.0 formats appended, %zu with their vertex colour moved to its own stream\n", added, tails.size());
    return fails;
}

}  // namespace octtest

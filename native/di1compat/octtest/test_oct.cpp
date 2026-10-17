// Tuple acceptance tests: the codec round trip (oct.py --selftest's oracle), OCT-1 counts, the served vformat.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"
#include "../convert/counts.hpp"
#include "../convert/records.hpp"
#include "../formats/vtxf.hpp"
#include "../base/text.hpp"
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
            const std::string ext = text::lower(e.path().extension().string());
            bool want = false;
            for (auto x : kTupleExts) want |= ext == x;
            if (!want) continue;
            const std::string b = slurp(e.path());
            if (isTuple(b)) fn(e.path(), ext, b);   // other formats share the extensions
        }
}

// True when every root from argv[first] is a directory; else says which is not (OT-02).
bool rootsAreDirs(const Args& a, int first)
{
    for (int i = first; i < a.argc; ++i) {
        if (fs::is_directory(a.argv[i])) continue;
        fprintf(stderr, "octtest: %s is not a directory\n", a.argv[i]);
        return false;
    }
    return true;
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
    size_t failed = 0;   // files a parse or edit threw on (OT-04)
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

// One file, written once with its record offsets: a SubNetwork's check then costs its block only (OT-25).
struct CountsFile {
    oct::File f;
    const std::string& b;      // the shipped bytes
    const std::string& path;
    std::string written;       // f.write()
    std::vector<size_t> at;    // record k's offset in written; at[f.recs.size()] = the tail's
    size_t namesEnd = 0;       // where the name table ends
    size_t sameHead = 0;       // the first offset past the header where written and b differ
    size_t sameTail = 0;       // the length of their common suffix
};

void indexWritten(CountsFile& cf)
{
    cf.written = cf.f.write();
    cf.namesEnd = oct::HDR;
    for (const std::string& s : cf.f.names) cf.namesEnd += s.size() + 1;
    cf.at.reserve(cf.f.recs.size() + 1);
    size_t p = cf.namesEnd;
    for (const oct::Rec& r : cf.f.recs) {
        cf.at.push_back(p);
        p += oct::kRecordHead + r.pay.size();
    }
    cf.at.push_back(p);
    const std::string& w = cf.written;
    const size_t n = (std::min)(w.size(), cf.b.size());
    size_t h = oct::HDR, t = 0;
    while (h < n && w[h] == cf.b[h]) ++h;
    while (t < n && w[w.size() - 1 - t] == cf.b[cf.b.size() - 1 - t]) ++t;
    cf.sameHead = h;
    cf.sameTail = t;
}

// The SubNetwork [first, last) of the file, edited alone in g, which holds the file's names and those it added.
struct Edited {
    size_t first, last;
    const oct::File& g;
    size_t names0;   // the file's own names
};

// The record bytes oct::File::write gives recs: a file without names, past its header.
std::string recordBytes(const std::vector<oct::Rec>& recs)
{
    oct::File t;
    t.hdr.assign(oct::HDR, '\0');
    t.recs = recs;
    return t.write().substr(oct::HDR);
}

// The edited file's header, as oct::File::write sets its sizes and record count.
std::string editedHeader(const CountsFile& cf, const Edited& e, size_t added, size_t len)
{
    std::string hdr = cf.f.hdr;
    const size_t namesSize = cf.namesEnd - oct::HDR + added;
    const size_t recs = cf.f.recs.size() - (e.last - e.first) + e.g.recs.size();
    bytes::setLe32(hdr, oct::kNamesSizeAt, (uint32_t)namesSize);
    bytes::setLe32(hdr, oct::kDataSizeAt, (uint32_t)(len - oct::HDR - namesSize));
    bytes::setLe32(hdr, oct::kRecordCountAt, cf.f.recordCount == 0 ? 0u : (uint32_t)recs);
    return hdr;
}

// The edited file written equals b. Its parts against b: header, names, records before, block, the rest.
bool editedEquals(const CountsFile& cf, const Edited& e)
{
    std::string added;
    for (size_t k = e.names0; k < e.g.names.size(); ++k) added += e.g.names[k] + '\0';
    const std::string block = recordBytes(e.g.recs);
    const std::string& w = cf.written;
    const size_t start = cf.at[e.first], rest = w.size() - cf.at[e.last];
    const size_t len = start + added.size() + block.size() + rest;
    if (len != cf.b.size() || cf.b.compare(0, oct::HDR, editedHeader(cf, e, added.size(), len)) != 0) return false;
    if (cf.sameHead < cf.namesEnd || cf.b.compare(cf.namesEnd, added.size(), added) != 0) return false;
    const size_t pre = start - cf.namesEnd;   // the records before the block, shifted by the added names
    const bool before = added.empty() ? cf.sameHead >= start
                                      : cf.b.compare(cf.namesEnd + added.size(), pre, w, cf.namesEnd, pre) == 0;
    if (!before || cf.b.compare(start + added.size(), block.size(), block) != 0) return false;
    return cf.sameTail >= rest;
}

// The whole edited file, for OCTTEST_DUMP.
oct::File editedFile(const CountsFile& cf, const Edited& e)
{
    oct::File full = cf.f;
    full.names = e.g.names;
    full.recs.erase(full.recs.begin() + e.first, full.recs.begin() + e.last);
    full.recs.insert(full.recs.begin() + e.first, e.g.recs.begin(), e.g.recs.end());
    return full;
}

// A processor-free graph without constant data comes back byte-identical.
void compareEdited(const CountsFile& cf, const Edited& e, CountsRun& run)
{
    if (editedEquals(cf, e)) {
        ++run.ok;
        return;
    }
    ++run.bad;
    if (run.bad <= kShownMismatches) printf("MISMATCH %s subnetwork @%zu\n", cf.path.c_str(), e.first);
    if (run.bad == 1 && env("OCTTEST_DUMP")) writeFile(env("OCTTEST_DUMP"), editedFile(cf, e).write());
}

// Drops the names an edit added past n, keeping addName's index in step with the table.
void dropNames(oct::File& f, size_t n)
{
    if (f.names.size() == n) return;
    for (size_t k = n; k < f.names.size(); ++k) f.nameIndex.erase(f.names[k]);
    if (f.nameIndexed == f.names.size()) f.nameIndexed = n;
    f.names.resize(n);
}

// SubNetwork i alone, the file's names lent (as addAllCounts edits it): counts stripped, regenerated, compared.
void countsSubNetwork(CountsFile& cf, size_t i, CountsRun& run)
{
    Fields shipped = countFields(cf.f, i);
    bool fits = !shipped["ProcessorNodeCount"] && !shipped["ConstantDataNodeCount"];
    const size_t end = cf.f.blockEnd(i), names0 = cf.f.names.size();
    oct::File g;
    g.recs.assign(cf.f.recs.begin() + i, cf.f.recs.begin() + end);
    {
        octx::detail::LentNames lent(cf.f, g);
        stripCounts(g, 0);
        std::string why;
        if (!octx::addCounts(g, 0, run.octx, &why)) {
            ++run.skip;
            ++run.skipWhy[why];
        } else if (fits) {
            compareEdited(cf, {i, end, g, names0}, run);
        } else {
            Fields ours = countFields(g, 0);
            compareFields(shipped, ours, cf.path, run);
        }
    }
    dropNames(cf.f, names0);
}

void countsFile(const std::string& b, const std::string& path, CountsRun& run)
{
    CountsFile cf{parse(b), b, path};
    long pool = cf.f.pool("SubNetworkPool");
    if (pool < 0) return;
    indexWritten(cf);
    for (size_t i = pool + 1, end = cf.f.blockEnd(pool); i < end; i = cf.f.blockEnd(i))
        if (cf.f.recs[i].level == 2) countsSubNetwork(cf, i, run);
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

// The round trip; roots that are not directories are a usage error, finding no tuple file fails (OT-02).
int testRoundTrip(const Args& a)
{
    if (!rootsAreDirs(a, 1)) return kUsageError;
    size_t files = 0, bad = 0;
    forEachTupleFile(a, 1, [&](const fs::path& p, const std::string&, const std::string& b) {
        ++files;
        bad += !roundTrips(p, b);
    });
    printf("round-trip %zu/%zu exact\n", files - bad, files);
    if (!files) fprintf(stderr, "octtest: no tuple file under the roots\n");
    return bad || !files ? 1 : 0;
}

// OCT-1: in every shipped processor-free SubNetwork, counts stripped and regenerated must come back byte-identical.
int testCounts(const Args& a)
{
    if (!rootsAreDirs(a, 2)) return kUsageError;
    CountsRun run;
    run.octx = a.octx;
    forEachTupleFile(a, 2, [&run](const fs::path& p, const std::string& ext, const std::string& b) {
        if (ext != ".oct") return;
        try {
            countsFile(b, p.string(), run);
        } catch (std::exception& x) {   // reported and failing the run (OT-04)
            ++run.failed;
            printf("FAIL %s: %s\n", p.string().c_str(), x.what());
        }
    });
    printf("SubNetwork counts regenerated: %zu exact, %zu mismatch, %zu outside the rule\n", run.ok, run.bad, run.skip);
    for (auto& [k, n] : run.skipWhy) printf("  skipped: %-40s %zu\n", k.c_str(), n);
    for (auto& [k, t] : run.fieldTally)
        printf("  per-field (processor / constant graphs) %-32s %zu ok %zu miss\n", k.c_str(), t.first, t.second);
    if (run.failed) printf("%zu files failed\n", run.failed);
    const bool none = run.ok + run.bad + run.skip == 0;
    if (none) fprintf(stderr, "octtest: no SubNetwork under the roots\n");
    return run.bad || run.failed || none ? 1 : 0;
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

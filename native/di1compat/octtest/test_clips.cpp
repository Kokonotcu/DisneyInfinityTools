// Clip acceptance tests: clipBlock30 against clip twins, the plugin's cho steps on files, banm30 against 3.0 text.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include "../cho.hpp"
#include "chotree.hpp"
#include "files.hpp"
#include "run.hpp"
#include "tuples.hpp"
#include "modes.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

// A ClipDataBlock as cho::clipBlock30 reads it: u16 counts, u32 constant offset, a u32 offset per descriptor.
constexpr size_t kDescCountAt = 8, kSectionCountAt = 10, kConstOffsetAt = 12, kOffsetsAt = 0x10, kOffsetBytes = 4;
// A descriptor: u16 input count at +2, the u16 inputs, u16 X (its constant table), then the sampled table.
constexpr size_t kInputCountAt = 2, kInputsAt = 4, kSampledAt = 6, kU16Bytes = 2;
// A 3.0 output table: u16 count, u16 heads (comps in bits 13-15, port id below), then the comps.
constexpr int kCompShift = 13;
constexpr uint32_t kIdMask = 0x1fff;
constexpr uint32_t kPastEnd = 0xffffffffu;   // cho::rd past the payload
constexpr size_t kShapeShown = 300;          // characters of a block shape printed

uint32_t rd16(const std::string& d, size_t o) { return cho::rd(d, o, 2); }
uint32_t rd32(const std::string& d, size_t o) { return cho::rd(d, o, 4); }

// A comp's bytes: code kinds 1 and 2 (top nibble) carry 8, the rest 4.
size_t compBytes(uint32_t code)
{
    constexpr uint32_t kKind = 0xf000, kWide1 = 0x1000, kWide2 = 0x2000;
    return (code & kKind) == kWide1 || (code & kKind) == kWide2 ? 8 : 4;
}

bool everyMotion(const std::string&) { return true; }
bool noMotion(const std::string&) { return false; }

// ---- --cho-clips ----

struct ClipTally {
    size_t exact = 0, sameLen = 0, other = 0, failed = 0, files = 0;
    size_t areaSame = 0, shapeSame = 0, anyExact = 0, anyShape = 0;
};

// Header and descriptor area of a 3.0-layout block: up to the first section or constant offset.
std::string descriptorArea(const std::string& d)
{
    uint32_t nDesc = rd16(d, kDescCountAt), nSec = rd16(d, kSectionCountAt), e = rd32(d, kConstOffsetAt);
    for (uint32_t i = 0; i < nSec; ++i) e = (std::min)(e, rd32(d, kOffsetsAt + kOffsetBytes * (nDesc + i)));
    return d.substr(0, (std::min<size_t>)(e, d.size()));
}

// One output table's heads and comp codes, appended to s; false when its count is past the payload.
bool appendTableShape(const std::string& d, size_t t, std::string& s)
{
    uint32_t nOut = rd16(d, t), comps = 0;
    if (nOut == kPastEnd) return false;
    s += "[";
    for (uint32_t h = 0; h < nOut; ++h) {
        uint32_t hd = rd16(d, t + kU16Bytes + kU16Bytes * h);
        comps += hd >> kCompShift;
        s += std::to_string(hd) + " ";
    }
    size_t c = t + kU16Bytes + kU16Bytes * nOut;
    for (uint32_t k = 0; k < comps; ++k) {
        uint32_t code = rd16(d, c);
        s += std::to_string(code) + ".";
        c += compBytes(code);
    }
    s += "]";
    return true;
}

// A 3.0-layout block's shape: counts, section ids, output heads and comp codes (operands left out).
std::string blockShape(const std::string& d)
{
    std::string s;
    uint32_t nDesc = rd16(d, kDescCountAt), nSec = rd16(d, kSectionCountAt);
    s += std::to_string(nDesc) + "/" + std::to_string(nSec) + ":";
    for (uint32_t i = 0; i < nDesc; ++i) {
        uint32_t o = rd32(d, kOffsetsAt + kOffsetBytes * i), n = rd16(d, o + kInputCountAt);
        for (uint32_t k = 0; k < n; ++k) s += std::to_string(rd16(d, o + kInputsAt + kU16Bytes * k)) + ",";
        uint32_t x = rd16(d, o + kInputsAt + kU16Bytes * n);
        for (size_t t : {(size_t)o + kSampledAt + kU16Bytes * n, (size_t)o + x})
            if (!appendTableShape(d, t, s)) return s + "?";
    }
    return s;
}

std::vector<std::string> clipBlocks(const std::string& path)
{
    std::vector<std::string> v;
    std::string b = slurp(path);
    oct::File f = parse(b);
    for (auto& r : f.recs)
        if (f.name(r) == "ClipDataBlock") v.push_back(blobData(r));
    return v;
}

// Block i of the 1.0 twin through clipBlock30, against the 3.0 twin's block i and every other block.
void clipTwinBlock(const std::string& name10, const std::vector<std::string>& A, const std::vector<std::string>& B,
                   size_t i, ClipTally& t)
{
    std::string out;
    if (!cho::clipBlock30(A[i], out)) {
        ++t.failed;
        return;
    }
    if (out == B[i]) ++t.exact;
    else if (out.size() == B[i].size()) ++t.sameLen;
    else ++t.other;
    if (out != B[i] && descriptorArea(out) == descriptorArea(B[i])) ++t.areaSame;
    bool anyShape = false, anyExact = false;   // blocks are not always in the same order in the twin
    for (auto& b : B) {
        anyExact |= out == b;
        anyShape |= blockShape(out) == blockShape(b);
    }
    t.anyExact += anyExact;
    t.anyShape += anyShape;
    if (out != B[i] && blockShape(out) == blockShape(B[i])) ++t.shapeSame;
    else if (!anyShape && env("CHO_SHAPES"))
        printf("shape %s\n  10>30 %s\n  30    %s\n", name10.c_str(), blockShape(out).substr(0, kShapeShown).c_str(),
               blockShape(B[i]).substr(0, kShapeShown).c_str());
}

// ---- --clip-ids ----

using IdTables = std::vector<std::vector<uint32_t>>;

std::vector<uint32_t> tableIds(const std::string& d, size_t s)
{
    std::vector<uint32_t> v;
    for (uint32_t h = 0, k = rd16(d, s); k != kPastEnd && h < k; ++h)
        v.push_back(rd16(d, s + kU16Bytes + kU16Bytes * h) & kIdMask);
    return v;
}

// Output ids per table of a 3.0-layout block: each descriptor's sampled, then constant table.
IdTables outputIds30(const std::string& d)
{
    IdTables t;
    uint32_t nDesc = rd16(d, kDescCountAt);
    for (uint32_t i = 0; i < nDesc; ++i) {
        uint32_t o = rd32(d, kOffsetsAt + kOffsetBytes * i), n = rd16(d, o + kInputCountAt);
        uint32_t x = rd16(d, o + kInputsAt + kU16Bytes * n);
        for (size_t s : {(size_t)o + kSampledAt + kU16Bytes * n, (size_t)o + x}) t.push_back(tableIds(d, s));
    }
    return t;
}

// Counts key over all templates and under 'tmpl:', for the per-template verdict.
void countBoth(Tally& c, const char* tmpl, const char* key)
{
    ++c[key];
    ++c[std::string(tmpl) + ":" + key];
}

// The ids of one converted block: ours, with the ids kept, and the twin's.
struct IdView {
    Tally& c;
    const char* tmpl;
    IdTables to, tk, ty;
    std::set<uint32_t> twinIds;
    bool sameSizes = false, dropped = false;
};

// Per table, each twin id among ours: a wrong number shows; a twin that dropped or re-ordered outputs does not.
void tallyTwinIds(const IdView& v)
{
    for (size_t q = 0; q < v.to.size(); ++q) {
        std::set<uint32_t> so(v.to[q].begin(), v.to[q].end());
        for (uint32_t j : v.ty[q])
            countBoth(v.c, v.tmpl,
                      so.count(j) ? "  twin ids, per table: among ours" : "  twin ids, per table: NOT among ours");
    }
}

void tallyId(const IdView& v, size_t q, size_t j)
{
    countBoth(v.c, v.tmpl, v.twinIds.count(v.to[q][j]) ? "  ids, order-free: ours among the twin's"
                                                        : "  ids, order-free: ours NOT among the twin's");
    if (!v.dropped)
        countBoth(v.c, v.tmpl, v.twinIds.count(v.tk[q][j]) ? "  (ids kept, order-free: among the twin's)"
                                                            : "  (ids kept, order-free: NOT among)");
    if (!v.sameSizes) return;
    countBoth(v.c, v.tmpl, v.to[q][j] == v.ty[q][j] ? "  ids, same-size tables: same position"
                                                     : "  ids, same-size tables: DIFFERENT");
    if (!v.dropped)
        countBoth(v.c, v.tmpl, v.tk[q][j] == v.ty[q][j] ? "  (ids kept, same-size tables: same position)"
                                                         : "  (ids kept, same-size tables: different)");
}

void tallyIds(Tally& c, const char* tmpl, const std::string& ours, const std::string& kept, const std::string& y)
{
    IdView v{c, tmpl, outputIds30(ours), outputIds30(kept), outputIds30(y), {}};
    for (auto& t : v.ty) v.twinIds.insert(t.begin(), t.end());
    v.sameSizes = v.to.size() == v.ty.size();
    for (size_t q = 0; v.sameSizes && q < v.to.size(); ++q) v.sameSizes = v.to[q].size() == v.ty[q].size();
    for (size_t q = 0; q < v.to.size(); ++q) {
        v.dropped |= v.to[q].size() != v.tk[q].size();
        for (size_t j = v.to[q].size(); j < v.tk[q].size(); ++j) countBoth(c, tmpl, "  outputs dropped (kDropPort)");
    }
    if (v.to.size() == v.ty.size()) tallyTwinIds(v);
    for (size_t q = 0; q < v.to.size(); ++q)
        for (size_t j = 0; j < v.to[q].size(); ++j) tallyId(v, q, j);
}

// The twin's ClipDataBlock data in record order, and its first ConnectionTemplate.
struct TwinBlocks {
    std::vector<std::string> blocks;
    std::string tmpl;
};

TwinBlocks twinBlocks(const oct::File& B)
{
    TwinBlocks t;
    for (auto& r : B.recs) {
        if (B.name(r) == "ClipDataBlock") t.blocks.push_back(blobData(r));
        if (B.name(r) == "ConnectionTemplate" && t.tmpl.empty()) t.tmpl = B.nameValues(r)[0];
    }
    return t;
}

// A motion clip block at position at, through its port map, against the twin's block there.
void clipIdsBlock(const oct::Rec& r, size_t at, const cho::PortMap& pm, const TwinBlocks& tb, Tally& c)
{
    ++c[std::string("blocks on ") + pm.tmpl];
    if (_stricmp(tb.tmpl.c_str(), pm.tmpl)) {
        countBoth(c, pm.tmpl, "  twin binds another template");
        return;
    }
    if (at >= tb.blocks.size()) {
        countBoth(c, pm.tmpl, "  twin has no block there");
        return;
    }
    std::string d = blobData(r), ours, kept;
    if (!cho::clipBlock30(d, ours, &pm) || !cho::clipBlock30(d, kept)) {
        countBoth(c, pm.tmpl, "  NOT CONVERTED");
        return;
    }
    const std::string& y = tb.blocks[at];
    countBoth(c, pm.tmpl, ours == y               ? "  bytes: identical to the twin"
                          : ours.size() == y.size() ? "  bytes: same length"
                                                    : "  bytes: other length");
    if (kept == y) countBoth(c, pm.tmpl, "  (ids kept: identical to the twin)");
    tallyIds(c, pm.tmpl, ours, kept, y);
}

// Each 1.0 motion clip on a ports10.hpp template, against the twin when it binds the same template.
void clipIdsFile(const oct::File& A, const oct::File& B, Tally& c)
{
    const TwinBlocks tb = twinBlocks(B);
    const cho::PortMap* pm = nullptr;
    size_t k = 0;
    for (size_t i = 0; i < A.recs.size(); ++i) {
        if (A.recs[i].level == 2 && A.name(A.recs[i]) == "SubNetwork") {
            pm = cho::clipPorts(A, i);
            continue;
        }
        if (A.name(A.recs[i]) != "ClipDataBlock") continue;
        const size_t at = k++;
        if (pm) clipIdsBlock(A.recs[i], at, *pm, tb, c);
    }
}

// ---- --cho-file, --cho-corpus ----

struct ChoFileTotals {
    int bad = 0;
    size_t clips = 0, layouts = 0, instances = 0, timeRanges = 0, blocks = 0, animInputs = 0, sceneRefs = 0;
    size_t renumbered = 0, linksRenumbered = 0, changedIds = 0, changedElse = 0, sceneBlocks = 0, sceneMiss = 0;
};
struct ChoFileRun {
    bool printAll;        // every file's line, not only the bad ones
    const char* outDir;   // CHO_OUT: each result written here
    ChoFileTotals t;
};

// The end of a 3.0 output table at t; false when a count or code is past the payload.
bool tableEnd30(const std::string& d, size_t t, size_t& end)
{
    uint32_t nOut = rd16(d, t), comps = 0;
    if (nOut == kPastEnd) return false;
    for (uint32_t h = 0; h < nOut; ++h) comps += rd16(d, t + kU16Bytes + kU16Bytes * h) >> kCompShift;
    size_t c = t + kU16Bytes + kU16Bytes * (size_t)nOut;
    for (uint32_t q = 0; q < comps; ++q) {
        uint32_t code = rd16(d, c);
        if (code == kPastEnd) return false;
        c += compBytes(code);
    }
    end = c;
    return true;
}

// A re-packed block parses in the 3.0 layout: each table ends at the next one, the data or X.
bool parses30(const std::string& d)
{
    const uint32_t nDesc = rd16(d, kDescCountAt);
    for (uint32_t i = 0; i < nDesc; ++i) {
        uint32_t o = rd32(d, kOffsetsAt + kOffsetBytes * i), n = rd16(d, o + kInputCountAt);
        if (n == kPastEnd) return false;
        uint32_t x = rd16(d, o + kInputsAt + kU16Bytes * n);
        size_t sampledEnd = 0, constEnd = 0;
        if (!tableEnd30(d, o + kSampledAt + kU16Bytes * (size_t)n, sampledEnd)) return false;
        if (sampledEnd != o + x || sampledEnd > d.size()) return false;   // the sampled table ends where X says
        if (!tableEnd30(d, o + x, constEnd) || constEnd > d.size()) return false;
    }
    return true;
}

// clips30 with clip10ids on f, and without on a copy: the ids may change only renumbered clips and links.
int clipsWithIds(const std::string& b, oct::File& f, int& failed, ChoFileTotals& t)
{
    int ren = 0;
    oct::File g0 = parse(b);
    cho::clips30(g0, everyMotion);
    const int nc = cho::clips30(f, everyMotion, &failed, true, &ren);
    const int nk = cho::links30(f);
    if (f.write() != g0.write()) ++(ren || nk ? t.changedIds : t.changedElse);
    t.renumbered += ren;
    t.linksRenumbered += nk;
    return nc;
}

// Scene clips (outside Type 'Clip' SubNetworks) are re-packed even when 3.0 owns every template.
void sceneClips(const std::string& b, const std::string& path, ChoFileTotals& t)
{
    oct::File g1 = parse(b);
    int scene = 0;
    bool clipSn = false;
    for (size_t i = 0; i < g1.recs.size(); ++i) {
        const oct::Rec& r = g1.recs[i];
        if (r.level == 2) clipSn = false;
        if (r.level == 3 && g1.name(r) == "Type") clipSn = g1.nameValues(r) == std::vector<std::string>{"Clip"};
        if (g1.name(r) == "ClipDataBlock" && !clipSn) ++scene;
    }
    int n1 = cho::clips30(g1, noMotion);
    t.sceneBlocks += scene;
    if (n1 >= scene) return;
    ++t.sceneMiss;
    printf("BAD %s: %d of %d scene blocks re-packed when 3.0 owns the templates\n", path.c_str(), n1, scene);
}

// The plugin's cho steps on one file: the result re-parses identically and every block parses as 3.0.
void choFileOne(const std::string& path, ChoFileRun& run)
{
    std::string b = slurp(path);
    if (!isTuple(b)) return;   // empty files, other formats
    oct::File f = parse(b);
    int failed = 0, inst = 0;
    const int nc = clipsWithIds(b, f, failed, run.t);
    sceneClips(b, path, run.t);
    const int nl = cho::layout30(f, &inst), nt = cho::time30(f), na = cho::anim30(f), ns = cho::scene30(f);
    const std::string w = f.write();
    if (run.outDir) writeFile(fs::path(run.outDir) / fs::path(path).filename(), w);
    oct::File g = parse(w);
    const bool round = g.write() == w;
    size_t blocks = 0, ok30 = 0;
    for (auto& r : g.recs) {
        if (g.name(r) != "ClipDataBlock") continue;
        ++blocks;
        ok30 += parses30(blobData(r));
    }
    const bool good = round && ok30 == blocks && !failed;
    ChoFileTotals& t = run.t;
    t.bad += !good;
    t.clips += nc;
    t.layouts += nl;
    t.instances += inst;
    t.timeRanges += nt;
    t.blocks += blocks;
    t.animInputs += na;
    t.sceneRefs += ns;
    if (run.printAll || !good)
        printf("%s %s: clips %d (not parsed %d), layouts %d (%d instances), time ranges %d, anim inputs %d, scene refs "
               "%d; re-parse %s, %zu/%zu blocks parse as 3.0\n", good ? "ok " : "BAD", path.c_str(), nc, failed, nl,
               inst, nt, na, ns, round ? "same" : "DIFFERS", ok30, blocks);
}

int choFiles(const std::vector<std::string>& files, ChoFileRun run)
{
    for (auto& path : files) choFileOne(path, run);
    const ChoFileTotals& t = run.t;
    printf("clip10ids: %zu blocks with 3.0 port ids, %zu model links renumbered; files changed by it %zu, changed "
           "without a renumbered clip or link %zu\n", t.renumbered, t.linksRenumbered, t.changedIds, t.changedElse);
    printf("scene clips with every template 3.0's: %zu blocks, %zu files with one left in the 1.0 layout\n",
           t.sceneBlocks, t.sceneMiss);
    printf("%zu files, %d bad; %zu blocks re-packed of %zu, %zu layouts (%zu instances), %zu time ranges, %zu anim "
           "inputs, %zu scene refs\n", files.size(), t.bad, t.clips, t.blocks, t.layouts, t.instances, t.timeRanges,
           t.animInputs, t.sceneRefs);
    return t.bad || t.changedElse || t.sceneMiss ? 1 : 0;
}

// ---- --banm ----

struct BanmTally {
    size_t same = 0, differ = 0, rejected = 0, otherDiff = 0;
};
constexpr size_t kShownNameDiffs = 5;

// 'depth|name|values' per record below the root, numbers %.6g: the text form's canonical lines.
std::vector<std::string> canonBin(const oct::File& f)
{
    std::vector<std::string> out;
    for (size_t i = 1; i < f.recs.size(); ++i) {
        const oct::Rec& r = f.recs[i];
        oct::Spec s = oct::spec(r.type);
        std::string l = std::to_string(r.level - 1) + "|" + f.name(r) + "|";
        if (s.kind == oct::NAME || s.keyed) {
            for (auto& v : f.nameValues(r)) l += v + ",";
        }
        if (s.kind != oct::FLOAT && s.kind != oct::INT) {
            out.push_back(l);
            continue;
        }
        for (int32_t v : f.ints(r)) {
            char b[32];
            float x;
            memcpy(&x, &v, sizeof x);
            snprintf(b, sizeof b, "%.6g,", s.kind == oct::FLOAT ? (double)x : (double)v);
            l += b;
        }
        out.push_back(l);
    }
    return out;
}

// The tokens of tuple text: quoted strings, the marks ={}[], and bare words.
std::vector<std::string> textTokens(const std::string& t)
{
    std::vector<std::string> tok;
    for (size_t i = 0; i < t.size();) {
        char c = t[i];
        if (isspace((unsigned char)c)) {
            ++i;
            continue;
        }
        if (c == '"') {
            size_t e = t.find('"', i + 1);
            tok.push_back(t.substr(i, e - i + 1));
            i = e + 1;
            continue;
        }
        if (strchr("={}[]", c)) {
            tok.push_back(std::string(1, c));
            ++i;
            continue;
        }
        size_t e = i;
        while (e < t.size() && !isspace((unsigned char)t[e]) && !strchr("={}[]\"", t[e])) ++e;
        tok.push_back(t.substr(i, e - i));
        i = e;
    }
    return tok;
}

std::string textValue(const std::string& s)
{
    if (s[0] == '"') return s.substr(1, s.size() - 2) + ",";
    char b[32];
    snprintf(b, sizeof b, "%.6g,", atof(s.c_str()));
    return std::string(b);
}

// A minimal tuple-text reader: the lines canonBin gives for the binary form.
std::vector<std::string> canonText(const std::string& t)
{
    const std::vector<std::string> tok = textTokens(t);
    std::vector<std::string> out;
    int depth = 0;
    for (size_t i = 0; i < tok.size(); ++i) {
        if (tok[i] == "}") {
            --depth;
            continue;
        }
        std::string l = std::to_string(depth) + "|" + tok[i] + "|";
        size_t j = i + 1;
        const bool keyed = j < tok.size() && tok[j][0] == '"';
        if (keyed && j + 1 < tok.size() && tok[j + 1] == "=") l += textValue(tok[j++]);
        ++j;   // '='
        if (j < tok.size() && tok[j] == "{") {
            ++depth;
        } else if (j < tok.size() && tok[j] == "[") {
            for (++j; j < tok.size() && tok[j] != "]"; ++j) l += textValue(tok[j]);
        } else {
            l += textValue(tok[j]);
        }
        i = j;
        out.push_back(l);
    }
    return out;
}

std::vector<std::string> withoutFilenames(const std::vector<std::string>& v)
{
    std::vector<std::string> o;
    for (auto& s : v)
        if (s.find("|Filename|") == std::string::npos) o.push_back(s);
    return o;
}

void printFirstDiff(const std::vector<std::string>& A, const std::vector<std::string>& B)
{
    for (size_t k = 0; k < A.size() && k < B.size(); ++k) {
        if (A[k] == B[k]) continue;
        printf("  ours %s\n  3.0  %s\n", A[k].c_str(), B[k].c_str());
        return;
    }
}

// One line '<1.0 .banm>|<3.0 .banm text>|<engine path>': banm30's records against the twin's.
void banmPair(const std::string& line, BanmTally& t)
{
    size_t b1 = line.find('|'), b2 = line.find('|', b1 + 1);
    if (b2 == std::string::npos) return;
    std::string b = slurp(line.substr(0, b1));
    oct::File f = parse(b);
    if (!cho::banm30(f, line.substr(b2 + 1))) {
        ++t.rejected;
        return;
    }
    const std::string w = f.write();
    oct::File g = parse(w);
    auto A = canonBin(g), B = canonText(slurp(line.substr(b1 + 1, b2 - b1 - 1)));
    if (A == B) {
        ++t.same;
        return;
    }
    ++t.differ;
    const bool onlyNames = withoutFilenames(A) == withoutFilenames(B);   // other lines differ when re-exported
    if (!onlyNames) ++t.otherDiff;
    if (t.differ - t.otherDiff > kShownNameDiffs || !onlyNames) return;
    printf("names differ %s\n", line.substr(b2 + 1).c_str());
    printFirstDiff(A, B);
}

}  // namespace

int testChoClips(const Args& a)
{
    ClipTally t;
    for (const std::string& line : pairLines(a.argv[2])) {
        size_t bar = line.find('|');
        std::vector<std::string> A, B;
        try {
            A = clipBlocks(line.substr(0, bar));
            B = clipBlocks(line.substr(bar + 1));
        } catch (...) {
            continue;
        }
        ++t.files;
        for (size_t i = 0; i < A.size() && i < B.size(); ++i) clipTwinBlock(line.substr(0, bar), A, B, i, t);
    }
    printf("clips: %zu twin files; blocks exact %zu, same length %zu, other length %zu, not parsed %zu; of the "
           "inexact: header+descriptor area identical %zu, same shape (heads + comp codes) %zu; any twin block: exact "
           "%zu, same shape %zu\n", t.files, t.exact, t.sameLen, t.other, t.failed, t.areaSame, t.shapeSame, t.anyExact,
           t.anyShape);
    return 0;
}

int testClipIds(const Args& a)
{
    Tally c;
    for (const std::string& line : pairLines(a.argv[2])) {
        size_t bar = line.find('|');
        std::string ba, bb;
        try {
            ba = slurp(line.substr(0, bar));
            bb = slurp(line.substr(bar + 1));
        } catch (...) {
            continue;
        }
        if (ba.size() < kMagicBytes || bb.size() < kMagicBytes) continue;
        oct::File A = parse(ba), B = parse(bb);
        clipIdsFile(A, B, c);
    }
    printTally(c, 8);
    return c.count("  NOT CONVERTED") ? 1 : 0;
}

int testChoFile(const Args& a)
{
    return choFiles(std::vector<std::string>(a.argv + 2, a.argv + a.argc), {true, env("CHO_OUT"), {}});
}

int testChoCorpus(const Args& a)
{
    std::vector<std::string> files;
    for (int i = 3; i < a.argc; ++i)
        for (const fs::path& p : withMotions(choIndex(a.argv[2], a.argv[i]))) files.push_back(p.string());
    return choFiles(files, {false, nullptr, {}});
}

int testBanm(const Args& a)
{
    BanmTally t;
    for (const std::string& line : pairLines(a.argv[2])) banmPair(line, t);
    printf("banm: %zu identical to the 3.0 twin, %zu differ (%zu of them outside Filename lines: re-exported), %zu not "
           "1.0f\n", t.same, t.differ, t.otherDiff, t.rejected);
    return 0;
}

}  // namespace octtest

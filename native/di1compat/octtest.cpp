// octtest <root>...           parse and rewrite every tuple file under the roots (oct.py --selftest is the oracle)
// octtest --counts <root>...  OCT-1 check: in every shipped processor-free SubNetwork, strip the count records and
//                             regenerate them with octx::addCounts; the file must come back byte-identical
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "octx.hpp"
#include "bent.hpp"
#include "btd.hpp"
#include "chd.hpp"
#include "mer.hpp"
#include "render.hpp"
#include "arc30.hpp"
#include "datamap.hpp"
#include "dct.hpp"
#include "luachunk.hpp"
#include "cho.hpp"

static std::string slurp(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// octtest --render <1.0 world dir> <render table> <3.0 extract root> <1.0 extract root> <out dir>
// The C++ render transcode over the extracts; renderoracle.py writes the Python pipeline's output for comparison.
static int renderTest(char** a)
{
    namespace fs = std::filesystem;
    fs::path world = a[2], out = a[6];
    std::string stem = world.filename().string();
    render::Table T;
    if (!T.load(slurp(a[3]))) { printf("table load failed\n"); return 2; }
    std::string ext30 = a[4], ext10 = a[5];
    render::IO io;
    // ARC30_GAME=<3.0 install>: read donors from the install's archives as the plugin does (arc30), timed
    const char* game = getenv("ARC30_GAME");
    int reads = 0;
    double readMs = 0;
    io.read30 = [&](const std::string& p, std::string& o) {   // '3:<extract path>'
        if (!game) { o = slurp(fs::path(ext30) / "assets" / p.substr(2)); return !o.empty(); }
        auto t = std::chrono::steady_clock::now();
        std::string rel = p.substr(2);
        size_t cut = rel.rfind('/');
        bool ok = cut != std::string::npos && arc30::readMember(std::string(game) + "/assets/" + rel.substr(0, cut) + ".zip", rel.substr(cut + 1), o);
        readMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
        ++reads;
        return ok;
    };
    io.verbose = getenv("RENDER_VERBOSE") != nullptr;
    io.translate = getenv("RENDER_TRANSLATE") != nullptr;   // real 1.0 shaders (read10 finds shaders/shaders/<x>.shd)
    gen30::VFormats vf10 = gen30::vformats(slurp(fs::path(ext10) / "assets/shaders/shaders/vformat.vtxf"));
    io.skinned = [&](uint32_t vf) { auto f = vf10.find(vf); return f != vf10.end() && f->second.count("BLENDINDICES0"); };
    io.log = [](const std::string& l) { printf("%s\n", l.c_str()); };
    auto t0 = std::chrono::steady_clock::now();
    io.read10 = [&](const std::string& p, std::string& o) { o = slurp(fs::path(ext10) / "assets" / p); return !o.empty(); };
    std::string ob = slurp(world / (stem + ".oct"));
    oct::File f = oct::File::parse(ob.data(), ob.size());
    render::Result r;
    bool ok = render::transcodeWorld(T, io, f, stem, slurp(world / (stem + ".mtb")), slurp(world / (stem + "_0.vbuf")),
                                     slurp(world / (stem + "_0.ibuf")), r);
    printf("render %.0f ms, %d 3.0 reads %.0f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
           reads, readMs);
    for (auto& p : r.problems) printf("problem: %s\n", p.c_str());
    if (!ok) return 1;
    fs::create_directories(out);
    auto put = [&](const char* n, const std::string& d) { std::ofstream(out / (stem + n), std::ios::binary).write(d.data(), d.size()); };
    put(".oct", f.write()); put(".mtb", r.mtb); put("_0.vbuf", r.vbuf); put("_0.ibuf", r.ibuf);
    if (!r.ibuf2.empty()) put("_2.ibuf", r.ibuf2);
    for (auto& [t, n] : r.tiers) printf("tier %s %d\n", t.c_str(), n);
    printf("wrote %s: oct %zu, mtb %zu, vbuf %zu, ibuf %zu, ibuf2 %zu, 1.0 textures %zu\n", out.string().c_str(),
           f.write().size(), r.mtb.size(), r.vbuf.size(), r.ibuf.size(), r.ibuf2.size(), r.textures10.size());
    return 0;
}

static bool isCountRecord(const std::string& n)
{
    for (auto c : octx::COUNTS_A) if (n == c) return true;
    for (auto c : octx::COUNTS_B) if (n == c) return true;
    return n == "ConstantLayoutCount" || n == "VariableAccumulatedSize32" || n == "VariableAccumulatedSize64" ||
           n == "ConstantAccumulatedSize32" || n == "ConstantAccumulatedSize64" || n == "DefaultDataSize" ||
           n == "BaseDataNodeLinksAdditionalSize" || n == "LayerBindAdditionalSize";
}

// level-3 scalar counts and the all-keys arena total (FUN_004b4970 with every capability flag set) of one SubNetwork
static std::map<std::string, long> countFields(const oct::File& f, size_t sn)
{
    std::map<std::string, long> m;
    std::string in;
    for (size_t j = sn + 1; j < f.blockEnd(sn); ++j) {
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

// -> exact / mismatch / skipped counts over one file's SubNetworks. Processor-free graphs without constant data must
// come back byte-identical; the others are compared per field (tally), arena totals as bound >= shipped.
static std::map<std::string, std::pair<size_t, size_t>> g_fieldTally;
static std::map<std::string, size_t> g_skipWhy;
static void countsCheck(const std::string& b, const std::string& path, size_t* ok, size_t* bad, size_t* skip)
{
    oct::File f = oct::File::parse(b.data(), b.size());
    long pool = f.pool("SubNetworkPool");
    if (pool < 0) return;
    for (size_t i = pool + 1; i < f.blockEnd(pool); i = f.blockEnd(i)) {
        if (f.recs[i].level != 2) continue;
        auto shipped = countFields(f, i);
        bool fits = !shipped["ProcessorNodeCount"] && !shipped["ConstantDataNodeCount"];
        oct::File g = f;   // strip this SubNetwork's counts, regenerate, compare
        size_t end = g.blockEnd(i);
        for (size_t j = end; j-- > i + 1;)
            if (g.recs[j].level == 3 && isCountRecord(g.name(g.recs[j])))
                g.recs.erase(g.recs.begin() + j, g.recs.begin() + g.blockEnd(j));
        std::string why;
        if (!octx::addCounts(g, i, &why)) { ++*skip; ++g_skipWhy[why]; continue; }
        if (fits) {
            if (g.write() == b) ++*ok;
            else {
                ++*bad;
                if (*bad <= 10) printf("MISMATCH %s subnetwork @%zu\n", path.c_str(), i);
                if (*bad == 1 && getenv("OCTTEST_DUMP")) { std::string o = g.write(); std::ofstream(getenv("OCTTEST_DUMP"), std::ios::binary).write(o.data(), o.size()); }
            }
            continue;
        }
        auto ours = countFields(g, i);
        bool all = true;
        for (auto& [k, v] : shipped) {
            bool bound = k.find("AccumulatedSize") != std::string::npos || k == "LayerBindAdditionalSize";
            bool hit = bound ? ours[k] >= v : ours[k] == v;
            if (!hit && shipped["ConstantHeaderCount"] && (k == "ConstantHeaderCount" || k == "VariableHeaderCount" || k == "ConstantLayoutCount"))
                hit = k != "VariableHeaderCount" ? ours[k] == 0 : ours[k] == v + shipped["ConstantHeaderCount"];   // all variable by design
            if (!hit && shipped["ConstantDataNodeCount"] && (k == "ConstantDataNodeCount" || k == "VariableDataNodeCount"))
                hit = k == "ConstantDataNodeCount" ? ours[k] == 0 : ours[k] == v + shipped["ConstantDataNodeCount"];   // static -> variable
            if (!hit && k == "ConstantAccumulatedSize32" && !ours.count(k)) hit = true;   // no constant nodes left
            (hit ? g_fieldTally[k].first : g_fieldTally[k].second)++;
            if (!hit && g_fieldTally[k].second <= 3) printf("FIELD %s ours %ld shipped %ld  %s\n", k.c_str(), ours[k], v, path.c_str());
            all &= hit;
        }
        ++*(all ? ok : bad);
    }
}

// ---- cho.hpp checks ----
// <sub>/<xx>/<name>/<name>.oct of an extract's assets dir (choreographies, exploders), by name
static std::map<std::string, std::filesystem::path> choIndex(const std::filesystem::path& assets, const char* sub = "choreographies")
{
    std::map<std::string, std::filesystem::path> m;
    for (auto& xx : std::filesystem::directory_iterator(assets / sub)) {
        if (!xx.is_directory()) continue;
        for (auto& d : std::filesystem::directory_iterator(xx.path())) {
            auto p = d.path() / (d.path().filename().string() + ".oct");
            if (std::filesystem::exists(p)) m[d.path().filename().string()] = p;
        }
    }
    return m;
}

// "Kind{field=value ...}" per PlacementNode / InstanceNode / Attachment entry
static std::vector<std::string> layoutDump(const oct::File& f, const char* kind)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < f.recs.size(); ++i) {
        if (f.name(f.recs[i]) != kind) continue;
        std::string s = std::string(kind) + "{";
        for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
            const oct::Rec& r = f.recs[j];
            s += f.name(r) + "=";
            if (oct::spec(r.type).kind == oct::NAME) for (auto& v : f.nameValues(r)) s += v;
            else for (int32_t v : f.ints(r)) s += std::to_string(v);
            s += " ";
        }
        out.push_back(s + "}");
    }
    return out;
}

// --cho-layout <1.0 assets> <3.0 assets>: layout30 on every 1.0 choreography twin with a LayoutPool vs the 3.0 file
static int choLayoutTest(char** a)
{
    auto A = choIndex(a[2]), B = choIndex(a[3]);
    std::map<std::string, int> c;
    for (auto& [n, p] : A) {
        if (!B.count(n)) continue;
        std::string b = slurp(p);
        if (b.find(std::string("LayoutPool\0", 11)) == std::string::npos) continue;
        oct::File f = oct::File::parse(b.data(), b.size());
        std::string b3 = slurp(B[n]);
        oct::File g = oct::File::parse(b3.data(), b3.size());
        cho::layout30(f);
        for (const char* k : {"PlacementNode", "InstanceNode", "Attachment"}) {
            bool ok = layoutDump(f, k) == layoutDump(g, k);
            ++c[std::string(k) + (ok ? " same" : " differ")];
            if (!ok && c[std::string(k) + " differ"] <= 3) printf("differ %s %s\n", n.c_str(), k);
        }
    }
    for (auto& [k, v] : c) printf("%6d %s\n", v, k.c_str());
    return 0;
}

// --cho-time <assets> <subdir>...: every shipped 3.0 .oct with MinTime/MaxTime stripped and regenerated by time30
// must come back byte-identical (<subdir>/<xx>/<name>/<name>.oct: choreographies, exploders)
static int choTimeTest(int argc, char** a)
{
    namespace fs = std::filesystem;
    size_t same = 0, differ = 0, with = 0;
    auto one = [&](const fs::path& p) {
        std::string b = slurp(p);
        if (b.find(std::string("SubNetworkPool\0", 15)) == std::string::npos) return;
        oct::File f = oct::File::parse(b.data(), b.size());
        size_t n0 = f.recs.size();
        bool inSn = false;
        for (size_t i = 0; i < f.recs.size();) {
            const oct::Rec& r = f.recs[i];
            if (r.level <= 2) inSn = r.level == 2 && f.name(r) == "SubNetwork";
            if (inSn && r.level == 3 && (f.name(r) == "MinTime" || f.name(r) == "MaxTime")) f.recs.erase(f.recs.begin() + i);
            else ++i;
        }
        if (f.recs.size() != n0) ++with;
        cho::time30(f);
        if (f.write() == b) ++same;
        else if (++differ <= 5) printf("differ %s\n", p.string().c_str());
    };
    for (int i = 3; i < argc; ++i)
        for (auto& [n, p] : choIndex(a[2], a[i])) one(p);
    printf("time30: %zu files byte-identical, %zu differ (%zu had a time range)\n", same, differ, with);
    return differ ? 1 : 0;
}

// --cho-time10 <1.0 assets> <3.0 assets>: time30 on each 1.0 choreography twin; its SubNetworks' MinTime/MaxTime
// (in order) against the 3.0 twin's records
static int choTime10Test(char** a)
{
    auto A = choIndex(a[2]), B = choIndex(a[3]);
    auto ranges = [](const oct::File& f) {
        std::vector<std::pair<float, float>> v;
        for (size_t i = 0; i < f.recs.size(); ++i) {
            const std::string& n = f.name(f.recs[i]);
            if (f.recs[i].level == 3 && (n == "MinTime" || n == "MaxTime")) {
                float x;
                memcpy(&x, f.recs[i].pay.data(), 4);
                if (n == "MinTime") v.push_back({x, 0.f}); else if (!v.empty()) v.back().second = x;
            }
        }
        return v;
    };
    size_t same = 0, close = 0, differ = 0, none = 0;
    for (auto& [n, p] : A) {
        if (!B.count(n)) continue;
        std::string b = slurp(p), b3 = slurp(B[n]);
        oct::File f = oct::File::parse(b.data(), b.size()), g = oct::File::parse(b3.data(), b3.size());
        cho::time30(f);
        auto x = ranges(f), y = ranges(g);
        if (x.empty() && y.empty()) { ++none; continue; }
        bool nearly = x.size() == y.size();
        for (size_t k = 0; nearly && k < x.size(); ++k)
            nearly = fabs(x[k].first - y[k].first) < 1e-4 && fabs(x[k].second - y[k].second) < 1e-4;
        if (x == y) ++same;
        else if (nearly) ++close;
        else if (++differ <= 8) printf("differ %s: %zu ranges vs %zu, first %g..%g vs %g..%g\n", n.c_str(), x.size(), y.size(),
                                       x.empty() ? 0 : x[0].first, x.empty() ? 0 : x[0].second, y.empty() ? 0 : y[0].first,
                                       y.empty() ? 0 : y[0].second);
    }
    printf("time30 on 1.0 twins: %zu equal to 3.0, %zu within 1e-4, %zu differ, %zu with no range in either\n", same, close,
           differ, none);
    return 0;
}

// --cho-clips <pairs.txt>: lines '<1.0 .oct>|<3.0 .oct>' (clip twins); every 1.0 ClipDataBlock through clipBlock30
// against the 3.0 twin's block in the same position
static int choClipsTest(char** a)
{
    std::ifstream in(a[2]);
    std::string line;
    size_t exact = 0, sameLen = 0, other = 0, failed = 0, files = 0, areaSame = 0, shapeSame = 0, anyExactN = 0, anyShapeN = 0;
    // header + descriptor area: up to the first section / constant offset (the data sections follow)
    auto area = [](const std::string& d) {
        uint32_t nDesc = cho::rd(d, 8, 2), nSec = cho::rd(d, 10, 2), e = cho::rd(d, 12, 4);
        for (uint32_t i = 0; i < nSec; ++i) e = (std::min)(e, cho::rd(d, 0x10 + 4 * (nDesc + i), 4));
        return d.substr(0, (std::min<size_t>)(e, d.size()));
    };
    // the shape of a 3.0-layout block: counts, section ids, output heads and comp codes (operands left out)
    auto shape = [](const std::string& d) {
        std::string s;
        uint32_t nDesc = cho::rd(d, 8, 2), nSec = cho::rd(d, 10, 2);
        s += std::to_string(nDesc) + "/" + std::to_string(nSec) + ":";
        for (uint32_t i = 0; i < nDesc; ++i) {
            uint32_t o = cho::rd(d, 0x10 + 4 * i, 4), n = cho::rd(d, o + 2, 2);
            for (uint32_t k = 0; k < n; ++k) s += std::to_string(cho::rd(d, o + 4 + 2 * k, 2)) + ",";
            uint32_t x = cho::rd(d, o + 4 + 2 * n, 2);
            for (size_t t : {(size_t)o + 6 + 2 * n, (size_t)o + x}) {
                uint32_t nOut = cho::rd(d, t, 2), comps = 0;
                if (nOut == 0xffffffffu) return s + "?";
                s += "[";
                for (uint32_t h = 0; h < nOut; ++h) { uint32_t hd = cho::rd(d, t + 2 + 2 * h, 2); comps += hd >> 13; s += std::to_string(hd) + " "; }
                size_t c = t + 2 + 2 * nOut;
                for (uint32_t k = 0; k < comps; ++k) {
                    uint32_t code = cho::rd(d, c, 2);
                    s += std::to_string(code) + ".";
                    c += (code & 0xf000) == 0x1000 || (code & 0xf000) == 0x2000 ? 8 : 4;
                }
                s += "]";
            }
        }
        return s;
    };
    auto blocks = [](const std::string& path) {
        std::vector<std::string> v;
        std::string b = slurp(path);
        oct::File f = oct::File::parse(b.data(), b.size());
        for (auto& r : f.recs)
            if (f.name(r) == "ClipDataBlock") v.push_back(r.pay.substr(oct::spec(r.type).cw));
        return v;
    };
    while (std::getline(in, line)) {
        size_t bar = line.find('|');
        if (bar == std::string::npos) continue;
        std::vector<std::string> A, B;
        try { A = blocks(line.substr(0, bar)); B = blocks(line.substr(bar + 1)); } catch (...) { continue; }
        ++files;
        for (size_t i = 0; i < A.size() && i < B.size(); ++i) {
            std::string out;
            if (!cho::clipBlock30(A[i], out)) { ++failed; continue; }
            if (out == B[i]) ++exact;
            else if (out.size() == B[i].size()) ++sameLen;
            else ++other;
            if (out != B[i] && area(out) == area(B[i])) ++areaSame;
            bool anyShape = false, anyExact = false;   // blocks are not always in the same order in the twin
            for (auto& b : B) { anyExact |= out == b; anyShape |= shape(out) == shape(b); }
            anyExactN += anyExact;
            anyShapeN += anyShape;
            if (out != B[i] && shape(out) == shape(B[i])) ++shapeSame;
            else if (!anyShape && getenv("CHO_SHAPES"))
                printf("shape %s\n  10>30 %s\n  30    %s\n", line.substr(0, bar).c_str(), shape(out).substr(0, 300).c_str(),
                       shape(B[i]).substr(0, 300).c_str());
        }
    }
    printf("clips: %zu twin files; blocks exact %zu, same length %zu, other length %zu, not parsed %zu; of the inexact: "
           "header+descriptor area identical %zu, same shape (heads + comp codes) %zu; any twin block: exact %zu, same "
           "shape %zu\n", files, exact, sameLen, other, failed, areaSame, shapeSame, anyExactN, anyShapeN);
    return 0;
}

// --cho-file <1.0 .oct>...: the plugin's cho steps (clips with every motion clip, layout, time) on each file; the
// result must re-parse and re-write byte-identically, and every re-packed block must parse in the 3.0 layout (desc
// tables ending at the next descriptor or the data). Prints what changed.
static bool parses30(const std::string& d)
{
    uint32_t nDesc = cho::rd(d, 8, 2), nSec = cho::rd(d, 10, 2);
    for (uint32_t i = 0; i < nDesc; ++i) {
        uint32_t o = cho::rd(d, 0x10 + 4 * i, 4), n = cho::rd(d, o + 2, 2);
        if (n == 0xffffffffu) return false;
        uint32_t x = cho::rd(d, o + 4 + 2 * n, 2);
        size_t t = o + 6 + 2 * (size_t)n;
        for (int k = 0; k < 2; ++k) {
            uint32_t nOut = cho::rd(d, t, 2), comps = 0;
            if (nOut == 0xffffffffu) return false;
            for (uint32_t h = 0; h < nOut; ++h) comps += cho::rd(d, t + 2 + 2 * h, 2) >> 13;
            size_t c = t + 2 + 2 * (size_t)nOut;
            for (uint32_t q = 0; q < comps; ++q) {
                uint32_t code = cho::rd(d, c, 2);
                if (code == 0xffffffffu) return false;
                c += (code & 0xf000) == 0x1000 || (code & 0xf000) == 0x2000 ? 8 : 4;
            }
            if (k == 0 && c != o + x) return false;   // the sampled table ends where X says
            if (c > d.size()) return false;
            t = o + x;
        }
    }
    (void)nSec;
    return true;
}

// --cho-corpus <1.0 assets> <sub>...: the same over every <sub>/<xx>/<name>/<name>.oct (and its motions/*.oct);
// prints only the bad ones and a total
static int choFileTest(int argc, char** a)
{
    std::vector<std::string> files;
    bool corpus = std::string(a[1]) == "--cho-corpus";
    if (corpus) {
        for (int i = 3; i < argc; ++i)
            for (auto& [n, p] : choIndex(a[2], a[i])) {
                files.push_back(p.string());
                auto md = p.parent_path() / "motions";
                if (std::filesystem::is_directory(md))
                    for (auto& e : std::filesystem::directory_iterator(md))
                        if (e.path().extension() == ".oct") files.push_back(e.path().string());
            }
    } else files.assign(a + 2, a + argc);
    int bad = 0;
    size_t tc = 0, tl = 0, ti = 0, tt = 0, tb = 0;
    for (auto& path : files) {
        std::string b = slurp(path);
        if (b.size() < 4 || oct::u((const uint8_t*)b.data(), 4) != oct::MAGIC) continue;   // empty / other tuples
        oct::File f = oct::File::parse(b.data(), b.size());
        int failed = 0, inst = 0;
        int nc = cho::clips30(f, [](const std::string&) { return true; }, &failed);
        int nl = cho::layout30(f, &inst);
        int nt = cho::time30(f);
        std::string w = f.write();
        oct::File g = oct::File::parse(w.data(), w.size());
        bool round = g.write() == w;
        size_t blocks = 0, ok30 = 0;
        for (auto& r : g.recs)
            if (g.name(r) == "ClipDataBlock") { ++blocks; ok30 += parses30(r.pay.substr(oct::spec(r.type).cw)); }
        bool good = round && ok30 == blocks && !failed;
        bad += !good;
        tc += nc; tl += nl; ti += inst; tt += nt; tb += blocks;
        if (!corpus || !good)
            printf("%s %s: clips %d (not parsed %d), layouts %d (%d instances), time ranges %d; re-parse %s, %zu/%zu blocks "
                   "parse as 3.0\n", good ? "ok " : "BAD", path.c_str(), nc, failed, nl, inst, nt, round ? "same" : "DIFFERS", ok30, blocks);
    }
    printf("%zu files, %d bad; %zu blocks re-packed of %zu, %zu layouts (%zu instances), %zu time ranges\n", files.size(),
           bad, tc, tb, tl, ti, tt);
    return bad ? 1 : 0;
}

// --banm <pairs.txt>: lines '<1.0 .banm>|<3.0 .banm (text)>|<engine path>'; cho::banm30 on the 1.0 binary tuple must
// give the 3.0 twin's records ("depth|name|values", numbers %.6g), the text read by a minimal tuple-text reader
static std::vector<std::string> canonBin(const oct::File& f)
{
    std::vector<std::string> out;
    for (size_t i = 1; i < f.recs.size(); ++i) {
        const oct::Rec& r = f.recs[i];
        oct::Spec s = oct::spec(r.type);
        std::string l = std::to_string(r.level - 1) + "|" + f.name(r) + "|";
        if (s.kind == oct::NAME || s.keyed) for (auto& v : f.nameValues(r)) l += v + ",";
        if (s.kind == oct::FLOAT || s.kind == oct::INT)
            for (int32_t v : f.ints(r)) {
                char b[32];
                float x;
                memcpy(&x, &v, 4);
                sprintf_s(b, "%.6g,", s.kind == oct::FLOAT ? (double)x : (double)v);
                l += b;
            }
        out.push_back(l);
    }
    return out;
}

static std::vector<std::string> canonText(const std::string& t)
{
    std::vector<std::string> out, tok;
    for (size_t i = 0; i < t.size();) {
        char c = t[i];
        if (isspace((unsigned char)c)) { ++i; continue; }
        if (c == '"') { size_t e = t.find('"', i + 1); tok.push_back(t.substr(i, e - i + 1)); i = e + 1; continue; }
        if (strchr("={}[]", c)) { tok.push_back(std::string(1, c)); ++i; continue; }
        size_t e = i;
        while (e < t.size() && !isspace((unsigned char)t[e]) && !strchr("={}[]\"", t[e])) ++e;
        tok.push_back(t.substr(i, e - i));
        i = e;
    }
    int depth = 0;
    auto val = [](const std::string& s) {
        if (s[0] == '"') return s.substr(1, s.size() - 2) + ",";
        char b[32];
        sprintf_s(b, "%.6g,", atof(s.c_str()));
        return std::string(b);
    };
    for (size_t i = 0; i < tok.size(); ++i) {
        if (tok[i] == "}") { --depth; continue; }
        std::string l = std::to_string(depth) + "|" + tok[i] + "|";
        size_t j = i + 1;
        if (j < tok.size() && tok[j][0] == '"' && j + 1 < tok.size() && tok[j + 1] == "=") l += val(tok[j++]);
        ++j;   // '='
        if (j < tok.size() && tok[j] == "{") { ++depth; i = j; }
        else if (j < tok.size() && tok[j] == "[") { while (++j < tok.size() && tok[j] != "]") l += val(tok[j]); i = j; }
        else { l += val(tok[j]); i = j; }
        out.push_back(l);
    }
    return out;
}

static int banmTest(char** a)
{
    std::ifstream in(a[2]);
    std::string line;
    size_t same = 0, differ = 0, rejected = 0, otherDiff = 0;
    while (std::getline(in, line)) {
        size_t b1 = line.find('|'), b2 = line.find('|', b1 + 1);
        if (b2 == std::string::npos) continue;
        std::string b = slurp(line.substr(0, b1));
        oct::File f = oct::File::parse(b.data(), b.size());
        if (!cho::banm30(f, line.substr(b2 + 1))) { ++rejected; continue; }
        std::string w = f.write();
        oct::File g = oct::File::parse(w.data(), w.size());
        auto A = canonBin(g), B = canonText(slurp(line.substr(b1 + 1, b2 - b1 - 1)));
        if (A == B) { ++same; continue; }
        ++differ;
        // a difference outside Filename lines is the re-export, not the codec
        auto strip = [](std::vector<std::string> v) {
            std::vector<std::string> o;
            for (auto& s : v) if (s.find("|Filename|") == std::string::npos) o.push_back(s);
            return o;
        };
        bool onlyNames = strip(A) == strip(B);
        if (!onlyNames) ++otherDiff;
        if (differ - otherDiff <= 5 && onlyNames) {
            printf("names differ %s\n", line.substr(b2 + 1).c_str());
            for (size_t k = 0; k < A.size() && k < B.size(); ++k)
                if (A[k] != B[k]) { printf("  ours %s\n  3.0  %s\n", A[k].c_str(), B[k].c_str()); break; }
        }
    }
    printf("banm: %zu identical to the 3.0 twin, %zu differ (%zu of them outside Filename lines: re-exported), %zu not 1.0f\n",
           same, differ, otherDiff, rejected);
    return 0;
}

// --cho-clipdump <oct> <out>: the re-packed blocks of one file, concatenated (for the Python oracle clip10.py)
static int choClipDump(char** a)
{
    std::string b = slurp(a[2]), all;
    oct::File f = oct::File::parse(b.data(), b.size());
    for (auto& r : f.recs) {
        if (f.name(r) != "ClipDataBlock") continue;
        std::string out;
        if (!cho::clipBlock30(r.pay.substr(oct::spec(r.type).cw), out)) out = "FAIL";
        all += out;
    }
    std::ofstream(a[3], std::ios::binary).write(all.data(), all.size());
    return 0;
}

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    const char* exts[] = {".oct", ".mer", ".merdebug", ".bent", ".banm", ".bct"};
    if (argc > 6 && std::string(argv[1]) == "--render") return renderTest(argv);
    if (argc > 3 && std::string(argv[1]) == "--cho-layout") return choLayoutTest(argv);
    if (argc > 3 && std::string(argv[1]) == "--cho-time") return choTimeTest(argc, argv);
    if (argc > 2 && std::string(argv[1]) == "--cho-clips") return choClipsTest(argv);
    if (argc > 3 && std::string(argv[1]) == "--cho-clipdump") return choClipDump(argv);
    if (argc > 2 && std::string(argv[1]) == "--cho-file") return choFileTest(argc, argv);
    if (argc > 3 && std::string(argv[1]) == "--cho-corpus") return choFileTest(argc, argv);
    if (argc > 2 && std::string(argv[1]) == "--banm") return banmTest(argv);
    if (argc > 3 && std::string(argv[1]) == "--cho-time10") return choTime10Test(argv);
    if (argc > 3 && std::string(argv[1]) == "--cho-alias") {   // --cho-alias <1.0 datamap.lua> <engine path>...: Aliases as the plugin feeds it
        std::string in = slurp(argv[2]), out;
        cho::Aliases al;
        luachunk::rewrite(in, out, [&](std::string& s) { al.add(s); return datamap::normalise(s); });
        int hit = 0;
        for (int i = 3; i < argc; ++i) {
            std::string r = al.resolve(argv[i]);
            hit += !r.empty();
            printf("%s -> %s\n", argv[i], r.empty() ? "(none)" : r.c_str());
        }
        printf("%zu aliases, %d of %d paths resolved\n", al.map.size(), hit, argc - 3);
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--chd") {   // --chd <root>: every .chd with its .lua beside it, compiled
        size_t ok = 0, bad = 0;
        for (auto& e : fs::recursive_directory_iterator(argv[2])) {
            if (e.path().extension() != ".chd") continue;
            fs::path src = e.path();
            src.replace_extension(".lua");
            if (!fs::exists(src)) continue;
            std::string got;
            try { got = chd::compile(chd::lua(slurp(src))); } catch (std::exception& x) { printf("FAIL %s: %s\n", src.string().c_str(), x.what()); }
            if (got == slurp(e.path())) ++ok;
            else { ++bad; printf("MISMATCH %s (%zu vs %zu B)\n", e.path().string().c_str(), got.size(), (size_t)fs::file_size(e.path())); }
        }
        printf("chd compiled exact: %zu, mismatch %zu\n", ok, bad);
        return 0;
    }
    if (argc > 4 && std::string(argv[1]) == "--mer") {   // --mer <di1compat_mer.txt> <1.0 .mer> <out.mer> [out.mtc]
        mer::Table T;
        if (!T.load(slurp(argv[2]))) { printf("table load failed\n"); return 2; }
        std::string b = slurp(argv[3]), why;
        oct::File f = oct::File::parse(b.data(), b.size());
        mer::Stats st;
        if (!mer::transcode(T, f, &st, &why)) { printf("not transcoded: %s\n", why.c_str()); return 1; }
        std::string o = f.write();
        std::ofstream(argv[4], std::ios::binary).write(o.data(), o.size());
        if (argc > 5) { std::string m = mer::mtc(f); std::ofstream(argv[5], std::ios::binary).write(m.data(), m.size()); }
        printf("%d namespaces, %d nodes, %d params, %d pin tables copied, %d stand-ins, %d links, %d dropped; %zu B\n",
               st.namespaces, st.nodes, st.params, st.pinsCopied, st.stubs, st.links, st.linksDropped, o.size());
        return 0;
    }
    if (argc > 4 && std::string(argv[1]) == "--dct") {   // --dct <1.0 table> <its stringids> <key name>
        std::string raw = slurp(argv[2]), ids = slurp(argv[3]);
        dct::Dict d;
        if (!dct::load(raw, d) || dct::dump(d) != raw) { printf("round trip FAILED\n"); return 1; }
        int dropped = 0, kept = dct::reseed(raw, ids, dct::SEED30, &dropped);
        printf("reseeded: %d keys kept, %d unnamed dropped\n", kept, dropped);
        // the engine's probe: lowercase key, hash with the table's seed, slot = hash % slots, linear
        std::string k = argv[4];
        for (char& c : k) c = (char)tolower((unsigned char)c);
        uint32_t h = dct::lookup2((const uint8_t*)k.data(), k.size(), dct::u32(raw, 8)), ns = dct::u32(raw, 0x10);
        for (uint32_t i = h % ns; dct::u32(raw, 0x24 + 12 * i); i = (i + 1) % ns)
            if (dct::u32(raw, 0x20 + 12 * i) == h) { printf("%s = \"%s\"\n", argv[4], raw.c_str() + 0x25 + 12 * i + (int32_t)dct::u32(raw, 0x24 + 12 * i)); return 0; }
        printf("%s not found\n", argv[4]);
        return 1;
    }
    if (argc > 3 && std::string(argv[1]) == "--btd") {   // --btd <1.0 .btd> <out .btd>: + <out>.btm beside it
        std::string b = slurp(argv[2]), o;
        if (!btd::convert(b, o)) { printf("not the 1.0 block layout\n"); return 1; }
        std::ofstream(argv[3], std::ios::binary).write(o.data(), o.size());
        std::string m = btd::btm(o), mp = std::string(argv[3]);
        mp = mp.substr(0, mp.size() - 4) + ".btm";
        std::ofstream(mp, std::ios::binary).write(m.data(), m.size());
        printf("%u blocks, %u B\n", (unsigned)btd::blocks(o, 3).size(), (unsigned)o.size());
        return 0;
    }
    if (argc > 4 && std::string(argv[1]) == "--bent") {   // --bent <1.0 .bent> <engine path> <out>: 4.1 -> 4.1f
        std::string b = slurp(argv[2]), why;
        oct::File f = oct::File::parse(b.data(), b.size());
        if (!bent::transcode(f, argv[3], &why)) { printf("not transcoded: %s\n", why.c_str()); return 1; }
        std::string o = f.write();
        std::ofstream(argv[4], std::ios::binary).write(o.data(), o.size());
        printf("wrote %zu B, %zu records\n", o.size(), f.recs.size());
        return 0;
    }
    if (argc > 3 && std::string(argv[1]) == "--oct") {   // --oct <1.0 .oct> <out>: the SubNetwork transforms only
        std::string b = slurp(argv[2]);
        auto t0 = std::chrono::steady_clock::now();
        auto lap = [&](const char* what, int n) {
            auto t1 = std::chrono::steady_clock::now();
            printf("%-28s %8d  %7.1f ms\n", what, n, std::chrono::duration<double, std::milli>(t1 - t0).count());
            t0 = t1;
        };
        oct::File f = oct::File::parse(b.data(), b.size());
        lap("parse", 0);
        lap("skin influences redirected", octx::skin30(f));
        lap("locators redirected", octx::locator30(f));
        lap("asset pool added", (int)octx::addAssetPool(f));
        lap("collision aggregates added", octx::collision30(f));
        lap("lighting nodes converted", octx::realm30w(f));
        lap("subnetworks with counts", octx::addAllCounts(f));
        std::string o = f.write();
        lap("write", (int)o.size());
        std::ofstream(argv[3], std::ios::binary).write(o.data(), o.size());
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--datamap") {   // --datamap <1.0 *datamap.lua> [out]: PS-2 rewrite
        std::string in = slurp(argv[2]), out;
        std::map<std::string, int> kinds;
        int n = luachunk::rewrite(in, out, [&](std::string& s) {
            std::string before = s;
            if (!datamap::normalise(s)) return false;
            kinds[before.substr(0, before.find('|'))]++;
            return true;
        });
        for (auto& [k, c] : kinds) printf("%s %d\n", k.c_str(), c);
        printf("%d constants rewritten, %zu -> %zu B\n", n, in.size(), out.size());
        if (argc > 3) std::ofstream(argv[3], std::ios::binary).write(out.data(), out.size());
        return 0;
    }
    if (argc > 3 && std::string(argv[1]) == "--zip") {   // --zip <1.0 zip> <member>: the plugin's read10 calls
        mz_zip_archive z{};
        if (!mz_zip_reader_init_file(&z, argv[2], 0)) { printf("init FAILED (%s)\n", mz_zip_get_error_string(mz_zip_get_last_error(&z))); return 1; }
        int i = mz_zip_reader_locate_file(&z, argv[3], nullptr, 0);
        size_t n = 0;
        void* d = i < 0 ? nullptr : mz_zip_reader_extract_to_heap(&z, (mz_uint)i, &n, 0);
        printf("locate %d, extract %s %zu B (%s)\n", i, d ? "ok" : "FAILED", n, mz_zip_get_error_string(mz_zip_get_last_error(&z)));
        mz_free(d);
        return d ? 0 : 1;
    }
    if (argc > 4 && std::string(argv[1]) == "--gen30") {   // --gen30 <render table> <1.0 extract root> <gen30.py dump dir>
        // every shader gen30.py dumped (.shd + <shader>.<program id>.hlsl, compile off): the C++ port must match
        render::Table T;
        if (!T.load(slurp(argv[2]))) { printf("table load failed\n"); return 2; }
        fs::path ext10 = fs::path(argv[3]) / "assets", dump = argv[4];
        gen30::Read read10 = [&](const std::string& p, std::string& o) { o = slurp(ext10 / p); return !o.empty(); };
        gen30::VFormats vf = gen30::vformats(slurp(ext10 / "shaders/shaders/vformat.vtxf"));
        size_t ok = 0, bad = 0, progs = 0;
        for (auto& e : fs::directory_iterator(dump)) {
            if (e.path().extension() != ".shd") continue;
            std::string sh = e.path().stem().string();
            gen30::Generated g;
            gen30::generate(T.shaders10, read10, vf, sh, nullptr, g);
            bool same = g.shd == slurp(e.path());
            for (auto& p : g.programs) {
                char nm[64];
                sprintf_s(nm, "%s.%08x.hlsl", sh.c_str(), p.id);
                ++progs;
                if (getenv("GEN30_DUMP")) std::ofstream(fs::path(getenv("GEN30_DUMP")) / nm, std::ios::binary) << p.hlsl;
                if (p.hlsl != slurp(dump / nm)) {
                    if (same && bad < 3) printf("HLSL differs: %s\n", nm);
                    same = false;
                }
            }
            if (same) ++ok;
            else if (++bad <= 5) printf("MISMATCH %s (shd %s)\n", sh.c_str(), g.shd == slurp(e.path()) ? "same" : "differs");
        }
        printf("%zu shaders identical to gen30.py, %zu differ (%zu programs)\n", ok, bad, progs);
        return bad ? 1 : 0;
    }
    if (argc > 5 && std::string(argv[1]) == "--warm") {   // --warm <render table> <1.0 extract root> <cache dir> <1.0 .mtb>...
        // compile the translated shaders these bundles name into the plugin's cache (same compiler, same keys)
        render::Table T;
        if (!T.load(slurp(argv[2]))) { printf("table load failed\n"); return 2; }
        fs::path ext10 = fs::path(argv[3]) / "assets";
        std::string cache = argv[4];
        fs::create_directories(cache);
        gen30::Read read10 = [&](const std::string& p, std::string& o) { o = slurp(ext10 / p); return !o.empty(); };
        gen30::VFormats vf = gen30::vformats(slurp(ext10 / "shaders/shaders/vformat.vtxf"));
        typedef HRESULT(WINAPI * CompileFn)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, void**, void**);
        HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
        CompileFn cf = dc ? (CompileFn)GetProcAddress(dc, "D3DCompile") : nullptr;
        if (!cf) { printf("no d3dcompiler_47\n"); return 2; }
        auto fnv = [](const std::string& x) { uint64_t h = 1469598103934665603ull; for (unsigned char c : x) h = (h ^ c) * 1099511628211ull; return h; };
        size_t compiled = 0, cached = 0, failed = 0;
        auto blob = [](void* b, std::string& out) {
            void** vt = *(void***)b;
            out.assign((const char*)((void*(__stdcall*)(void*))vt[3])(b), ((SIZE_T(__stdcall*)(void*))vt[4])(b));
            ((ULONG(__stdcall*)(void*))vt[2])(b);
        };
        gen30::Compile comp = [&](const std::string& hlsl, const char* target, std::string& code, std::string& msg) {
            char nm[40];
            sprintf_s(nm, "%016llx.dxbc", (unsigned long long)fnv(std::string(target) + "\n" + hlsl));
            fs::path path = fs::path(cache) / nm;
            code = slurp(path);
            if (code.size() > 32) { ++cached; return true; }
            void *c = nullptr, *e = nullptr;
            HRESULT hr = cf(hlsl.data(), hlsl.size(), "di1compat", nullptr, nullptr, "main", target, 0, 0, &c, &e);
            if (e) blob(e, msg);
            if (FAILED(hr) || !c) { ++failed; return false; }
            blob(c, code);
            std::ofstream(path, std::ios::binary).write(code.data(), code.size());
            ++compiled;
            return true;
        };
        std::set<std::string> shaders;
        for (int a = 5; a < argc; ++a) {
            std::string b = slurp(argv[a]);
            size_t o = 16, nc = b.size() >= 16 ? gen30::u32(b, 12) : 0;
            for (size_t c = 0; c < nc && o + 16 <= b.size(); ++c) {
                if (!b.compare(o, 4, "MATP")) {
                    size_t p = (o + 16 + 3) & ~(size_t)3;
                    uint32_t ns = gen30::u32(b, p);
                    p += 4;
                    for (uint32_t i = 0; i < ns; ++i) { size_t e = b.find('\0', p); shaders.insert(b.substr(p, e - p)); p = e + 1; }
                }
                o += gen30::u32(b, o + 8);
            }
        }
        size_t probs = 0;
        for (auto& sh : shaders) {
            gen30::Generated g;
            gen30::generate(T.shaders10, read10, vf, sh, comp, g);
            for (auto& pr : g.problems) if (++probs <= 10) printf("  %s: %s\n", sh.c_str(), pr.c_str());
        }
        printf("%zu shaders: %zu programs compiled, %zu already cached, %zu failed, %zu problems\n", shaders.size(), compiled, cached, failed, probs);
        return failed ? 1 : 0;
    }
    if (argc > 3 && std::string(argv[1]) == "--read10") {   // --read10 <1.0 install> <engine path>: read10's candidates
        std::string p = argv[3];
        for (char& c : p) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
        for (size_t cut = p.rfind('/'); cut != std::string::npos && cut > 0; cut = p.rfind('/', cut - 1)) {
            std::string dir = p.substr(0, cut), member = p.substr(cut + 1), stem = member.substr(0, member.find('.'));
            std::string zips[3] = {dir + ".zip", dir + "/" + dir.substr(dir.rfind('/') + 1) + ".zip", dir + "/" + stem + ".zip"};
            for (auto& zr : zips) {
                std::string full = std::string(argv[2]) + "\\assets\\" + zr;
                for (char& c : full) if (c == '/') c = '\\';
                mz_zip_archive z{};
                bool ok = GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES && mz_zip_reader_init_file(&z, full.c_str(), 0);
                int i = ok ? mz_zip_reader_locate_file(&z, member.c_str(), nullptr, 0) : -2;
                printf("%s : %s -> %d\n", zr.c_str(), member.c_str(), i);
                if (ok) mz_zip_reader_end(&z);
            }
        }
        return 0;
    }
    if (argc > 3 && std::string(argv[1]) == "--member") {   // --member <archive> <name>: one read, its size
        std::string got;
        bool r = arc30::readMember(argv[2], argv[3], got);
        printf("%s %zu B\n", r ? "ok" : "FAILED", got.size());
        return r ? 0 : 1;
    }
    if (argc > 4 && std::string(argv[1]) == "--arc") {   // --arc <render table> <game dir> <3.0 extract root>
        // every table bundle read from the install's archive (arc30) must equal the extract's file
        render::Table T;
        if (!T.load(slurp(argv[2]))) return 2;
        std::set<std::string> seen;
        size_t ok = 0, bad = 0;
        size_t skip = getenv("ARC_SKIP") ? (size_t)atoll(getenv("ARC_SKIP")) : 0, n = 0;
        auto check = [&](const std::string& x) {
            if (!seen.insert(x).second) return;
            if (n++ < skip) return;
            std::string rel = x.substr(2), got;
            size_t cut = rel.rfind('/');
            fprintf(stderr, "%s\n", x.c_str());
            std::string arc = std::string(argv[3]) + "/assets/" + rel.substr(0, cut) + ".zip";
            bool r = arc30::readMember(arc, rel.substr(cut + 1), got);
            fprintf(stderr, "  read %d %zu\n", (int)r, got.size());
            std::string ext = slurp(std::filesystem::path(argv[4]) / "assets" / rel);
            fprintf(stderr, "  extract %zu\n", ext.size());
            if (r && got == ext) ++ok;
            else { ++bad; if (bad <= 10) printf("MISMATCH %s (%s)\n", x.c_str(), r ? "differs" : "unreadable"); }
        };
        for (auto& [k, r] : T.donors) check(r.extract);
        for (auto& [k, r] : T.matindex) check(r.extract);
        printf("archive reads: %zu equal the extract, %zu not\n", ok, bad);
        return bad ? 1 : 0;
    }
    bool counts = argc > 1 && std::string(argv[1]) == "--counts";
    size_t files = 0, bad = 0, cOk = 0, cBad = 0, cSkip = 0;
    for (int a = counts ? 2 : 1; a < argc; ++a)
        for (auto& e : fs::recursive_directory_iterator(argv[a], fs::directory_options::skip_permission_denied)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (auto& c : ext) c = (char)tolower((unsigned char)c);
            bool want = false;
            for (auto x : exts) want |= ext == x;
            if (!want) continue;
            std::ifstream in(e.path(), std::ios::binary);
            std::string b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (b.size() < 4 || oct::u((const uint8_t*)b.data(), 4) != oct::MAGIC) continue;   // other formats share the extension
            ++files;
            if (counts) {
                if (ext == ".oct")
                    try { countsCheck(b, e.path().string(), &cOk, &cBad, &cSkip); }
                    catch (std::exception& x) { printf("FAIL %s: %s\n", e.path().string().c_str(), x.what()); }
                continue;
            }
            try {
                oct::File f = oct::File::parse(b.data(), b.size());
                if (f.write() != b || !f.tail.empty()) { ++bad; printf("MISMATCH %s\n", e.path().string().c_str()); }
            } catch (std::exception& x) {
                ++bad;
                printf("FAIL %s: %s\n", e.path().string().c_str(), x.what());
            }
        }
    if (counts) {
        printf("SubNetwork counts regenerated: %zu exact, %zu mismatch, %zu outside the rule\n", cOk, cBad, cSkip);
        for (auto& [k, n] : g_skipWhy) printf("  skipped: %-40s %zu\n", k.c_str(), n);
        for (auto& [k, t] : g_fieldTally) printf("  per-field (processor / constant graphs) %-32s %zu ok %zu miss\n", k.c_str(), t.first, t.second);
        return cBad ? 1 : 0;
    }
    printf("round-trip %zu/%zu exact\n", files - bad, files);
    return bad ? 1 : 0;
}

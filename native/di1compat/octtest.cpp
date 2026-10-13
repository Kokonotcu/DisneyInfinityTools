// octtest <root>...           parse and rewrite every tuple file under the roots (oct.py --selftest is the oracle)
// octtest --counts <root>...  OCT-1 check: in every shipped processor-free SubNetwork, strip the count records and
//                             regenerate them with octx::addCounts; the file must come back byte-identical
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "octx.hpp"
#include "bent.hpp"
#include "chd.hpp"
#include "render.hpp"
#include "arc30.hpp"
#include "datamap.hpp"
#include "luachunk.hpp"

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
    io.read30 = [&](const std::string& p, std::string& o) {   // '3:<extract path>'
        o = slurp(fs::path(ext30) / "assets" / p.substr(2));
        return !o.empty();
    };
    io.read10 = [&](const std::string& p, std::string& o) { o = slurp(fs::path(ext10) / "assets" / p); return !o.empty(); };
    std::string ob = slurp(world / (stem + ".oct"));
    oct::File f = oct::File::parse(ob.data(), ob.size());
    render::Result r;
    bool ok = render::transcodeWorld(T, io, f, stem, slurp(world / (stem + ".mtb")), slurp(world / (stem + "_0.vbuf")),
                                     slurp(world / (stem + "_0.ibuf")), r);
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
            bool hit = k.find("AccumulatedSize") != std::string::npos ? ours[k] >= v : ours[k] == v;
            if (shipped["ConstantHeaderCount"] && (k == "ConstantHeaderCount" || k == "VariableHeaderCount" || k == "ConstantLayoutCount"))
                hit = k != "VariableHeaderCount" || ours[k] == v + shipped["ConstantHeaderCount"];   // headers all variable by design
            (hit ? g_fieldTally[k].first : g_fieldTally[k].second)++;
            if (!hit && g_fieldTally[k].second <= 3) printf("FIELD %s ours %ld shipped %ld  %s\n", k.c_str(), ours[k], v, path.c_str());
            all &= hit;
        }
        ++*(all ? ok : bad);
    }
}

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    const char* exts[] = {".oct", ".mer", ".merdebug", ".bent", ".banm", ".bct"};
    if (argc > 6 && std::string(argv[1]) == "--render") return renderTest(argv);
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
        oct::File f = oct::File::parse(b.data(), b.size());
        printf("subnetworks with counts added: %d\n", octx::addAllCounts(f));
        std::string o = f.write();
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

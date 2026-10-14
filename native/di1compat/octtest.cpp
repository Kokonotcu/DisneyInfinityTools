// octtest <root>...           parse and rewrite every tuple file under the roots (oct.py --selftest is the oracle)
// octtest --counts <root>...  OCT-1 check: in every shipped processor-free SubNetwork, strip the count records and
//                             regenerate them with octx::addCounts; the file must come back byte-identical
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
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
#include "sx.hpp"
#include "sys.hpp"
#include "npc.hpp"
#include "cho.hpp"
#include "ui.hpp"
#include "audio.hpp"
#include <cstdarg>
#include "vfx.hpp"

static std::string slurp(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// octtest --ui <1.0 extract root> <3.0 extract root> <out dir> [3.0 zone names file, one per line]
// ui.hpp + dct::supplement over the extracts: loading-screen sequence and its movies, flash image ids against every
// shipped movie, FMV clips, tips merge (written to <out>/tips.lua for the Lua check), lang/IN + lang/main supplement.
static int uiTest(char** a, int argc)
{
    namespace fs = std::filesystem;
    fs::path e10 = fs::path(a[2]) / "assets", e30 = fs::path(a[3]) / "assets", out = a[4];
    fs::create_directories(out);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); fails += !ok; };
    // 1. loading screens: 21 loads of a non-intro zone, and every pick a 1.0 movie that 3.0 lacks
    int counter = 0;
    std::string seq;
    bool movies = true;
    for (int i = 0; i < 21; ++i) {
        std::string s = ui::loadingScreen10("LoadingScreen_Inc", counter), f = s;
        for (char& c : f) c = (char)tolower((unsigned char)c);
        seq += s.substr(14) + " ";
        movies &= fs::exists(e10 / "flash/loadingscreen/loadingscreen" / (f + ".gfx")) && !fs::exists(e30 / "flash/loadingscreen/loadingscreen" / (f + ".gfx"));
    }
    check(seq == "01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 01 02 ", "loading-screen sequence " + seq);
    check(movies, "every pick is a 1.0 movie (flash/loadingscreen/loadingscreen_NN.gfx) 3.0 does not ship");
    check(ui::loadingScreen10("LoadingScreen_Intro", counter) == "LoadingScreen_Intro", "an Intro screen is kept");
    // 2. flash image ids: every shipped <movie>_i<hex>.mtb maps to a movie beside it; 1.0 images named after a 3.0 movie
    for (auto [root, tag] : {std::pair{e10, "1.0"}, std::pair{e30, "3.0"}}) {
        size_t imgs = 0, orphan = 0, coll = 0;
        for (auto& d : fs::directory_iterator(root / "flash")) {
            if (!d.is_directory()) continue;
            for (auto& sub : fs::directory_iterator(d.path())) {
                if (!sub.is_directory()) continue;
                for (auto& f : fs::directory_iterator(sub.path())) {
                    std::string n = f.path().filename().string();
                    for (char& c : n) c = (char)tolower((unsigned char)c);
                    std::string mv = ui::flashImageMovie("flash/" + d.path().filename().string() + "/" + n);
                    if (mv.empty()) continue;
                    ++imgs;
                    std::string stem = mv.substr(mv.rfind('/') + 1);
                    bool found = fs::exists(sub.path() / (stem + ".gfx"));
                    for (int s = 0; s <= 5 && !found; ++s)   // the resident startup archives hold full-path members
                        found = fs::exists(root / (s ? "startup" + std::to_string(s) : std::string("startup")) / "flash" / d.path().filename() / (stem + ".gfx"));
                    if (!found) { ++orphan; printf("  orphan %s\n", f.path().string().c_str()); }
                    if (tag[0] == '1') {
                        fs::path m30 = e30 / "flash" / d.path().filename() / d.path().filename() / (stem + ".gfx");
                        if (fs::exists(m30)) { ++coll; printf("  1.0 image of a 3.0 movie: %s\n", f.path().string().c_str()); }
                    }
                }
            }
        }
        check(orphan == 0, std::string(tag) + ": " + std::to_string(imgs) + " flash images, " + std::to_string(orphan) + " without their movie");
        if (tag[0] == '1') printf("     1.0 images named after a movie 3.0 ships (uiscope10 keeps 3.0's miss): %zu\n", coll);
    }
    check(ui::flashImageMovie("flash/progresslog/progresslog_i1e.mtb") == "flash/progresslog/progresslog" &&
          ui::flashImageMovie("flash/loadingscreen/loadingscreen_01_i10.mtb") == "flash/loadingscreen/loadingscreen_01" &&
          ui::flashImageMovie("flash/hud/hud_info.mtb").empty() && ui::flashImageMovie("ui/icons/x_i1.mtb").empty() &&
          ui::flashMovie("flash/progresslog/progresslog.gfxmanifest") == "flash/progresslog/progresslog",
          "flash path forms");
    // 3. FMV: the clip names the player asks for -> 1.0 fmv/PC1 (only via the install, so names only here)
    check(ui::fmvClip("PC1\\IN_COM_GP") == "IN_COM_GP" && ui::fmvClip("HOH__Info_MrInc.bik") == "HOH__Info_MrInc" &&
          ui::fmvClip("http://x/y.bik").empty(), "FMV clip names");
    // 4. tips
    std::string t30 = slurp(e30 / "data/tips.lua"), t10 = slurp(e10 / "data/tips.lua"), zl;
    if (argc > 5) zl = slurp(a[5]);
    zl.erase(std::remove(zl.begin(), zl.end(), '\r'), zl.end());
    auto isZone = [&](const std::string& k) {
        std::string lk = "\n" + k + "\n", z = "\n" + zl;
        for (char& c : lk) c = (char)tolower((unsigned char)c);
        for (char& c : z) c = (char)tolower((unsigned char)c);
        return z.find(lk) != std::string::npos;
    };
    std::string merged = t30;
    int k = ui::mergeTips(merged, t10, isZone);
    std::ofstream(out / "tips.lua", std::ios::binary).write(merged.data(), merged.size());
    check(k > 0 && merged.compare(0, 12, "TipCounts = ") == 0, "tips: " + std::to_string(k) + " 1.0 records added -> " + (out / "tips.lua").string());
    std::string again = merged;
    check(ui::mergeTips(again, t10, isZone) == 0 && again == merged, "tips merge is idempotent");
    // 5. text: lang/IN re-keyed + the 1.0 lang/main keys no 3.0 english table has
    std::string in = slurp(e10 / "lang/in/in/english_ntsc_win32.dct"), ids = slurp(e10 / "lang/in/in/stringids_win32.dct");
    int kept = dct::reseed(in, ids, dct::SEED30);
    std::unordered_set<uint32_t> have;
    for (auto& d : fs::directory_iterator(e30 / "lang")) {
        for (const char* v : {"_win32.dct", "_pc_con.dct"}) {
            fs::path f = d.path() / d.path().filename() / (std::string("english_ntsc") + v);
            if (fs::exists(f)) dct::hashes(slurp(f), have);
        }
    }
    std::string before = in;
    int add = dct::supplement(in, slurp(e10 / "lang/main/main/english_ntsc_win32.dct"), slurp(e10 / "lang/main/main/stringids_win32.dct"), have);
    dct::Dict d0, d1;
    dct::load(before, d0);
    dct::load(in, d1);
    std::unordered_set<uint32_t> k1;
    for (auto& x : d1.keys) k1.insert(x.first);
    bool allIn = true, shadow = false;
    for (auto& x : d0.keys) allIn &= k1.count(x.first) > 0;
    for (auto& x : d1.keys) shadow |= have.count(x.first) && std::find_if(d0.keys.begin(), d0.keys.end(), [&](auto& y) { return y.first == x.first; }) == d0.keys.end();
    check(kept == 2534 && allIn, "lang/IN: " + std::to_string(kept) + " keys re-keyed, all kept after the supplement");
    check(add > 0 && !shadow && (int)d1.keys.size() == kept + add, "text10: " + std::to_string(add) + " 1.0 lang/main keys added, none a 3.0 key");
    auto probe = [&](std::string key) {   // the engine's probe (octtest --dct)
        for (char& c : key) c = (char)tolower((unsigned char)c);
        uint32_t h = dct::lookup2((const uint8_t*)key.data(), key.size(), dct::u32(in, 8)), ns = dct::u32(in, 0x10);
        for (uint32_t i = h % ns; dct::u32(in, 0x24 + 12 * i); i = (i + 1) % ns)
            if (dct::u32(in, 0x20 + 12 * i) == h) return std::string(in.c_str() + 0x25 + 12 * i + (int32_t)dct::u32(in, 0x24 + 12 * i));
        return std::string();
    };
    int tips = 0;
    for (int i = 1; i <= 28; ++i) { char b[64]; snprintf(b, sizeof(b), "Load_Tips_Incredibles_%.2d", i); tips += !probe(b).empty(); }
    check(tips == 28, "Load_Tips_Incredibles_01..28 answered: " + std::to_string(tips) + " (e.g. \"" + probe("Load_Tips_Incredibles_01").substr(0, 60) + "\")");
    std::ofstream(out / "english_ntsc_win32.dct", std::ios::binary).write(in.data(), in.size());
    // 6. controller-context lists: the 1.0 IN exclusion lists (nodeflds.py ControllerContextNode) and every 3.0
    // exclusion list shipped (ctx30.py over 1,464 3.0 graphs)
    check(ui::contextList10("TUT_DISABLE_*,-TUT_DISABLE_SELECT_ITEM") == "TUT_DISABLE_*,-TUT_DISABLE_SELECT_ITEM,-TUT_DISABLE_WEAPONWHEEL" &&
          ui::contextList10("TUT_DISABLE_*, !TUT_DISABLE_SELECT_ITEM ") == "TUT_DISABLE_*, !TUT_DISABLE_SELECT_ITEM ,-TUT_DISABLE_WEAPONWHEEL" &&
          ui::contextList10("TUT_DISABLE_*,-TUT_DISABLE_SELECT_ITEM,-TUT_DISABLE_WEAPONWHEEL").empty(),
          "1.0 list keeping SELECT_ITEM also keeps 3.0's TUT_DISABLE_WEAPONWHEEL");
    bool same = true;
    for (const char* l : {"TUT_DISABLE_*,-TUT_DISABLE_TOY_STORE", "TUT_DISABLE_*", "TUT_DISABLE", "TUT_DISABLE_SELECT_ITEM",
                          "UI_TUT_DISABLE_GN_LST_*,-UI_TUT_DISABLE_GN_LST_SELECT",
                          "UI_TUT_DISABLE_PROG_LOG_*,-UI_TUT_DISABLE_PROG_LOG_ACTIVATE_MISSION",
                          "UI_TUT_DISABLE_TOOL_SELECT_*,-UI_TUT_DISABLE_TOOL_SELECT_TOGGLE_LISTS", "TUT_DISABLE_*,-TUT_DISABLE_CAM",
                          "UI_TUT_DISABLE_GEN_*,-UI_TUT_DISABLE_GEN_CANCEL", "UI_TUT_DISABLE_GEN_*,-UI_TUT_DISABLE_GEN_OK"})
        same &= ui::contextList10(l).empty();
    check(same, "every other 1.0 IN list and every shipped 3.0 exclusion list is unchanged");
    printf("%d failed\n", fails);
    return fails ? 1 : 0;
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

// chd::V trees equal (the chd parser is independent of sys::keepRecords' scanner)
static bool sameTree(const chd::V& a, const chd::V& b)
{
    if (a.t != b.t || a.i != b.i || a.s != b.s || memcmp(&a.f, &b.f, 4) || a.named.size() != b.named.size() || a.items.size() != b.items.size())
        return false;
    for (size_t k = 0; k < a.named.size(); ++k)
        if (a.named[k].first != b.named[k].first || !sameTree(a.named[k].second, b.named[k].second)) return false;
    for (size_t k = 0; k < a.items.size(); ++k)
        if (!sameTree(a.items[k], b.items[k])) return false;
    return true;
}
static const chd::V* field(const chd::V& t, const char* k)
{
    for (auto& e : t.named) if (e.first == k) return &e.second;
    return nullptr;
}

// octtest --keep <1.0 table.lua> <3.0 table.lua>...: PS-5 supplement = the 1.0 records whose UID no 3.0 table has
static int keepTest(int argc, char** argv)
{
    std::set<uint32_t> excl;
    for (int a = 3; a < argc; ++a) {
        chd::V t = chd::lua(slurp(argv[a]));
        const chd::V* r = field(t, "Data") ? field(*field(t, "Data"), "Records") : nullptr;
        if (!r) { printf("no Records in %s\n", argv[a]); return 2; }
        for (auto& x : r->items) if (auto u = field(x, "UID")) excl.insert((uint32_t)u->i);
    }
    std::string src = slurp(argv[2]), out;
    int kept = 0, total = 0;
    if (!sys::keepRecords(src, [&](uint32_t u) { return !excl.count(u); }, out, kept, total)) { printf("layout not found\n"); return 1; }
    chd::V in = chd::lua(src), got = chd::lua(out);
    const chd::V *ri = field(*field(in, "Data"), "Records"), *ro = field(got, "Data") ? field(*field(got, "Data"), "Records") : nullptr;
    int bad = 0, want = 0;
    if (!ro) { printf("output has no Records\n"); return 1; }
    size_t o = 0;
    for (auto& x : ri->items) {
        const chd::V* u = field(x, "UID");
        if (!u || excl.count((uint32_t)u->i)) continue;
        ++want;
        if (o >= ro->items.size() || !sameTree(x, ro->items[o++])) ++bad;
    }
    for (const char* k : {"DBName", "Type", "Version", "ID", "TableName"}) {
        const chd::V *a = field(*field(in, "Data"), k), *b = field(*field(got, "Data"), k);
        if (!a != !b || (a && !sameTree(*a, *b))) { ++bad; printf("header %s differs\n", k); }
    }
    printf("%s: %d records (chd parse %zu), kept %d (expected %d, output parse %zu), 3.0 UIDs %zu, mismatches %d\n", argv[2], total,
           ri->items.size(), kept, want, ro->items.size(), excl.size(), bad + (int)(ro->items.size() != (size_t)want) + (total != (int)ri->items.size()));
    if (getenv("KEEP_OUT")) { std::ofstream(getenv("KEEP_OUT"), std::ios::binary) << out; }
    return bad || kept != want || ro->items.size() != (size_t)want || total != (int)ri->items.size();
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

// octtest --fmod <1.0 install> <names.tsv> <fev>...  AUD-2 offline acceptance: the plugin's FMOD host (audio.hpp) loads
// the unmodified 1.0 FMOD DLLs with no output, loads Initialize.fev:Permanent and the given FEVs with 1.0's load and
// registration rules, then resolves every name of names.tsv (kind, source, name, uses: the graph references from the
// research catalogues) as a posted event or, for cues, through the playset's Music_<X>.fev. EV lines list every
// registered event (checked against the FEV string tables by fevcheck.py).
static void stdoutLog(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}

static int fmodTest(int argc, char** argv)
{
    audio::logfn = stdoutLog;
    std::map<std::string, std::string> dropped;
    audio::g.dropped = &dropped;
    if (!audio::g.init(argv[2], true)) return 1;
    audio::g.loadTree("Initialize.fev", "Permanent");
    for (int i = 4; i < argc; ++i) {
        std::string n = audio::g.name10(argv[i]);
        if (n.empty()) printf("NOFILE %s\n", argv[i]);
        else audio::g.loadTree(n, nullptr);
    }
    std::map<void*, std::string> projName;
    for (auto& p : audio::g.projects) projName[p.second.p] = p.first;
    for (auto& e : audio::g.byId) printf("EV\t%s\t%s\t%u\n", projName[e.second.proj].c_str(), e.second.name.c_str(), e.second.flags);
    std::ifstream in(argv[3]);
    std::string line;
    std::map<std::string, int> tally;
    while (std::getline(in, line)) {
        size_t a = line.find('\t'), b = line.find('\t', a + 1), c = line.find('\t', b + 1);
        if (c == std::string::npos) continue;
        std::string kind = line.substr(0, a), src = line.substr(a + 1, b - a - 1), name = line.substr(b + 1, c - b - 1);
        std::string verdict;
        if (kind == "cue") {
            size_t u = name.find('_');
            std::string x = u == std::string::npos ? "" : name.substr(0, u);
            unsigned id = 0;
            if (!x.empty() && audio::music.ensure(x))
                verdict = audio::Music::cue(name.c_str(), &id) ? "OK " + audio::music.file : "MISS (not a cue of " + audio::music.file + ")";
            else if (!audio::music.file.empty() && audio::Music::cue(name.c_str(), &id)) {   // 1.0: the playset's loaded project
                struct Entity { const char* name; unsigned id; };
                struct Iter { const Entity* value; const char* filter; } ci{};
                audio::g.f.MusicSystem_GetCues(audio::g.ms, &ci, name.c_str());
                verdict = "OK " + audio::music.file + " as " + (ci.value ? ci.value->name : "?");
            } else
                verdict = "MISS (no Music_" + x + ".fev)";
        } else {
            auto it = audio::g.byId.find(audio::fnv1(name));
            if (it != audio::g.byId.end()) verdict = "OK " + projName[it->second.proj] + " flags " + std::to_string(it->second.flags);
            else if (dropped.count(name)) verdict = "MISS (left out by the 1.0 rule: " + dropped[name] + ")";
            else verdict = "MISS";
        }
        ++tally[kind + " " + src + (verdict[0] == 'O' ? " ok" : " miss")];
        printf("%s\t%s\t%s\t%s\n", kind.c_str(), src.c_str(), name.c_str(), verdict.c_str());
    }
    // the Music_<X>.fev loaded last: its full cue list (fevcheck.py compares it with the file's cues chunk)
    struct Entity { const char* name; unsigned id; };
    struct Iter { const Entity* value; const char* filter; } it{};
    if (audio::g.ms && !audio::g.f.MusicSystem_GetCues(audio::g.ms, &it, nullptr))
        for (; it.value; audio::g.f.MusicSystem_GetNextCue(audio::g.ms, &it)) printf("CUE\t%s\t%u\n", it.value->name, it.value->id);
    for (auto& t : tally) printf("TALLY %s %d\n", t.first.c_str(), t.second);

    // lifecycle (needs IN_City.fev): a one-shot ends by itself and calls back once, a loop runs until stopped, a music
    // cue's prompt turns active and ends; what the plugin's tick thread delivers, run here on the main thread
    static int calls = 0;
    static uint32_t lastPid = 0;
    audio::AkCallback cb = [](uint32_t type, void* info) { calls += type == 1; lastPid = ((uint32_t*)info)[2]; };
    uint32_t one = audio::g.play(audio::fnv1("billboardStart"), 7, 1, (void*)cb, nullptr);
    uint32_t loop = audio::g.play(audio::fnv1("fire_loop"), 7, 1, (void*)cb, nullptr);
    auto run = [](int ms) { for (int t = 0; t < ms; t += 16) { Sleep(16); audio::tickAndDeliver(); } };
    run(6000);
    int afterOne = calls;
    uint32_t pidOne = lastPid;
    audio::g.act(loop, 0);
    run(2000);
    printf("LIFE one-shot id %u: %d callback(s) in 6 s (pid %u); loop id %u: %s before its stop, %d after\n", one, afterOne,
           pidOne, loop, afterOne == 1 ? "running" : "ENDED", calls - afterOne);
    int active = -1, activeAfter = -1;
    if (audio::music.ensure("Inc") && audio::music.begin("Inc_mx_Metroville")) {
        run(1000);
        audio::g.f.MusicPrompt_IsActive(audio::music.prompt, &active);
        audio::music.endPrompt();
        audio::g.f.MusicSystem_Reset(audio::g.ms);
        run(500);
        unsigned id;
        void* p = nullptr;
        if (audio::Music::cue("Inc_mx_Metroville", &id)) audio::g.f.MusicSystem_PrepareCue(audio::g.ms, id, &p);
        if (p) { audio::g.f.MusicPrompt_IsActive(p, &activeAfter); audio::g.f.MusicPrompt_Release(p); }
    }
    printf("LIFE music Inc_mx_Metroville: active %d after 1 s, fresh prompt active %d after end+reset\n", active, activeAfter);

    // a 'virtual' event at its playback limit (fire_loop: MAX_PLAYBACKS_BEHAVIOR 5, 'just fail if quietest'): with the
    // listener at the origin and the limit filled at 10 m, a post at 30 m fails and one at 2 m gets a voice by stealing
    // one of the 10 m sounds -- judged where each sound plays only if the info-only instance is placed first (1.0
    // FUN_01136520); the info-only instance otherwise stays where the last post left it
    auto fire = audio::g.byId.find(audio::fnv1("fire_loop"));
    if (fire != audio::g.byId.end() && (fire->second.flags & 1)) {
        void* info = nullptr;
        int maxPb = 0;
        float dmin = 0, dmax = 0;
        audio::g.f.EventSystem_GetEventBySystemID(audio::g.es, fire->second.sysId, 4, &info);
        if (info) {
            audio::g.f.Event_GetPropertyByIndex(info, 12, &maxPb, 0);   // FMOD_EVENTPROPERTY_MAX_PLAYBACKS
            audio::g.f.Event_GetPropertyByIndex(info, 17, &dmin, 0);    // 3D_MINDISTANCE (1.0 FUN_01136520 sets 0x11)
            audio::g.f.Event_GetPropertyByIndex(info, 18, &dmax, 0);    // 3D_MAXDISTANCE (0x12)
        }
        float dFill = dmin + 0.5f * (dmax - dmin), dFar = dmin + 0.9f * (dmax - dmin), dNear = dmin * 0.5f;
        printf("VIRT fire_loop 3D min %.1f max %.1f: limit filled at %.1f, posts at %.1f and %.1f\n", dmin, dmax, dFill, dFar, dNear);
        {   // through the hooks, as the engine sets them (listener at the origin); no Wwise behind them here
            audio::oListener = [](const float*, uint32_t) { return 1; };
            audio::oSetPos = [](uint32_t, const float*) { return 1; };
            const float lis[9] = {0, 0, 1, 0, 1, 0, 0, 0, 0};
            audio::hkListener(lis, 0);
            for (int i = 0; i < 40; ++i) {
                const float p[6] = {0, 0, -(i < 32 ? dFill : i == 32 ? dFar : dNear), 0, 0, 1};   // Wwise z = -engine z
                audio::hkSetPos(100 + i, p);
            }
        }
        calls = 0;
        std::vector<uint32_t> ids;
        int got = 0;
        for (int i = 0; i < maxPb && i < 32; ++i) {
            uint32_t id = audio::g.play(fire->first, 100 + i, 1, (void*)cb, nullptr);
            got += id != 0;
            ids.push_back(id);
            run(32);
        }
        run(200);
        uint32_t farId = audio::g.play(fire->first, 132, 1, (void*)cb, nullptr);
        run(200);
        // control: without the placement (the info-only instance still where the far post put it), FMOD judges the
        // near sound as if it were far
        void* raw = nullptr;
        audio::FR rr;
        {
            std::lock_guard<std::recursive_mutex> l(audio::g.mx);
            rr = audio::g.f.EventSystem_GetEventBySystemID(audio::g.es, fire->second.sysId, 0, &raw);
            if (!rr && raw) audio::g.f.Event_Stop(raw, 1);
        }
        printf("VIRT control: near request with the info-only instance left at the far position -> %s (FMOD %d)\n",
               !rr && raw ? "granted" : "refused", (int)rr);
        run(200);
        uint32_t nearId = audio::g.play(fire->first, 133, 1, (void*)cb, nullptr);
        run(500);
        printf("VIRT fire_loop max playbacks %d: %d of %d started at the fill distance; far post %s; near post %s, %d stolen end(s)\n",
               maxPb, got, (int)ids.size(), farId ? "STARTED (wrong)" : "failed", nearId ? "started" : "FAILED (wrong)", calls);
        for (uint32_t id : ids) audio::g.act(id, 0);
        if (nearId) audio::g.act(nearId, 0);
        if (farId) audio::g.act(farId, 0);
        run(500);
    }
    return 0;
}

// octtest --mixer <1.0 install>  mixer10 offline acceptance (audio.hpp): the 'Mixer' entry of 1.0's core datamap, its
// Lua table read from the install's root archives, the at-rest 'Mixer' source per group (mixercheck.py recomputes it
// from the file on its own), then applied through the real FMOD DLLs (no output) with Initialize, IN_City and
// Music_Inc loaded: every group's category volume read back, and where the music system's own category sits.
static int mixerTest(char** argv)
{
    audio::logfn = stdoutLog;
    std::string inst = argv[2], core, lua;
    if (!audio::readRoot10(inst, "GameDB/Core/CoreDataMap.lua", core)) { printf("MIXER no core datamap\n"); return 1; }
    std::vector<std::string> paths = audio::mixerPaths(core);
    for (auto& p : paths) printf("MIXER datamap entry %s\n", p.c_str());
    if (paths.empty() || !audio::readRoot10(inst, paths[0], lua)) { printf("MIXER no mixer file\n"); return 1; }
    audio::MixerRest rest;
    std::string err;
    if (!audio::mixerRest(lua, rest, &err)) { printf("MIXER parse failed: %s\n", err.c_str()); return 1; }
    for (auto& a : rest.active) printf("MIXER active %s\n", a.c_str());
    for (auto& v : rest.vol) printf("MIXER group %s %.6f\n", v.first.c_str(), v.second);
    printf("MIXER pitch sum %.6f\n", rest.pitchSum);
    if (!audio::g.init(inst, true)) return 1;
    audio::g.loadTree("Initialize.fev", "Permanent");
    audio::g.loadTree("IN_City.fev", nullptr);
    audio::music.ensure("Inc");
    {
        std::lock_guard<std::recursive_mutex> l(audio::g.mx);
        audio::g.mixer = rest.vol;
        audio::g.faders["music"]["Options"] = 0.5f;   // a 3.0 option fader on top: music categories at 0.5 x mixer
        audio::g.applyVolumes();
    }
    for (const char* c : {"sfx", "music", "dialog", "ui/sfx", "ui/songs", "ui/dialog", "sfx_sfx2d", "sfx_sfx3d_ambience",
                          "sfx_sfx3d_ship", "sfx_sfx3d_ambience_waterfall_virtual", "master"}) {
        void* cat = nullptr;
        float v = -1;
        bool has = !audio::g.f.EventSystem_GetCategory(audio::g.es, c, &cat) && cat;
        if (has) audio::g.f.EventCategory_GetVolume(cat, &v);
        printf("CAT %s %s %.6f\n", c, has ? "exists" : "absent", v);
    }
    void* mc = nullptr;
    if (!audio::g.f.EventSystem_GetMusicCategory(audio::g.es, &mc) && mc) {
        std::string chain;
        for (void* c = mc; c;) {
            char* n = nullptr;
            audio::g.f.EventCategory_GetInfo(c, nullptr, &n);
            chain += std::string(chain.empty() ? "" : " < ") + (n ? n : "?");
            void* parent = nullptr;
            if (audio::g.f.EventCategory_GetParentCategory(c, &parent)) parent = nullptr;
            c = parent;
        }
        void* music = nullptr;
        audio::g.f.EventSystem_GetCategory(audio::g.es, "music", &music);
        float v = -1;
        audio::g.f.EventCategory_GetVolume(mc, &v);
        printf("MUSICCAT %s (same object as 'music': %s) volume %.6f\n", chain.c_str(), mc == music ? "yes" : "no", v);
    } else
        printf("MUSICCAT none\n");
    return 0;
}

// octtest --vo <1.0 install> <out dir> [<dir of decrypted 3.0 scripts>]  vo10 offline acceptance (audio.hpp), real
// FMOD DLLs, no output:
//  DLG   every 1.0 DLG v11 script through dlgTranscode -> <out>\<name>.dlg30, and with 3.0's script of the same name
//        when given -> <name>.merged.dlg30 (vocheck.py resolves every 1.0 line and named entry, and every 3.0 one
//        1.0 does not define, through the 3.0 tables and compares with the source files read on their own)
//  NAME  every 1.0 dialog bank opened as 1.0 opens it (subsound count == .name), its 3.0-layout bank built, every line
//        and variant resolved through 3.0's reader (FUN_0182bf80 / FUN_0182c000 / FUN_0182d2f0, ported here) down to
//        the media record the post hook reads (vocheck.py compares with the .name read by 1.0's rule, FUN_01132360)
//  PLAY  ED019 through the post hook's path: Edna.fev's dialog_edna with the subsound as programmer sound; AK_Duration
//        and AK_EndOfEvent as 3.0's callback LAB_01830120 takes them
static int voTest(char** argv)
{
    namespace fs = std::filesystem;
    audio::logfn = stdoutLog;
    std::string inst = argv[2], outDir = argv[3];
    if (!audio::g.init(inst, true)) return 1;
    audio::vo.root10 = inst + "\\assets\\sound\\dialog";
    audio::vo.pickLanguage();   // no engine: ENU
    audio::vo.buildEvNames(audio::g.sound10);
    int bad = 0;
    for (auto& e : fs::directory_iterator(inst + "\\assets\\sound\\dialog")) {
        if (e.path().extension() != ".dlg") continue;
        std::string file = e.path().filename().string();
        std::vector<uint8_t> in, out;
        audio::slurp(e.path().string(), in);
        uint32_t ver = in.size() >= 4 ? *(uint32_t*)in.data() : 0;
        if (ver != 11) { printf("DLG %s skipped (v%u)\n", file.c_str(), ver); continue; }
        audio::DlgStats st;
        std::string err;
        bool ok = audio::dlgTranscode(in, audio::vo.evNames, out, &st, &err);
        bad += !ok;
        printf("DLG %s %s classes %d lines %d dup %d ranges %d named %d events %d unresolved %d %s\n", file.c_str(), ok ? "ok" : "FAIL",
               st.classes, st.lines, st.dupLines, st.ranges, st.named, st.events, st.unresolved, err.c_str());
        if (ok) std::ofstream(outDir + "\\" + e.path().stem().string() + ".dlg30", std::ios::binary).write((const char*)out.data(), out.size());
        std::string stem = e.path().stem().string();   // with 3.0's own script of the name (decrypted copies in argv[4])
        for (char& c : stem) c = (char)tolower((unsigned char)c);
        std::vector<uint8_t> b30;
        if (argv[4] && audio::slurp(std::string(argv[4]) + "\\3.0_assets_sound_dialog_" + stem + ".dlg", b30)) {
            ok = audio::dlgTranscode(in, audio::vo.evNames, out, &st, &err, &b30);
            bad += !ok;
            printf("DLG %s +3.0 %s classes %d named %d events %d kept30 %d lines30 %d %s\n", file.c_str(), ok ? "ok" : "FAIL", st.classes,
                   st.named, st.events, st.kept30, st.lines30, err.c_str());
            if (ok) std::ofstream(outDir + "\\" + e.path().stem().string() + ".merged.dlg30", std::ios::binary).write((const char*)out.data(), out.size());
        }
    }
    auto mediaOf = [](const uint8_t* d, uint32_t m) {   // FUN_0182c000 / FUN_0182d2f0's view of media m
        return (const uint32_t*)(d + 0x20 + ((size_t)*(const uint16_t*)(d + 0x14) + (size_t)m * 3) * 4);
    };
    for (auto& e : fs::directory_iterator(audio::vo.dir10)) {
        if (e.path().extension() != ".fsb") continue;
        std::string file = e.path().filename().string();
        audio::DlgBank* b = audio::vo.open(file);
        if (!b) { printf("BANK %s FAIL\n", file.c_str()); ++bad; continue; }
        std::vector<uint8_t> h = audio::dbnkFromName10(b->idx().prefix(), {b});
        const uint8_t* d = h.data();
        uint16_t lo = *(const uint16_t*)(d + 0x10), hi = *(const uint16_t*)(d + 0x12);
        int lines = 0;
        for (uint32_t l = lo; l <= hi; ++l) {
            const uint8_t* le = d + 0x20 + (l - lo) * 4;
            if (*(const int32_t*)le == -1) continue;
            ++lines;
            for (int v = 0; v < (le[1] > 1 ? le[1] : 1); ++v) {   // FUN_0182bf80's random pick, enumerated
                const uint32_t* me = mediaOf(d, *(const uint16_t*)(le + 2) + v);
                bool inMem = *(const uint32_t*)(d + 0x18) > me[1] && me[2] == 8;
                const uint32_t* rec = (const uint32_t*)(d + me[1]);
                bool ok = inMem && me[0] && (audio::DlgBank*)(uintptr_t)rec[0] == b && rec[1] < b->idx().subs();
                bad += !ok;
                printf("NAME %s %c%c%03u %d %s %u %s\n", file.c_str(), d[8], d[9], l, v, me[0] ? (const char*)d + me[0] : "-", rec[1], ok ? "ok" : "BAD");
            }
        }
        printf("BANK %s ok prefix %c%c lines %d subsounds %u header %u B\n", file.c_str(), d[8], d[9], lines, b->idx().subs(), (unsigned)h.size());
        audio::vo.unload(file);
    }
    audio::g.load("Edna.fev");
    if (!audio::vo.load("Edna_str.fsb")) { printf("PLAY no Edna_str.fsb\n"); return 1; }   // vo.mgr null: no registry step
    const uint8_t* d = audio::vo.speakers[audio::vo.banks["edna_str.fsb"]->idx().prefix()].hdrs.back()->data();
    const uint8_t* le = d + 0x20 + (19 - *(const uint16_t*)(d + 0x10)) * 4;
    const uint32_t* me = mediaOf(d, *(const uint16_t*)(le + 2));
    audio::DlgBank* b = nullptr;
    int sub = -1;
    bool found = audio::vo.media(d + me[1], &b, &sub);
    static float dur = -1;
    static int ends = 0;
    static DWORD endAt = 0;
    audio::AkCallback cb = [](uint32_t type, void* info) {
        if (type == 8) dur = ((float*)info)[4];
        if (type == 1) { ++ends; endAt = GetTickCount(); }
    };
    DWORD t0 = GetTickCount();
    uint32_t id = found ? audio::g.playLine(audio::fnv1("dialog_edna"), 7, 1 | 4 | 8, (void*)cb, nullptr, b->snd, sub) : 0;
    // GetSourcePlayPosition through the hook (the lipsync player's clock, FUN_014cb180): sampled each second it must
    // grow with the wall clock while the line plays
    std::vector<std::pair<DWORD, int>> posAt;
    DWORD lastSample = 0;
    for (int t = 0; t < 20000 && !ends; t += 16) {
        Sleep(16);
        audio::tickAndDeliver();
        int pos = -1;
        DWORD now = GetTickCount() - t0;
        if (!ends && now >= lastSample + 1000 && audio::hkSrcPos(id, &pos, 1) == 1) { posAt.emplace_back(now, pos); lastSample = now; }
    }
    int posBad = 0, prev = -1;
    for (auto& p : posAt) {
        posBad += p.second < prev || p.second > (int)p.first + 50 || p.second + 250 < (int)p.first;
        prev = p.second;
        printf("POS ED019 at %lu ms wall: position %d ms\n", (unsigned long)p.first, p.second);
    }
    printf("POS %d samples, %d off the wall clock (more than 250 ms behind or 50 ms ahead, or going back)\n", (int)posAt.size(), posBad);
    bad += posAt.size() < 5 || posBad;
    unsigned ms = 0;
    void* s = nullptr;
    if (b && b->snd && !audio::g.f.Sound_GetSubSound(b->snd, sub, &s) && s) audio::g.f.Sound_GetLength(s, &ms, 1);
    printf("PLAY ED019 media %s sub %d (%s) id %u: duration cb %.0f ms (subsound %u ms), end cb %d after %lu ms\n", found ? "ours" : "NOT FOUND",
           sub, found ? b->idx().subName(sub) : "-", id, dur, ms, ends, (unsigned long)(endAt - t0));
    bad += !found || !id || ends != 1 || dur != (float)ms;
    // the next line of the same bank while one plays (3.0 FUN_01830370 posts it before stopping the old one): the old
    // ends once, at once; the new one keeps playing after 3.0's StopPlayingID of the old id
    int endsBefore = ends;
    uint32_t a = audio::g.playLine(audio::fnv1("dialog_edna"), 7, 1, (void*)cb, nullptr, b->snd, sub);
    for (int t = 0; t < 500; t += 16) { Sleep(16); audio::tickAndDeliver(); }
    uint32_t c = audio::g.playLine(audio::fnv1("dialog_edna"), 7, 1, (void*)cb, nullptr, b->snd, sub + 1);
    audio::g.act(a, 0);
    for (int t = 0; t < 500; t += 16) { Sleep(16); audio::tickAndDeliver(); }
    bool cAlive;
    {
        std::lock_guard<std::recursive_mutex> l(audio::g.mx);
        cAlive = audio::g.plays.count(c) && audio::g.plays[c].ev;
    }
    printf("PLAY overlap: old id %u ended %d time(s), new id %u %s\n", a, ends - endsBefore, c, cAlive ? "still playing" : "ENDED");
    bad += ends - endsBefore != 1 || !cAlive;
    // lipsync: the path 3.0 FUN_014c5550 builds from that media name -> 1.0's Edna.zip member (vocheck.py: 19023 of
    // 19026 subsounds of the 1.0 characters with a zip have theirs)
    std::string zr, mem, ap = std::string("sound/dialog/enu/ED/") + (found ? b->idx().subName(sub) : "") + ".anno";
    bool anno = audio::annoSource(ap.c_str(), zr, mem);
    printf("ANNO %s -> %s : %s\n", ap.c_str(), anno ? zr.c_str() : "-", anno ? mem.c_str() : "-");
    bad += !anno || zr != "sound/dialog/ENU/Edna.zip";
    printf("VO bad %d\n", bad);
    return bad != 0;
}

// octtest --fevdeps <1.0 install>: every 1.0 FEV's 'Load' list as the plugin reads it through FMOD (the file's stem
// group and the groups under it, 1.0 FUN_011348e0) -> DEPS lines; fevdeps.py compares with the FEV bytes
static int fevDeps(char** argv)
{
    namespace fs = std::filesystem;
    audio::logfn = stdoutLog;
    if (!audio::g.init(argv[2], true)) return 1;
    for (auto& e : fs::directory_iterator(audio::g.sound10)) {
        if (e.path().extension() != ".fev") continue;
        std::string file = e.path().filename().string();
        void* p = nullptr;
        if (audio::g.f.EventSystem_Load(audio::g.es, file.c_str(), nullptr, &p) || !p) { printf("DEPS %s LOADFAIL\n", file.c_str()); continue; }
        audio::Project pr;
        pr.p = p;
        std::string list;
        for (auto& d : audio::g.depsOf(pr, e.path().stem().string())) list += (list.empty() ? "" : "|") + d;
        printf("DEPS %s %s\n", file.c_str(), list.c_str());
        audio::g.f.EventProject_Release(p);
    }
    return 0;
}

// octtest --host <1.0 install>: audio::install as Pentane_Main runs it, i.e. inside Pentane.dll's DllMain, with the
// loader lock held (taken here through ntdll's LdrLockLoaderLock). install must return at once (no hook is patched:
// the hook and address functions are stand-ins); the host thread cannot start while the lock is held; after the
// release, ready() sees FMOD up with Initialize.fev and the 1.0 mixer loaded on the host thread.
static int hostTest(char** argv)
{
    audio::logfn = stdoutLog;
    typedef LONG(NTAPI * LockFn)(ULONG, ULONG*, ULONG_PTR*);
    typedef LONG(NTAPI * UnlockFn)(ULONG, ULONG_PTR);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    LockFn lock = (LockFn)GetProcAddress(nt, "LdrLockLoaderLock");
    UnlockFn unlock = (UnlockFn)GetProcAddress(nt, "LdrUnlockLoaderLock");
    if (!lock || !unlock) { printf("HOST no LdrLockLoaderLock\n"); return 1; }
    static uint8_t dummy[64];
    static int hooks = 0;
    auto hook = [](const char*, uint32_t, const char*, void*, void**) { ++hooks; return true; };
    auto rt = [](uint32_t) { return (uintptr_t)dummy; };
    audio::hostStart.nosound = true;
    ULONG state = 0;
    ULONG_PTR cookie = 0;
    if (lock(0, &state, &cookie) < 0) { printf("HOST lock failed\n"); return 1; }
    DWORD t0 = GetTickCount();
    bool ok = audio::install(hook, rt, argv[2], ".");
    DWORD tInstall = GetTickCount() - t0;
    DWORD w = WaitForSingleObject(audio::hostStart.done, 2000);   // the old install waited here (20 s)
    unlock(0, cookie);
    bool up = audio::ready();
    DWORD tUp = GetTickCount() - t0;
    bool init = false, mixer = false;
    {
        std::lock_guard<std::recursive_mutex> l(audio::g.mx);
        init = audio::g.projects.count("initialize.fev") != 0;
        mixer = audio::g.mixer.count("sfx") && fabs(audio::g.mixer["sfx"] - 0.707946f) < 1e-5f;
    }
    printf("HOST install under the loader lock: %s in %lu ms, %d hooks; host init while locked: %s; ready %d after %lu ms; "
           "Initialize.fev %s; mixer sfx %s\n", ok ? "returned" : "FAILED", (unsigned long)tInstall, hooks,
           w == WAIT_TIMEOUT ? "blocked (as expected)" : "RAN", (int)up, (unsigned long)tUp, init ? "loaded" : "MISSING",
           mixer ? "0.708" : "WRONG");
    return !(ok && tInstall < 1000 && w == WAIT_TIMEOUT && up && init && mixer);
}

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    const char* exts[] = {".oct", ".mer", ".merdebug", ".bent", ".banm", ".bct"};
    if (argc > 2 && std::string(argv[1]) == "--host") return hostTest(argv);
    if (argc > 3 && std::string(argv[1]) == "--fmod") return fmodTest(argc, argv);
    if (argc > 2 && std::string(argv[1]) == "--fevdeps") return fevDeps(argv);
    if (argc > 2 && std::string(argv[1]) == "--mixer") return mixerTest(argv);
    if (argc > 3 && std::string(argv[1]) == "--vo") return voTest(argv);
    if (argc > 6 && std::string(argv[1]) == "--render") return renderTest(argv);
    if (argc > 3 && std::string(argv[1]) == "--sxkeys") {   // --sxkeys <names file> <out.tsv>: sx::keyId of every line
        std::ifstream in(argv[2]);                          // (keycheck.py recomputes the ids independently)
        std::ofstream o(argv[3]);
        for (std::string s; std::getline(in, s);)
            if (!s.empty()) o << s << '\t' << sx::keyId(s.c_str()) << '\n';
        return 0;
    }
    if (argc > 4 && std::string(argv[1]) == "--ai") {   // --ai <out dir> <1.0 characters dir> <1.0 scripts dir>: BMX-1 /
        fs::path out = argv[2], scripts = argv[4];      // BTX-1 over every .bmx/.btx -> <out>/<file>.lua (aicheck.py)
        fs::create_directories(out);
        size_t ok = 0, bad = 0;
        auto exists = [&](const std::string& p) { return fs::exists(scripts / fs::path(p).filename()); };
        for (auto& e : fs::directory_iterator(argv[3])) {
            std::string ext = e.path().extension().string();
            if (ext != ".bmx" && ext != ".btx") continue;
            std::string b = slurp(e.path()), o, why;
            int k = ext == ".bmx" ? sx::ai::bmx(b.data(), b.size(), exists, o, &why) : sx::ai::btx(b.data(), b.size(), exists, o, &why);
            if (k < 0) { ++bad; printf("REFUSED %s: %s\n", e.path().string().c_str(), why.c_str()); continue; }
            ++ok;
            std::ofstream(out / (e.path().filename().string() + ".lua"), std::ios::binary).write(o.data(), o.size());
        }
        printf("ai tables transcoded %zu, refused %zu\n", ok, bad);
        return bad ? 1 : 0;
    }
    if (argc > 3 && std::string(argv[1]) == "--sx") {   // --sx <out dir> <dir>...: SX-1 over every .sx into
        fs::path out = argv[2];                        // <out>/<input dir name>/ (sxcheck.py checks the files);
        fs::create_directories(out);                   // <out>/natives.tsv, keys.tsv: the embedded tables
        size_t ok = 0, bad = 0, imports = 0;
        for (int i = 3; i < argc; ++i) {
            fs::path sub = out / fs::path(argv[i]).filename();   // characters/ and scripts/ share names (cannonball.sx)
            fs::create_directories(sub);
            for (auto& e : fs::directory_iterator(argv[i])) {
                if (e.path().extension() != ".sx") continue;
                std::string b = slurp(e.path()), o, why;
                std::string name = e.path().parent_path().filename().string() + "/" + e.path().filename().string();
                int k = sx::transcode(b.data(), b.size(), name, o, &why);
                if (k < 0) { ++bad; printf("REFUSED %s: %s\n", e.path().string().c_str(), why.c_str()); continue; }
                ++ok; imports += k;
                std::ofstream(sub / e.path().filename(), std::ios::binary).write(o.data(), o.size());
            }
        }
        std::ofstream nt(out / "natives.tsv"), kt(out / "keys.tsv");
        for (auto& n : sx::g_natives) nt << n.name << '\t' << (int)n.args << '\t' << (int)n.results << '\n';
        for (auto& n : sx::g_ports) nt << "port:" << n.name << '\t' << (int)n.args << '\t' << (int)n.results << '\n';
        for (auto& k : sx::KEYS30) kt << k.name << '\t' << k.id << '\n';
        std::ofstream bt(out / "beh10.tsv");
        for (auto* n : sx::BEH10) bt << n << '\n';
        std::ofstream et(out / "events.tsv");   // TriggerEvent type map (sxcheck.py: == the exes' tables by name)
        for (int t = 1; t <= 14; ++t) et << t << '\t' << sx::eventTo30(t) << '\n';
        const char* flips[][2] = {{"HIT_BACK_HEAVY", "HIT_FRONT_HEAVY"}, {"FRONT", "BACK"}, {"BACK", "FRONT"},
                                  {"FRONTBACK", "BACKFRONT"}, {"FRON", "FRON"}, {"BAC", "BAC"}, {"", ""}};
        for (auto& f : flips) {
            char b[64];
            sx::flipMotionName(f[0], b, sizeof(b));
            if (strcmp(b, f[1])) { printf("FlipMotionName(%s) = %s, want %s\n", f[0], b, f[1]); ++bad; }
        }
        printf("sx transcoded %zu (%zu imports), refused %zu; %zu natives, %zu ports, %zu keys\n", ok, imports, bad,
               std::size(sx::g_natives), std::size(sx::g_ports), std::size(sx::KEYS30));
        return bad ? 1 : 0;
    }
    if (argc > 3 && std::string(argv[1]) == "--keep") return keepTest(argc, argv);
    if (argc > 3 && std::string(argv[1]) == "--chest") {   // --chest <1.0 chestinfo.bin> <out.lua>: GLB-1 transcode
        std::string out, why;
        if (!sys::chestInfoLua(slurp(argv[2]), out, &why)) { printf("FAILED: %s\n", why.c_str()); return 1; }
        std::ofstream(argv[3], std::ios::binary) << out;
        chd::V t = chd::lua(out);   // independent parse: the three tables and their entry counts
        const chd::V* c = field(t, "ChestInfo");
        for (const char* k : {"Resource", "Choreography", "Chests"})
            printf("%s %zu  ", k, c && field(*c, k) ? field(*c, k)->named.size() : 0);
        printf("(%zu B)\n", out.size());
        return 0;
    }
    if (argc > 5 && std::string(argv[1]) == "--perc") {   // --perc <1.0 perceptioninfo.bin> <3.0 names> <3.0 groups tsv> <out.lua>
        std::set<std::string> names;
        std::map<std::string, std::string> groups;
        std::istringstream nl(slurp(argv[3])), gl(slurp(argv[4]));
        for (std::string l; std::getline(nl, l);) if (!l.empty()) names.insert(l);
        for (std::string l; std::getline(gl, l);) if (l.find('\t') != std::string::npos) groups.emplace(l.substr(0, l.find('\t')), l.substr(l.find('\t') + 1));
        sys::Perc30 p30{[&](const std::string& s) { return names.count(s) != 0; },
                        [&](const std::string& s) { auto it = groups.find(s); return it == groups.end() ? std::string() : it->second; }};
        std::string out, why;
        int added = 0;
        if (!sys::perceptionInfoLua(slurp(argv[2]), p30, out, added, &why)) { printf("FAILED: %s\n", why.c_str()); return 1; }
        std::ofstream(argv[5], std::ios::binary) << out;
        chd::V t = chd::lua(out);   // independent parse: the sections and their entry counts
        const chd::V* c = field(t, "PerceptionInfo");
        for (const char* k : {"StatePriority", "TypePriority", "Detect", "TargetMode", "Reticule", "Ricochet", "PartnerCheck",
                              "InteractionCheck", "Groups", "UIText", "Perceptions"})
            printf("%s %zu  ", k, c && field(*c, k) ? field(*c, k)->named.size() : 0);
        printf("(added %d, %zu B)\n", added, out.size());
        return 0;
    }
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
    if (argc > 4 && std::string(argv[1]) == "--ui") return uiTest(argv, argc);
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
    if (argc > 4 && std::string(argv[1]) == "--mer") {   // --mer <di1compat_mer.txt> <1.0 .mer> <out.mer> [out.mtc] [flags]
        mer::Table T;
        if (!T.load(slurp(argv[2]))) { printf("table load failed\n"); return 2; }
        for (std::string a; argc > 5 && (a = argv[argc - 1]).rfind("--", 0) == 0; --argc) {   // trailing flags, any order
            if (a == "--noportrefs") T.portRefs = false;     // cfg portrefs10=0
            else if (a == "--noports") T.ports10 = false;    // cfg ports10=0
            else if (a == "--nopins") T.pins10 = false;      // cfg pins10=0
            else if (a == "--noevents") T.events10 = false;  // cfg events10=0
            else if (a == "--aliases") mer::addAliases(T);   // as after hkMerRegistry
        }
        std::string b = slurp(argv[3]), why;
        oct::File f = oct::File::parse(b.data(), b.size());
        mer::Stats st;
        if (!mer::transcode(T, f, &st, &why)) { printf("not transcoded: %s\n", why.c_str()); return 1; }
        std::string o = f.write();
        std::ofstream(argv[4], std::ios::binary).write(o.data(), o.size());
        if (argc > 5) { std::string m = mer::mtc(f); std::ofstream(argv[5], std::ios::binary).write(m.data(), m.size()); }
        printf("%d namespaces, %d nodes, %d params, %d pin tables copied, %d stand-ins, %d links, %d dropped, %d events, %d fetches; %zu B\n",
               st.namespaces, st.nodes, st.params, st.pinsCopied, st.stubs, st.links, st.linksDropped, st.events, st.fetches, o.size());
        return 0;
    }
    if (argc > 3 && std::string(argv[1]) == "--mission") {   // --mission <1.0 mission .lua> <out.lua>: events -> 3.0
        std::string t = slurp(argv[2]);
        int n = mission::luaEvents30(t);
        std::ofstream(argv[3], std::ios::binary).write(t.data(), t.size());
        printf("%d presentation events remapped\n", n);
        return 0;
    }
    if (argc > 3 && std::string(argv[1]) == "--missiongraph") {   // --missiongraph <1.0 __missiongraph.lua> <out.lua>
        std::string t = slurp(argv[2]);
        int n = mission::behaviour30(t);
        std::ofstream(argv[3], std::ios::binary).write(t.data(), t.size());
        printf("%d behaviours 2 -> story/side\n", n);
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
    if (argc > 3 && std::string(argv[1]) == "--nav") {   // --nav <out dir> <1.0 .oct>...: navmesh blobs -> 3.0 layout
        // writes <out>/<stem>_<i>.bin per mesh (payload, no count) for navconv.py's byte comparison
        int files = 0, meshes = 0, failed = 0;
        for (int k = 3; k < argc; ++k) {
            std::string b = slurp(argv[k]), why;
            oct::File f = oct::File::parse(b.data(), b.size());
            int n = npc::navmesh30(f, &why);
            ++files;
            if (n < 0) { ++failed; printf("FAIL %s: %s\n", argv[k], why.c_str()); continue; }
            long p = f.pool("NavMeshPool");
            int i = 0;
            for (size_t j = p < 0 ? 0 : p + 1; p >= 0 && j < f.blockEnd(p); ++j)
                if (f.name(f.recs[j]) == "Data") {
                    oct::Spec s = oct::spec(f.recs[j].type);
                    std::string pay = f.recs[j].pay.substr(s.cw);
                    std::ofstream(fs::path(argv[2]) / (fs::path(argv[k]).stem().string() + "_" + std::to_string(i++) + ".bin"), std::ios::binary)
                        .write(pay.data(), pay.size());
                }
            meshes += n;
            std::string w = f.write();
            oct::File::parse(w.data(), w.size());   // the edited file re-parses (count widths, sizes)
        }
        printf("nav: %d files, %d meshes converted, %d files failed\n", files, meshes, failed);
        return failed ? 1 : 0;
    }
    if (argc > 4 && std::string(argv[1]) == "--skin") {   // --skin <1.0 vformat.vtxf> <out dir> <1.0 .oct>...: REN-3
        // writes <out>/<stem>_0.vbuf: the .oct's vertex buffer with its InfluenceRemap palettes applied
        npc::SkinLayouts L = npc::skinLayouts(slurp(argv[2]));
        int files = 0, prims = 0;
        for (int k = 4; k < argc; ++k) {
            fs::path p = argv[k];
            std::string b = slurp(p), vb = slurp(p.parent_path() / (p.stem().string() + "_0.vbuf")), why;
            std::string ib = slurp(p.parent_path() / (p.stem().string() + "_0.ibuf"));
            oct::File f = oct::File::parse(b.data(), b.size());
            int n = npc::skinRemap(f, vb, ib, L, &why);
            std::ofstream(fs::path(argv[3]) / (p.stem().string() + "_0.vbuf"), std::ios::binary).write(vb.data(), vb.size());
            ++files; prims += n;
            if (!why.empty()) printf("%s: %d primitives; %s\n", p.stem().string().c_str(), n, why.c_str());
        }
        printf("skin: %zu skinned 1.0 formats, %d files, %d primitives remapped\n", L.size(), files, prims);
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--dmeval") {   // --dmeval <DataMap.lua>...: npc::dataMap, one line per entry
        for (int k = 2; k < argc; ++k) {
            npc::DataMap m;
            std::string why;
            if (!npc::dataMap(slurp(argv[k]), m, &why)) { printf("%s: FAILED %s\n", argv[k], why.c_str()); return 1; }
            for (auto& [key, r] : m) {
                for (auto& f : r.files) printf("%s|%s|F|%s\n", key.first.c_str(), key.second.c_str(), f.c_str());
                for (auto& i : r.includes) printf("%s|%s|I|%s\n", key.first.c_str(), key.second.c_str(), i.c_str());
            }
        }
        return 0;
    }
    if (argc > 4 && std::string(argv[1]) == "--psdna") {   // --psdna <1.0 characters dir> <3.0 characters dir> <out dir>
        namespace fs = std::filesystem;
        int ok = 0, no = 0;
        for (auto& e : fs::directory_iterator(argv[2])) {
            std::string n = e.path().filename().string();
            if (n.size() < 9 || n.compare(n.size() - 8, 8, "_ps.dnax") || fs::exists(fs::path(argv[3]) / n)) continue;
            fs::path base = fs::path(argv[3]) / (n.substr(0, n.size() - 8) + ".dnax");
            if (!fs::exists(base)) continue;
            std::string out, why;
            bool r = npc::psDna30(slurp(e.path()), slurp(base), out, &why);
            printf("%s: %s %s\n", n.c_str(), r ? "ok" : "kept 1.0", why.c_str());
            if (r) std::ofstream(fs::path(argv[4]) / n, std::ios::binary).write(out.data(), out.size());
            (r ? ok : no)++;
        }
        printf("psdna: %d served from the 3.0 base, %d kept\n", ok, no);
        return 0;
    }
    if (argc > 5 && std::string(argv[1]) == "--playset") {   // --playset <1.0 DM> <ToyBox DM> <other 3.0 DM> <playset>
        npc::DataMap m10, tb, other;
        if (!npc::dataMap(slurp(argv[2]), m10) || !npc::dataMap(slurp(argv[3]), tb) || !npc::dataMap(slurp(argv[4]), other)) {
            printf("DataMap eval failed\n");
            return 1;
        }
        std::string t = npc::playsetText(argv[5], npc::gamedb30(m10, tb).playset, tb, other);
        fwrite(t.data(), 1, t.size(), stdout);
        return 0;
    }
    if (argc > 4 && std::string(argv[1]) == "--avperc") {   // --avperc <1.0 avatar.btx> <3.0 avatar.btx> <zone>: npc::percAdds
        std::vector<npc::PercList> l10, l30;
        std::string why;
        if (!npc::percLists(slurp(argv[2]), l10, &why) || !npc::percLists(slurp(argv[3]), l30, &why)) { printf("btx eval failed: %s\n", why.c_str()); return 1; }
        auto adds = npc::percAdds(l10, l30, argv[4]);
        std::set<std::string> lines;
        auto names = [](const npc::PercList& l) {
            std::set<std::string> s;
            for (auto& e : l) { if (!e.on.empty()) s.insert(npc::lower(e.on)); if (!e.off.empty()) s.insert(npc::lower(e.off)); }
            std::string o;
            for (auto& n : s) o += (o.empty() ? "" : ",") + n;
            return o;
        };
        for (size_t j = 0; j < l30.size(); ++j) {
            if (adds[j].first < 0) continue;
            std::map<std::string, std::string> a;
            for (auto& [n, h] : adds[j].second) a[npc::lower(n)] = n + (h ? "|1" : "|0");
            std::string s;
            for (auto& [k, v] : a) s += (s.empty() ? "" : ",") + v;
            lines.insert("L " + std::to_string(l30[j].size()) + " [" + names(l30[j]) + "] <- " + std::to_string(l10[adds[j].first].size()) + ": " + s);
        }
        printf("%zu 1.0 lists, %zu 3.0 lists\n", l10.size(), l30.size());
        for (auto& l : lines) printf("%s\n", l.c_str());
        return 0;
    }
    if (argc > 3 && std::string(argv[1]) == "--gamedb") {   // --gamedb <1.0 DataMap> <3.0 ToyBox DataMap>: npc::gamedb30
        npc::DataMap m10, m30;
        if (!npc::dataMap(slurp(argv[2]), m10) || !npc::dataMap(slurp(argv[3]), m30)) { printf("DataMap eval failed\n"); return 1; }
        npc::GameDb g = npc::gamedb30(m10, m30);
        std::set<std::string> lines;
        for (auto& [t, p] : g.playset) lines.insert("P " + t + " " + p);
        for (auto& [a, v] : g.actor) for (auto& [t, p] : v) lines.insert("A " + a + " " + t + " " + p);
        for (auto& l : lines) printf("%s\n", l.c_str());
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
    // --pd <1.0 .particledata | .p3d> <out>: vfx.hpp's transcode (particles.py writes the oracle's bytes to compare)
    if (argc > 3 && std::string(argv[1]) == "--pd") {
        std::string in = slurp(argv[2]), out;
        vfx::Stats st;
        bool ok = vfx::isPd10(in) ? vfx::transcode(in, out, &st) : vfx::p3d30(in, out);
        if (!ok) { printf("not a 1.0 particle package or mesh\n"); return 1; }
        printf("%u -> %u B, %d emitters %d effects %d params %d keys %d mesh emitters\n", (unsigned)in.size(),
               (unsigned)out.size(), st.emitters, st.effects, st.params, st.keys, st.meshes);
        std::ofstream(argv[3], std::ios::binary).write(out.data(), out.size());
        return 0;
    }
    // --mtb <render table> <3.0 extract root> <1.0 extract root> <1.0 .mtb> <engine path>: the plugin's serve of a
    // stand-alone bundle (di1compat.cpp transcode() .mtb case: donors from the engine path's 3.0 folder); a failure
    // there serves the 1.0 bundle raw
    if (argc > 6 && std::string(argv[1]) == "--mtb") {
        render::Table T;
        if (!T.load(slurp(argv[2]))) { printf("table load failed\n"); return 2; }
        std::string ext30 = argv[3], ext10 = argv[4], p = argv[6];
        render::IO io;
        io.read30 = [&](const std::string& q, std::string& o) { o = slurp(std::filesystem::path(ext30) / "assets" / q.substr(2)); return !o.empty(); };
        io.read10 = [&](const std::string& q, std::string& o) { o = slurp(std::filesystem::path(ext10) / "assets" / q); return !o.empty(); };
        render::Result r;
        bool ok = render::transcodeBundle(T, io, slurp(argv[5]), "3:" + p.substr(0, p.find('/') + 1), r);
        for (auto& pr : r.problems) printf("problem: %s\n", pr.c_str());
        int n = 0;
        for (auto& [t, c] : r.tiers) { printf("tier %s %d\n", t.c_str(), c); n += c; }
        printf("%s: %d materials, %zu 1.0 textures, %zu B\n", ok ? "ok" : "FAILED", n, r.textures10.size(), r.mtb.size());
        return ok ? 0 : 1;
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

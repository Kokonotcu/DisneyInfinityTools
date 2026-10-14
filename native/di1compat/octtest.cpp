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
#include "sx.hpp"

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

int main(int argc, char** argv)
{
    namespace fs = std::filesystem;
    const char* exts[] = {".oct", ".mer", ".merdebug", ".bent", ".banm", ".bct"};
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

// Acceptance tests of plugin helpers: core::readable's pointer check, and ui.hpp with dct::supplement on the extracts.
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../core/memcheck.hpp"
#include "../formats/dct.hpp"
#include "../npc.hpp"
#include "../ui.hpp"
#include "files.hpp"
#include "modes.hpp"
#include "run.hpp"
#include "tables.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

// ---- --readable ----

constexpr size_t kPage = 0x1000, kRegion = 0x10000, kTwoPages = 2 * kPage;
constexpr size_t kArenaBytes = 256u << 20;   // the engine arena's shape: one touched 256 MB region
constexpr size_t kProbe = 8, kArenaAt = 16, kArenaProbe = 64, kHeapBytes = 64;
constexpr int kTimedCalls = 200;
constexpr double kMaxCostUs = 50;

// The VirtualQuery check core::readable replaced: committed, neither no-access nor guard, the range inside.
bool queryReadable(const void* p, size_t n)
{
    constexpr uintptr_t kNullPages = 0x10000;   // never mapped
    MEMORY_BASIC_INFORMATION m;
    if ((uintptr_t)p < kNullPages || !VirtualQuery(p, &m, sizeof(m))) return false;
    const bool usable = m.State == MEM_COMMIT && m.Protect && !(m.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    if (!usable) return false;
    return (uintptr_t)p + n <= (uintptr_t)m.BaseAddress + m.RegionSize;
}

int checkReadable(const char* what, const void* p, size_t n, bool want)
{
    bool now = core::readable(p, n), was = queryReadable(p, n);
    printf("%-44s new %d old %d%s\n", what, now, was,
           now != want ? "  <-- WRONG" : now != was ? "  (both pages readable)" : "");
    return now != want;
}

char* allocate(size_t n, DWORD type, DWORD prot) { return (char*)VirtualAlloc(nullptr, n, type, prot); }

// Every kind of page: reserved, committed untouched, released, no-access, guard; failures.
int checkPageKinds()
{
    static const char rdata[] = "rdata";   // static: it must sit in .rdata
    int stackVar = 1;
    std::vector<char> heap(kHeapBytes);
    char* reserved = allocate(kRegion, MEM_RESERVE, PAGE_NOACCESS);
    char* fresh = allocate(kRegion, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    char* freed = allocate(kRegion, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    freed[0] = 1;
    VirtualFree(freed, 0, MEM_RELEASE);
    char* noacc = allocate(kRegion, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    char* guard = allocate(kRegion, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD);
    int bad = checkReadable("null", nullptr, 1, false);
    bad += checkReadable("0x1000", (void*)kPage, 1, false);
    bad += checkReadable("stack", &stackVar, sizeof stackVar, true);
    bad += checkReadable("heap", heap.data(), heap.size(), true);
    bad += checkReadable(".rdata", rdata, sizeof(rdata), true);
    bad += checkReadable("reserved, not committed", reserved, 1, false);
    bad += checkReadable("committed, never touched (not resident)", fresh, 1, true);
    bad += checkReadable("released", freed, 1, false);
    bad += checkReadable("committed no-access", noacc, 1, false);
    bad += checkReadable("committed guard page", guard, 1, false);
    MEMORY_BASIC_INFORMATION m;
    VirtualQuery(guard, &m, sizeof(m));
    bool armed = (m.Protect & PAGE_GUARD) != 0;
    printf("%-44s %s\n", "guard page still armed after the checks", armed ? "yes" : "NO  <-- WRONG");
    return bad + !armed;
}

// 8 bytes up to, and across, a page that is no-access or read-only; failures.
int checkPageEdges()
{
    char* split = allocate(kTwoPages, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);   // page 1: no access
    split[0] = 1;
    DWORD was;
    VirtualProtect(split + kPage, kPage, PAGE_NOACCESS, &was);
    char* split2 = allocate(kTwoPages, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);   // page 1: read-only
    split2[0] = split2[kPage] = 1;
    VirtualProtect(split2 + kPage, kPage, PAGE_READONLY, &was);
    int bad = checkReadable("8 bytes up to a no-access page", split + kPage - kProbe, kProbe, true);
    bad += checkReadable("8 bytes across into a no-access page", split + kPage - kProbe / 2, kProbe, false);
    bad += checkReadable("8 bytes across RW -> read-only", split2 + kPage - kProbe / 2, kProbe, true);
    return bad;
}

// Microseconds per call of f, over kTimedCalls calls.
template <class F>
double usPerCall(F f)
{
    LARGE_INTEGER a, b, q;
    QueryPerformanceFrequency(&q);
    QueryPerformanceCounter(&a);
    for (int i = 0; i < kTimedCalls; ++i) f();
    QueryPerformanceCounter(&b);
    return 1e6 * (b.QuadPart - a.QuadPart) / q.QuadPart / kTimedCalls;
}

// The cost of both checks at the start of a touched 256 MB region; failures (more than 50 us fails).
int checkArenaCost()
{
    char* arena = allocate(kArenaBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!arena) return 0;
    for (size_t o = 0; o < kArenaBytes; o += kPage) arena[o] = 1;
    double tNew = usPerCall([&] { core::readable(arena + kArenaAt, kArenaProbe); });
    double tOld = usPerCall([&] { queryReadable(arena + kArenaAt, kArenaProbe); });
    int bad = checkReadable("start of a touched 256 MB region", arena + kArenaAt, kArenaProbe, true);
    printf("cost per check there: new %.2f us, old (VirtualQuery) %.1f us\n", tNew, tOld);
    bad += tNew > kMaxCostUs;
    VirtualFree(arena, 0, MEM_RELEASE);
    return bad;
}

// ---- --ui ----

// The 1.0 IN extract's facts --ui checks (dct.hpp, ui.hpp): loading screens, tips, keys.
constexpr int kLoads = 21;   // two past the 19 movies, to see the wrap
constexpr char kSequence[] = "01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 01 02 ";
constexpr char kScreenPrefix[] = "LoadingScreen_";
constexpr int kStartupArchives = 5;    // startup.zip, startup1..5.zip: resident, full-path members
constexpr int kLangInKeys = 2534;      // lang/IN keys re-keyed
constexpr int kTips = 28;              // Load_Tips_Incredibles_01..28
constexpr size_t kTipShown = 60;
constexpr char kTipCounts[] = "TipCounts = ";

struct UiRun {
    int fails = 0;
    void check(bool ok, const std::string& what)
    {
        printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        fails += !ok;
    }
};

// 1. 21 loads of a non-intro zone pick 1.0 movies 3.0 lacks, in sequence; an Intro screen is kept.
void checkLoadingScreens(UiRun& r, const fs::path& e10, const fs::path& e30)
{
    int counter = 0;
    std::string seq;
    bool movies = true;
    for (int i = 0; i < kLoads; ++i) {
        std::string s = ui::loadingScreen10("LoadingScreen_Inc", counter), f = npc::lower(s);
        seq += s.substr(sizeof kScreenPrefix - 1) + " ";
        movies &= fs::exists(e10 / "flash/loadingscreen/loadingscreen" / (f + ".gfx")) &&
                  !fs::exists(e30 / "flash/loadingscreen/loadingscreen" / (f + ".gfx"));
    }
    r.check(seq == kSequence, "loading-screen sequence " + seq);
    r.check(movies, "every pick is a 1.0 movie (flash/loadingscreen/loadingscreen_NN.gfx) 3.0 does not ship");
    r.check(ui::loadingScreen10("LoadingScreen_Intro", counter) == "LoadingScreen_Intro", "an Intro screen is kept");
}

// The flash images of one extract: how many, how many lack their movie, 1.0 ones named after a 3.0 movie.
struct FlashScan {
    fs::path root, e30;
    bool is10;
    size_t imgs = 0, orphan = 0, coll = 0;
};

// The image's movie beside it or in a resident startup archive.
bool movieFound(const FlashScan& s, const fs::path& dir, const fs::path& sub, const std::string& stem)
{
    bool found = fs::exists(sub / (stem + ".gfx"));
    for (int k = 0; k <= kStartupArchives && !found; ++k) {
        const std::string archive = k ? "startup" + std::to_string(k) : std::string("startup");
        found = fs::exists(s.root / archive / "flash" / dir.filename() / (stem + ".gfx"));
    }
    return found;
}

void flashImage(FlashScan& s, const fs::path& dir, const fs::path& sub, const fs::path& file)
{
    const std::string n = npc::lower(file.filename().string());
    std::string mv = ui::flashImageMovie("flash/" + dir.filename().string() + "/" + n);
    if (mv.empty()) return;
    ++s.imgs;
    std::string stem = mv.substr(mv.rfind('/') + 1);
    if (!movieFound(s, dir, sub, stem)) {
        ++s.orphan;
        printf("  orphan %s\n", file.string().c_str());
    }
    if (!s.is10) return;
    if (!fs::exists(s.e30 / "flash" / dir.filename() / dir.filename() / (stem + ".gfx"))) return;
    ++s.coll;
    printf("  1.0 image of a 3.0 movie: %s\n", file.string().c_str());
}

void scanFlashImages(FlashScan& s)
{
    for (auto& d : fs::directory_iterator(s.root / "flash")) {
        if (!d.is_directory()) continue;
        for (auto& sub : fs::directory_iterator(d.path())) {
            if (!sub.is_directory()) continue;
            for (auto& f : fs::directory_iterator(sub.path())) flashImage(s, d.path(), sub.path(), f.path());
        }
    }
}

// 2. Every shipped <movie>_i<hex>.mtb maps to a movie beside it; the path forms.
void checkFlashImages(UiRun& r, const fs::path& e10, const fs::path& e30)
{
    for (auto [root, tag] : {std::pair{e10, "1.0"}, std::pair{e30, "3.0"}}) {
        FlashScan s{root, e30, tag[0] == '1'};
        scanFlashImages(s);
        r.check(s.orphan == 0, std::string(tag) + ": " + std::to_string(s.imgs) + " flash images, " +
                                   std::to_string(s.orphan) + " without their movie");
        if (s.is10) printf("     1.0 images named after a movie 3.0 ships (uiscope10 keeps 3.0's miss): %zu\n", s.coll);
    }
    const bool images = ui::flashImageMovie("flash/progresslog/progresslog_i1e.mtb") ==
                            "flash/progresslog/progresslog" &&
                        ui::flashImageMovie("flash/loadingscreen/loadingscreen_01_i10.mtb") ==
                            "flash/loadingscreen/loadingscreen_01";
    const bool others = ui::flashImageMovie("flash/hud/hud_info.mtb").empty() &&
                        ui::flashImageMovie("ui/icons/x_i1.mtb").empty() &&
                        ui::flashMovie("flash/progresslog/progresslog.gfxmanifest") == "flash/progresslog/progresslog";
    r.check(images && others, "flash path forms");
}

// 4. The tips merge, written to <out>/tips.lua for the Lua check; merging again changes nothing.
void checkTips(UiRun& r, const Args& a, const fs::path& e10, const fs::path& e30, const fs::path& out)
{
    std::string t30 = slurp(e30 / "data/tips.lua"), t10 = slurp(e10 / "data/tips.lua"), zl;
    if (a.argc > 5) zl = slurp(a.argv[5]);
    zl.erase(std::remove(zl.begin(), zl.end(), '\r'), zl.end());
    auto isZone = [&](const std::string& k) {
        std::string lk = npc::lower("\n" + k + "\n"), z = npc::lower("\n" + zl);
        return z.find(lk) != std::string::npos;
    };
    std::string merged = t30;
    int k = ui::mergeTips(merged, t10, isZone);
    writeFile(out / "tips.lua", merged);
    r.check(k > 0 && merged.compare(0, sizeof kTipCounts - 1, kTipCounts) == 0,
            "tips: " + std::to_string(k) + " 1.0 records added -> " + (out / "tips.lua").string());
    std::string again = merged;
    r.check(ui::mergeTips(again, t10, isZone) == 0 && again == merged, "tips merge is idempotent");
}

// The key hashes of every 3.0 english table.
std::unordered_set<uint32_t> keys30(const fs::path& e30)
{
    std::unordered_set<uint32_t> have;
    for (auto& d : fs::directory_iterator(e30 / "lang"))
        for (const char* v : {"_win32.dct", "_pc_con.dct"}) {
            fs::path f = d.path() / d.path().filename() / (std::string("english_ntsc") + v);
            if (fs::exists(f)) dct::hashes(slurp(f), have);
        }
    return have;
}

// 5. lang/IN re-keyed, plus the 1.0 lang/main keys no 3.0 english table has; tips answered by the probe.
void checkText(UiRun& r, const fs::path& e10, const fs::path& e30, const fs::path& out)
{
    std::string in = slurp(e10 / "lang/in/in/english_ntsc_win32.dct");
    const std::string ids = slurp(e10 / "lang/in/in/stringids_win32.dct");
    int kept = dct::reseed(in, ids, dct::SEED30);
    const std::unordered_set<uint32_t> have = keys30(e30);
    std::string before = in;
    int add = dct::supplement(in, slurp(e10 / "lang/main/main/english_ntsc_win32.dct"),
                              slurp(e10 / "lang/main/main/stringids_win32.dct"), have);
    dct::Dict d0, d1;
    dct::load(before, d0);
    dct::load(in, d1);
    std::unordered_set<uint32_t> k1, k0;
    for (auto& x : d1.keys) k1.insert(x.first);
    for (auto& x : d0.keys) k0.insert(x.first);
    bool allIn = true, shadow = false;
    for (auto& x : d0.keys) allIn &= k1.count(x.first) > 0;
    for (auto& x : d1.keys) shadow |= have.count(x.first) && !k0.count(x.first);
    r.check(kept == kLangInKeys && allIn,
            "lang/IN: " + std::to_string(kept) + " keys re-keyed, all kept after the supplement");
    r.check(add > 0 && !shadow && (int)d1.keys.size() == kept + add,
            "text10: " + std::to_string(add) + " 1.0 lang/main keys added, none a 3.0 key");
    auto probe = [&in](const std::string& key) {
        std::string v;
        return dctLookup(in, key, v) ? v : std::string();
    };
    int tips = 0;
    for (int i = 1; i <= kTips; ++i) {
        char b[64];
        snprintf(b, sizeof(b), "Load_Tips_Incredibles_%.2d", i);
        tips += !probe(b).empty();
    }
    r.check(tips == kTips, "Load_Tips_Incredibles_01..28 answered: " + std::to_string(tips) + " (e.g. \"" +
                               probe("Load_Tips_Incredibles_01").substr(0, kTipShown) + "\")");
    writeFile(out / "english_ntsc_win32.dct", in);
}

// 6. Controller-context lists: the 1.0 IN exclusion lists (nodeflds.py) and every shipped 3.0 one (ctx30.py).
void checkContextLists(UiRun& r)
{
    r.check(ui::contextList10("TUT_DISABLE_*,-TUT_DISABLE_SELECT_ITEM") ==
                    "TUT_DISABLE_*,-TUT_DISABLE_SELECT_ITEM,-TUT_DISABLE_WEAPONWHEEL" &&
                ui::contextList10("TUT_DISABLE_*, !TUT_DISABLE_SELECT_ITEM ") ==
                    "TUT_DISABLE_*, !TUT_DISABLE_SELECT_ITEM ,-TUT_DISABLE_WEAPONWHEEL" &&
                ui::contextList10("TUT_DISABLE_*,-TUT_DISABLE_SELECT_ITEM,-TUT_DISABLE_WEAPONWHEEL").empty(),
            "1.0 list keeping SELECT_ITEM also keeps 3.0's TUT_DISABLE_WEAPONWHEEL");
    bool same = true;
    for (const char* l : {"TUT_DISABLE_*,-TUT_DISABLE_TOY_STORE", "TUT_DISABLE_*", "TUT_DISABLE",
                          "TUT_DISABLE_SELECT_ITEM", "UI_TUT_DISABLE_GN_LST_*,-UI_TUT_DISABLE_GN_LST_SELECT",
                          "UI_TUT_DISABLE_PROG_LOG_*,-UI_TUT_DISABLE_PROG_LOG_ACTIVATE_MISSION",
                          "UI_TUT_DISABLE_TOOL_SELECT_*,-UI_TUT_DISABLE_TOOL_SELECT_TOGGLE_LISTS",
                          "TUT_DISABLE_*,-TUT_DISABLE_CAM", "UI_TUT_DISABLE_GEN_*,-UI_TUT_DISABLE_GEN_CANCEL",
                          "UI_TUT_DISABLE_GEN_*,-UI_TUT_DISABLE_GEN_OK"})
        same &= ui::contextList10(l).empty();
    r.check(same, "every other 1.0 IN list and every shipped 3.0 exclusion list is unchanged");
}

}  // namespace

// core::readable (working set first) against the VirtualQuery check it replaced, on every kind of page.
int testReadable(const Args&)
{
    int bad = checkPageKinds();
    bad += checkPageEdges();
    bad += checkArenaCost();
    printf("READABLE %s\n", bad ? "FAILED" : "ok");
    return bad != 0;
}

// ui.hpp and dct::supplement over the extracts: screens, flash images, FMV, tips, text, context lists.
int testUi(const Args& a)
{
    fs::path e10 = fs::path(a.argv[2]) / "assets", e30 = fs::path(a.argv[3]) / "assets", out = a.argv[4];
    fs::create_directories(out);
    UiRun r;
    checkLoadingScreens(r, e10, e30);
    checkFlashImages(r, e10, e30);
    // 3. FMV: the clip names the player asks for -> 1.0 fmv/PC1 (only via the install, so names only here)
    r.check(ui::fmvClip("PC1\\IN_COM_GP") == "IN_COM_GP" && ui::fmvClip("HOH__Info_MrInc.bik") == "HOH__Info_MrInc" &&
                ui::fmvClip("http://x/y.bik").empty(),
            "FMV clip names");
    checkTips(r, a, e10, e30, out);
    checkText(r, e10, e30, out);
    checkContextLists(r);
    printf("%d failed\n", r.fails);
    return r.fails ? 1 : 0;
}

}  // namespace octtest

// Graph and world converters: .oct, .mer, mission, challenge, zone list, navmesh, skin, collision and clip outputs.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include "../games/di1/cho.hpp"
#include "../formats/btd.hpp"
#include "../games/di1/activities.hpp"
#include "../games/di1/challenges.hpp"
#include "../games/di1/surfaces.hpp"
#include "../games/di1/toolset.hpp"
#include "../games/di1/bent.hpp"
#include "../games/di1/btd.hpp"
#include "../games/di1/zones.hpp"
#include "../games/di1/mer.hpp"
#include "../games/di1/mission.hpp"
#include "../games/di1/navmesh.hpp"
#include "../games/di1/skin.hpp"
#include "../third_party/miniz/miniz.h"
#include "../formats/bndl.hpp"
#include "../games/di1/ui.hpp"
#include "files.hpp"
#include "modes.hpp"
#include "plugin.hpp"
#include "run.hpp"
#include "tables.hpp"
#include "tuples.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

constexpr size_t kExtLen = 4;   // ".mtb", ".btd": the extension a sibling output replaces

// ---- --mer ----

// A trailing flag sets one cfg switch to 0 (di1::mer::Rules' defaults equal the cfg's).
struct MerFlag {
    const char* flag;
    bool di1::mer::Rules::*off;
};
constexpr MerFlag kMerFlags[] = {{"--noportrefs", &di1::mer::Rules::portRefs},
                                 {"--noports", &di1::mer::Rules::ports10},
                                 {"--nopins", &di1::mer::Rules::pins10},
                                 {"--noevents", &di1::mer::Rules::events10},
                                 {"--nogate", &di1::mer::Rules::gate10},
                                 {"--notoggletype", &di1::mer::Rules::toggleType10}};

void applyMerFlag(mer::Table& T, di1::mer::Rules& R, const std::string& flag)
{
    for (const MerFlag& f : kMerFlags) {
        if (flag != f.flag) continue;
        R.*f.off = false;
        return;
    }
    if (flag == "--aliases") di1::mer::addAliases(T);   // as after hkMerRegistry
}

// ---- --challenge ----

// One file's challenge transform: its count of changes; o gets the bytes to write.
using ChallengeFn = int (*)(std::string& t, const std::string& beacons10, std::string& o);

int challengeTypes(std::string& t, const std::string&, std::string& o)
{
    int k = di1::challenges::challengeTypes30(t);
    o = t;
    return k;
}
int challengeBeacons(std::string& t, const std::string& beacons10, std::string& o)
{
    int k = di1::challenges::beacons30(t, beacons10);
    o = t;
    return k;
}
int challengeZoneList(std::string& t, const std::string&, std::string& o)
{
    int k = di1::challenges::zonelist30(t);
    o = t;
    return k;
}
int challengeAp(std::string& t, const std::string&, std::string& o)
{
    int k = di1::activities::apGivers30(t);
    o = t;
    return k;
}
int challengeDna(std::string& t, const std::string&, std::string& o) { return di1::activities::dnaGadgets30(t, o); }

struct ChallengeKind {
    const char* kind;
    ChallengeFn fn;
};

// A file's kind by its name; the first match wins. Null for anything else.
ChallengeKind challengeKind(const std::string& name, const fs::path& in)
{
    if (name.rfind("activitytypedata", 0) == 0) return {"types", challengeTypes};
    if (name == "challengedata.lua") return {"beacons", challengeBeacons};
    if (name.find("zonelist") != std::string::npos) return {"zonelist", challengeZoneList};
    if (in.extension() == ".ap") return {"ap", challengeAp};
    if (in.extension() == ".dnax") return {"dna", challengeDna};
    return {nullptr, nullptr};
}

// ---- --activity ----

// activityGraph30 (<name>.pre.mer), then di1::mer::transcode at the default rules (<name>.mer).
void activityMer(const mer::Table& T, const fs::path& in, const fs::path& outDir)
{
    std::string t = slurp(in), name = in.filename().string();
    oct::File f = parse(t);
    int re = 0, dr = 0, k = di1::activities::activityGraph30(f, &re, &dr);
    if (!k) {
        printf("mer %-52s unchanged\n", name.c_str());
        return;
    }
    writeFile(outDir / (in.stem().string() + ".pre.mer"), f.write());
    di1::mer::Stats st;
    std::string why;
    bool ok = di1::mer::transcode(T, di1::mer::Rules(), f, &st, &why);
    writeFile(outDir / name, f.write());
    printf("mer %-52s %d nodes, %d rerouted, %d dropped; mer %s: %d nodes %d stubs %d links %d dropped\n", name.c_str(),
           k, re, dr, ok ? "ok" : why.c_str(), st.nodes, st.stubs, st.links, st.linksDropped);
}

// A missiongraph .ap: apGivers30 gated by activityGivers, read from the files beside it.
void activityAp(const fs::path& in, const fs::path& outDir)
{
    std::string t = slurp(in), name = in.filename().string();
    fs::path dir = in.parent_path();
    std::string graph = slurp(dir / (in.stem().string() + ".lua"));
    auto ok = di1::activities::activityGivers(graph, [&dir](const std::string& n, std::string& o) {
        o = slurp(dir / text::lower(n + ".lua"));
        return !o.empty();
    });
    int k = di1::activities::apGivers30(t, &ok);
    writeFile(outDir / name, t);
    printf("ap  %-52s %d givers redirected:", name.c_str(), k);
    for (auto& g : ok) printf(" %s", g.c_str());
    printf("\n");
}

// ---- --zones10, --menu ----

// The plugin's zones10 on the install's startup2.zip gamedb/core/zonelist.lua: t edited, its playsets.
struct Zones {
    std::string t;                                      // the 3.0 zonelist.lua
    std::vector<std::pair<std::string, std::string>> list;   // playset name, start zone
};

bool zones10(const char* inst, const char* z30, const std::string& opt, Zones& z)
{
    std::string zip = std::string(inst) + "\\assets\\startup2.zip", c10;
    z.t = slurp(z30);
    mz_zip_archive ar{};
    if (!mz_zip_reader_init_file(&ar, zip.c_str(), 0)) {
        printf("no %s\n", zip.c_str());
        return false;
    }
    size_t n = 0;
    int i = mz_zip_reader_locate_file(&ar, "gamedb/core/zonelist.lua", nullptr, 0);
    void* d = i < 0 ? nullptr : mz_zip_reader_extract_to_heap(&ar, (mz_uint)i, &n, 0);
    if (d) {
        c10.assign((const char*)d, n);
        mz_free(d);
    }
    mz_zip_reader_end(&ar);
    di1::Zones10Options zo;   // the plugin's zones10 (games/di1/zones.hpp); noadd: zones10 off
    zo.add = opt != "noadd";
    zo.world = [inst](const std::string& zone) { return di1::worldInstalled(inst, zone); };
    for (auto& x : di1::zones10(z.t, c10, zo).playsets) {
        printf("%-16s %s id %d, start %s, zones %zu\n", x.name.c_str(), x.added ? "added" : "present", x.id,
               x.start.c_str(), x.zones.size());
        z.list.push_back({x.name, x.start});
    }
    return true;
}

}  // namespace

// The plugin's .oct steps at cfg defaults, without render, the 3.0 motion test or the engine's surface table.
int convertOct(const Args& a)
{
    std::string b = slurp(a.argv[2]), note;
    auto t0 = std::chrono::steady_clock::now();
    oct::File f = parse(b);
    bool changed = di1::transcodeOct(enginePath(a.argv[2]), f, pluginOctSteps(a.octx), note);
    std::string o = f.write();
    printf("%s %s\n%zu B, %.1f ms\n", changed ? "changed" : "unchanged", note.c_str(), o.size(),
           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    writeFile(a.argv[3], o);
    return 0;
}

// One file through the plugin's dispatch (--mission, --missiongraph): its rules' output and note.
int convertByPlugin(const Args& a)
{
    std::string out, note, codec = pluginTranscode(a.argv[2], a.octx, out, note);
    if (codec == "-") out = slurp(a.argv[2]);
    writeFile(a.argv[3], out);
    printf("%s: %s\n", codec.c_str(), note.c_str());
    return 0;
}

// di1::mer::transcode of one 1.0 graph at the default rules, then the trailing flags'; [out.mtc] mer::mtc.
int convertMer(const Args& a)
{
    mer::Table T;
    di1::mer::Rules R;
    if (!T.load(slurp(a.argv[2]))) {
        printf("table load failed\n");
        return 2;
    }
    int argc = a.argc;
    for (; argc > 5; --argc) {   // trailing flags, any order
        const std::string flag = a.argv[argc - 1];
        if (flag.rfind("--", 0) != 0) break;
        applyMerFlag(T, R, flag);
    }
    std::string b = slurp(a.argv[3]), why;
    oct::File f = parse(b);
    di1::mer::Stats st;
    if (!di1::mer::transcode(T, R, f, &st, &why)) {
        printf("not transcoded: %s\n", why.c_str());
        return 1;
    }
    std::string o = f.write();
    writeFile(a.argv[4], o);
    if (argc > 5) writeFile(a.argv[5], mer::mtc(f));
    printf("%d namespaces, %d nodes, %d params, %d pin tables copied, %d stand-ins, %d links, %d dropped, %d events, "
           "%d fetches, %d net-sync links, %d toggle types; %zu B\n", st.namespaces, st.nodes, st.params, st.pinsCopied,
           st.stubs, st.links, st.linksDropped, st.events, st.fetches, st.netSync, st.typed, o.size());
    return 0;
}

// Activity missions -> challenge (games/di1/activities.hpp): .mer graphs, mission .lua tasks, missiongraph .ap givers.
int convertActivity(const Args& a)
{
    mer::Table T;
    if (!T.load(slurp(a.argv[2]))) {
        printf("mer table load failed\n");
        return 2;
    }
    const fs::path outDir = a.argv[3];
    for (int i = 4; i < a.argc; ++i) {
        fs::path in = a.argv[i];
        if (in.extension() == ".mer") {
            activityMer(T, in, outDir);
        } else if (in.extension() == ".lua") {
            std::string t = slurp(in);
            int k = di1::activities::activityTasks30(t);
            if (k) writeFile(outDir / in.filename(), t);
            printf("lua %-52s %d\n", in.filename().string().c_str(), k);
        } else if (in.extension() == ".ap") {
            activityAp(in, outDir);
        }
    }
    return 0;
}

// The activity -> challenge transforms by file kind (challenge.py checks the outputs).
int convertChallenge(const Args& a)
{
    std::string beacons10;   // a ChallengeData.lua needs the 1.0 beacons file given before it
    for (int i = 3; i < a.argc; ++i) {
        fs::path in = a.argv[i];
        std::string t = slurp(in), o, name = text::lower(in.filename().string());
        if (name == "defaultactivitybeacons.lua") {
            beacons10 = t;
            continue;
        }
        const ChallengeKind ck = challengeKind(name, in);
        if (!ck.fn) continue;
        int k = ck.fn(t, beacons10, o);
        writeFile(fs::path(a.argv[2]) / in.filename(), o);
        printf("%-8s %-48s %d\n", ck.kind, in.filename().string().c_str(), k);
    }
    return 0;
}

// beacon10: the zone list's BeaconMap rows in 3.0 form, Beacon_Arrow_Blue rows added.
int convertBeaconMap(const Args& a)
{
    std::string t = slurp(a.argv[2]);
    int aliases = 0, n = di1::mission::beaconMap30(t, &aliases);
    writeFile(a.argv[3], t);
    printf("%d BeaconMap rows in 3.0 form, %d Beacon_Arrow_Blue rows added\n", n, aliases);
    return 0;
}

// The plugin's zone list steps: zones10 (noadd: off), then zonelist30 and beaconMap30 (only: without them).
int convertZones10(const Args& a)
{
    const std::string opt = a.argc > 5 ? a.argv[5] : "";
    Zones z;
    if (!zones10(a.argv[2], a.argv[3], opt, z)) return 1;
    if (opt != "only") {
        int k = di1::challenges::zonelist30(z.t), ka = 0, kb = di1::mission::beaconMap30(z.t, &ka);
        printf("zonelist30 %d records, beaconMap30 %d rows (+%d)\n", k, kb, ka);
    }
    writeFile(a.argv[4], z.t);
    return 0;
}

// playsetmenu10 on a 3.0 chunk (a file, or an archive member) with the playsets zones10 finds.
int convertMenu(const Args& a)
{
    Zones z;
    if (!zones10(a.argv[3], a.argv[4], "", z)) return 1;
    std::string in;
    bool ok;
    if (a.argc > 6) {
        ok = readArc30(a.argv[5], a.argv[6], in);
    } else {
        in = slurp(a.argv[5]);
        ok = !in.empty();
    }
    if (!ok) return 1;
    std::string out = di1::ui::playsetMenu10(in, z.list);
    printf("%zu B 3.0 chunk -> %zu B Lua\n", in.size(), out.size());
    writeFile(a.argv[2], out);
    return 0;
}

// OCT-5: each 1.0 .oct's shapes remapped to 3.0's surface table (surf.py).
int convertSurf(const Args& a)
{
    const std::vector<std::string> names30 = surfaceNames30(slurp(a.argv[2]));
    printf("%u names in the 3.0 table\n", (unsigned)names30.size());
    for (int i = 3; i < a.argc; ++i) {
        std::string b = slurp(a.argv[i]);
        oct::File f = parse(b);
        if (int n = di1::surfaces::surfaces30(f, names30)) printf("%s: %d shapes remapped\n", a.argv[i], n);
    }
    return 0;
}

// TL-1: the 1.0 tool set's level lists, and the tools allowed in a zone (toolgate.py).
int convertTools(const Args& a)
{
    std::string b = slurp(a.argv[2]);
    auto t = di1::toolset::toolLevels10((const uint8_t*)b.data(), b.size());
    int listed = 0;
    for (auto& kv : t) listed += !kv.second.empty();
    printf("%u tools, %d with a level list; with a list allowing %s:", (unsigned)t.size(), listed, a.argv[3]);
    for (auto& kv : t)
        if (!kv.second.empty() && di1::toolset::levelAllows(kv.second, a.argv[3])) printf(" %s", kv.first.c_str());
    printf("\n");
    return 0;
}

// Navmesh blobs to the 3.0 layout: <out>/<stem>_<i>.bin per mesh for navconv.py's byte comparison.
int convertNav(const Args& a)
{
    int files = 0, meshes = 0, failed = 0;
    for (int k = 3; k < a.argc; ++k) {
        std::string b = slurp(a.argv[k]), why;
        oct::File f = parse(b);
        int n = di1::navmesh::navmesh30(f, &why);
        ++files;
        if (n < 0) {
            ++failed;
            printf("FAIL %s: %s\n", a.argv[k], why.c_str());
            continue;
        }
        long p = f.pool("NavMeshPool");
        int i = 0;
        for (size_t j = p < 0 ? 0 : p + 1, e = p < 0 ? 0 : f.blockEnd(p); j < e; ++j) {
            if (f.name(f.recs[j]) != "Data") continue;
            const std::string stem = fs::path(a.argv[k]).stem().string();
            writeFile(fs::path(a.argv[2]) / (stem + "_" + std::to_string(i++) + ".bin"), blobData(f.recs[j]));
        }
        meshes += n;
        std::string w = f.write();
        oct::File::parse(w.data(), w.size());   // the edited file re-parses (count widths, sizes)
    }
    printf("nav: %d files, %d meshes converted, %d files failed\n", files, meshes, failed);
    return failed ? 1 : 0;
}

// REN-3: <out>/<stem>_0.vbuf, each .oct's vertex buffer with its InfluenceRemap palettes applied.
int convertSkin(const Args& a)
{
    di1::skin::SkinLayouts L = di1::skin::skinLayouts(slurp(a.argv[2]));
    int files = 0, prims = 0;
    for (int k = 4; k < a.argc; ++k) {
        fs::path p = a.argv[k];
        const std::string stem = p.stem().string();
        std::string b = slurp(p), vb = slurp(p.parent_path() / (stem + "_0.vbuf")), why;
        std::string ib = slurp(p.parent_path() / (stem + "_0.ibuf"));
        oct::File f = parse(b);
        int n = di1::skin::skinRemap(f, vb, ib, L, &why);
        writeFile(fs::path(a.argv[3]) / (stem + "_0.vbuf"), vb);
        ++files;
        prims += n;
        if (!why.empty()) printf("%s: %d primitives; %s\n", stem.c_str(), n, why.c_str());
    }
    printf("skin: %zu skinned 1.0 formats, %d files, %d primitives remapped\n", L.size(), files, prims);
    return 0;
}

// A 1.0 .btd in 3.0's block layout, and its .btm beside the output.
int convertBtd(const Args& a)
{
    std::string b = slurp(a.argv[2]), o;
    if (!di1::btd::convert(b, o)) {
        printf("not the 1.0 block layout\n");
        return 1;
    }
    writeFile(a.argv[3], o);
    std::string mp = a.argv[3];
    writeFile(mp.substr(0, mp.size() - kExtLen) + ".btm", btd::btm(o));
    printf("%u blocks, %u B\n", (unsigned)btd::blocks(o, btd::kLayout30).size(), (unsigned)o.size());
    return 0;
}

// A 1.0 .bent (4.1) in 3.0's 4.1f form.
int convertBent(const Args& a)
{
    std::string b = slurp(a.argv[2]), why;
    oct::File f = parse(b);
    if (!di1::bent::transcode(f, a.argv[3], &why)) {
        printf("not transcoded: %s\n", why.c_str());
        return 1;
    }
    std::string o = f.write();
    writeFile(a.argv[4], o);
    printf("wrote %zu B, %zu records\n", o.size(), f.recs.size());
    return 0;
}

// bndl::texBundleAs: a texture-only bundle renamed to the output's name.
int convertTexName(const Args& a)
{
    std::string o = a.argv[3], name = o.substr(o.find_last_of("/\\") + 1);
    if (name.size() <= kExtLen) {   // a name shorter than its extension fails cleanly (OT-24)
        fprintf(stderr, "octtest: %s is no <name>.mtb\n", o.c_str());
        return kUsageError;
    }
    name.resize(name.size() - kExtLen);
    std::string t = bndl::texBundleAs(slurp(a.argv[2]), name);
    if (t.empty()) {
        printf("not a texture-only bundle\n");
        return 1;
    }
    writeFile(o, t);
    printf("%s: %u B, TEXB %s\n", o.c_str(), (unsigned)t.size(), name.c_str());
    return 0;
}

// The re-packed clip blocks of one file, concatenated (for the Python oracle clip10.py).
int convertClipDump(const Args& a)
{
    std::string b = slurp(a.argv[2]), all;
    oct::File f = parse(b);
    for (auto& r : f.recs) {
        if (f.name(r) != "ClipDataBlock") continue;
        std::string out;
        if (!di1::cho::clipBlock30(blobData(r), out)) out = "FAIL";
        all += out;
    }
    writeFile(a.argv[3], all);
    return 0;
}

}  // namespace octtest

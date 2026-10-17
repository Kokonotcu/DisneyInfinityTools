// hkOpen's stages that serve files 3.0 lacks: GameDB supplements, the session, the 1.0 install. Open threads.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
#include <set>
#include <string>
#include "../../base/format.hpp"
#include "../../base/text.hpp"
#include "../../core/engine_file.hpp"
#include "../../core/log.hpp"
#include "../../core/stack.hpp"
#include "../../core/zip.hpp"
#include "../../formats/oct.hpp"
#include "../../gameplay.hpp"
#include "../../sys.hpp"
#include "../../third_party/miniz/miniz.h"
#include "../../trace/loadprof.hpp"
#include "../../ui.hpp"
#include "audio.hpp"
#include "config.hpp"
#include "openstage.hpp"
#include "session.hpp"
#include "transcode.hpp"
#include "transcodeio.hpp"
#include "vfs.hpp"

namespace di1::serve {

// serve10: a di1sys/ path is a GameDB supplement (sys.hpp), served only where 3.0 has no file.
inline bool sysStage(openstage::Open& o, int*& out);
// block10: a path under a listed prefix is never served from 1.0. Always passes on.
inline bool blockStage(openstage::Open& o, int*& out);
// serve10: a file 3.0 lacks, from the session or the 1.0 install; passes on when 1.0 has none.
inline bool serveStage(openstage::Open& o, int*& out);

namespace detail {

constexpr size_t kSysDir = sizeof("di1sys/") - 1;
constexpr size_t kLuaSuffix = sizeof(".lua") - 1;
constexpr size_t kMerSuffix = sizeof(".mer") - 1;
constexpr size_t kPcConSuffix = sizeof("_pc_con.dct") - 1;
constexpr size_t kHoloSuffix = sizeof("_holo.mtb") - 1;
constexpr size_t kPresentationDir = sizeof("presentation/") - 1;

// A DDS file: the magic, then DDS_HEADER (Microsoft's "DDS file layout")
constexpr size_t kDdsBytes = 128;   // magic and header
constexpr size_t kDdsHeight = 12;   // u32 dwHeight, then u32 dwWidth
constexpr size_t kDdsWidth = 16;
constexpr size_t kDdsMips = 28;     // u32 dwMipMapCount
constexpr size_t kDdsFourCc = 84;   // the pixel format's FourCC
constexpr uint32_t kBlockSide = 4;  // a block-compressed texture's blocks are 4x4 texels

// loadprof10's served-file stacks: the first 3 per kind, 15 in all, 16 exe addresses each
inline constexpr const char* kStackKinds[] = {".oct", ".bent", ".banm", ".mer", ".tbody"};
constexpr LONG kStacksPerKind = 3;
constexpr LONG kStacksAll = 15;
constexpr int kStackWords = 16;

struct State {
    std::atomic<LONG> stacks{0};                                   // served-file stacks logged; any open thread
    std::atomic<LONG> perKind[sizeof(kStackKinds) / sizeof(kStackKinds[0])] = {};   // the same per kind
};
inline State g_state;

// A di1sys/ supplement's server: p as opened, np normalised, pre its prefix's length.
typedef int* (*SysServeFn)(const char* p, const std::string& np, size_t pre);

// The 1.0 file at a path 3.0 also has.
inline int* serveRaw10(const char* p, const std::string& np, size_t pre)
{
    std::string src;
    bool ok = vfs::read10s(np.substr(pre), src);
    core::logf("served %s %u B from 1.0 %s%s", p, (unsigned)src.size(), np.substr(pre).c_str(),
               ok ? "" : " -- MISSING");
    return ok ? core::memFile(src.data(), src.size()) : nullptr;
}

// GLB-1: 1.0's ChestInfo.bin as chestInfo Lua.
inline int* serveChest10(const char* p, const std::string& np, size_t pre)
{
    std::string bin = np.substr(pre, np.size() - pre - kLuaSuffix) + ".bin", why, src, out;
    bool ok = vfs::read10s(bin, src) && sys::chestInfoLua(src, out, &why);
    core::logf("served %s from 1.0 %s (%u B) -> chestInfo Lua %u B%s%s", p, bin.c_str(), (unsigned)src.size(),
               (unsigned)out.size(), ok ? "" : " FAILED: ", ok ? "" : (src.empty() ? "no 1.0 file" : why.c_str()));
    return ok ? core::memFile(out.data(), out.size()) : nullptr;
}

// GLB-1: 1.0's PerceptionInfo.bin as perceptions Lua, the perceptions 3.0 lacks.
inline int* servePerc10(const char* p, const std::string& np, size_t pre)
{
    std::string bin = np.substr(pre, np.size() - pre - kLuaSuffix) + ".bin", why, src, out;
    int added = 0;
    bool ok = vfs::read10s(bin, src) && sys::perceptionInfoLua(src, sys::perc30(), out, added, &why);
    core::logf("served %s from 1.0 %s (%u B) -> perceptions Lua %u B, %d perceptions 3.0 lacks%s%s", p, bin.c_str(),
               (unsigned)src.size(), (unsigned)out.size(), added, ok ? "" : " FAILED: ",
               ok ? "" : (src.empty() ? "no 1.0 file" : why.c_str()));
    return ok ? core::memFile(out.data(), out.size()) : nullptr;
}

// A 1.0 lock table reduced to the records whose UIDs the Locks DB lacks.
inline int* serveLocks10(const char* p, const std::string& np, size_t pre)
{
    std::string src, out;
    if (!vfs::read10s(np.substr(pre), src)) {
        core::logf("supplement %s: no 1.0 table", p);
        return nullptr;
    }
    std::set<uint32_t> have = sys::lockUids();
    int kept = 0, total = 0;
    bool ok = sys::keepRecords(src, [&](uint32_t u) { return !have.count(u); }, out, kept, total);
    core::logf("supplement %s: %d of %d 1.0 lock records (UIDs the Locks DB lacked; it held %u)%s", p, kept, total,
               (unsigned)have.size(), ok ? "" : " -- layout not found, not served");
    return ok ? core::memFile(out.data(), out.size()) : nullptr;
}

struct SysSupplement {
    const char* prefix;
    bool lua;   // the path must also end in .lua
    SysServeFn serve;
};

// sys.hpp supplements: a 1.0 GameDB table reduced to the records the target DB lacks (served under a di1sys/ path)
inline int* sysServe(const char* p)
{
    // first match wins, in this order (C3)
    const SysSupplement supplements[] = {{sys::kRaw10Prefix, false, serveRaw10},
                                         {sys::kChest10Prefix, true, serveChest10},
                                         {sys::kPerc10Prefix, true, servePerc10},
                                         {sys::kLocks10Prefix, false, serveLocks10}};
    std::string np = text::norm(p);
    for (const SysSupplement& s : supplements) {
        size_t pre = strlen(s.prefix);
        const bool lua = !s.lua || text::endsWith(p, ".lua");
        if (np.compare(0, pre, s.prefix) || !lua) continue;
        return s.serve(p, np, pre);
    }
    return nullptr;
}

// D3D11 rejects block-compressed textures under 4x4; a 1x1/2x2 one-mip DXT file holds a whole block:
// header only (conv.copy_texture).
inline void padSmallDxt(std::string& d)
{
    if (d.size() < kDdsBytes || d.compare(0, 4, "DDS ") || d.compare(kDdsFourCc, 3, "DXT")) return;
    const uint8_t* b = (const uint8_t*)d.data();
    uint32_t h = oct::u(b + kDdsHeight, 4), w = oct::u(b + kDdsWidth, 4);
    uint32_t mips = oct::u(b + kDdsMips, 4);
    const bool underBlock = w < kBlockSide || h < kBlockSide;
    if (underBlock && mips <= 1) {
        uint32_t v[2] = {h < kBlockSide ? kBlockSide : h, w < kBlockSide ? kBlockSide : w};
        memcpy(&d[kDdsHeight], v, sizeof(v));
    }
}

inline int* serveSession(const openstage::Open& o, const std::string& hit)
{
    int* r = core::memFile(hit.data(), hit.size());
    char pf[100] = "";
    if (o.prof)
        format::fitOrFailFast(pf, sizeof(pf), " [prof tid %u frame %ld: 3.0 open %.2f, total %.2f ms]",
                              GetCurrentThreadId(), trace::loadprof::frameNo(), o.t30,
                              trace::loadprof::nowMs() - o.tIn);
    core::logf("served %s %u B from asset session -> %s%s", o.p, (unsigned)hit.size(), r ? "ok" : "FAILED", pf);
    return r;
}

// The 1.0 bytes an open is served from: the member (mz_free it), its size, "zip:member".
struct Read10 {
    void* data = nullptr;
    size_t n = 0;
    std::string where;
    std::string chal;   // gp: the ChallengeData/<1.0 type file> path read instead, "" if none
};

// uiscope10: an image of a flash movie not served from 1.0 is not served either; that sets the miss reason.
inline bool imageOfMovie30(openstage::Open& o, const std::string& np)
{
    std::string movie = o.cfg->uiScope10 ? ui::flashImageMovie(np) : "";
    if (movie.empty()) return false;
    const bool scoped = !session::movieServed(movie);
    if (scoped) o.missWhy = " (image id of a movie not served from 1.0: not served)";
    return scoped;
}

// Stand-ins 1.0 never shipped: a language table's WIN32 for its _pc_con, a giver's portrait for its _Holo icon.
inline void* read10StandIn(const openstage::Open& o, const std::string& np, size_t* n, std::string* where)
{
    if (text::endsWith(o.p, "_pc_con.dct")) {
        std::string w = std::string(o.p, strlen(o.p) - kPcConSuffix) + "_WIN32.dct";
        return vfs::read10(w.c_str(), n, where);
    }
    if (np.rfind("ui/charactericons/", 0) != 0 || !text::endsWith(o.p, "_holo.mtb")) return nullptr;
    std::string w = std::string(o.p, strlen(o.p) - kHoloSuffix) + ".mtb";
    return vfs::read10(w.c_str(), n, where);
}

// vo10: a 1.0 dialog line's lipsync (audio.hpp annoSource), read from its zip.
inline void* read10Lipsync(const Config& c, const char* p, size_t* n, std::string* where)
{
    std::string annoZip, annoMember;
    const bool vo = c.audio10 && c.vo10;
    if (!vo || !audio::annoSource(p, annoZip, annoMember)) return nullptr;
    void* data = nullptr;
    vfs::withArchive(annoZip, [&](mz_zip_archive* z) {
        if (z) data = core::extractZipMember(z, annoMember.c_str(), n);
    });
    if (data) *where = annoZip + ":" + annoMember;
    return data;
}

// The 1.0 read of a missing file: overlay member, ChallengeData redirect, stand-ins, lipsync. Sets the miss reason.
inline Read10 read10For(openstage::Open& o, const std::string& np, const std::string& over)
{
    double tR = o.prof ? trace::loadprof::nowMs() : 0;
    Read10 rd;
    // the count includes the candidate names that were not there (A-12)
    o.missWhy = over.empty() ? " (not in the 1.0 archives, " + std::to_string(vfs::archiveCount()) + " archives open)"
                             : " (overlay " + over + " unreadable)";
    const bool scoped = imageOfMovie30(o, np);
    rd.chal = o.cfg->challenge10 ? gameplay::challengeTypePath(np) : "";
    const char* from = !rd.chal.empty() ? rd.chal.c_str() : over.empty() ? o.p : over.c_str();
    rd.data = scoped ? nullptr : vfs::read10(from, &rd.n, &rd.where);
    if (!rd.data) rd.data = read10StandIn(o, np, &rd.n, &rd.where);
    if (!rd.data) rd.data = read10Lipsync(*o.cfg, o.p, &rd.n, &rd.where);
    if (o.prof) o.tRead = trace::loadprof::nowMs() - tR;
    return rd;
}

// A served file's bytes in the form 3.0 reads, its codec ("" raw) and the note its line carries.
struct Transcoded {
    std::string out, note;
    const char* codec = nullptr;
};

// A served file's transcode; a throw never reaches the engine: it is noted, the raw bytes served.
inline Transcoded transcodeServed(const openstage::Open& o, const std::string& np, const std::string& over,
                                  const Read10& rd)
{
    Transcoded t;
    std::string mtc;
    try {
        if (text::endsWith(o.p, ".tbody")) {   // D3D11 refuses sub-4x4 block textures (padSmallDxt)
            t.out.assign((const char*)rd.data, rd.n);
            padSmallDxt(t.out);
            t.codec = t.out.size() == rd.n && memcmp(t.out.data(), rd.data, rd.n) ? "tbody" : nullptr;
        } else {
            t.codec = transcode({o.p, rd.data, rd.n}, {t.out, t.note, &mtc}, transcodeio::options(), transcodeio::io());
        }
        // the .mtc request transcodes read10s(<same path>.mer): these bytes when nothing redirected the read
        const bool mer = t.codec && !strcmp(t.codec, "mer") && !mtc.empty();
        if (mer && over.empty() && rd.chal.empty())
            session::keep({{np.substr(0, np.size() - kMerSuffix) + ".mtc", mtc}});
    } catch (std::exception& e) {   // never unwind into engine code (that ends in a silent fail-fast)
        t.note += std::string("TRANSCODE EXCEPTION: ") + e.what() + " ";
        t.codec = nullptr;
    } catch (...) {
        t.note += "TRANSCODE EXCEPTION ";
        t.codec = nullptr;
    }
    if (!over.empty()) t.note += "(archive overlay) ";
    return t;
}

// ui: a flash movie served from 1.0 (its images may be too) and a presentation screen served from 1.0 (gprt).
inline void noteServedUi(const std::string& np, const char* p)
{
    std::string mv = ui::flashMovie(np);
    if (!mv.empty()) session::noteMovie(mv);
    if (np.rfind("presentation/", 0) != 0 || !text::endsWith(p, ".lua")) return;
    session::noteScreen(np.substr(kPresentationDir, np.size() - kPresentationDir - kLuaSuffix));
}

// loadprof10: the engine stack of the first 3 served files per kind (15 in all), appended to pf.
inline void appendServedStack(const char* p, const uint32_t* sp, char* pf, size_t cap, int len)
{
    State& s = g_state;
    for (size_t k = 0; k < sizeof(kStackKinds) / sizeof(kStackKinds[0]); ++k) {
        if (!text::endsWith(p, kStackKinds[k])) continue;
        const LONG ofKind = ++s.perKind[k];
        if (ofKind > kStacksPerKind) continue;
        const LONG all = ++s.stacks;
        if (all > kStacksAll) continue;
        len += format::fitOrFailFast(pf + len, cap - len, " stack:");
        core::stackOf(sp, pf, cap, len, kStackWords);
    }
}

// A file served from 1.0: indexed overlay, transcode, MemFile, the served line. Frees rd's bytes.
inline int* serveRead(const openstage::Open& o, const std::string& np, const std::string& over, const Read10& rd)
{
    session::indexArchive(rd.where.substr(0, rd.where.find(':')));
    double tT = o.prof ? trace::loadprof::nowMs() : 0;
    Transcoded t = transcodeServed(o, np, over, rd);
    noteServedUi(np, o.p);
    double tTc = o.prof ? trace::loadprof::nowMs() - tT : 0;
    double tM = o.prof ? trace::loadprof::nowMs() : 0;
    int* r = t.codec ? core::memFile(t.out.data(), t.out.size()) : core::memFile(rd.data, rd.n);
    char pf[600] = "";
    if (o.prof) {
        int len = format::fitOrFailFast(pf, sizeof(pf), " [prof tid %u frame %ld: 3.0 open %.2f, 1.0 read %.2f, "
                                        "transcode %.2f, memfile %.2f, total %.2f ms]", GetCurrentThreadId(),
                                        trace::loadprof::frameNo(), o.t30, o.tRead, tTc,
                                        trace::loadprof::nowMs() - tM, trace::loadprof::nowMs() - o.tIn);
        appendServedStack(o.p, o.sp, pf, sizeof(pf), len);
    }
    core::logf("served %s %u B from 1.0 %s codec=%s%s%s -> %s%s", o.p, (unsigned)(t.codec ? t.out.size() : rd.n),
               rd.where.c_str(), t.codec ? t.codec : "raw", t.note.empty() ? "" : " ", t.note.c_str(),
               r ? "ok" : "FAILED", pf);
    mz_free(rd.data);
    return r;
}

}  // namespace detail

inline bool sysStage(openstage::Open& o, int*& out)
{
    const bool absent = !o.r && !o.write;
    if (!absent || !o.cfg->serve10 || _strnicmp(o.p, "di1sys/", detail::kSysDir)) return false;
    out = detail::sysServe(o.p);
    return true;
}

inline bool blockStage(openstage::Open& o, int*&)
{
    if (!o.cfg->block10[0]) return false;
    std::string np = text::norm(o.p), list = o.cfg->block10;
    for (size_t a = 0, b; a < list.size(); a = b + 1) {
        b = list.find(',', a);
        if (b == std::string::npos) b = list.size();
        std::string pre = list.substr(a, b - a);
        if (!pre.empty() && np.rfind(pre, 0) == 0) o.blocked = true;
    }
    return false;
}

inline bool serveStage(openstage::Open& o, int*& out)
{
    const bool absent = !o.r && !o.blocked && !o.write;
    if (!absent || !o.cfg->serve10 || !o.cfg->install10[0]) return false;
    std::string np = text::norm(o.p), hit, over;
    session::lookup(np, hit, over);
    if (!hit.empty()) {
        out = detail::serveSession(o, hit);
        return true;
    }
    detail::Read10 rd = detail::read10For(o, np, over);
    if (!rd.data) return false;
    out = detail::serveRead(o, np, over, rd);
    return true;
}

}  // namespace di1::serve

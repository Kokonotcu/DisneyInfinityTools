// carbody10: Cars townsfolk car bodies: 1.0's tinted models in playsets, 3.0's in its Toy Box.
// Open threads.
#pragma once
#include <cstddef>
#include <iterator>
#include <string>
#include "../../base/text.hpp"
#include "../../core/engine_file.hpp"
#include "../../core/log.hpp"
#include "../../engine/assets.hpp"
#include "../../engine/gamedb.hpp"
#include "config.hpp"
#include "openstage.hpp"
#include "vfs.hpp"

namespace di1::carbody {

// A 3.0 cust table: the three body pieces get hasRRVer = 1. Consumes r; answers the text or reopens.
inline bool rrFlagStage(openstage::Open& o, int*& out);
// A body model at 3.0's playset path goes to 1.0's model. Destroys r; serveStage answers.
inline bool playsetStage(openstage::Open& o, int*& out);
// A body file at its RR/ path, which 3.0 lacks: 3.0's own file at the playset path. Answers it.
inline bool toyBoxStage(openstage::Open& o, int*& out);

namespace detail {

// FUN_01668910 puts "RR/" after a piece path's first '/' when hasRRVer (DBPiece +0x30) and !WorldIsPlayset
constexpr char kRr[] = "rr/";
constexpr size_t kRrLen = sizeof(kRr) - 1;
constexpr char kHasRr[] = "hasRRVer = ";
constexpr size_t kHasRrLen = sizeof(kHasRr) - 1;

// 3.0 re-exported these as sedan_blue/truck_green/van_red: tint 1, coloured texture. 1.0: white, tinted.
inline constexpr const char* kPieces[] = {"Core/SimpleVehicle_CarBody/sedan", "Core/SimpleVehicle_CarBody/truck",
                                          "Core/SimpleVehicle_CarBody/van"};
inline constexpr const char* kStems[] = {"avatars/pieces/core/simplevehicle_carbody/sedan",
                                         "avatars/pieces/core/simplevehicle_carbody/truck",
                                         "avatars/pieces/core/simplevehicle_carbody/van"};
// A piece's files; the model is the first kModelExts, the Toy Box placement voxels (.vxl) stay 3.0's.
inline constexpr const char* kExts[] = {".oct", ".mtb", "_0.vbuf", "_0.ibuf", ".vxl"};
constexpr size_t kModelExts = 4;

struct State {
    core::LogOnce<std::string> logged;   // first serve of each path kind; its own lock
};
inline State g_state;

// kExts' index of np's file when np is a body piece's file, else std::size(kExts).
inline size_t bodyFile(const std::string& np)
{
    for (const char* s : kStems) {
        const std::string stem = s;
        if (np.compare(0, stem.size(), stem) != 0) continue;
        for (size_t i = 0; i < std::size(kExts); ++i)
            if (np.compare(stem.size(), std::string::npos, kExts[i]) == 0) return i;
    }
    return std::size(kExts);
}

// Where np holds an "rr/" path segment whose removal names a body piece file, else npos.
inline size_t rrSegment(const std::string& np)
{
    for (size_t k = np.find(kRr); k != std::string::npos; k = np.find(kRr, k + 1)) {
        const bool segment = k == 0 || np[k - 1] == '/';
        if (segment && bodyFile(np.substr(0, k) + np.substr(k + kRrLen)) < std::size(kExts)) return k;
    }
    return std::string::npos;
}

// t with each body piece record's hasRRVer set to 1; the count set.
inline int setRrFlags(std::string& t)
{
    int set = 0;
    for (const char* piece : kPieces) {
        const size_t n = t.find("name = '" + std::string(piece) + "'");
        const size_t f = n == std::string::npos ? n : t.find(kHasRr, n);
        const size_t end = n == std::string::npos ? n : t.find('}', n);
        if (f == std::string::npos || f > end || t.compare(f + kHasRrLen, 1, "0") != 0) continue;
        t[f + kHasRrLen] = '1';
        ++set;
    }
    return set;
}

inline bool custTable(const char* p)
{
    return text::endsWith(p, "/cust__lua.lua") || text::endsWith(p, "/cust_simplevehicle__lua.lua");
}

}  // namespace detail

inline bool rrFlagStage(openstage::Open& o, int*& out)
{
    if (!o.edits() || !o.cfg->carBody10 || !detail::custTable(text::norm(o.path).c_str())) return false;
    if (engine::gamedb::isChdPath(o.path)) {
        core::logf("carbody10: %s: a CHD load, its pieces keep hasRRVer", o.p);
        return false;
    }
    std::string t;
    const bool read = core::readStream(o.r, t);
    const bool chunk = !t.empty() && t[0] == openstage::kLuaChunk;
    if (!read || chunk) {
        core::logf("carbody10: %s: not readable text (%u B): unchanged", o.p, (unsigned)t.size());
        out = openstage::reopen(o);
        return true;
    }
    const int set = detail::setRrFlags(t);
    if (set) core::logf("carbody10: %s: %d car body pieces get hasRRVer (Toy Box keeps 3.0's)", o.p, set);
    out = core::memFile(t.data(), t.size());
    return true;
}

inline bool playsetStage(openstage::Open& o, int*&)
{
    if (!o.edits() || !o.cfg->carBody10) return false;
    const std::string np = text::norm(o.path);
    std::string t10;
    if (detail::bodyFile(np) >= detail::kModelExts || !vfs::read10s(np, t10)) return false;
    // the prebuilt colour (Cars_Townspeople_Sedan: index 0, grey) is 1.0's block A tint
    if (detail::g_state.logged.first(np))
        core::logf("carbody10: %s: 1.0's tinted car body served (playset path)", o.p);
    openstage::destroyStream(o.r);
    o.r = nullptr;
    o.missWhy = " (carbody10: 1.0's car body)";
    return false;
}

inline bool toyBoxStage(openstage::Open& o, int*& out)
{
    if (o.r || o.write || !o.cfg->carBody10) return false;
    const std::string path = o.path;
    const std::string np = text::norm(path.c_str());
    const size_t k = detail::rrSegment(np);
    if (k == std::string::npos) return false;
    // norm keeps the length, less one leading "assets/"
    std::string p30 = path;
    p30.erase(k + (path.size() - np.size()), detail::kRrLen);
    out = engine::assets::open(p30.c_str(), o.flags, o.a3, o.a4, o.a5);
    if (detail::g_state.logged.first(np))
        core::logf("carbody10: %s: 3.0's car body served from %s (Toy Box)", o.p, p30.c_str());
    return out != nullptr;
}

}  // namespace di1::carbody

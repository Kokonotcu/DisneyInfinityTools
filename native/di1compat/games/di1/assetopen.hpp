// 1.0's AssetOpen detour: every engine file open through the stages, first answer wins. games/di1.
// Several engine threads.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/assets.hpp"
#include "config.hpp"
#include "edits30.hpp"
#include "openstage.hpp"
#include "serve.hpp"

namespace di1::assetopen {

// Keeps c for the stages; installs no hook: engine::assets installs hkOpen (H1).
inline bool install(const Config& c);
// FUN_0041cb10's detour for engine::assets: the stages in order, else the engine's own stream.
inline int* __cdecl hkOpen(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5);

namespace detail {

constexpr uint32_t kFlagsLogged = 0xff;   // the open line shows the flags' low byte

struct State {
    Config cfg;   // set once in install(), before engine::assets installs hkOpen
};
inline State g_state;

// First answer wins, in this order (B3, C3); a serveStage miss passes on.
inline constexpr openstage::Stage kStages[] = {
    edits30::vtxfStage, edits30::tipsStage, edits30::playsetMenuStage, edits30::hudCurrencyStage,
    edits30::zoneListStage, edits30::catalogTypeStage, edits30::chdStage, edits30::rootGraphStage,
    edits30::olderCompanionStage, edits30::carBodyStage,
    serve::sysStage, serve::serveStage};

// Misses only: rare and cheap, they show what an unexplained fallback asks for.
inline void logOpen(const openstage::Open& o)
{
    if (!o.r) core::logf("open %s flags=%X -> MISSING%s", o.p, o.flags & kFlagsLogged, o.missWhy.c_str());
}

}  // namespace detail

// FUN_0041cb10, cdecl (path, flags, a3, a4, a5): AssetOpen, every engine file open; several engine threads.
inline int* __cdecl hkOpen(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5)
{
    const Config& c = detail::g_state.cfg;
    openstage::Open o{&c, path, flags, a3, a4, a5, (flags & openstage::kOpenWrite) != 0};
    o.r = engine::assets::open(path, flags, a3, a4, a5);
    core::safeCopy(o.p, sizeof(o.p), path);
    for (openstage::Stage stage : detail::kStages) {
        int* out = nullptr;
        if (stage(o, out)) return out;
    }
    detail::logOpen(o);
    return o.r;
}

inline bool install(const Config& c)
{
    detail::g_state.cfg = c;
    return true;
}

}  // namespace di1::assetopen

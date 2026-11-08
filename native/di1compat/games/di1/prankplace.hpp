// prankplace10: the slot natives 3.0's PlaceDeco screen calls but its exe lacks, on prank.hpp's sockets.
// deliveries, prank. Main (UI) thread.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "config.hpp"
#include "deliveries.hpp"
#include "prank.hpp"

namespace di1::prankplace {

// Hooks the Place_* native registration when c.prankPlace10 is on (needs prank10, H1, H2, H4).
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kPlaceNatives = 0x00FEAB80;   // thiscall (L; ret 4): registers the Place_* UI natives
inline constexpr char kPlaceNativesBytes[] = "6AFF64A10000000068????????50";
// FUN_00feab80's Lua calls (lapi.c order, FUN_0130d7a0 lua_pushnil .. FUN_0130dca0 lua_settable)
constexpr uint32_t kTableRef = 0x0130E3D0;       // (L, LUA_GLOBALSINDEX) -> the index it sets natives into
constexpr uint32_t kTableUnref = 0x0130E4D0;     // (L, that index): its last call
constexpr uint32_t kPushString = 0x0130D840;     // lua_pushstring (L, s)
constexpr uint32_t kPushClosure = 0x0130D8E0;    // lua_pushcclosure (L, fn, n)
constexpr uint32_t kSetTable = 0x0130DCA0;       // lua_settable (L, idx)
constexpr uint32_t kToInteger = 0x0130D530;      // lua_tointeger (L, idx)
constexpr uint32_t kToString = 0x0130D5A0;       // lua_tolstring (L, idx, len*) -> null unless a string
constexpr uint32_t kPushBoolean = 0x0130D960;    // lua_pushboolean (L, b)
constexpr uint32_t kPushInteger = 0x0130D7E0;    // lua_pushinteger (L, n)
constexpr int kGlobals = -10002;                 // LUA_GLOBALSINDEX: 0xffffd8ee, as FUN_0042d9a0 passes it
constexpr int kPlayers = 4;
}  // namespace eng

struct Call {
    uint32_t va;
    const char* bytes;
};
// Each Lua call checked before the first use (H4)
inline constexpr Call kCalls[] = {
    {eng::kTableRef, "83EC088B4C24105556"},       {eng::kTableUnref, "53BB807BE1FF2B5C240C"},
    {eng::kPushString, "8B54240885D2750E"},       {eng::kPushClosure, "56578B7C240C8B4710"},
    {eng::kSetTable, "8B4C240853568B74240C"},     {eng::kToInteger, "8B4C24088B54240483EC08"},
    {eng::kToString, "568B742408578B7C2410"},     {eng::kPushBoolean, "8B4424048B480833D2"},
    {eng::kPushInteger, "8B4C24048B4108660F6E"},
};

typedef void(__fastcall* PlaceNativesFn)(void* self, void* edx, void* L);
typedef int(__cdecl* LuaFn)(void* L);
typedef int(__cdecl* RefFn)(void* L, int idx);
typedef void(__cdecl* UnrefFn)(void* L, int idx);
typedef void(__cdecl* PushStringFn)(void* L, const char* s);
typedef void(__cdecl* PushClosureFn)(void* L, LuaFn fn, int n);
typedef void(__cdecl* SetTableFn)(void* L, int idx);
typedef int(__cdecl* ToIntegerFn)(void* L, int idx);
typedef const char*(__cdecl* ToStringFn)(void* L, int idx, size_t* len);
typedef void(__cdecl* PushIntFn)(void* L, int n);

struct State {
    PlaceNativesFn g_placeNatives = nullptr;   // set once by hook() in install()
    std::mutex mx;                             // guards chosen
    std::array<std::string, eng::kPlayers> chosen;   // per player: the RRO object the screen picked
};
inline State g_state;

inline int argInt(void* L, int i) { return ((ToIntegerFn)core::rt(eng::kToInteger))(L, i); }
inline std::string argStr(void* L, int i)
{
    const char* s = ((ToStringFn)core::rt(eng::kToString))(L, i, nullptr);
    return s ? std::string(s) : std::string();
}
inline int player(void* L) { return (std::max)(0, (std::min)(argInt(L, 1), eng::kPlayers - 1)); }
inline int pushInt(void* L, int n)
{
    ((PushIntFn)core::rt(eng::kPushInteger))(L, n);
    return 1;
}

// The socket by the player's avatar, -1 none (the screen's own slot list is 3.0's empty SlotManager list)
inline int socketByAvatar(int who)
{
    float p[3] = {};
    if (!deliveries::avatarPosition(who, p)) return -1;
    return prank::socketNear({p[0], p[1], p[2]});
}

// Place_CanPlaceToyOnSlot (): any prank item may stand on a prank socket
inline int __cdecl canPlace(void* L)
{
    ((PushIntFn)core::rt(eng::kPushBoolean))(L, 1);
    return 1;
}

// Place_CreateGhostOnSlot (player, ghost, rroObjectName, slot) -> ghost: the pick is remembered, nothing drawn
inline int __cdecl createGhost(void* L)
{
    State& s = g_state;
    const int who = player(L);
    const std::string rro = argStr(L, 3);
    {
        std::lock_guard<std::mutex> lock(s.mx);
        s.chosen[who] = rro;
    }
    const int socket = socketByAvatar(who);
    core::logf("prankplace10: player %d picked %s, socket %d", who, rro.c_str(), socket);
    return pushInt(L, (std::max)(socket + 1, 1));   // placetoyonslot.lua skips placing a ghost of 0
}

// Place_PickGhostOnSlot (player, object, slot) -> ghost: none to pick up
inline int __cdecl pickGhost(void* L) { return pushInt(L, 0); }

// Place_MoveObjectToSlot (player, ghost, slot) / Place_EndSlotSelection (slot, player): nothing to move or end
inline int __cdecl nothing(void*) { return 0; }

// Place_PlaceObjectOnSlot (player, ghost, slot, name): the picked object stands on the socket by the avatar
inline int __cdecl placeObject(void* L)
{
    State& s = g_state;
    const int who = player(L);
    std::string rro;
    {
        std::lock_guard<std::mutex> lock(s.mx);
        rro.swap(s.chosen[who]);
    }
    if (rro.empty()) rro = prank::detail::rroOf(argStr(L, 4));
    const int socket = socketByAvatar(who);
    if (socket < 0 || rro.empty()) {
        core::logf("prankplace10: player %d placed '%s' with no prank socket within %.0f m", who, rro.c_str(),
                   prank::detail::kPlaceRadius);
        return 0;
    }
    prank::placeOn(socket, rro, "PlaceDeco");
    return 0;
}

// The natives 3.0's placetoyonslot.lua / placedeco.lua call (FORMATS.md 5.9), in the 1.0 names
struct Native {
    const char* name;
    LuaFn fn;
};
inline constexpr Native kNatives[] = {
    {"Place_CanPlaceToyOnSlot", &canPlace},   {"Place_CreateGhostOnSlot", &createGhost},
    {"Place_PickGhostOnSlot", &pickGhost},    {"Place_PlaceObjectOnSlot", &placeObject},
    {"Place_EndSlotSelection", &nothing},     {"Place_MoveObjectToSlot", &nothing},
};

// Ours into the table FUN_00feab80 fills, the way it fills it (push name, closure, settable)
inline void addNatives(void* L)
{
    const int t = ((RefFn)core::rt(eng::kTableRef))(L, eng::kGlobals);
    for (const Native& n : kNatives) {
        ((PushStringFn)core::rt(eng::kPushString))(L, n.name);
        ((PushClosureFn)core::rt(eng::kPushClosure))(L, n.fn, 0);
        ((SetTableFn)core::rt(eng::kSetTable))(L, t);
    }
    ((UnrefFn)core::rt(eng::kTableUnref))(L, t);
}

// FUN_00feab80, thiscall (L; ret 4): a UI Lua state's Place_* natives, main thread, once per Lua state.
inline void __fastcall hkPlaceNatives(void* self, void* edx, void* L)
{
    g_state.g_placeNatives(self, edx, L);
    try {
        addNatives(L);
        core::logf("prankplace10: %u slot natives added to a UI Lua state", (unsigned)std::size(kNatives));
    } catch (...) {   // H6: bad_alloc in the log line only
    }
}

inline bool callsMatch()
{
    for (const Call& k : kCalls) {
        if (core::prologueMatches(k.va, k.bytes)) continue;
        core::logf("prankplace10 off: %08X differs from the analysed bytes", k.va);
        return false;
    }
    return true;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    if (!c.prankPlace10 || !c.prank10) return true;
    if (!detail::callsMatch()) return true;
    return core::installFeature("prankplace10", {{"Place natives (prankplace10)", detail::eng::kPlaceNatives,
                                                  detail::eng::kPlaceNativesBytes, (void*)&detail::hkPlaceNatives,
                                                  (void**)&s.g_placeNatives}});
}

}  // namespace di1::prankplace

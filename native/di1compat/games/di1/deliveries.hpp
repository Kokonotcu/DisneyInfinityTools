// rcdelivery10 / rcbox10: a 1.0 catalog purchase is delivered at once (3.0 lacks DeliveryManager).
// engine/players, playsets. Main thread.
#pragma once
#include <windows.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../../core/enginestring.hpp"
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/players.hpp"
#include "config.hpp"
#include "playsets.hpp"

namespace di1::deliveries {

// Installs the lock unlock hook when c has rcdelivery10, all checks or nothing (H1, H2, H4).
inline bool install(const Config& c);
// Whether a lock entering state with this catalog Expendable gets a delivery (1.0 FUN_00bac360).
inline bool needsDelivery(uint8_t state, const char* expendable);

namespace detail {

namespace eng {
// lock states, 3.0 FUN_01613630: 4 PURCHASED, 5 DELIVERED, 6 AVAILABLE (1.0 FUN_00c3d780 alike)
constexpr uint8_t kPurchased = 4;
constexpr uint8_t kDelivered = 5;
constexpr uint32_t kUnlock = 0x01614D60;        // FUN_01614d60: thiscall on a DBLock, posts LockUnlocked
constexpr uint32_t kGetState = 0x016138E0;      // FUN_016138e0: thiscall (player, 0, 0) -> state
constexpr uint32_t kSetDelivered = 0x01613D60;  // FUN_01613d60: thiscall (player, notify); 1.0 FUN_0105c530
constexpr uint32_t kSetAvailable = 0x01613D80;  // FUN_01613d80: thiscall (player, notify, 0, 0); 1.0 FUN_0105c550
constexpr uint32_t kCatalog = 0x02289738;       // *: the RC catalog, as FUN_00b50ed0 passes it
constexpr uint32_t kCatalogField = 0x014B92C0;  // FUN_014b92c0: thiscall (name, field, CString* out, player)
constexpr uint32_t kExpendables = 0x0228AA40;   // *: the expendables manager
constexpr uint32_t kDispense = 0x0160F370;      // FUN_0160f370: thiscall (name, float xform[16], count, 1)
constexpr uint32_t kHandles = 0x02202A38;       // the handle table FUN_00492e20 reads through (FUN_015c5380)
constexpr uint32_t kResolve = 0x00492E20;       // FUN_00492e20: thiscall (uint32_t* out, handle*) -> entry
constexpr uint32_t kRefresh = 0x01887040;       // FUN_01887040: thiscall on a dirty avatar: its transform
constexpr uint32_t kAvatarType = 0x0228E4AC;    // u16 DAT_0228e4ac: the avatar actor type (FUN_00ba9bd0)
constexpr size_t kLockName = 0x10;              // DBLock vtable[10] FUN_006a8100: CString at +0x10
constexpr size_t kEntryObject = 8;              // a resolved handle entry: the actor
constexpr size_t kActorType = 6;                // u16, low 10 bits the type
constexpr uint16_t kTypeMask = 0x3ff;
constexpr size_t kActorDirty = 0x51;            // bit 0: transform stale, FUN_01887040 refreshes it
constexpr size_t kAvatarXform = 0x258;          // avatar: quaternion, then position (FUN_015c5380)
constexpr size_t kActorXform = 0x1c;            // other actors
constexpr size_t kActorRead = kAvatarXform + 7 * sizeof(float);
constexpr int kXformFloats = 16;                // FUN_00a7c170's transform: quaternion, position, then 1.0s
constexpr int kXformCopied = 7;
constexpr int kXformPos = 4;
constexpr int kXformY = 5;
constexpr float kLiftY = 0.5f;                  // 1.0 FUN_00b7b430 spawns half a metre over the avatar
constexpr float kAhead = 3.0f;                  // metres in front: a delivered vehicle must not land on the avatar
}  // namespace eng

typedef void(__fastcall* UnlockFn)(uint8_t* lock, void* edx, uint32_t state, int player, uint32_t network,
                                   int initiator, uint32_t a5, uint32_t a6);
typedef uint8_t(__fastcall* GetStateFn)(uint8_t* lock, void* edx, int player, uint32_t a2, uint32_t a3);
typedef uint32_t(__fastcall* SetDeliveredFn)(uint8_t* lock, void* edx, int player, uint32_t notify);
typedef uint32_t(__fastcall* SetAvailableFn)(uint8_t* lock, void* edx, int player, uint32_t notify, uint32_t a3,
                                             uint32_t a4);
typedef void(__fastcall* CatalogFieldFn)(void* cat, void* edx, const char* name, const char* field, char** out,
                                         int player);
typedef uint32_t(__fastcall* DispenseFn)(void* mgr, void* edx, const char* name, const float* xform, int count,
                                         uint32_t a4);
typedef uint8_t*(__fastcall* ResolveFn)(void* handles, void* edx, uint32_t* out, const uint32_t* handle);
typedef void(__fastcall* RefreshFn)(uint8_t* actor, void* edx);

struct State {
    UnlockFn g_unlock = nullptr;   // set once by hook() in install()
    bool box = false;              // rcbox10 and its prologues: set once in install()
};
inline State g_state;

// The catalog Expendable of an RC lock, as FUN_00b50ed0 reads it; false when it names none.
inline bool catalogExpendable(const char* name, char* buf, size_t cap)
{
    void* cat = *(void**)core::rt(eng::kCatalog);
    if (!cat) return false;
    core::EngineString out("");
    ((CatalogFieldFn)core::rt(eng::kCatalogField))(cat, nullptr, name, "Expendable", &out.p, 0);
    core::safeCopy(buf, cap, out.p);
    if (char* comma = strchr(buf, ',')) *comma = 0;   // FUN_014b92c0 joins a field list with ','
    return needsDelivery(eng::kPurchased, buf);
}

// The actor of a player's avatar, through 3.0's handle table; null when it is not there.
inline uint8_t* avatarActor(int player)
{
    uint8_t* pm = engine::players::manager();
    const uint8_t* table = pm ? engine::players::table(pm) : nullptr;
    uint32_t handle = table ? engine::players::avatarHandle(table, player) : 0;
    void* handles = *(void**)core::rt(eng::kHandles);
    if (!handle || !core::readable(handles, sizeof(void*))) return nullptr;
    uint32_t slot = 0;
    uint8_t* entry = ((ResolveFn)core::rt(eng::kResolve))(handles, nullptr, &slot, &handle);
    if (!entry || !core::readable(entry + eng::kEntryObject, sizeof(void*))) return nullptr;
    uint8_t* actor = *(uint8_t**)(entry + eng::kEntryObject);
    return actor && core::readable(actor, eng::kActorRead) ? actor : nullptr;
}

// The transform's position moved kAhead along its facing: local -z (in game: +z lands behind), kept level.
inline void stepAhead(float* xform)
{
    const float x = xform[0], y = xform[1], z = xform[2], w = xform[3];
    float fx = -2.f * (x * z + w * y), fz = -(1.f - 2.f * (x * x + y * y));
    const float len = std::sqrt(fx * fx + fz * fz);
    if (len < 1e-3f) return;   // facing straight up or down: keep the avatar's spot
    xform[eng::kXformPos] += eng::kAhead * fx / len;
    xform[eng::kXformPos + 2] += eng::kAhead * fz / len;
}

// rcbox10: the Delivery expendable at the avatar, as 1.0's initiator path FUN_00b7b430 spawns it.
inline void spawnAtAvatar(const char* expendable, int player)
{
    uint8_t* actor = avatarActor(player);
    void* mgr = *(void**)core::rt(eng::kExpendables);
    if (!actor || !mgr) {
        core::logf("rcbox10: no avatar for player %d or no expendables manager; %s not spawned", player, expendable);
        return;
    }
    const uint16_t avatarType = *(const uint16_t*)core::rt(eng::kAvatarType);
    const bool isAvatar = (*(const uint16_t*)(actor + eng::kActorType) & eng::kTypeMask) == avatarType;
    if (isAvatar && (actor[eng::kActorDirty] & 1)) ((RefreshFn)core::rt(eng::kRefresh))(actor, nullptr);
    const float* at = (const float*)(actor + (isAvatar ? eng::kAvatarXform : eng::kActorXform));
    float xform[eng::kXformFloats];
    for (int i = 0; i < eng::kXformFloats; ++i) xform[i] = i < eng::kXformCopied ? at[i] : 1.f;
    xform[eng::kXformY] += eng::kLiftY;
    stepAhead(xform);
    uint32_t r = ((DispenseFn)core::rt(eng::kDispense))(mgr, nullptr, expendable, xform, 1, 1);
    const float* pos = xform + eng::kXformPos;
    core::logf("rcbox10: %s dispensed at player %d's avatar (%.1f, %.1f, %.1f) -> %u", expendable, player, pos[0],
               pos[1], pos[2], r);
}

inline int validPlayer(int p, int fallback) { return p >= 0 && p < engine::players::eng::kMaxPlayers ? p : fallback; }

// 1.0's delivery steps on a purchased lock: DELIVERED (FUN_00b63930), the package, AVAILABLE (FUN_00b8e640).
inline void deliver(uint8_t* lock, int player, int initiator)
{
    const char* const* nameAt = (const char* const*)(lock + eng::kLockName);
    if (!core::readable(nameAt, sizeof(*nameAt)) || !core::readable(*nameAt, 1) || !playsets::currentZone10()) return;
    GetStateFn getState = (GetStateFn)core::rt(eng::kGetState);
    if (getState(lock, nullptr, player, 0, 0) != eng::kPurchased) return;   // a refused change (net client) stays
    char name[96], expendable[96];
    core::safeCopy(name, sizeof(name), *nameAt);
    if (!catalogExpendable(*nameAt, expendable, sizeof(expendable))) {
        core::logf("rcdelivery10: %s PURCHASED without a catalog Expendable; left as is", name);
        return;
    }
    ((SetDeliveredFn)core::rt(eng::kSetDelivered))(lock, nullptr, player, 1);
    if (g_state.box) spawnAtAvatar(expendable, validPlayer(initiator, player));
    if (getState(lock, nullptr, player, 0, 0) == eng::kDelivered)
        ((SetAvailableFn)core::rt(eng::kSetAvailable))(lock, nullptr, player, 1, 0, 0);
    core::logf("rcdelivery10: %s PURCHASED -> DELIVERED -> state %u for player %d (catalog Expendable %s)", name,
               (unsigned)getState(lock, nullptr, player, 0, 0), player, expendable);
}

// FUN_01614d60, thiscall on a DBLock (state, player, network, initiator, a5, a6), ret 0x18. Main thread; rare.
inline void __fastcall hkUnlock(uint8_t* lock, void* edx, uint32_t state, int player, uint32_t network, int initiator,
                                uint32_t a5, uint32_t a6)
{
    g_state.g_unlock(lock, edx, state, player, network, initiator, a5, a6);
    try {
        // 1.0 set the steps for player 0 (FUN_0105c530(0, 1)); the original maps a bad player to its default
        if ((uint8_t)state == eng::kPurchased && lock) deliver(lock, validPlayer(player, 0), initiator);
    } catch (...) {
        core::logf("rcdelivery10: delivery of an unlocked lock threw; lock left as the engine set it");
    }
}

struct Called {
    const char* name;
    uint32_t va;
    const char* prologueHex;
};

// Engine functions the steps call (H4); '??' is a relocated SEH handler address.
inline constexpr Called kLockCalls[] = {
    {"lock state", eng::kGetState, "64A1000000006AFF68????????506489250000000083EC10"},
    {"lock delivered", eng::kSetDelivered, "8B4424088B5424046A006A005052"},
    {"lock available", eng::kSetAvailable, "8B4424108B54240C508B44240C528B54240C"},
    {"catalog field", eng::kCatalogField, "6AFF68????????64A100000000506489250000000081EC24010000"},
};
inline constexpr Called kBoxCalls[] = {
    {"expendable dispense", eng::kDispense, "6AFF68????????64A100000000506489250000000081EC84000000"},
    {"handle resolve", eng::kResolve, "8B4424088B0085C07503C20800"},
    {"avatar transform", eng::kRefresh, "568BF1F6460C2074"},
};

// Whether every function of calls still has its analysed prologue; logs the first that does not.
template <size_t N>
inline bool callsMatch(const char* feature, const Called (&calls)[N])
{
    for (const Called& f : calls) {
        if (core::prologueMatches(f.va, f.prologueHex)) continue;
        core::logf("%s off: %s @ %08X prologue differs", feature, f.name, f.va);
        return false;
    }
    return true;
}

}  // namespace detail

inline bool needsDelivery(uint8_t state, const char* expendable)
{
    const bool named = expendable && expendable[0];
    return state == detail::eng::kPurchased && named && _stricmp(expendable, "none") != 0;
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    if (!c.rcDelivery10) return true;
    if (!detail::callsMatch("rcdelivery10", detail::kLockCalls)) return false;
    s.box = c.rcBox10 && detail::callsMatch("rcbox10", detail::kBoxCalls);
    return core::installFeature("rcdelivery10", {{"lock unlock (rcdelivery10)", detail::eng::kUnlock,
                                                  "64A1000000006AFF68????????506489250000000083EC20",
                                                  (void*)&detail::hkUnlock, (void**)&s.g_unlock}});
}

}  // namespace di1::deliveries

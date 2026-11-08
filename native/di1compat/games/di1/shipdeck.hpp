// shiprules10: a Tardis-spawned ship hull (AV_PirateShipRB) gets its deck, slaved to it. core, oceanboat. Game thread.
#pragma once
#include <cstdint>
#include <mutex>
#include <unordered_set>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "oceanboat.hpp"

namespace di1::shipdeck {

// Checks the called functions' prologues once (H4); false when one differs and decks stay unspawned.
inline bool check();
// After the Tardis mounted the player on hull: the deck at the hull's place, slaved to it. Once per hull.
inline void spawnFor(uint32_t hull);
// Whether deck came from spawnFor: shipspawn10 then gives it no second hull.
inline bool isOurs(uint32_t deck);

namespace detail {

namespace eng {
// FUN_0075a3f0 (CreateFromInitString) inlined: init object, actor create, manager notify, init freed
constexpr uint32_t kInitCtor = 0x00705EC0;     // thiscall (init; text; ret 4): 16-byte init object from "Key=Value;"
constexpr uint32_t kInitDtor = 0x006FF650;     // thiscall (init): frees it (0x703840 jumps here)
constexpr uint32_t kCreate = 0x0075A150;       // cdecl (init*, pos*, rotY, 2, 0, 0, 0, 3, 0, 0, 0, 0) -> actor
constexpr uint32_t kNotifier = 0x0228DB60;     // called after a create, vtable +0x20, no arguments (0x75a476)
constexpr size_t kNotifySlot = 0x20 / sizeof(void*);
constexpr size_t kActorHandle = 4;             // actor +4: its handle (FUN_00732250)
constexpr size_t kInitBytes = 16;              // FUN_00705ec0 clears 4 dwords
// ActorGetRotationY (0x74c580) -> FUN_0074b350, cdecl (handle) -> float
constexpr uint32_t kRotationY = 0x0074B350;
constexpr size_t kActorPos = 0x2c;             // ActorGetPosition (0x74b7f7): a non-avatar actor's position
// SlaveActorToActor (0x6f69c0) -> FUN_018a8d40, thiscall (mgr; slave, master, offset*, 3x3*, mode, bool; ret 0x18)
constexpr uint32_t kSlave = 0x018A8D40;
constexpr uint32_t kSlaveMgr = 0x0228E658;
// SlaveSetPlacedPassthrough (0x6f6d10) -> FUN_0186a4a0, thiscall (mgr; slave, bool; ret 8)
constexpr uint32_t kPassthrough = 0x0186A4A0;
// CreateFromInitString's own constants (0x75a44e..0x75a449)
constexpr int kCreateMode = 2;
constexpr int kCreateFlags = 3;
}  // namespace eng

// av_pirateship.dnax: the deck whose ShipRigidBodyConnector names AV_PirateShipRB
constexpr char kDeckInit[] = "DNAFile=characters/AV_PirateShip.dnax;";
constexpr int kSlaveMode = 1;   // mgshipriding.btl.sx (3.0's own ship slave) passes 1, true
constexpr int kSlaveFlag = 1;
constexpr int kVec = 3;

typedef void(__fastcall* InitCtorFn)(void* init, void* edx, const char* text);
typedef void(__fastcall* InitDtorFn)(void* init, void* edx);
typedef uint8_t*(__cdecl* CreateFn)(void* init, const float* pos, float rot, int mode, int a, int b, int c, int flags,
                                    int d, int e, int f, int g);
typedef void(__fastcall* NotifyFn)(void* notifier, void* edx);
typedef float(__cdecl* RotationFn)(uint32_t handle);
typedef void(__fastcall* SlaveFn)(void* mgr, void* edx, uint32_t slave, uint32_t master, const float* offset,
                                  const float* rows, int mode, int flag);
typedef void(__fastcall* PassthroughFn)(void* mgr, void* edx, uint32_t slave, int on);

struct State {
    bool checked = false;              // set once in check()
    std::mutex m;                      // guards hulls, decks
    std::unordered_set<uint32_t> hulls;   // ponytail: never pruned, a few per session
    std::unordered_set<uint32_t> decks;
};
inline State g_state;

// The hull's position as ActorGetPosition reads it; false when a step is null or unreadable (H8).
inline bool positionOf(uint32_t handle, float* out)
{
    namespace ob = oceanboat::detail;
    void* handles = *(void**)core::rt(ob::eng::kHandles);
    uint32_t slot = 0;
    const uint8_t* entry = handles ? ((ob::ResolveFn)core::rt(ob::eng::kResolve))(handles, nullptr, &slot, &handle)
                                   : nullptr;
    if (!entry || !core::readable(entry + ob::eng::kEntryActor, sizeof(void*))) return false;
    const uint8_t* actor = *(uint8_t* const*)(entry + ob::eng::kEntryActor);
    if (!actor || !core::readable(actor + eng::kActorPos, kVec * sizeof(float))) return false;
    for (int i = 0; i < kVec; ++i) out[i] = ((const float*)(actor + eng::kActorPos))[i];
    return true;
}

// The deck at pos and heading rot, as CreateFromInitString makes it; its handle, 0 when none.
inline uint32_t createDeck(const float* pos, float rot)
{
    alignas(8) uint8_t init[eng::kInitBytes];
    ((InitCtorFn)core::rt(eng::kInitCtor))(init, nullptr, kDeckInit);
    uint8_t* actor = ((CreateFn)core::rt(eng::kCreate))(init, pos, rot, eng::kCreateMode, 0, 0, 0, eng::kCreateFlags,
                                                        0, 0, 0, 0);
    void* notifier = *(void**)core::rt(eng::kNotifier);
    if (actor && notifier) ((NotifyFn)(*(void***)notifier)[eng::kNotifySlot])(notifier, nullptr);
    ((InitDtorFn)core::rt(eng::kInitDtor))(init, nullptr);
    return actor ? *(const uint32_t*)(actor + eng::kActorHandle) : 0;
}

// The deck slaved to the hull at no offset and no turn, passthrough on so a placed hull carries it.
inline void slave(uint32_t deck, uint32_t hull)
{
    static constexpr float kOffset[kVec] = {0.f, 0.f, 0.f};
    static constexpr float kRows[kVec * kVec] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    void* mgr = *(void**)core::rt(eng::kSlaveMgr);
    ((SlaveFn)core::rt(eng::kSlave))(mgr, nullptr, deck, hull, kOffset, kRows, kSlaveMode, kSlaveFlag);
    ((PassthroughFn)core::rt(eng::kPassthrough))(mgr, nullptr, deck, 1);
}

struct Called {
    uint32_t va;
    const char* prologueHex;
};
inline constexpr Called kCalled[] = {
    {eng::kInitCtor, "33C0568BF1890689460489460889460C"}, {eng::kInitDtor, "578BF98B470485C0740AFF4808C74704"},
    {eng::kCreate, "83EC5CF30F1005????????8B4424640F"},  {eng::kRotationY, "83EC2C8B442430568D4C2404518B0D"},
    {eng::kSlave, "0FB705????????8B4C24045051E8"},       {eng::kPassthrough, "0FB705????????8B4C24045051E8"},
    {oceanboat::detail::eng::kResolve, "8B4424088B0085C07503C20800"},
};

}  // namespace detail

inline bool check()
{
    for (const detail::Called& f : detail::kCalled) {
        if (core::prologueMatches(f.va, f.prologueHex)) continue;
        core::logf("shiprules10: no deck spawn, %08X prologue differs", f.va);
        return false;
    }
    detail::g_state.checked = true;
    return true;
}

inline void spawnFor(uint32_t hull)
{
    detail::State& s = detail::g_state;
    float pos[detail::kVec];
    if (!s.checked || !detail::positionOf(hull, pos)) return;
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (!s.hulls.insert(hull).second) return;
    }
    const float rot = ((detail::RotationFn)core::rt(detail::eng::kRotationY))(hull);
    const uint32_t deck = detail::createDeck(pos, rot);
    core::logf("shiprules10: hull %08X spawned deck %08X at (%.1f, %.1f, %.1f)", hull, deck, pos[0], pos[1], pos[2]);
    if (!deck) return;
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.decks.insert(deck);
    }
    detail::slave(deck, hull);
}

inline bool isOurs(uint32_t deck)
{
    std::lock_guard<std::mutex> lock(detail::g_state.m);
    return detail::g_state.decks.count(deck) != 0;
}

}  // namespace di1::shipdeck

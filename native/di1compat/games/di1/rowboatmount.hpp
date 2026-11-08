// rowboatmount10: a Tardis PIR_Rowboat mount goes to the player's current avatar; rowboatconnect10 (off) connects.
// core, engine/players. Game thread.
#pragma once
#include <atomic>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/players.hpp"
#include "config.hpp"

namespace di1::rowboatmount {

// Installs the transport rule hook when c.rowboatMount10 or c.rowboatConnect10 is on; false when refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// FUN_00df4cb0, TransportRule vtable 0x1d16f70 slot 4, thiscall(rule, avatar handle, info*; ret 8): the mount
constexpr uint32_t kTransportApply = 0x00DF4CB0;
constexpr size_t kInfoTransport = 4;      // info+4 -> transport record; record +4: the vehicle handle (0xdf4cd4)
constexpr size_t kTransportVehicle = 4;
// FUN_00cd2b20, thiscall(VehicleManager, biped, vehicle, other, bool driver; ret 0x10)

// called by VehicleManager_BipedInteractionConnect (0xa7d830) with the manager at 0x222146c
constexpr uint32_t kConnect = 0x00CD2B20;
constexpr uint32_t kVehicleManager = 0x0222146C;
// The rule's own PIR_Rowboat test (0xdf4f3a): type component's mask (+0x20) has the ActorType UID's bit
constexpr uint32_t kComponent = 0x0069ECD0;   // cdecl (handle, u16 type) -> component
constexpr uint32_t kTypeIndex = 0x0070F700;   // cdecl (UID) -> bit index
constexpr uint32_t kHasBit = 0x006F9AB0;      // cdecl (mask, index) -> bool
constexpr uint32_t kTypeComponent = 0x0228E4A8;   // u16 component type id the rule reads
constexpr size_t kTypeMask = 0x20;
constexpr uint32_t kRowboatUid = 2165;        // ActorType PIR_Rowboat (gamedb/pirates bitmasks.lua)
}  // namespace eng

typedef void(__fastcall* ApplyFn)(void* rule, void* edx, uint32_t avatar, const uint8_t* info);
typedef void(__fastcall* ConnectFn)(void* mgr, void* edx, uint32_t biped, uint32_t vehicle, uint32_t other,
                                    uint32_t driver);
typedef void*(__cdecl* ComponentFn)(uint32_t handle, uint32_t type);
typedef uint32_t(__cdecl* TypeIndexFn)(uint32_t uid);
typedef bool(__cdecl* HasBitFn)(void* mask, uint32_t index);

struct State {
    ApplyFn g_apply = nullptr;   // the original; set once in install()
    bool retarget = false;       // rowboatmount10; set once in install()
    bool connect = false;        // rowboatconnect10; set once in install()
    std::atomic<int> logged{0};
};
inline State g_state;

// The vehicle handle of the rule's transport record, 0 when unreadable.
inline uint32_t vehicleOf(const uint8_t* info)
{
    if (!info || !core::readable(info + eng::kInfoTransport, sizeof(void*))) return 0;
    const uint8_t* rec = *(const uint8_t* const*)(info + eng::kInfoTransport);
    if (!rec || !core::readable(rec + eng::kTransportVehicle, sizeof(uint32_t))) return 0;
    return *(const uint32_t*)(rec + eng::kTransportVehicle);
}

// Whether the handle's actor is typed PIR_Rowboat, by the rule's own calls.
inline bool isRowboat(uint32_t vehicle)
{
    const uint16_t type = *(const uint16_t*)core::rt(eng::kTypeComponent);
    uint8_t* comp = (uint8_t*)((ComponentFn)core::rt(eng::kComponent))(vehicle, type);
    if (!comp || !core::readable(comp + eng::kTypeMask, sizeof(void*))) return false;
    const uint32_t index = ((TypeIndexFn)core::rt(eng::kTypeIndex))(eng::kRowboatUid);
    return ((HasBitFn)core::rt(eng::kHasBit))(*(void**)(comp + eng::kTypeMask), index);
}

// The first local player's avatar handle, 0 when there is none.
inline uint32_t localAvatar()
{
    const uint8_t* pm = engine::players::manager();
    const uint8_t* table = pm ? engine::players::table(pm) : nullptr;
    if (!table) return 0;
    for (int p = 0; p < engine::players::eng::kMaxPlayers; ++p)
        if (engine::players::isLocal(table, p)) return engine::players::avatarHandle(table, p);
    return 0;
}

inline void logOnce(const char* what, uint32_t a, uint32_t b)
{
    if (g_state.logged.fetch_add(1) < 8) core::logf("rowboatmount10: %s %08X / %08X", what, a, b);
}

// FUN_00df4cb0: a rowboat mount goes to the player's avatar; in-game the rule named 0x34025000, Jack is 0x30007033.
inline void __fastcall hkApply(void* rule, void* edx, uint32_t avatar, const uint8_t* info)
{
    State& s = g_state;
    const uint32_t vehicle = vehicleOf(info);
    const bool rowboat = vehicle && isRowboat(vehicle);
    const uint32_t local = rowboat && s.retarget ? localAvatar() : 0;
    if (local && local != avatar) {
        logOnce("transport rule's avatar / the player's avatar mounted instead", avatar, local);
        avatar = local;
    }
    s.g_apply(rule, edx, avatar, info);
    void* mgr = *(void**)core::rt(eng::kVehicleManager);
    if (!rowboat || !s.connect || !avatar || !mgr) return;
    ((ConnectFn)core::rt(eng::kConnect))(mgr, nullptr, avatar, vehicle, 0, 1);
    logOnce("driver connected to rowboat (rowboatconnect10)", avatar, vehicle);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.rowboatMount10 && !c.rowboatConnect10) return true;
    detail::State& s = detail::g_state;
    s.retarget = c.rowboatMount10;
    s.connect = c.rowboatConnect10;
    // the engine functions the detour calls (H4); push / mov immediates are relocated, so wildcarded
    const bool connect = core::prologueMatches(detail::eng::kConnect, "6AFF68????????64A100000000506489");
    const bool component = core::prologueMatches(detail::eng::kComponent, "8B5424088B0D????????8B018B40106A");
    const bool index = core::prologueMatches(detail::eng::kTypeIndex, "51568B74240C56B9????????E8");
    const bool bit = core::prologueMatches(detail::eng::kHasBit, "8B4C240485C9741B8B44240885C07413");
    const bool calls = connect && component && index;
    if (!calls || !bit) {
        core::logf("rowboatmount10 off: a called function's prologue differs");
        return false;
    }
    return core::installFeature("rowboatmount10", {{"transport rule apply (rowboatmount10)",
                                                    detail::eng::kTransportApply, "6AFF68????????64A100000000506489",
                                                    (void*)&detail::hkApply, (void**)&s.g_apply}});
}

}  // namespace di1::rowboatmount

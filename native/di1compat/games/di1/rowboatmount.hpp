// rowboatmount10: a Tardis PIR_Rowboat mount goes to the player's current avatar; rowboatconnect10 (off) connects.
// core, engine/players. Game thread.
#pragma once
#include <atomic>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/enginestring.hpp"
#include "../../core/memcheck.hpp"
#include "../../engine/players.hpp"
#include "config.hpp"

namespace di1::rowboatmount {

// Installs the transport rule hook when c.rowboatMount10 or c.rowboatConnect10 is on; false when refused.
inline bool install(const Config& c);
// From the TriggerAction hook (rowboatdrive10): a host avatar that replaced the rowboat's driver gets the Drive.
inline void onAction();
// Whether the handle's actor has that ActorType UID (bitmasks.lua); false until install() checked the calls.
inline bool hasActorType(uint32_t handle, uint32_t uid);

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
// FUN_013807c0: *(0x228db58) ? its vtable +0x14 () : 0, the host player id (no this; reads its own global)
constexpr uint32_t kHostId = 0x013807C0;
constexpr uint32_t kSession = 0x0228DB58;
// The rule's own Drive send (0xdf4e64..0xdf5021), repeated for a swapped-in avatar
constexpr uint32_t kAgent = 0x0074B200;            // cdecl (handle) -> its AI agent, null when it is none
constexpr uint32_t kComponentByName = 0x00747410;  // thiscall (agent; name, 0; ret 8) -> agent component
constexpr uint32_t kHashManager = 0x0225F7E8;      // the object FUN_01534050 runs on
constexpr uint32_t kHashCreate = 0x01534050;       // thiscall (manager; CString by value; ret 4) -> hash
constexpr uint32_t kHasher = 0x0133C310;           // cdecl () -> the hash singleton
constexpr uint32_t kHashAdd = 0x01347FE0;          // thiscall (hasher; hash, key, 0, value; ret 0x10)
constexpr uint32_t kTriggerAction = 0x00C9D510;    // thiscall (agent component; action UID, hash; ret 8)
constexpr int kTargetActorKey = 593;               // key ids (sxkeys KEYS30), as the rule adds them
constexpr int kInstantKey = 252;
constexpr int kDrive = 1448;                       // AvatarAction Drive (core bitmasks.lua)
}  // namespace eng

constexpr int kMaxTries = 600;   // actions to wait for the new avatar's driver agent before giving up

typedef void(__fastcall* ApplyFn)(void* rule, void* edx, uint32_t avatar, const uint8_t* info);
typedef void(__fastcall* ConnectFn)(void* mgr, void* edx, uint32_t biped, uint32_t vehicle, uint32_t other,
                                    uint32_t driver);
typedef void*(__cdecl* ComponentFn)(uint32_t handle, uint32_t type);
typedef uint32_t(__cdecl* TypeIndexFn)(uint32_t uid);
typedef bool(__cdecl* HasBitFn)(void* mask, uint32_t index);
typedef int(__fastcall* HostIdFn)(void* unused);
typedef void*(__cdecl* AgentFn)(uint32_t handle);
typedef void*(__fastcall* ComponentByNameFn)(void* agent, void* edx, const char* name, int zero);
typedef void*(__fastcall* HashCreateFn)(void* manager, void* edx, char* name);
typedef void*(__cdecl* HasherFn)();
typedef bool(__fastcall* HashAddFn)(void* hasher, void* edx, void* hash, int key, int zero, uint32_t value);
typedef void(__fastcall* TriggerActionFn)(void* agent, void* edx, int action, void* hash);
typedef void*(__fastcall* CStringCtorFn)(char** str, void* edx, const char* s);

struct State {
    ApplyFn g_apply = nullptr;          // the original; set once in install()
    bool retarget = false;              // rowboatmount10; set once in install()
    bool connect = false;               // rowboatconnect10; set once in install()
    bool checked = false;               // every called prologue matched; set once in install()
    std::atomic<int> logged{0};
    std::atomic<uint32_t> vehicle{0};   // the last Tardis rowboat (game thread writes, script threads read)
    std::atomic<uint32_t> mounted{0};   // the avatar its mount went to
    std::atomic<int> tries{0};
    std::atomic<bool> busy{false};      // a re-mount runs: its own TriggerAction must not start another
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

// Whether the handle's actor has the ActorType UID's bit, by the rule's own calls (0xdf4f3a).
inline bool actorTypeIs(uint32_t handle, uint32_t uid)
{
    const uint16_t type = *(const uint16_t*)core::rt(eng::kTypeComponent);
    uint8_t* comp = (uint8_t*)((ComponentFn)core::rt(eng::kComponent))(handle, type);
    if (!comp || !core::readable(comp + eng::kTypeMask, sizeof(void*))) return false;
    const uint32_t index = ((TypeIndexFn)core::rt(eng::kTypeIndex))(uid);
    return ((HasBitFn)core::rt(eng::kHasBit))(*(void**)(comp + eng::kTypeMask), index);
}

inline bool isRowboat(uint32_t vehicle) { return actorTypeIs(vehicle, eng::kRowboatUid); }

// The host player's id, as Lua's Players_GetHostPlayerID (FUN_00b3ed40 -> FUN_013807c0): 0 without a session.
inline int hostPlayer()
{
    void* session = *(void**)core::rt(eng::kSession);
    return session ? ((HostIdFn)core::rt(eng::kHostId))(nullptr) : 0;
}

// The host player's avatar, as Players.GetAvatarFromPlayerId (FUN_01550120) and Crabe's GetAvatarHandle read it.
inline uint32_t hostAvatar(const uint8_t* table)
{
    const int p = hostPlayer();
    const bool inRange = p >= 0 && p < engine::players::eng::kMaxPlayers;
    return inRange ? engine::players::avatarHandle(table, p) : 0;
}

inline bool logMore() { return g_state.logged.fetch_add(1) < 8; }

// What the retarget saw, once per rowboat transport (at most 8): the rule's avatar, the host's, players 0-3.
inline void logAvatars(uint32_t rule, const uint8_t* table)
{
    if (!logMore()) return;
    uint32_t a[engine::players::eng::kMaxPlayers] = {};
    for (int p = 0; table && p < engine::players::eng::kMaxPlayers; ++p) a[p] = engine::players::avatarHandle(table, p);
    core::logf("rowboatmount10: rowboat transport for %08X; host player %d avatar %08X; avatars %08X %08X %08X %08X",
               rule, hostPlayer(), table ? hostAvatar(table) : 0, a[0], a[1], a[2], a[3]);
}

// rowboatconnect10: the driver connected as VehicleManager_BipedInteractionConnect does.
inline void connectDriver(uint32_t avatar, uint32_t vehicle)
{
    void* mgr = *(void**)core::rt(eng::kVehicleManager);
    if (!mgr || !avatar) return;
    ((ConnectFn)core::rt(eng::kConnect))(mgr, nullptr, avatar, vehicle, 0, 1);
    if (logMore()) core::logf("rowboatmount10: driver %08X connected to rowboat %08X", avatar, vehicle);
}

// FUN_00df4cb0: a rowboat mount goes to the player's avatar; in-game the rule named 0x34025000, Jack is 0x30007033.
inline void __fastcall hkApply(void* rule, void* edx, uint32_t avatar, const uint8_t* info)
{
    State& s = g_state;
    const uint32_t vehicle = vehicleOf(info);
    const bool rowboat = vehicle && isRowboat(vehicle);
    const uint8_t* pm = rowboat ? engine::players::manager() : nullptr;
    const uint8_t* table = pm ? engine::players::table(pm) : nullptr;
    if (rowboat) logAvatars(avatar, table);
    const uint32_t host = table && s.retarget ? hostAvatar(table) : 0;
    if (host && host != avatar) {
        if (logMore()) core::logf("rowboatmount10: mounting the host's avatar %08X, not %08X", host, avatar);
        avatar = host;
    }
    s.g_apply(rule, edx, avatar, info);
    if (!rowboat) return;
    s.vehicle = vehicle;
    s.mounted = avatar;
    s.tries = 0;
    if (s.connect) connectDriver(avatar, vehicle);
}

// The rule's ForceMountHash: TargetActor = vehicle, Instant = 1 (0xdf4e91..0xdf4edd).
inline void* forceMountHash(uint32_t vehicle)
{
    char* name = nullptr;
    ((CStringCtorFn)core::rt(core::eng::kCStringCtor))(&name, nullptr, "ForceMountHash");
    void* hash = ((HashCreateFn)core::rt(eng::kHashCreate))((void*)core::rt(eng::kHashManager), nullptr, name);
    if (!hash) return nullptr;
    const HashAddFn add = (HashAddFn)core::rt(eng::kHashAdd);
    add(((HasherFn)core::rt(eng::kHasher))(), nullptr, hash, eng::kTargetActorKey, 0, vehicle);
    add(((HasherFn)core::rt(eng::kHasher))(), nullptr, hash, eng::kInstantKey, 0, 1);
    return hash;
}

// Drive to rowboat sent to avatar's driver agent, as the rule sends it; false until that agent exists.
inline bool remount(uint32_t avatar, uint32_t vehicle)
{
    State& s = g_state;
    if (!((AgentFn)core::rt(eng::kAgent))(vehicle)) {
        s.vehicle = 0;
        core::logf("rowboatmount10: rowboat %08X is gone; no re-mount", vehicle);
        return true;
    }
    void* agent = ((AgentFn)core::rt(eng::kAgent))(avatar);
    void* driver = agent ? ((ComponentByNameFn)core::rt(eng::kComponentByName))(agent, nullptr, "AiAvatarDriver", 0)
                         : nullptr;
    void* hash = driver ? forceMountHash(vehicle) : nullptr;
    if (!hash) return false;
    core::logf("rowboatmount10: host avatar %08X replaced %08X: Drive to rowboat %08X re-sent", avatar,
               s.mounted.load(), vehicle);
    ((TriggerActionFn)core::rt(eng::kTriggerAction))(driver, nullptr, eng::kDrive, hash);
    if (s.connect) connectDriver(avatar, vehicle);
    return true;
}

// A called function's prologue, checked once in install() (H4); relocated immediates are ??.

// Not kTriggerAction: rowboatdrive10's row checks it before hooking, and only its detour calls onAction.
struct Called {
    uint32_t va;
    const char* prologueHex;
};
inline constexpr Called kCalled[] = {
    {eng::kConnect, "6AFF68????????64A100000000506489"}, {eng::kComponent, "8B5424088B0D????????8B018B40106A"},
    {eng::kTypeIndex, "51568B74240C56B9????????E8"}, {eng::kHasBit, "8B4C240485C9741B8B44240885C07413"},
    {eng::kHostId, "8B0D????????33C085C974078B018B50"}, {eng::kAgent, "83EC088B44240C8D0C24518B0D"},
    {eng::kComponentByName, "8B89F000000085C97405E9"}, {eng::kHashCreate, "64A1000000006AFF68????????50"},
    {eng::kHasher, "A1????????85C075226A24E8"}, {eng::kHashAdd, "53568BF18B4E0432DB85C97507"},
    {core::eng::kCStringCtor, "8B44240456508BF1E8"},
};

}  // namespace detail

inline void onAction()
{
    detail::State& s = detail::g_state;
    const uint32_t vehicle = s.vehicle.load();
    if (!vehicle || !s.retarget) return;
    const uint8_t* pm = engine::players::manager();
    const uint8_t* table = pm ? engine::players::table(pm) : nullptr;
    const uint32_t host = table ? detail::hostAvatar(table) : 0;
    const bool swapped = host && host != s.mounted.load();
    if (!swapped || s.tries.fetch_add(1) >= detail::kMaxTries || s.busy.exchange(true)) return;
    if (detail::remount(host, vehicle)) s.mounted = host;
    s.busy = false;
}

inline bool hasActorType(uint32_t handle, uint32_t uid)
{
    return handle && detail::g_state.checked && detail::actorTypeIs(handle, uid);
}

inline bool install(const Config& c)
{
    if (!c.rowboatMount10 && !c.rowboatConnect10) return true;
    detail::State& s = detail::g_state;
    s.retarget = c.rowboatMount10;
    s.connect = c.rowboatConnect10;
    for (const detail::Called& f : detail::kCalled) {
        if (core::prologueMatches(f.va, f.prologueHex)) continue;
        core::logf("rowboatmount10 off: %08X prologue differs", f.va);
        return false;
    }
    s.checked = true;
    return core::installFeature("rowboatmount10", {{"transport rule apply (rowboatmount10)",
                                                    detail::eng::kTransportApply, "6AFF68????????64A100000000506489",
                                                    (void*)&detail::hkApply, (void**)&s.g_apply}});
}

}  // namespace di1::rowboatmount

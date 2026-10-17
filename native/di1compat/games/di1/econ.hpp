// econ10 (NPC-8): 1.0's .ap key SpawnEconomyOnDeath, read into 3.0's combat getter. core. Game threads, per actor.
#pragma once
#include <windows.h>
#include <cstdint>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "config.hpp"

namespace di1::econ {

// Installs the combat getter hook when c has econ10 (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: the engine functions below are called without a prologue check (X-12)
constexpr uint32_t kComponentOf = 0x0069ECD0;       // FUN_0069ecd0, cdecl (actor, component id): its component
constexpr uint32_t kParamsComponent = 0x0220353C;   // DAT_0220353c: the params component's u16 id (as FUN_00b9ebb0)
constexpr uint32_t kHasKey = 0x006F9640;            // FUN_006f9640, thiscall (key, a2): the params have key
constexpr uint32_t kGetBool = 0x006F9610;           // FUN_006f9610, thiscall (key, a2, a3): key's bool
constexpr uint32_t kSetSpawn = 0x00FEE6B0;   // FUN_00fee6b0 (thiscall, v): Combat_SetOnDeathSpawnEconomy's setter
constexpr size_t kParams = 8;                       // a params component's parameters
constexpr size_t kDecided = 0x90c;                  // the getter decides once per actor: this byte's kDecidedBit
constexpr uint8_t kDecidedBit = 0x20;
}  // namespace eng

typedef uint32_t(__fastcall* CombatGetFn)(uint8_t* self, void* edx);

struct State {
    CombatGetFn g_econOnDeath = nullptr;   // set once by hook() in install()
};
inline State g_state;

// The actor's SpawnEconomyOnDeath, when its params have the key, set through 3.0's setter.
inline void readSpawnKey(uint8_t* self)
{
    typedef void*(__cdecl * ComponentFn)(void* actor, uint32_t id);
    typedef uint8_t(__fastcall * HasKeyFn)(void* params, void* edx, const char* key, uint32_t a2);
    typedef uint8_t(__fastcall * GetBoolFn)(void* params, void* edx, const char* key, uint32_t a2, uint32_t a3);
    typedef void(__fastcall * SetFn)(void* self, void* edx, uint32_t v);
    const ComponentFn componentOf = (ComponentFn)core::rt(eng::kComponentOf);
    void* comp = componentOf(*(void**)self, *(uint16_t*)core::rt(eng::kParamsComponent));
    void* params = comp ? (uint8_t*)comp + eng::kParams : nullptr;
    if (!params || !((HasKeyFn)core::rt(eng::kHasKey))(params, nullptr, "SpawnEconomyOnDeath", 0)) return;
    const uint8_t v = ((GetBoolFn)core::rt(eng::kGetBool))(params, nullptr, "SpawnEconomyOnDeath", 0, 0);
    ((SetFn)core::rt(eng::kSetSpawn))(self, nullptr, v);
    if (!v) core::logf("SpawnEconomyOnDeath = 0 read for actor %p", *(void**)self);
}

// FUN_00ff69c0, thiscall on a combat component: SpawnEconomyOnDeath, 1 unless decided. Game threads, per call.
inline uint32_t __fastcall hkEconOnDeath(uint8_t* self, void*)
{
    if (!(self[eng::kDecided] & eng::kDecidedBit)) readSpawnKey(self);
    return g_state.g_econOnDeath(self, nullptr);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.econ10) return true;
    return core::hook("combat SpawnEconomyOnDeath get", 0x00FF69C0, "53568BF1F6860C09000020",
                      (void*)&detail::hkEconOnDeath, (void**)&detail::g_state.g_econOnDeath);
}

}  // namespace di1::econ

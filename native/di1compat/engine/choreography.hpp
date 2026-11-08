// A choreography's start (before and after) and its updates with its clock. core.
// Start: threads unverified. Update: game thread.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include "../core/hooks.hpp"
#include "../core/memcheck.hpp"

namespace engine::choreography {

// cho: the instance; name: its path (at most 99 characters); flags: its +0x1b4 flags (0x20: audio sync).
using Notify = std::function<void(const uint8_t* cho, const char* name, uint32_t flags)>;
// cho: the instance after its update; flags: its +0x1b4 flags then (0x80: playing).
using Update = std::function<void(const uint8_t* cho, uint32_t flags)>;

struct Config {
    Notify starting;   // before the start; empty: none
    Notify started;    // after it; empty: none
    Update updated;    // after each update; empty: none
};

// The start hook for a start callback, the update hook for 'updated' (H1); false when one was refused.
inline bool install(const Config& c);
// Only inside 'updated': the playing instance's clock in seconds (1.0 FUN_00ec3820 reads it from net+0x54).
inline std::optional<float> clock(const uint8_t* cho);

namespace detail {

namespace eng {
constexpr size_t kPath = 0x38;    // the instance's path (char*)
constexpr size_t kFlags = 0x1b4;  // u32 flags
constexpr size_t kNameMax = 100;
constexpr uint32_t kUpdate = 0x0144A4D0;   // FUN_0144a4d0: the instance update (1.0 FUN_00ed4170)
constexpr uint32_t kNetOf = 0x0076FD00;    // FUN_0076fd00: thiscall on the owner -> its playback network
constexpr size_t kOwner = 0x4c;            // FUN_00895c20 returns it; FUN_01447d60 passes it to FUN_0076fd00
constexpr size_t kNetTime = 0x54;          // float: FUN_00525340 reads it between range +0x3c and +0x40
}  // namespace eng

typedef void(__fastcall* ChoStartFn)(uint8_t* self, void* edx);
typedef void(__fastcall* ChoUpdateFn)(uint8_t* self, void* edx);
typedef const uint8_t*(__fastcall* NetOfFn)(void* owner, void* edx);

struct State {
    ChoStartFn g_choStart = nullptr;     // set once by hook() in install()
    ChoUpdateFn g_choUpdate = nullptr;   // set once by hook() in install()
    NetOfFn netOf = nullptr;             // set once in install(), after its prologue check (H4)
    Config cfg;                          // set once in install()
};
inline State g_state;

// FUN_01449390, thiscall on a choreography instance: its start. Per choreography played.
inline void __fastcall hkChoStart(uint8_t* self, void*)
{
    State& s = g_state;
    char name[eng::kNameMax] = "";
    core::safeCopy(name, sizeof(name), *(const char**)(self + eng::kPath));
    const uint32_t flags = *(uint32_t*)(self + eng::kFlags);
    if (s.cfg.starting) s.cfg.starting(self, name, flags);
    s.g_choStart(self, nullptr);
    if (s.cfg.started) s.cfg.started(self, name, flags);
}

// FUN_0144a4d0, thiscall on a choreography instance: its update. Game thread, per frame per instance.
inline void __fastcall hkChoUpdate(uint8_t* self, void*)
{
    State& s = g_state;
    s.g_choUpdate(self, nullptr);
    try {
        s.cfg.updated(self, *(uint32_t*)(self + eng::kFlags));
    } catch (...) {   // H6: nothing thrown reaches the engine; the original already ran
    }
}

inline bool installUpdate()
{
    State& s = g_state;
    if (!core::prologueMatches(eng::kNetOf, "568BF18B462485C0")) {
        core::logf("hook choreography update (vosync10) refused: FUN_0076fd00's prologue differs");
        return false;
    }
    s.netOf = (NetOfFn)core::rt(eng::kNetOf);
    return core::hook("choreography update (vosync10)", eng::kUpdate, "83EC1C568BF18B86B4010000",
                      (void*)&hkChoUpdate, (void**)&s.g_choUpdate);
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    const bool start = c.starting || c.started;
    if (!start && !c.updated) return true;
    s.cfg = c;
    bool ok = true;
    if (start)
        ok = core::hook("choreography start (cinesync10 / breaktrace)", 0x01449390, "83EC18568BF1E8",
                        (void*)&detail::hkChoStart, (void**)&s.g_choStart);
    if (c.updated) ok = detail::installUpdate() && ok;
    return ok;
}

inline std::optional<float> clock(const uint8_t* cho)
{
    const detail::State& s = detail::g_state;
    void* owner = *(void* const*)(cho + detail::eng::kOwner);
    const uint8_t* net = s.netOf && owner ? s.netOf(owner, nullptr) : nullptr;
    if (!net || !core::readable(net + detail::eng::kNetTime, sizeof(float))) return std::nullopt;
    float t;
    memcpy(&t, net + detail::eng::kNetTime, sizeof t);
    return t;
}

}  // namespace engine::choreography

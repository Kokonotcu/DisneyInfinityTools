// currency10: a 1.0 playset's zone pays and shows its own currency, not INV_SPARKS. core, engine/zones. Main thread.
#pragma once
#include <windows.h>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../engine/zones.hpp"
#include "config.hpp"

namespace di1::currency {

// Installs the three currency hooks when c has currency10, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c);
// inventoryFilter -> the 1.0 playset's currency (currencies10, zones.hpp), as the zone list read finds them.
inline void setCurrencies(std::map<std::string, std::string> currencies);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kCStringAssign = 0x0041A250;   // FUN_0041a250: thiscall (const char*) on a CString
constexpr uint32_t kLuaPushString = 0x0130D840;   // FUN_0130d840: cdecl (L, s), lua_pushstring
constexpr uint32_t kInventory = 0x02289734;       // *: the inventory
constexpr uint32_t kAmount = 0x00E07F60;          // FUN_00e07f60: thiscall (name) on the inventory -> amount
constexpr uint32_t kScriptTable = 0x0154C520;     // FUN_0154c520: thiscall (name, ScriptRef* out) on a script
constexpr uint32_t kRefField = 0x004510B0;        // FUN_004510b0: thiscall (ScriptRef* out, key) on a ScriptRef
constexpr uint32_t kLuaToInteger = 0x0130D530;    // FUN_0130d530: cdecl (L, idx)
constexpr uint32_t kLuaRelease = 0x0130E4D0;      // FUN_0130e4d0: cdecl (L, idx)
constexpr uint32_t kSetString = 0x00B3F0A0;       // FUN_00b3f0a0: thiscall (const char** key, const char** value)
constexpr uint32_t kSetBool = 0x00B3F060;         // FUN_00b3f060: thiscall (const char** key, uint8_t* value)
constexpr uint32_t kSetInt = 0x0154C100;          // FUN_0154c100: thiscall (key, int) on the HUD data
constexpr size_t kHudScript = 0x5C;               // the HUD's script
constexpr size_t kHudForce = 0x87;                // the HUD forces its info update
constexpr uint32_t kNoRef = 0xFFE17B81u;          // an empty ScriptRef's index, as the engine starts one
}  // namespace eng

// A Lua reference: state and index.
struct ScriptRef {
    void* L;
    uint32_t idx;
};

typedef void(__fastcall* ZoneCurrencyFn)(void* self, void* edx, const char* filter, void* out);
typedef int(__fastcall* PlaysetCurrencyFn)(void* self, void* edx, void* L);
typedef uint8_t(__fastcall* HudCurrencyFn)(uint8_t* hud, void* edx, void* data);

struct State {
    // originals: set once by hook() in install()
    ZoneCurrencyFn g_zoneCurrency = nullptr;
    PlaysetCurrencyFn g_playsetCurrency = nullptr;
    HudCurrencyFn g_hudCurrency = nullptr;
    std::mutex mx;                                   // guards currencies and told (its lines are logged under it)
    std::map<std::string, std::string> currencies;   // inventoryFilter -> currency
    core::LogOnce<std::string> told;                 // (asker, filter) pairs logged
};
inline State g_state;

// The 1.0 currency of an inventory filter; "" for every other filter.
inline std::string currency(const char* filter, const char* who)
{
    State& s = g_state;
    if (!filter || !*filter) return {};
    std::lock_guard<std::mutex> lock(s.mx);
    auto it = s.currencies.find(filter);
    if (it == s.currencies.end()) return {};
    if (s.told.first(std::string(who) + filter))
        core::logf("currency10: %s currency for %s -> %s", who, filter, it->second.c_str());
    return it->second;
}

// The current zone's: its inventoryFilter as FUN_00e27ba0 and FUN_00b54b50 read it.
inline std::string zoneCurrency(const char* who)
{
    void* zm = engine::zones::manager();
    uint32_t zone = engine::zones::currentZone(zm);
    if (!zone) return {};
    return currency(engine::zones::zoneString(zm, "inventoryFilter", zone, ""), who);
}

// FUN_00e21850, thiscall (filter, CString* out; ret 8): the currency of an inventory filter. Per query.
inline void __fastcall hkZoneCurrency(void* self, void*, const char* filter, void* out)
{
    std::string cur = currency(filter, "zone");
    if (cur.empty()) return g_state.g_zoneCurrency(self, nullptr, filter, out);
    ((void(__fastcall*)(void*, void*, const char*))core::rt(eng::kCStringAssign))(out, nullptr, cur.c_str());
}

// FUN_00b54b50, thiscall (L; ret 4): UI_GetPlaysetCurrency, the Toy Store's and the HUD script's. Per Lua call.
inline int __fastcall hkPlaysetCurrency(void* self, void*, void* L)
{
    std::string cur = zoneCurrency("Toy Store / UI_GetPlaysetCurrency");
    if (cur.empty()) return g_state.g_playsetCurrency(self, nullptr, L);
    ((void(__cdecl*)(void*, const char*))core::rt(eng::kLuaPushString))(L, cur.c_str());
    return 1;
}

// The amount the HUD's PlayerInfo table shows (currencyAmt).
inline int shownAmount(ScriptRef& info)
{
    typedef ScriptRef*(__fastcall * FieldFn)(ScriptRef* ref, void* edx, ScriptRef* out, const char* key);
    ScriptRef f{nullptr, 0};
    ScriptRef* got = ((FieldFn)core::rt(eng::kRefField))(&info, nullptr, &f, "currencyAmt");
    int shown = ((int(__cdecl*)(void*, uint32_t))core::rt(eng::kLuaToInteger))(got->L, got->idx);
    if (f.L) ((void(__cdecl*)(void*, uint32_t))core::rt(eng::kLuaRelease))(f.L, f.idx);
    return shown;
}

// currencyIcon (the currency less "INV_"), dirty, PlayerCurrencyAmt: the HUD's own update steps.
inline void showCurrency(ScriptRef& info, void* data, const std::string& cur, int amt, uint8_t* changed)
{
    std::string icon = cur.compare(0, 4, "INV_") ? cur : cur.substr(4);
    const char* key = "currencyIcon";
    const char* val = icon.c_str();
    ((void(__fastcall*)(ScriptRef*, void*, const char**, const char**))core::rt(eng::kSetString))(&info, nullptr, &key,
                                                                                                 &val);
    *changed = 1;
    key = "dirty";
    typedef void(__fastcall * SetBoolFn)(ScriptRef* ref, void* edx, const char** key, uint8_t* value);
    typedef void(__fastcall * SetIntFn)(void* data, void* edx, const char* key, int value);
    ((SetBoolFn)core::rt(eng::kSetBool))(&info, nullptr, &key, changed);
    ((SetIntFn)core::rt(eng::kSetInt))(data, nullptr, "PlayerCurrencyAmt", amt);
}

// FUN_00fbb420, thiscall (data; ret 4): the HUD's currency, INV_SPARKS fixed; a 1.0 zone's shows its own. Per update.
inline uint8_t __fastcall hkHudCurrency(uint8_t* hud, void*, void* data)
{
    std::string cur = zoneCurrency("HUD");
    if (cur.empty() || cur == "INV_SPARKS") return g_state.g_hudCurrency(hud, nullptr, data);
    typedef int(__fastcall * AmountFn)(void* inventory, void* edx, const char* name);
    typedef void(__fastcall * TableFn)(void* script, void* edx, const char* name, ScriptRef* out);
    int amt = ((AmountFn)core::rt(eng::kAmount))(*(void**)core::rt(eng::kInventory), nullptr, cur.c_str());
    ScriptRef info{nullptr, eng::kNoRef};
    ((TableFn)core::rt(eng::kScriptTable))(*(void**)(hud + eng::kHudScript), nullptr, "PlayerInfo", &info);
    int shown = shownAmount(info);
    uint8_t changed = 0;
    if (shown != amt || hud[eng::kHudForce]) showCurrency(info, data, cur, amt, &changed);
    if (info.L) ((void(__cdecl*)(void*, uint32_t))core::rt(eng::kLuaRelease))(info.L, info.idx);
    return changed;
}

}  // namespace detail

inline void setCurrencies(std::map<std::string, std::string> currencies)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> lock(s.mx);
    s.currencies = std::move(currencies);
}

inline bool install(const Config& c)
{
    if (!c.currency10) return true;
    detail::State& s = detail::g_state;
    return core::installFeature("currency10", {
        {"zone currency (currency10)", 0x00E21850, "8B4C240868????????E8", (void*)&detail::hkZoneCurrency,
         (void**)&s.g_zoneCurrency},
        {"UI_GetPlaysetCurrency (currency10)", 0x00B54B50, "6AFF68????????64A100000000",
         (void*)&detail::hkPlaysetCurrency, (void**)&s.g_playsetCurrency},
        {"HUD currency (currency10)", 0x00FBB420, "6AFF68????????64A100000000", (void*)&detail::hkHudCurrency,
         (void**)&s.g_hudCurrency},
    });
}

}  // namespace di1::currency

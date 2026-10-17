// currencytrace10: currency payouts, pickup awards and the balances read, logged. core. Game threads, log only.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"
#include "../core/stack.hpp"

namespace trace::currency {

struct Config {
    bool on = false;   // currencytrace10
};

// Installs the five probes when c asks for them, all or none (H1, H2, H4); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
constexpr uint32_t kRecordByUid = 0x0070F700;   // FUN_0070f700: cdecl (uid) -> a GameDB record
constexpr const char* kRecordByUidPrologue = "51568B74240C56B9";
constexpr uint32_t kCurrencyType = 0x4BE;       // InventoryType 'CURRENCY' (bitmasks.lua UID 1214), as FUN_014b4b40
constexpr size_t kItemName = 0x10;              // InventoryItem: name, a CString (FUN_018db5b0 'name')
constexpr size_t kItemType = 0x2C;              // InventoryItem: its InventoryType record (FUN_014bf7f0 'type')
constexpr size_t kAwardKind = 0;                // a queued pickup award (FUN_00ba6a30): FUN_00ba1270's kind
constexpr size_t kAwardPlayer = 4;
constexpr size_t kAwardValue = 8;               // float
constexpr size_t kAwardStat = 0x10;             // char*: the Stat kind's stat (FUN_00ba13f0)
constexpr uint32_t kStatKind = 6;
}  // namespace eng

constexpr LONG kLines = 5000;   // log budget of the run
constexpr int kStackRets = 4;   // exe return addresses a payout line lists
constexpr int kStackRetChars = 9;   // core::stackOf writes each as " %08X"
constexpr int kReads = 64;      // (item, caller) balance reads followed
constexpr size_t kName = 64;

// FUN_00ba1270's kinds by number; 0 is any other Type, 'Playset' in 3.0's editor list (expendableproperties.lua).
inline constexpr const char* kKinds[] = {"Playset", "Spark", "SparkUnmetered", "Experience", "Health", "Super",
                                         "Stat"};

// An exe function by its static VA and size (3.0 dump), named for the log.
struct Source {
    uint32_t start, size;
    const char* name;
};

// Payers: the first stack return address inside one names the payout's source. First match wins.
inline constexpr Source kPayers[] = {
    {0x00E27C90, 508, "coin flush (FUN_00e27c90, the zone's currency)"},
    {0x00A5EC60, 24, "AwardSparks (Spark pickup)"},
    {0x015A50A0, 502, "mission Inventory reward (FUN_015a50a0)"},
    {0x015A4BD0, 35, "mission StatEvent reward (FUN_015a4bd0)"},
    {0x015A49F0, 473, "mission StatEvent reward (FUN_015a49f0)"},
    {0x00B4EB90, 36, "Lua call (UI_ChangePelletCount: store charge)"},
    {0x00FD43B0, 486, "Toy Box purchase (FUN_00fd43b0)"},
};

// Readers whose balance reads are followed: the immediate caller of FUN_014b4b40.
inline constexpr Source kReaders[] = {
    {0x00E07F60, 34, "amount by name (FUN_00e07f60: the HUD, among others)"},
    {0x014B8930, 1380, "item details (FUN_014b8930: UI_GetItemDetailsForPlayer, the store)"},
};

// One followed balance: the last amount logged for an item one reader read.
struct Read {
    const void* item = nullptr;
    uint32_t caller = 0;
    bool currency = false;
    uint32_t last = 0;
};

typedef void(__fastcall* ItemAddFn)(uint8_t* item, void* edx, float amount);
typedef void(__fastcall* ItemChangeFn)(uint8_t* item, void* edx, int delta);
typedef uint32_t(__fastcall* ItemAmountFn)(uint8_t* item, void* edx);
typedef void(__fastcall* AwardFn)(uint8_t* award, void* edx);
typedef uint8_t(__fastcall* StatEventGrantFn)(void* self, void* edx, uint32_t a1, uint32_t a2);

struct State {
    // originals: set once by installFeature in install()
    ItemAddFn g_itemAdd = nullptr;
    ItemChangeFn g_itemChange = nullptr;
    ItemAmountFn g_itemAmount = nullptr;
    AwardFn g_award = nullptr;
    StatEventGrantFn g_statEventGrant = nullptr;
    std::atomic<LONG> lines{0};   // budget; any thread
    std::mutex readsMx;           // guards reads; try_lock only: HUD reads run per update (L4)
    Read reads[kReads];
};
inline State g_state;

inline bool budget() { return ++g_state.lines <= kLines; }

// The source va lies in, from table; null when none.
template <size_t N>
inline const char* sourceOf(const Source (&table)[N], uint32_t va)
{
    for (const Source& s : table)
        if (va - s.start < s.size) return s.name;
    return nullptr;
}

// The payer named by the first address of a stackOf text that lies in kPayers; "?" when none does.
inline const char* payer(const char* stack)
{
    const char* p = stack;
    for (char* end = nullptr;; p = end) {
        const uint32_t va = strtoul(p, &end, 16);
        if (end == p) return "?";
        if (const char* name = sourceOf(kPayers, va)) return name;
    }
}

// A payout to an item: its name, change, balance after, the payer and the stack it was found on.
inline void logPayout(uint8_t* item, const char* how, double amount, const uint32_t* sp)
{
    char name[kName], stack[kStackRets * kStackRetChars + 1] = "";
    core::cstr(item + eng::kItemName, name, sizeof(name));
    core::stackOf(sp, stack, sizeof(stack), 0, kStackRets);
    const uint32_t now = g_state.g_itemAmount(item, nullptr);
    core::logf("currencytrace10: %s %s %+.0f -> balance %u, by %s; stack%s", how, name, amount, now, payer(stack),
               stack);
}

// FUN_014b4c00, thiscall on an InventoryItem (float; ret 4): adds to its amount. Game threads, per payout.
inline void __fastcall hkItemAdd(uint8_t* item, void*, float amount)
{
    g_state.g_itemAdd(item, nullptr, amount);
    if (budget()) logPayout(item, "add", amount, (const uint32_t*)_AddressOfReturnAddress());
}

// FUN_014bb210, thiscall on an InventoryItem (int; ret 4): changes its count. Game threads, per payout or charge.
inline void __fastcall hkItemChange(uint8_t* item, void*, int delta)
{
    g_state.g_itemChange(item, nullptr, delta);
    if (budget()) logPayout(item, "change", delta, (const uint32_t*)_AddressOfReturnAddress());
}

// Whether FUN_014b4b40 counts an item as a currency: type CURRENCY (the record given), or the name INV_SPARKS.
inline bool currencyItem(const uint8_t* item, const char* name, const void* currency)
{
    const bool typed = currency && *(void* const*)(item + eng::kItemType) == currency;
    return typed || !_stricmp(name, "INV_SPARKS");
}

// The followed read of (item, caller): found, or a free slot claimed (fresh); null when every slot is taken.
inline Read* readOf(const void* item, uint32_t caller, bool& fresh)
{
    for (Read& r : g_state.reads) {
        fresh = !r.item;
        if (fresh) {
            r.item = item;
            r.caller = caller;
            return &r;
        }
        if (r.item == item && r.caller == caller) return &r;
    }
    return nullptr;
}

// A currency balance a reader read, logged on its first read and whenever the amount differs. No engine call held.
inline void followRead(uint8_t* item, uint32_t caller, uint32_t amount, const void* currency)
{
    std::unique_lock<std::mutex> lock(g_state.readsMx, std::try_to_lock);
    bool fresh = false;
    Read* r = lock.owns_lock() ? readOf(item, caller, fresh) : nullptr;
    if (!r) return;
    const bool ignored = !fresh && !r->currency;
    const bool same = !fresh && r->last == amount;
    if (ignored || same) return;
    char name[kName];
    core::cstr(item + eng::kItemName, name, sizeof(name));
    if (fresh) r->currency = currencyItem(item, name, currency);
    r->last = amount;
    if (!r->currency || !budget()) return;
    core::logf("currencytrace10: balance %s = %u, read by %s", name, amount, sourceOf(kReaders, caller));
}

// FUN_014b4b40, thiscall on an InventoryItem: its amount, a GLOBAL stat for a currency. Game threads, per HUD update.
inline uint32_t __fastcall hkItemAmount(uint8_t* item, void*)
{
    const uint32_t amount = g_state.g_itemAmount(item, nullptr);
    const uint32_t caller = core::toStaticVa((uintptr_t)_ReturnAddress());
    if (!sourceOf(kReaders, caller)) return amount;
    // the lookup FUN_014b4b40 makes on every call
    const void* currency = ((void*(__cdecl*)(uint32_t))core::rt(eng::kRecordByUid))(eng::kCurrencyType);
    followRead(item, caller, amount, currency);
    return amount;
}

// FUN_00ba6a30, thiscall on a queued pickup award: pays it by kind. Game thread, per collected pickup.
inline void __fastcall hkAward(uint8_t* award, void*)
{
    const uint32_t kind = *(uint32_t*)(award + eng::kAwardKind);
    const uint32_t player = *(uint32_t*)(award + eng::kAwardPlayer);
    const float value = *(float*)(award + eng::kAwardValue);
    char stat[kName] = "";
    if (kind == eng::kStatKind) core::safeCopy(stat, sizeof(stat), *(const char**)(award + eng::kAwardStat));
    g_state.g_award(award, nullptr);
    if (!budget()) return;
    const char* k = kind < sizeof(kKinds) / sizeof(*kKinds) ? kKinds[kind] : "?";
    core::logf("currencytrace10: pickup %s (kind %u) player %u value %.0f%s%s", k, kind, player, value,
               *stat ? " stat " : "", stat);
}

// FUN_015a4bd0, thiscall on a StatEvent reward (a1, a2; ret 8): its grant, paid when a2. Mission thread, per reward.
inline uint8_t __fastcall hkStatEventGrant(void* self, void*, uint32_t a1, uint32_t a2)
{
    if (budget()) core::logf("currencytrace10: mission StatEvent reward %p granted (a2 %u)", self, a2);
    return g_state.g_statEventGrant(self, nullptr, a1, a2);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    if (!core::prologueMatches(detail::eng::kRecordByUid, detail::eng::kRecordByUidPrologue)) {
        core::logf("feature currencytrace10 off: FUN_0070f700's prologue differs");
        return false;
    }
    detail::State& s = detail::g_state;
    return core::installFeature("currencytrace10", {
        {"InventoryItem add (currencytrace10)", 0x014B4C00, "5668BE0400008BF1E8", (void*)&detail::hkItemAdd,
         (void**)&s.g_itemAdd},
        {"InventoryItem change (currencytrace10)", 0x014BB210, "5668BE0400008BF1E8", (void*)&detail::hkItemChange,
         (void**)&s.g_itemChange},
        {"InventoryItem amount (currencytrace10)", 0x014B4B40, "83EC1053565768BE040000",
         (void*)&detail::hkItemAmount, (void**)&s.g_itemAmount},
        {"pickup award (currencytrace10)", 0x00BA6A30, "568BF18B0D????????85C9", (void*)&detail::hkAward,
         (void**)&s.g_award},
        {"StatEvent reward grant (currencytrace10)", 0x015A4BD0, "568BF18B4C240C85C9",
         (void*)&detail::hkStatEventGrant, (void**)&s.g_statEventGrant},
    });
}

}  // namespace trace::currency

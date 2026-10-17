// trace probe: playset figure flow, avatar spawn, SubNetwork instancing, header clones. core, base. Threads unverified.
#pragma once
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include "../base/text.hpp"
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"
#include "../core/stack.hpp"
#include "../core/veh.hpp"
#include "start.hpp"

namespace trace::spawn {

struct Config {
    bool on = false;       // trace
    bool hidePs = false;   // hide_ps (experiment): the actor registry denies AV_<name>_PS, so avatars fall back
};

// Installs the probes when c asks for them, all or none (H1, H2); false when they were refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kResolveHandle = 0x004B46D0;   // FUN_004b46d0: thiscall on a handle field -> its object, 0 if dead
constexpr uint32_t kAvatarSpawned = 0x00A9B3E0;   // FUN_00a9b3e0: the avatar spawn's completion callback
constexpr uint32_t kAvatarSpawning = 0x02212BC4;  // *: AvatarSpawning
constexpr uint32_t kCloneSlots = 0x021B725C;      // *: temp slot count (FUN_004b4470)
constexpr uint32_t kCloneSlotTable = 0x021AF260;  // DAT_021af260: header temp slots
constexpr size_t kFigureSlots = 0xc;              // playset flow +0xc: player figure slots, +0xc + player * 4
// SubNetwork template (FUN_004ff0e0): +0xc root handle, +0x4c block, +0x5c headers, +0x60 procs, +0x64 conv
constexpr size_t kTmplRoot = 0xc;
constexpr size_t kTmplBlock = 0x4c;
constexpr size_t kTmplHeaders = 0x5c;
constexpr size_t kTmplProcs = 0x60;
constexpr size_t kTmplConv = 0x64;
constexpr size_t kHeaderName = 0xc;               // a header name object's text
constexpr size_t kLevelState = 0x2c;              // level loader state (FUN_00d00530)
constexpr size_t kLoadDoneFlag = 0x21;            // FUN_00ccc450's this: +0x21, +0xc
constexpr size_t kLoadDoneWord = 0xc;
constexpr size_t kAacCallback = 0x78;             // AsyncActorCreator +0x78 its completion callback, +0x18 mode
constexpr size_t kAacMode = 0x18;
constexpr size_t kSpawningReady = 0x88;           // AvatarSpawning +0x88; record +0x31
constexpr size_t kRecordFlag = 0x31;
// instance header [name, parent | rc, sibling | flags, child-or-self | slot, ...], 0x14 bytes
constexpr size_t kHeaderBytes = 0x14;
constexpr uint32_t kSlotMask = 0xe001ffff;
constexpr int kSlotShift = 17;
constexpr size_t kReaderItemWords = 20;           // reader list items 0x50 B from out+4: +0 type, +4 sku
}  // namespace eng

constexpr int kReaderItemsListed = 12;
constexpr LONG kHeadersMadeLogged = 6;
constexpr int kHeaderMakeStack = 20;
constexpr int kRemoveStack = 12;
constexpr int kPostsPerName = 3;
constexpr LONG kChoHasLogged = 6;
constexpr LONG kChoMakeLogged = 20;
constexpr LONG kClonesLogged = 6;
constexpr LONG kLocatorEvery = 100;
constexpr LONG kSpawnExpEvery = 50;

typedef uint8_t(__fastcall* ActorExistsFn)(void* self, void* edx, const char** name);
typedef uint8_t(__fastcall* FigureOkFn)(void* self, void* edx, uint32_t player, int sku, int* code, uint32_t a4,
                                        uint32_t a5);
typedef int(__fastcall* ReaderListFn)(void* self, void* edx, int* out);
typedef void*(__fastcall* IgpNameFn)(void* self, void* edx, void* out, uint32_t player, uint32_t flag);
typedef uint8_t(__fastcall* ReaderItemOkFn)(void* self, void* edx, uint32_t type, int sku, uint32_t a3, uint32_t a4,
                                            uint32_t a5);
typedef const char**(__fastcall* FigurePlaysetFn)(void* self, void* edx, const char** out, int sku);
typedef void*(__fastcall* AssetInstFn)(void* self, void* edx, void* out, const char** name, uint32_t a3);
typedef int(__fastcall* InstFn)(void* self, void* edx, uint32_t p1, uint32_t root, int no);
typedef void*(__fastcall* HdrMakeFn)(void* self, void* edx, void* out, uint32_t name, uint32_t parent, uint32_t a4,
                                     uint32_t a5);
typedef void(__fastcall* PostFn)(void* self, void* edx, const char* name, uint32_t a2, uint32_t a3);
typedef void(__fastcall* ThisFn)(void* self, void* edx);
typedef uint8_t(__fastcall* BoolThisFn)(void* self, void* edx);
typedef void(__fastcall* OneArgFn)(void* self, void* edx, uint32_t a1);
typedef uint8_t(__fastcall* ChoHasFn)(void* self, void* edx, uint32_t a1, const char* name);
typedef void(__fastcall* AvCreateFn)(void* self, void* edx, uint32_t player, uint32_t a2, uint32_t a3, uint32_t a4,
                                     uint32_t a5);
typedef void(__cdecl* AvSpawnedFn)(uint32_t actor, uint8_t* rec);
typedef uint32_t*(__fastcall* ChoMakeFn)(void* self, void* edx, uint32_t* out, const char* name, uint32_t a3,
                                         uint32_t a4, uint32_t a5);
typedef void(__cdecl* PlaceToyFn)(void* msg);
typedef void*(__cdecl* NodeMakeFn)(void* mem, uint32_t a2, uint32_t a3);
typedef void(__fastcall* HdrCloneFn)(void* self, void* edx, uint32_t index, uint32_t a2, int delta, uint32_t instRoot);
typedef void*(__fastcall* ResolveFn)(void* handleField, void* edx);

struct State {
    // originals: set once by hook() in install()
    ActorExistsFn g_actorExists = nullptr;
    FigureOkFn g_figureOk = nullptr;
    ReaderListFn g_readerList = nullptr;
    IgpNameFn g_igpName = nullptr;
    ReaderItemOkFn g_readerItemOk = nullptr;
    FigurePlaysetFn g_figurePlayset = nullptr;
    AssetInstFn g_assetInst = nullptr;
    InstFn g_inst = nullptr;
    HdrMakeFn g_hdrMake = nullptr;
    PostFn g_post = nullptr;
    ThisFn g_levelTick = nullptr, g_aacExec = nullptr;
    BoolThisFn g_loadDone = nullptr;
    OneArgFn g_avFinal = nullptr, g_avRemove = nullptr;
    ChoHasFn g_choHas = nullptr;
    AvCreateFn g_avCreate = nullptr;
    AvSpawnedFn g_avSpawned = nullptr;
    ChoMakeFn g_choMake = nullptr;
    PlaceToyFn g_placeToy = nullptr;
    NodeMakeFn g_mkStart = nullptr, g_mkLocator = nullptr, g_mkSpawnExp = nullptr;
    HdrCloneFn g_hdrClone = nullptr;
    bool hidePs = false;   // set once in install()
    // unguarded, as before: these hooks' threads are unverified (A-42)
    std::string readerLast, itemLast;   // the last reader list and item gate lines
    uint32_t instHeaders = 0;           // header count of the template being instanced, for the clone lines
    int levelState = -1;
    int loadDoneLast = -1;
    std::mutex postMx;                  // guards postSeen; post lines are logged under it
    std::map<std::string, int> postSeen;
    std::atomic<LONG> headersMade{0}, choHas{0}, choMake{0}, clones{0}, starts{0}, locators{0}, spawnExps{0};
};
inline State g_state;

// FUN_00751750, thiscall (ret 4): the actor registry's "does <name> exist" (the playset avatar choice asks it).
inline uint8_t __fastcall hkActorExists(void* self, void*, const char** name)
{
    State& s = g_state;
    uint8_t r = s.g_actorExists(self, nullptr, name);
    char n[200];
    core::safeCopy(n, sizeof(n), name && *name ? *name : "(null)");
    if (s.hidePs && text::endsWith(n, "_PS") && !strncmp(n, "AV_", 3)) r = 0;
    core::logf("actor exists? %s -> %u", n, r);
    return r;
}

// FUN_00ddae30, thiscall (player, sku, int* failCode, a4, a5; ret 0x14): the playset flow's figure check.
inline uint8_t __fastcall hkFigureOk(void* self, void*, uint32_t player, int sku, int* code, uint32_t a4, uint32_t a5)
{
    int mine = -1;
    uint8_t r = g_state.g_figureOk(self, nullptr, player, sku, code ? code : &mine, a4, a5);
    core::logf("figure check player %u sku %d (a4 %u a5 %u) -> %u code %d", player, sku, a4 & 0xff, a5 & 0xff, r,
               code ? *code : mine);
    return r;
}

// FUN_00427330, thiscall (out; ret 4): the reader item list the figure slots fill from; logged on change.
inline int __fastcall hkReaderList(void* self, void*, int* out)
{
    State& s = g_state;
    int n = s.g_readerList(self, nullptr, out);   // reader slot = this + 0x6c + index * 0x260, its toy id at +0x168
    std::string line = std::to_string(n) + ":";
    for (int i = 0; i < n && i < kReaderItemsListed; ++i) {
        const int* item = out + 1 + i * eng::kReaderItemWords;
        line += " " + std::to_string(item[0]) + "/" + std::to_string(item[1]);
    }
    if (line == s.readerLast) return n;
    s.readerLast = line;
    core::logf("reader list %s", line.c_str());
    return n;
}

// FUN_00df9fd0, thiscall (out, player, flag; ret 0xc): reads a player's figure slot; arms the DR0 watchpoint on it.
inline void* __fastcall hkIgpName(void* self, void*, void* out, uint32_t player, uint32_t flag)
{
    char* slots = self ? *(char**)((char*)self + eng::kFigureSlots) : nullptr;
    if (slots) core::watch((uintptr_t)slots + eng::kFigureSlots);
    return g_state.g_igpName(self, nullptr, out, player, flag);
}

// FUN_00deb610, thiscall (type, sku, a3, a4, a5; ret 0x14): a reader item's gate; logged on change.
inline uint8_t __fastcall hkReaderItemOk(void* self, void*, uint32_t type, int sku, uint32_t a3, uint32_t a4,
                                         uint32_t a5)
{
    State& s = g_state;
    uint8_t r = s.g_readerItemOk(self, nullptr, type, sku, a3, a4, a5);
    std::string line = std::to_string(type) + "/" + std::to_string(sku) + " -> " + std::to_string(r);
    if (line == s.itemLast) return r;
    s.itemLast = line;
    core::logf("reader item %s", line.c_str());
    return r;
}

// FUN_00435080, thiscall (out, sku; ret 8): the figure's playset string Gateway[sku].playset.
inline const char** __fastcall hkFigurePlayset(void* self, void*, const char** out, int sku)
{
    const char** r = g_state.g_figurePlayset(self, nullptr, out, sku);
    char str[200];
    core::safeCopy(str, sizeof(str), out && *out ? *out : "(null)");
    core::logf("Gateway[%d].playset = '%s'", sku, str);
    return r;
}

// FUN_007734f0, thiscall (out, const char** subName, a3; ret 0xc): an entity instances a SubNetwork ("" = root).
inline void* __fastcall hkAssetInst(void* self, void*, void* out, const char** name, uint32_t a3)
{
    char n[200];
    core::safeCopy(n, sizeof(n), name && *name ? *name : "(null)");
    core::logf("instance asset '%s' of entity %p", n, self);
    return g_state.g_assetInst(self, nullptr, out, name, a3);
}

// FUN_004ff0e0, thiscall on the template (ret 0xc): SubNetwork instancing. A dead root handle is skipped (the guard).
inline int __fastcall hkInst(void* self, void*, uint32_t p1, uint32_t root, int no)
{
    State& s = g_state;
    void* r = ((ResolveFn)core::rt(eng::kResolveHandle))((char*)self + eng::kTmplRoot, nullptr);
    const uint8_t* t = (const uint8_t*)self;
    if (!r) {
        // FUN_004f5900 faults on it (NULL+8); FUN_007734f0 handles a failed instance
        core::logf("instancing template %p with a dead root handle %08X (block %08X, headers %u, conv %u, procs %u): "
                   "skipped", self, *(const uint32_t*)(t + eng::kTmplRoot), *(const uint32_t*)(t + eng::kTmplBlock),
                   *(const uint32_t*)(t + eng::kTmplHeaders), *(const uint32_t*)(t + eng::kTmplConv),
                   *(const uint32_t*)(t + eng::kTmplProcs));
        return 0;
    }
    s.instHeaders = *(const uint32_t*)(t + eng::kTmplHeaders);
    return s.g_inst(self, nullptr, p1, root, no);
}

// The adapter name of a header being made ("" if unreadable); a function of its own for the __try (H7).
inline void headerName(uint32_t name, char* n, size_t cap)
{
    __try {
        if (name) core::safeCopy(n, cap, (const char*)(uintptr_t)(name + eng::kHeaderName));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Whether c is an ASCII capital.
inline bool capital(char c) { return c >= 'A' && c <= 'Z'; }

// FUN_004fae90, thiscall on the allocator (ret 0x14): header creation; the runtime 'As...' adapters with a stack.
inline void* __fastcall hkHdrMake(void* self, void*, void* out, uint32_t name, uint32_t parent, uint32_t a4,
                                  uint32_t a5)
{
    State& s = g_state;
    char n[80] = "";
    headerName(name, n, sizeof(n));
    const bool adapter = n[0] == 'A' && n[1] == 's' && capital(n[2]);
    if (adapter) {
        const LONG nth = ++s.headersMade;
        if (nth <= kHeadersMadeLogged) {
            char line[700];
            // never cut: at most 162 characters before the stack's 180
            int len = snprintf(line, sizeof(line), "make header '%s' (allocator %p, parent %08X, %u %u), stack:", n,
                               self, parent, a4, a5);
            core::stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, kHeaderMakeStack);
            core::logf("%s", line);
        }
    }
    return s.g_hdrMake(self, nullptr, out, name, parent, a4, a5);
}

// Whether an event name is one of the spawn path's (zone, avatar, creation, toy, spawn, level).
inline bool spawnEvent(const char* n)
{
    static const char* const kWords[] = {"Zone", "Avatar", "Creation", "Toy", "Spawn", "Level"};
    for (const char* w : kWords)
        if (strstr(n, w)) return true;
    return false;
}

// FUN_0068bbb0, thiscall on the event manager (name, a2, a3; ret 0xc): event post; each spawn name's first 3.
inline void __fastcall hkPost(void* self, void*, const char* name, uint32_t a2, uint32_t a3)
{
    State& s = g_state;
    char n[64] = "";
    core::safeCopy(n, sizeof(n), name);
    if (spawnEvent(n)) {
        std::lock_guard<std::mutex> lock(s.postMx);
        int& seen = s.postSeen[n];
        ++seen;
        if (seen <= kPostsPerName) core::logf("event %s (%08X %08X)", n, a2, a3);
    }
    s.g_post(self, nullptr, name, a2, a3);
}

// FUN_00d00530, thiscall: the level loader tick; its state (+0x2c) changes.
inline void __fastcall hkLevelTick(void* self, void*)
{
    State& s = g_state;
    int st = *(int*)((char*)self + eng::kLevelState);
    if (st != s.levelState) {
        core::logf("level loader %p state %d -> %d", self, s.levelState, st);
        s.levelState = st;
    }
    s.g_levelTick(self, nullptr);
    st = *(int*)((char*)self + eng::kLevelState);
    if (st == s.levelState) return;
    core::logf("level loader %p state -> %d", self, st);
    s.levelState = st;
}

// FUN_00ccc450, thiscall: the level load-done check; its result changes.
inline uint8_t __fastcall hkLoadDone(void* self, void*)
{
    State& s = g_state;
    uint8_t r = s.g_loadDone(self, nullptr);
    if (r == s.loadDoneLast) return r;
    const uint8_t* o = (const uint8_t*)self;
    core::logf("level load-done check %p -> %u (+0x21 %u, +0xc %08X)", self, r, o[eng::kLoadDoneFlag],
               *(const uint32_t*)(o + eng::kLoadDoneWord));
    s.loadDoneLast = r;
    return r;
}

// FUN_00aa36f0, thiscall (record): AvatarSpawning finalize. Rare.
inline void __fastcall hkAvFinal(void* self, void*, uint32_t a1)
{
    core::logf("AvatarSpawning finalize %p rec %08X", self, a1);
    g_state.g_avFinal(self, nullptr, a1);
}

// FUN_00a9ec70, thiscall (player): AvatarSpawning removes a player's avatar; logged with its stack. Rare.
inline void __fastcall hkAvRemove(void* self, void*, uint32_t a1)
{
    char line[400];
    // never cut: at most 58 characters before the stack's 108
    int len = snprintf(line, sizeof(line), "AvatarSpawning remove avatar of player %u, stack:", a1);
    core::stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, kRemoveStack);
    core::logf("%s", line);
    g_state.g_avRemove(self, nullptr, a1);
}

// FUN_014835c0, thiscall (2 args): the creation cho's 'Cho Controls' query (FUN_00a9f080 asks before PlaceToy).
inline uint8_t __fastcall hkChoHas(void* self, void*, uint32_t a1, const char* name)
{
    State& s = g_state;
    uint8_t r = s.g_choHas(self, nullptr, a1, name);
    char n[64] = "";
    core::safeCopy(n, sizeof(n), name);
    if (strcmp(n, "Cho Controls")) return r;
    const LONG nth = ++s.choHas;
    if (nth <= kChoHasLogged) core::logf("cho %p has '%s' (a1 %08X) -> %u", self, n, a1, r);
    return r;
}

// FUN_00a9f080, thiscall (player, 4 args): AvatarSpawning create. Rare.
inline void __fastcall hkAvCreate(void* self, void*, uint32_t player, uint32_t a2, uint32_t a3, uint32_t a4,
                                  uint32_t a5)
{
    core::logf("AvatarSpawning create player %u (%08X %08X %08X %08X) ->", player, a2, a3, a4, a5);
    g_state.g_avCreate(self, nullptr, player, a2, a3, a4, a5);
    core::logf("AvatarSpawning create player %u returned", player);
}

// FUN_0075a290, thiscall (vtable 1): AsyncActorCreator execute; the avatar spawn's only. Per async create.
inline void __fastcall hkAacExec(void* self, void*)
{
    const bool avatar = *(uintptr_t*)((char*)self + eng::kAacCallback) == core::rt(eng::kAvatarSpawned);
    if (avatar)
        core::logf("AsyncActorCreator %p execute (avatar spawn, mode %u)", self,
                   *(uint32_t*)((char*)self + eng::kAacMode));
    g_state.g_aacExec(self, nullptr);
    if (avatar) core::logf("AsyncActorCreator %p execute returned", self);
}

// FUN_00a9b3e0, cdecl (actor, record): the avatar spawn's completion callback. Rare.
inline void __cdecl hkAvSpawned(uint32_t actor, uint8_t* rec)
{
    uint8_t* as = *(uint8_t**)core::rt(eng::kAvatarSpawning);
    core::logf("avatar spawn callback actor %08X rec %p (AvatarSpawning %p +0x88 %08X, rec+0x31 %02X)", actor, rec, as,
               as ? *(uint32_t*)(as + eng::kSpawningReady) : 0, rec ? rec[eng::kRecordFlag] : 0);
    g_state.g_avSpawned(actor, rec);
}

// FUN_014523b0, thiscall (out, name, 3 args): cho creation; toy and spawn chos (and unnamed) logged.
inline uint32_t* __fastcall hkChoMake(void* self, void*, uint32_t* out, const char* name, uint32_t a3, uint32_t a4,
                                      uint32_t a5)
{
    State& s = g_state;
    uint32_t* r = s.g_choMake(self, nullptr, out, name, a3, a4, a5);
    char n[64] = "";
    core::safeCopy(n, sizeof(n), name);
    const bool spawny = strstr(n, "oy") || strstr(n, "pawn") || !n[0];
    if (!spawny) return r;
    const LONG nth = ++s.choMake;
    if (nth <= kChoMakeLogged) core::logf("cho make '%s' -> %08X", n, r == out && out ? *out : 0xffffffff);
    return r;
}

// FUN_00a8b670, cdecl (message): the ToyCreation_PlaceToy callback; sets the ready flag finalize waits for.
inline void __cdecl hkPlaceToy(void* msg)
{
    core::logf("ToyCreation_PlaceToy callback (msg %p)", msg);
    g_state.g_placeToy(msg);
}

// Meridian node creators (cdecl placement constructors, FUN_006bc220): count what a graph builds.
inline void* __cdecl hkMkStart(void* m, uint32_t a2, uint32_t a3)
{
    State& s = g_state;
    const LONG n = ++s.starts;
    core::logf("MNode PlayerStartPosNode %p (#%ld)", m, n);
    return s.g_mkStart(m, a2, a3);
}

inline void* __cdecl hkMkLocator(void* m, uint32_t a2, uint32_t a3)
{
    State& s = g_state;
    const LONG n = ++s.locators;
    if (n <= 2 || n % kLocatorEvery == 0) core::logf("MNode LocatorNode #%ld", n);
    return s.g_mkLocator(m, a2, a3);
}

inline void* __cdecl hkMkSpawnExp(void* m, uint32_t a2, uint32_t a3)
{
    State& s = g_state;
    const LONG n = ++s.spawnExps;
    if (n <= 2 || n % kSpawnExpEvery == 0) core::logf("MNode SpawnExpendable #%ld", n);
    return s.g_mkSpawnExp(m, a2, a3);
}

// A header's name; a function of its own for the __try (H7). out keeps its text when unreadable.
inline void cloneName(const uint32_t* h, char* out, size_t cap)
{
    __try {
        if (h && h[0]) core::safeCopy(out, cap, (const char*)(uintptr_t)(h[0] + eng::kHeaderName));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// A header clone as hkHdrClone saw it.
struct Clone {
    uint32_t index, instRoot, slots, slot;
    const uint32_t* tmpl;
    const uint32_t* t;   // the +0xc target, null when its handle is dead
};

// A cloned header whose +0xc target has no temp slot: the template, the target and its parent.
inline void logClone(const Clone& c)
{
    const uint32_t *tmpl = c.tmpl, *t = c.t;
    char name[80] = "?", tname[80] = "?", pname[80] = "?";
    cloneName(tmpl, name, sizeof(name));
    cloneName(t, tname, sizeof(tname));
    uint32_t* tp = t ? (uint32_t*)((ResolveFn)core::rt(eng::kResolveHandle))((void*)(t + 1), nullptr) : nullptr;
    cloneName(tp, pname, sizeof(pname));
    const uint32_t* base = (const uint32_t*)((const char*)tmpl - c.index * eng::kHeaderBytes);
    const int at = t ? (int)(((const char*)t - (const char*)base) / eng::kHeaderBytes) : -1;
    core::logf("header clone %u/%u '%s' (instRoot %08X, %u slots): template %p [%08X %08X %08X %08X %08X] +0xc -> %p "
               "'%s' (array index %d, slot %u) [%08X %08X %08X %08X %08X] parent %p '%s'", c.index,
               g_state.instHeaders, name, c.instRoot, c.slots, tmpl, tmpl[0], tmpl[1], tmpl[2], tmpl[3], tmpl[4], t,
               tname, at, c.slot, t ? t[0] : 0, t ? t[1] : 0, t ? t[2] : 0, t ? t[3] : 0, t ? t[4] : 0, tp, pname);
}

// FUN_004f5900, thiscall on the instance header (ret 0x10): header clone; 0x4f5a25 maps +0xc unchecked.
inline void __fastcall hkHdrClone(void* self, void*, uint32_t index, uint32_t a2, int delta, uint32_t instRoot)
{
    State& s = g_state;
    uint32_t* inst = (uint32_t*)self;
    uint32_t* tmpl = (uint32_t*)((char*)self - delta);
    uint32_t* t = (uint32_t*)((ResolveFn)core::rt(eng::kResolveHandle))(inst + 3, nullptr);
    uint32_t slots = *(uint32_t*)core::rt(eng::kCloneSlots), e = 1, slot = 0;
    if (t) {
        slot = ((t[2] & eng::kSlotMask) | (t[3] >> 3)) >> eng::kSlotShift;
        e = slot < slots ? ((uint32_t*)core::rt(eng::kCloneSlotTable))[slot] : 1;
    }
    if (!t || !e) {
        const LONG nth = ++s.clones;
        if (nth <= kClonesLogged) logClone({index, instRoot, slots, slot, tmpl, t});
    }
    s.g_hdrClone(self, nullptr, index, a2, delta, instRoot);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.on) return true;
    detail::State& s = detail::g_state;
    start::detail::State& st = start::detail::g_state;
    s.hidePs = c.hidePs;
    return core::installFeature("trace", {
        {"actor exists (trace)", 0x00751750, "518B4424088B00", (void*)&detail::hkActorExists, (void**)&s.g_actorExists},
        {"figure check (trace)", 0x00DDAE30, "64A1000000006AFF68", (void*)&detail::hkFigureOk, (void**)&s.g_figureOk},
        {"figure playset (trace)", 0x00435080, "6AFF68????????64A100000000", (void*)&detail::hkFigurePlayset,
         (void**)&s.g_figurePlayset},
        {"reader list (trace)", 0x00427330, "83EC08535556", (void*)&detail::hkReaderList, (void**)&s.g_readerList},
        {"reader item gate (trace)", 0x00DEB610, "538B5C2408", (void*)&detail::hkReaderItemOk,
         (void**)&s.g_readerItemOk},
        {"playset figure slot (trace)", 0x00DF9FD0, "6AFF68????????64A100000000", (void*)&detail::hkIgpName,
         (void**)&s.g_igpName},
        {"asset instance (trace)", 0x007734F0, "6AFF68????????64A100000000", (void*)&detail::hkAssetInst,
         (void**)&s.g_assetInst},
        {"SubNetwork instance guard (trace)", 0x004FF0E0, "6AFF68????????64A100000000", (void*)&detail::hkInst,
         (void**)&s.g_inst},
        {"header clone (trace)", 0x004F5900, "5153555657", (void*)&detail::hkHdrClone, (void**)&s.g_hdrClone},
        {"start lookup (trace)", 0x013A85D0, "6AFF68????????64A100000000", (void*)&start::detail::hkStartLookup,
         (void**)&st.g_startLookup},
        {"AsyncActorCreator execute (trace)", 0x0075A290, "6AFF68????????64A100000000", (void*)&detail::hkAacExec,
         (void**)&s.g_aacExec},
        {"avatar spawn callback (trace)", 0x00A9B3E0, "8B0D????????83B988000000", (void*)&detail::hkAvSpawned,
         (void**)&s.g_avSpawned},
        {"cho make (trace)", 0x014523B0, "83EC1C33C0", (void*)&detail::hkChoMake, (void**)&s.g_choMake},
        {"AvatarSpawning create (trace)", 0x00A9F080, "6AFF68????????64A100000000", (void*)&detail::hkAvCreate,
         (void**)&s.g_avCreate},
        {"cho has (trace)", 0x014835C0, "53568B74240C", (void*)&detail::hkChoHas, (void**)&s.g_choHas},
        {"PlaceToy callback (trace)", 0x00A8B670, "8B4424048B4004", (void*)&detail::hkPlaceToy, (void**)&s.g_placeToy},
        {"event post (trace)", 0x0068BBB0, "56578BF98B07", (void*)&detail::hkPost, (void**)&s.g_post},
        {"level loader tick (trace)", 0x00D00530, "6AFF64A100000000", (void*)&detail::hkLevelTick,
         (void**)&s.g_levelTick},
        {"level load-done check (trace)", 0x00CCC450, "568BF1807E2100", (void*)&detail::hkLoadDone,
         (void**)&s.g_loadDone},
        {"AvatarSpawning finalize (trace)", 0x00AA36F0, "64A1000000006AFF68", (void*)&detail::hkAvFinal,
         (void**)&s.g_avFinal},
        {"AvatarSpawning remove (trace)", 0x00A9EC70, "6AFF68????????64A100000000", (void*)&detail::hkAvRemove,
         (void**)&s.g_avRemove},
        {"start query (trace)", 0x0145F720, "8B44240C83EC30", (void*)&start::detail::hkStartQuery,
         (void**)&st.g_startQuery},
        {"MNode PlayerStartPosNode (trace)", 0x0148A790, "518B442410", (void*)&detail::hkMkStart, (void**)&s.g_mkStart},
        {"MNode LocatorNode (trace)", 0x01480090, "518B442410", (void*)&detail::hkMkLocator, (void**)&s.g_mkLocator},
        {"MNode SpawnExpendable (trace)", 0x017BF020, "518B442410", (void*)&detail::hkMkSpawnExp,
         (void**)&s.g_mkSpawnExp},
        {"header make (trace)", 0x004FAE90, "6AFF68????????64A100000000", (void*)&detail::hkHdrMake,
         (void**)&s.g_hdrMake},
    });
}

}  // namespace trace::spawn

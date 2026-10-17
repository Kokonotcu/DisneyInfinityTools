// Shader lookup; pass-state records made at a shader's first lookup, not at boot. core. Loader, render threads.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_set>
#include <vector>
#include "../core/hooks.hpp"
#include "../core/locks.hpp"
#include "../core/log.hpp"

namespace engine::shaderlinks {

struct Config {
    bool lazyLink = false;    // the engine's own shaders link at first lookup too; the boot linker only sorts
    bool fadeProbe = false;   // test: log passes that have no stage when linked
    std::function<uint8_t*(const char* name)> gameShader;   // the game's shader by name, linked always; nullptr: none
};

// Installs the shader lookup, (lazyLink) the deferred linker and the clone lock, all or none (H1, H2).
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: the engine functions below are called without a prologue check (X-12)
constexpr uint32_t kInputLayout = 0x0044D430;   // FUN_0044d430: a vertex format's input layout for a VS
constexpr uint32_t kTableIndex = 0x00450970;    // FUN_00450970, cdecl (id, table, count, fallback): sorted lookup
constexpr uint32_t kSortShaders = 0x004703A0;   // FUN_004703a0, cdecl (array, count): the boot linker's sort

// An engine table of 8-byte {id, handle} entries and its u16 count (FUN_00472050 reads them).
struct Table {
    uint32_t entries;
    uint32_t count;
};
constexpr uint32_t kEntrySize = 8;
constexpr uint32_t kHandle = 4;   // an entry's handle, after its id
constexpr Table kVertexPrograms = {0x0215EFC8, 0x0215EFC4};
constexpr Table kPixelPrograms = {0x0216B050, 0x0216B04C};
constexpr uint32_t kProgramIndexMask = 0x7ff;   // record program indices are 11 bits
constexpr uint32_t kProgramMiss = 0x7ff;

// The state tables a pass names, by its field; their record indices are 4 bits.
struct StateTable {
    size_t passField;
    Table table;
};
constexpr StateTable kStates[4] = {{0x2c, {0x0216AFCC, 0x0216AFC8}},
                                   {0x38, {0x0216F0D8, 0x0216F0D4}},
                                   {0x3c, {0x0216F15C, 0x0216F158}},
                                   {0x34, {0x0216F054, 0x0216F050}}};
constexpr uint32_t kStateIndexMask = 0xf;
constexpr uint32_t kStateMiss = 0xf;
constexpr uint32_t kStateBits = 4;

constexpr uint32_t kShaderCount = 0x0215CFC0;     // the engine's shader array: u32 count
constexpr uint32_t kShaderEntries = 0x0215CFC4;   // 8-byte entries, sorted by FUN_004703a0
constexpr uint32_t kShaderPointers = 0x0215CFC8;  // each entry's shader
constexpr uint32_t kShaderIndexMask = 0x3ff;      // a record's 10-bit shader field
constexpr uint32_t kNoShaderIndex = 0x3ff;        // its value for a shader outside the array

constexpr uint32_t kVformats = 0x02170664;        // vertex format entries (FUN_0044d430's ECX)
constexpr uint32_t kVformatSize = 0x34;

// .shd (shd.py, 3.0): name pointer at +0; after finalizing, passes via the pointer at +0x38.
constexpr size_t kShdPassCount = 0x19;
constexpr size_t kShdPasses = 0x38;
constexpr size_t kPassSize = 0x50;
constexpr size_t kPassWord = 0;       // render-state word
constexpr size_t kPassStage = 0x14;   // 0: not drawn
constexpr size_t kPassRun = 0x15;
constexpr size_t kPassRecord = 0x16;  // u16: its pass-state record
constexpr size_t kPassVformat = 0x18;
constexpr size_t kPassB1c = 0x1c;
constexpr size_t kPassVs = 0x28;
constexpr size_t kPassPs = 0x30;

// Pass-state records (FUN_00472050): 14 words; draw sort keys hold a 12-bit record index.
constexpr uint32_t kRecords = 0x02173A68;
constexpr uint32_t kRecordCount = 0x02173A66;   // u16
constexpr uint32_t kRecordWords = 14;
constexpr uint16_t kMaxRecords = 4095;
constexpr uint32_t kPassMask = 0x3f;            // record word 0 bits 0-5: the pass
// u16 fields by byte offset; the chain links a record's vformat clones (FUN_0045c5c0), a new one only itself
constexpr size_t kRecChain = 0x0c;
constexpr size_t kRecChainEnd = 0x0e;
constexpr size_t kRecVformat = 0x10;
constexpr size_t kRecRun = 0x12;
// u32 fields by word index
constexpr size_t kRecB1c = 5;
constexpr size_t kRecShader = 6;
constexpr size_t kRecLayout = 7;
constexpr size_t kRecVs = 8;
constexpr size_t kRecPs = 9;
constexpr size_t kRecStates = 10;

constexpr int kToldLimit = 40;   // each missing-input-layout line, at most this many times
constexpr size_t kLinkedReport = 50;
}  // namespace eng

typedef uint8_t*(__cdecl* ShaderByNameFn)(const char* name);
typedef void(__cdecl* LinkAllFn)();
typedef uint32_t(__cdecl* CloneRecFn)(uint32_t rec, uint32_t vformat);

struct State {
    // originals: set once by hook() in install()
    ShaderByNameFn g_shaderByName = nullptr;
    LinkAllFn g_linkAll = nullptr;
    CloneRecFn g_cloneRec = nullptr;
    Config cfg;   // set once in install()
    // The render thread clones records mid-draw (FUN_0045c5c0): one lock for both writers (RE-N1)
    CRITICAL_SECTION recCs;               // initialised in install(), before the hooks
    std::unordered_set<uint8_t*> linked;  // guarded by recCs
    int cloneTold = 0;                    // guarded by recCs
    int layoutTold = 0;                   // guarded by recCs
};
inline State g_state;

// A shader's name for the log; the engine's built-in shaders have none.
inline const char* nameOf(const uint8_t* shd)
{
    const char* name = *(const char* const*)shd;
    return name ? name : "(built-in)";
}

// FUN_0044d430: ECX = vformat entry, EAX = VS index, three 0xff stack arguments, caller cleans (B-44).
inline uint32_t inputLayout(uint32_t vfIndex, uint32_t vsIndex)
{
    void* fn = (void*)core::rt(eng::kInputLayout);
    uint32_t entry = (uint32_t)core::rt(eng::kVformats) + vfIndex * eng::kVformatSize;
    uint32_t r;
    __asm {
        push 0xff
        push 0xff
        push 0xff
        mov ecx, entry
        mov eax, vsIndex
        call fn
        add esp, 12
        mov r, eax
    }
    return r;
}

// A pass's index in an engine table (FUN_00450970), or 0 for id 0.
inline uint32_t tableIndex(uint32_t id, const eng::Table& t, uint32_t fallback)
{
    if (!id) return 0;
    typedef uint32_t(__cdecl * IndexFn)(uint32_t id, uint32_t table, uint32_t count, uint32_t fallback);
    const IndexFn index = (IndexFn)core::rt(eng::kTableIndex);
    return (uint16_t)index(id, (uint32_t)core::rt(t.entries), *(uint16_t*)core::rt(t.count), fallback);
}

// An index's handle in an engine table, or 0 for index 0.
inline uint32_t handleAt(const eng::Table& t, uint32_t index)
{
    return index ? *(uint32_t*)core::rt(t.entries + eng::kHandle + eng::kEntrySize * index) : 0;
}

// The record key's shader field: shd's index in the engine's array (kNoShaderIndex for the game's).
inline uint32_t shaderIndexOf(const uint8_t* shd)
{
    const uint32_t n = *(uint32_t*)core::rt(eng::kShaderCount);
    for (uint32_t k = 0; k < n; ++k)
        if (*(uint32_t*)core::rt(eng::kShaderPointers + eng::kEntrySize * k) == (uint32_t)(uintptr_t)shd)
            return k & eng::kShaderIndexMask;
    return eng::kNoShaderIndex;
}

// Record rec (index n) for pass i of shd, fields as FUN_00472050 fills them (recCs held).
inline void fillRecord(uint32_t* rec, uint16_t n, uint8_t* shd, int i, uint32_t shaderIndex)
{
    State& s = g_state;
    const uint8_t* p = *(uint8_t**)(shd + eng::kShdPasses) + eng::kPassSize * i;
    memset(rec, 0, eng::kRecordWords * sizeof(uint32_t));
    const uint32_t word = *(const uint32_t*)(p + eng::kPassWord);
    const uint32_t vsi = tableIndex(*(const uint32_t*)(p + eng::kPassVs), eng::kVertexPrograms, eng::kProgramMiss);
    const uint32_t psi = tableIndex(*(const uint32_t*)(p + eng::kPassPs), eng::kPixelPrograms, eng::kProgramMiss);
    uint32_t st[4];
    for (int k = 0; k < 4; ++k)
        st[k] = tableIndex(*(const uint32_t*)(p + eng::kStates[k].passField), eng::kStates[k].table, eng::kStateMiss);
    // word 0: pass, shader <<6, word>>25 <<16, PS low byte <<24; word 1: PS high bits, VS <<3, vformat <<14
    rec[0] = (uint32_t)(i & eng::kPassMask) | (shaderIndex << 6) | ((word >> 25) << 16) | ((psi & 0xff) << 24);
    const uint32_t vformat = p[eng::kPassVformat];
    rec[1] = ((psi & eng::kProgramIndexMask) >> 8) | ((vsi & eng::kProgramIndexMask) << 3) | (vformat << 14);
    for (int k = 0; k < 4; ++k) rec[2] |= (st[k] & eng::kStateIndexMask) << (eng::kStateBits * k);
    uint8_t* bytes = (uint8_t*)rec;
    *(uint16_t*)(bytes + eng::kRecChain) = n;
    *(uint16_t*)(bytes + eng::kRecChainEnd) = n;
    *(uint16_t*)(bytes + eng::kRecVformat) = *(const uint16_t*)(p + eng::kPassVformat);
    *(uint16_t*)(bytes + eng::kRecRun) = p[eng::kPassRun];
    rec[eng::kRecB1c] = *(const uint32_t*)(p + eng::kPassB1c);
    rec[eng::kRecShader] = (uint32_t)(uintptr_t)shd;
    rec[eng::kRecLayout] = inputLayout(vformat, vsi);
    // no layout: the VS reads an input its pass's vertex format lacks (CreateInputLayout refused)
    if (!rec[eng::kRecLayout]) {
        const int nth = s.layoutTold++;
        if (nth < eng::kToldLimit)
            core::logf("pass-state record without input layout: %s pass %d vformat %u", nameOf(shd), i,
                       p[eng::kPassVformat]);
    }
    if (shaderIndex == eng::kNoShaderIndex)
        core::logf("ours: %s pass %d word %08x stage %u run %u b1c %02x layout %08x", *(const char**)shd, i, word,
                   p[eng::kPassStage], p[eng::kPassRun], p[eng::kPassB1c], rec[eng::kRecLayout]);
    rec[eng::kRecVs] = handleAt(eng::kVertexPrograms, vsi);
    rec[eng::kRecPs] = handleAt(eng::kPixelPrograms, psi);
    for (int k = 0; k < 4; ++k) rec[eng::kRecStates + k] = handleAt(eng::kStates[k].table, st[k]);
}

// Whether pass i of shd has its record already: the record it names points back at it.
inline bool linkedAlready(const uint8_t* shd, int i, const uint8_t* p)
{
    const uint16_t had = *(const uint16_t*)(p + eng::kPassRecord);
    if (had >= *(uint16_t*)core::rt(eng::kRecordCount)) return false;
    const uint32_t* old = (uint32_t*)core::rt(eng::kRecords) + eng::kRecordWords * had;
    return old[eng::kRecShader] == (uint32_t)(uintptr_t)shd && (old[0] & eng::kPassMask) == (uint32_t)i;
}

// Appends pass i's record (recCs held), published last: fields, the pass's index, the count.
inline bool addRecord(uint8_t* shd, int i, uint8_t* p, uint32_t shaderIndex)
{
    uint16_t& count = *(uint16_t*)core::rt(eng::kRecordCount);
    if (count >= eng::kMaxRecords) {
        core::logf("pass-state records full (%u)", count);
        return false;
    }
    const uint16_t n = count;
    fillRecord((uint32_t*)core::rt(eng::kRecords) + eng::kRecordWords * n, n, shd, i, shaderIndex);
    *(uint16_t*)(p + eng::kPassRecord) = n;
    count = n + 1;
    return true;
}

// Gives each staged pass of shd that lacks one a pass-state record; false when the records are full.
inline bool linkShader(uint8_t* shd)
{
    State& s = g_state;
    core::CsLock lock(s.recCs);
    const bool seen = s.linked.count(shd) != 0;
    int fresh = 0;
    const uint32_t shaderIndex = shaderIndexOf(shd);
    uint8_t* passes = *(uint8_t**)(shd + eng::kShdPasses);
    const int npass = shd[eng::kShdPassCount];
    for (int i = 0; i < npass; ++i) {
        uint8_t* p = passes + eng::kPassSize * i;
        const bool staged = p[eng::kPassStage] != 0;
        if (!staged && s.cfg.fadeProbe && !seen)
            core::logf("fade probe: shader %s pass %d word %08x has no stage at link time (no record)", nameOf(shd), i,
                       *(uint32_t*)p);
        if (!staged || linkedAlready(shd, i, p)) continue;
        ++fresh;
        if (!addRecord(shd, i, p, shaderIndex)) return false;
    }
    if (seen && fresh) core::logf("late link: shader %p gained %d staged passes", shd, fresh);
    if (seen) return true;
    s.linked.insert(shd);
    if (s.linked.size() % eng::kLinkedReport == 0)
        core::logf("linked %u shaders (records %u)", (unsigned)s.linked.size(),
                   *(uint16_t*)core::rt(eng::kRecordCount));
    return true;
}

// FUN_0045c5c0, cdecl (record, vformat): a record's clone for another vertex format. Render thread, mid-draw.
inline uint32_t __cdecl hkCloneRec(uint32_t rec, uint32_t vformat)
{
    State& s = g_state;
    core::CsLock lock(s.recCs);
    const uint32_t r = s.g_cloneRec(rec, vformat);
    const uint32_t* c = (uint32_t*)core::rt(eng::kRecords) + eng::kRecordWords * (uint16_t)r;
    const char* nm = c[eng::kRecShader] ? *(const char**)(uintptr_t)c[eng::kRecShader] : nullptr;
    if (c[eng::kRecLayout] || !nm) return r;
    const int nth = s.cloneTold++;
    if (nth < eng::kToldLimit)
        core::logf("clone without input layout: %s pass %u vformat %u (record %u from %u)", nm, c[0] & eng::kPassMask,
                   (uint8_t)vformat, (uint16_t)r, (uint16_t)rec);
    return r;
}

// FUN_00472050, cdecl: the boot linker, once at renderer init. Only its sort runs; built-in shaders link now.
inline void __cdecl hkLinkAll()
{
    typedef void(__cdecl * SortFn)(uint32_t arr, uint32_t n);
    const SortFn sort = (SortFn)core::rt(eng::kSortShaders);
    sort((uint32_t)core::rt(eng::kShaderEntries), *(uint32_t*)core::rt(eng::kShaderCount));
    // built-in shaders have no name: used by pointer, never looked up (unlinked, the world drew black)
    unsigned builtin = 0;
    for (uint32_t k = 0, n = *(uint32_t*)core::rt(eng::kShaderCount); k < n; ++k) {
        uint8_t* shd = *(uint8_t**)core::rt(eng::kShaderPointers + eng::kEntrySize * k);
        if (*(const char**)shd) continue;
        linkShader(shd);
        ++builtin;
    }
    core::logf("shader linking deferred to first use (%u shaders registered, %u built-in linked now: %u pass-state "
               "records)",
               *(uint32_t*)core::rt(eng::kShaderCount), builtin, *(uint16_t*)core::rt(eng::kRecordCount));
}

// FUN_0045c820, cdecl (name): a shader by name. Loader threads, when a material is created.
inline uint8_t* __cdecl hkShaderByName(const char* name)
{
    State& s = g_state;
    uint8_t* shd = s.cfg.gameShader ? s.cfg.gameShader(name) : nullptr;
    const bool ours = shd != nullptr;
    if (!shd) shd = s.g_shaderByName(name);
    // with the boot linker on, the engine's own are linked already
    const bool unlinked = ours || s.cfg.lazyLink;
    if (shd && unlinked) linkShader(shd);
    return shd;
}

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    s.cfg = c;
    InitializeCriticalSection(&s.recCs);
    std::vector<core::HookSpec> shaders = {{"shader by name", 0x0045C820, "8B44240450E8????????8B0D",
                                            (void*)&detail::hkShaderByName, (void**)&s.g_shaderByName}};
    if (c.lazyLink)
        shaders.push_back({"shader linker (deferred)", 0x00472050, "83EC50A1????????535657", (void*)&detail::hkLinkAll,
                           (void**)&s.g_linkAll});
    shaders.push_back({"pass-state clone (locked)", 0x0045C5C0, "83EC4856578B7C2454", (void*)&detail::hkCloneRec,
                       (void**)&s.g_cloneRec});
    return core::installFeature("shaders10", shaders);
}

}  // namespace engine::shaderlinks

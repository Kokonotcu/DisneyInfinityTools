// The dialogue bridge: FMOD dialog banks and scripts in 3.0's speaker registry and DialogueManager. host. Any thread.
#pragma once
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../base/hash.hpp"
#include "../../base/text.hpp"
#include "../../convert/dbnk.hpp"
#include "../../core/hooks.hpp"
#include "../../core/locks.hpp"
#include "../../core/wholefile.hpp"
#include "../../formats/fsbname.hpp"
#include "dlgscript.hpp"
#include "host.hpp"
#include "log.hpp"
#include "system.hpp"

namespace engine::audio::dialogue {

using dlgscript::DlgSeq;
using dlgscript::DlgSeqItem;
using dlgscript::DlgStats;

// One dialog bank: its FSB and the '.name' index beside it.
struct DlgBank {
    std::string file;           // as the install spells it ('Edna_str.fsb')
    std::vector<uint8_t> name;  // its '.name'
    void* snd = nullptr;        // the FSB
    int refs = 0;
    fsbname::Index idx() const { return fsbname::Index{name.data(), name.size()}; }
};

struct Config {
    std::string banksDir;          // <banksDir>\<language>\<bank>.fsb and .name
    std::string projectsDir;       // FMOD projects whose strings name script events
    std::string defaultLanguage;   // the folder when the game's language setting names none present
    std::string streamSuffix;      // a bank whose stem ends so is opened as a stream
    unsigned streamMode = 0;       // System::createSound modes of a streamed and another bank
    unsigned sampleMode = 0;
    std::string slotSuffix;        // our registry slot's name: the prefix letters, then this
    std::function<bool(const char* path, std::vector<uint8_t>& source)> readScript;   // a script the game serves
    dlgscript::Transcode transcode;
    core::InstallFeatureFn installFeature = nullptr;
    uintptr_t (*rt)(uint32_t) = nullptr;   // core::rt; null: no engine (octtest)
};

// Keeps c and binds the dialog engine's functions; before any audio hook.
inline void configure(const Config& c);
// The vo10 hooks, all or none; false when refused.
inline bool install(const Config& c);
// The language folder: 1.0 FUN_01137bb0 builds it from 'LangHogFile' (FUN_007185d0); 3.0 keeps both.
inline void pickLanguage();
// The bank and its '.name'; the FSB must hold as many subsounds as the '.name' lists.
inline DlgBank* open(const std::string& file);
inline bool load(const std::string& file);
inline void unload(const std::string& file);
inline DlgBank* bank(const std::string& file);
// The {bank, subsound} record at p, written when p is in our headers; true when that bank is open.
inline bool media(const void* p, DlgBank** bank, int* sub);
// Every string of every FMOD project ('STRR' chunks), by its case-kept x131: what scripts name events by.
inline void buildEvNames();
// The host tick: the next line of each running sequence when due.
inline void seqTick();
// The subsound name a play of fsb's subsound sub played ("?" if unknown).
inline std::string lineName(void* fsb, int sub);
// fn(first bank file, language folder) under the lock when the speaker has banks -> fn's answer; else false.
template <class Fn>
inline bool withSpeaker(uint16_t prefix, Fn fn);

namespace detail {

namespace eng {
// H4 open: these engine functions are called without a prologue check (X-12)
constexpr uint32_t kLanguage = 0x00680C00;          // FUN_00680c00: cdecl, the 'LangHogFile' setting
constexpr uint32_t kSoundManager = 0x0228D668;      // the sound manager
constexpr uint32_t kSpeakerFind = 0x013CC1F0;       // FUN_013cc1f0: thiscall (key) on a map -> node
constexpr uint32_t kSpeakerAdd = 0x018308E0;        // FUN_018308e0: thiscall (key, record) on the speaker map
constexpr uint32_t kMapPut = 0x006EAA00;            // FUN_006eaa00: thiscall (key, value*) on a script's map
constexpr uint32_t kDialogueManager = 0x02204128;   // the DialogueManager
constexpr uint32_t kEngineAlloc = 0x00409E90;       // FUN_00409e90: cdecl (size, flag, got*, align)
constexpr uint32_t kEngineFree = 0x00409ED0;        // FUN_00409ed0: cdecl (p)
constexpr size_t kSpeakerMap = 0xd4;      // sound manager: speakers by line prefix (1.0 FUN_00f75df0's registry)
constexpr size_t kNodeSlots = 0x14;       // a speaker node's 3 bank pointers
constexpr int kSlots = 3;
constexpr size_t kSlotSize = 0x50;        // a 3.0 bank object: +0x20 set to 1, +0x28 header, +0x2c name
constexpr size_t kSlotFlag = 0x20;
constexpr size_t kSlotHeader = 0x28;
constexpr size_t kSlotName = 0x2c;
constexpr size_t kSlotNameSize = 0x20;
constexpr size_t kDmLock = 0x58;          // DialogueManager: the CS FUN_0073daf0 holds while it looks names up
constexpr size_t kDmScripts = 0x48;       // DialogueManager: the loaded scripts (FUN_0073f990 walks them)
constexpr size_t kScriptNext = 4;
constexpr size_t kScriptMap = 8;          // a script's entries by name hash
constexpr size_t kScriptBlob = 0x7c;      // the script's bytes, as the read returned them
constexpr size_t kNodeValue = 0xc;
constexpr uint32_t kAllocAlign = 0xffffffffu;   // FUN_00409e90's align argument
constexpr size_t kSpeakerRecord = 5;   // FUN_018308e0's record: 5 u32, the bank object third
constexpr size_t kRecordBank = 2;
}  // namespace eng

constexpr DWORD kArmedMs = 5000;   // an armed sequence waits this long for its first line
constexpr int kTracedPosts = 300;  // audiotrace10: dialog line posts logged

typedef const char*(__cdecl* LangFn)();
typedef void*(__fastcall* MapFindFn)(void* map, void* edx, uint32_t key);
typedef void(__fastcall* MapAddFn)(void* map, void* edx, uint32_t key, const uint32_t* rec);
typedef void(__fastcall* MapPutFn)(void* map, void* edx, uint32_t key, void* const* value);
typedef void*(__cdecl* AllocFn)(uint32_t size, char flag, uint32_t* got, uint32_t align);
typedef void(__cdecl* FreeFn)(void* p);
typedef uint32_t(__cdecl* PostExtFn)(uint32_t ev, uint32_t obj, uint32_t flags, void* cb, void* cookie, uint32_t n,
                                     const uint32_t* ext, uint32_t pid);
typedef void*(__cdecl* LoadFileFn)(const char* path, uint32_t* size);
typedef void*(__fastcall* DlgReqFn)(void* pool, void* edx, const char** nameRec, uint32_t a2);

struct Speaker {
    std::vector<DlgBank*> banks;
    uint8_t* slot = nullptr;   // our bank object in the registry; allocated once, never freed (S5)
    std::vector<std::unique_ptr<std::vector<uint8_t>>> hdrs;   // current last; never freed (posts point into them)
};

// The banks and the speakers they join.
struct Registry {
    std::string dir;                                         // the language folder in use
    std::map<std::string, std::unique_ptr<DlgBank>> banks;   // lower-case file; kept (posts hold their pointer)
    std::map<uint16_t, Speaker> speakers;
    // every header ever published, start -> size: live, non-empty, so never overlapping (AU-20)
    std::map<const uint8_t*, size_t> headers;
};

// What the transcoded scripts left: event names, sequences, line events, the blobs handed to the engine.
struct Scripts {
    dlgscript::EventNames evNames;
    bool evNamesBuilt = false;
    dlgscript::SeqMap seqs;               // armed by the request hook
    dlgscript::LineEventMap lineEvents;   // the post hook's event when the posted one is not ours
    std::unordered_set<const uint8_t*> served;   // never pruned (AU-27)
};

struct Armed {
    DlgSeq seq;
    DWORD at = 0;
    bool on = false;
};
struct Run {
    DlgSeq seq;
    size_t next;
    DWORD prevStart, prevEnd;
    uint32_t prevPid, obj;
};

struct State {
    Config cfg;   // set once by configure()
    LangFn lang = nullptr;           // engine functions and objects: each set once by configure()
    MapFindFn mapFind = nullptr;     // set once by configure()
    MapAddFn mapAdd = nullptr;       // set once by configure()
    MapPutFn mapPut = nullptr;       // set once by configure()
    uint8_t* mgr = nullptr;          // set once by configure()
    uint8_t* dialogMgr = nullptr;    // set once by configure()
    AllocFn engineAlloc = nullptr;   // set once by configure()
    FreeFn engineFree = nullptr;     // set once by configure()
    PostExtFn oPostExt = nullptr;     // originals: each set once by hook() in install()
    LoadFileFn oLoadFile = nullptr;   // set once by hook()
    DlgReqFn oDlgReq = nullptr;       // set once by hook()
    // after the DialogueManager's lock, before system's; held across bank loads
    std::recursive_mutex mx;   // guards the members below
    Registry reg;              // guarded by mx
    Scripts scripts;           // guarded by mx
    Armed armed;               // guarded by mx
    std::vector<Run> runs;     // guarded by mx
    int posts = 0;             // guarded by mx; audiotrace10 post lines logged
};
inline State g_state;

inline std::string stemOf(const std::string& file) { return file.substr(0, file.rfind('.')); }

// The line a {bank, subsound} plays: the '.name' line whose subsounds hold it.
inline int lineOf(DlgBank* b, int sub)
{
    const fsbname::Index x = b->idx();
    for (uint32_t l = x.lo(); l <= x.hi(); ++l) {
        const uint16_t v = x.entry(l);
        const int first = fsbname::firstSubsound(v), nv = fsbname::variants(v);
        if (v != fsbname::kNone && sub >= first && sub < first + nv) return (int)l;
    }
    return -1;
}

// A speaker whose banks are gone leaves the registry's slots it held.
inline void unpublish(Speaker& sp, void* map, uint16_t prefix)
{
    State& s = g_state;
    if (!map || !sp.slot) return;
    uint8_t* node = (uint8_t*)s.mapFind(map, nullptr, prefix);
    uint8_t** slots = node ? (uint8_t**)(node + eng::kNodeSlots) : nullptr;
    for (int i = 0; slots && i < eng::kSlots; ++i)
        if (slots[i] == sp.slot) slots[i] = nullptr;
}

// The speaker's slot into its registry node: the top free one (3.0's own banks keep the lower ones).
inline void publishSlot(const Speaker& sp, void* map, uint16_t prefix)
{
    State& s = g_state;
    uint8_t* node = (uint8_t*)s.mapFind(map, nullptr, prefix);
    if (!node) {
        uint32_t rec[eng::kSpeakerRecord] = {};
        rec[eng::kRecordBank] = (uint32_t)(uintptr_t)sp.slot;
        s.mapAdd(map, nullptr, prefix, rec);
        log("vo10: speaker %c%c registered with the 1.0 banks", prefix & 0xff, prefix >> 8);
        return;
    }
    uint8_t** slots = (uint8_t**)(node + eng::kNodeSlots);
    for (int i = 0; i < eng::kSlots; ++i)
        if (slots[i] == sp.slot) return;
    for (int i = eng::kSlots - 1; i >= 0; --i) {
        if (slots[i]) continue;
        slots[i] = sp.slot;
        log("vo10: speaker %c%c: 1.0 banks in slot %d", prefix & 0xff, prefix >> 8, i);
        return;
    }
    log("vo10: speaker %c%c has no free slot for the 1.0 banks", prefix & 0xff, prefix >> 8);
}

// 1.0 FUN_01137bb0's registration in 3.0's form (FUN_018312b0): the speaker's banks as one header in a slot.
inline void publish(uint16_t prefix)
{
    State& s = g_state;
    Speaker& sp = s.reg.speakers[prefix];
    void* map = s.mgr ? s.mgr + eng::kSpeakerMap : nullptr;
    if (sp.banks.empty()) {
        unpublish(sp, map, prefix);
        return;
    }
    std::vector<dbnk::Source> src;
    for (DlgBank* b : sp.banks) src.push_back({b->idx(), (uint32_t)(uintptr_t)b});
    sp.hdrs.push_back(std::make_unique<std::vector<uint8_t>>(dbnk::fromNames(prefix, src)));
    s.reg.headers[sp.hdrs.back()->data()] = sp.hdrs.back()->size();
    if (!map) return;   // octtest: no engine
    if (!sp.slot) {
        sp.slot = (uint8_t*)calloc(1, eng::kSlotSize);
        const uint32_t one = 1;
        memcpy(sp.slot + eng::kSlotFlag, &one, sizeof one);
        snprintf((char*)sp.slot + eng::kSlotName, eng::kSlotNameSize, "%c%c%s", prefix & 0xff, prefix >> 8,
                 s.cfg.slotSuffix.c_str());
    }
    *(uint8_t**)(sp.slot + eng::kSlotHeader) = sp.hdrs.back()->data();
    publishSlot(sp, map, prefix);
}

// The FSB opened as 1.0 FUN_01137bb0 opens it (system's lock held); false (logged, released) on a mismatch.
inline bool openSound(DlgBank* b, const std::string& file, bool streamed)
{
    State& s = g_state;
    const fmod::Api& f = system::api();
    const std::string path = s.reg.dir + "\\" + file;
    int subs = -1;
    fmod::Result r = f.System_CreateSound(system::soundSystem(), path.c_str(),
                                          streamed ? s.cfg.streamMode : s.cfg.sampleMode, nullptr, &b->snd);
    if (!r && b->snd) r = f.Sound_GetNumSubSounds(b->snd, &subs);
    const bool opened = !r && b->snd && subs == b->idx().subs();
    if (opened) return true;
    log("vo10: %s: FSB not opened or %d subsounds for %u names", file.c_str(), subs, b->idx().subs());
    if (b->snd) f.Sound_Release(b->snd);
    b->snd = nullptr;
    return false;
}

// The 'STRR' chunk of one project: n offsets, then strings.
inline void hashStrings(const std::vector<uint8_t>& d, dlgscript::EventNames& out)
{
    for (size_t i = 0; bytes::fits(d.size(), i, 12); ++i) {
        if (memcmp(d.data() + i, "STRR", 4)) continue;
        const uint32_t len = bytes::u32At(d.data() + i + 4);
        const uint8_t* s = d.data() + i + 8;
        if (!bytes::fits(d.size(), i + 8, len) || len < 4) return;
        const uint32_t cnt = bytes::u32At(s);
        if (cnt > len / 4 || !bytes::fits(len, 4, (size_t)cnt * 4)) return;
        for (uint32_t k = 0; k < cnt; ++k) {
            const uint32_t rel = bytes::u32At(s + 4 + (size_t)k * 4);
            if (!bytes::fits(len, 4 + (size_t)cnt * 4, rel)) continue;   // no 32-bit wrap of the string offset
            const uint32_t o = 4 + cnt * 4 + rel;
            if (o >= len || !memchr(s + o, 0, len - o)) continue;
            out.emplace(hash::h131Signed((const char*)s + o, SIZE_MAX), (const char*)s + o);
        }
        return;
    }
}

// Whether a run's next line is due: type 4 counts from the previous line's start, type 3 from its end.
inline bool isDue(Run& u, const DlgSeqItem& it, DWORD now)
{
    constexpr uint8_t kFromStart = 4;
    if (u.seq.type == kFromStart) return now - u.prevStart >= it.delay;
    if (!u.prevEnd) {
        std::unique_lock<std::recursive_mutex> l = system::lock();
        if (!host::playOf(u.prevPid)) u.prevEnd = now;
    }
    return u.prevEnd && now - u.prevEnd >= it.delay;
}

inline uint32_t playSeqLine(const DlgSeqItem& it, uint32_t obj)
{
    State& s = g_state;
    auto sp = s.reg.speakers.find(it.prefix);
    if (sp != s.reg.speakers.end()) {
        for (DlgBank* b : sp->second.banks) {
            const uint16_t v = b->idx().entry(it.line);
            if (v == fsbname::kNone || !b->snd || it.event.empty()) continue;
            const int nv = fsbname::variants(v);
            const int sub = fsbname::firstSubsound(v) + (nv > 1 ? rand() % nv : 0);
            const uint32_t id = host::playLine({hash::fnv1Lower(it.event), obj, 0, nullptr, nullptr}, b->snd, sub);
            log("vo10: sequence line %c%c%03u -> %s", it.prefix & 0xff, it.prefix >> 8, it.line,
                id ? "1.0 FMOD dialog" : "not played");
            return id;
        }
    }
    log("vo10: sequence line %c%c%03u: no loaded 1.0 bank holds it", it.prefix & 0xff, it.prefix >> 8, it.line);
    return 0;
}

// The first line of the armed sequence was posted: the rest runs from now (1.0 FUN_00718650 starts item 0).
inline void onLinePosted(DlgBank* b, int sub, uint32_t obj, uint32_t pid)
{
    State& s = g_state;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    if (!s.armed.on || GetTickCount() - s.armed.at > kArmedMs || s.armed.seq.items.empty()) return;
    const DlgSeqItem& first = s.armed.seq.items[0];
    if (b->idx().prefix() != first.prefix || lineOf(b, sub) != first.line) return;
    const DWORD now = GetTickCount();
    s.runs.push_back({s.armed.seq, 1, now, 0, pid, obj});
    s.armed.on = false;
    log("vo10: sequence of %u lines started at %c%c%03u (1.0 type %u)", (unsigned)s.runs.back().seq.items.size(),
        first.prefix & 0xff, first.prefix >> 8, first.line, s.armed.seq.type);
}

// A line posted with an event we lack plays with its line class's event, as 1.0 plays that class.
inline uint32_t lineClassEvent(const fsbname::Index& x, int sub, uint32_t ev)
{
    State& s = g_state;
    const char* nm = x.subName(sub);
    const bool named = nm && isalpha((uint8_t)nm[0]) && isalpha((uint8_t)nm[1]);
    if (!named || !isdigit((uint8_t)nm[2])) return ev;
    const uint32_t pfx = (uint32_t)(uint8_t)nm[0] | (uint32_t)(uint8_t)nm[1] << 8;
    auto le = s.scripts.lineEvents.find(dlgscript::dlgLineKey(pfx, (uint16_t)atoi(nm + 2)));
    return le != s.scripts.lineEvents.end() ? hash::fnv1Lower(le->second) : ev;
}

// audiotrace10: every dialog post up to kTracedPosts, with whether our sounds are paused (mx held).
inline void tracePost(const fsbname::Index& x, int sub, const host::Post& p, const DlgBank* b, uint32_t id)
{
    State& s = g_state;
    const int post = s.posts++;
    if (post >= kTracedPosts) return;
    std::string en = "?";
    bool paused;
    {
        std::unique_lock<std::recursive_mutex> gl = system::lock();
        if (std::optional<std::string> n = host::eventName(p.eventId); n) en = *n;
        paused = host::paused();
    }
    log("vo10: line %s posted: event %s, obj %u, flags %x, bank %s -> id %u%s", x.subName(sub), en.c_str(), p.obj,
        p.flags, b->file.c_str(), id, paused ? " (1.0 sounds paused)" : "");
}

// FUN_01948d80, cdecl PostEvent with external sources: 3.0 FUN_01830370 posts each line. Game threads, per line.
inline uint32_t __cdecl hkPostExt(uint32_t ev, uint32_t obj, uint32_t flags, void* cb, void* cookie, uint32_t n,
                                  const uint32_t* ext, uint32_t pid)
{
    State& s = g_state;
    DlgBank* b = nullptr;
    int sub = 0;
    std::lock_guard<std::recursive_mutex> l(s.mx);   // the FSB stays open through playLine (unload takes mx)
    constexpr size_t kMemory = 3;   // AkExternalSourceInfo's in-memory data pointer
    const bool oneSource = n == 1 && ext && ext[kMemory];
    const bool ours = oneSource && media((const void*)(uintptr_t)ext[kMemory], &b, &sub);
    if (!ours) return s.oPostExt(ev, obj, flags, cb, cookie, n, ext, pid);
    const fsbname::Index x = b->idx();
    if (!host::knowsEvent(ev)) ev = lineClassEvent(x, sub, ev);
    const host::Post post{ev, obj, flags, cb, cookie};
    const uint32_t id = host::playLine(post, b->snd, sub);
    traceBackend(ev ^ (uint32_t)sub, x.subName(sub), id ? "1.0 FMOD dialog" : "nothing (its 1.0 event is not loaded)");
    if (tracing()) tracePost(x, sub, post, b, id);
    if (id) onLinePosted(b, sub, obj, id);   // the first line of an armed sequence starts the rest
    return id;
}

// FUN_0182f570, cdecl: a sound file read whole into the engine heap ('dialog/<script>' too). Loader, per read.
inline void* __cdecl hkLoadFile(const char* path, uint32_t* size)
{
    State& s = g_state;
    std::vector<uint8_t> in;
    const bool ours = s.cfg.readScript(path, in);
    if (!ours) return s.oLoadFile(path, size);
    uint32_t n30 = 0;
    uint8_t* d30 = (uint8_t*)s.oLoadFile(path, &n30);   // the game's own script of that name (its entries are kept)
    std::vector<uint8_t> b30, out;
    if (d30) b30.assign(d30, d30 + n30);
    DlgStats st;
    std::string err;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    buildEvNames();
    uint8_t* p = nullptr;
    const dlgscript::ScriptRequest req{in, s.scripts.evNames, d30 ? &b30 : nullptr, &s.scripts.seqs,
                                       &s.scripts.lineEvents};
    if (const bool ok = s.cfg.transcode(req, out, &st, &err); ok) {
        uint32_t got = 0;
        p = (uint8_t*)s.engineAlloc((uint32_t)out.size(), 0, &got, eng::kAllocAlign);
    } else
        log("vo10: %s: 1.0 script not taken (%s)", path, err.c_str());
    if (!p) {
        if (size) *size = n30;
        return d30;
    }
    memcpy(p, out.data(), out.size());
    if (size) *size = (uint32_t)out.size();
    if (d30) s.engineFree(d30);
    s.scripts.served.insert(p);
    log("vo10: %s from 1.0: %d line classes (%d lines in %d ranges), %d named, %d events (%d unresolved); 3.0's "
        "kept: %d named, %d lines", path, st.classes, st.lines, st.ranges, st.named, st.events, st.unresolved,
        st.kept30, st.lines30);
    return p;
}

// Our scripts key named entries case kept; 3.0 looks up lower-cased (FUN_0040bb80): the alias goes in.
inline void aliasScripts(uint8_t* dm, const char* name, uint32_t cs, uint32_t lo)
{
    State& s = g_state;
    core::CsLock dmLock(*(CRITICAL_SECTION*)(dm + eng::kDmLock));
    std::lock_guard<std::recursive_mutex> l(s.mx);
    for (uint8_t* sc = *(uint8_t**)(dm + eng::kDmScripts); sc; sc = *(uint8_t**)(sc + eng::kScriptNext)) {
        void* map = sc + eng::kScriptMap;
        if (!s.scripts.served.count(*(uint8_t**)(sc + eng::kScriptBlob))) continue;
        if (void* have = s.mapFind(map, nullptr, lo); have) continue;
        uint8_t* node = (uint8_t*)s.mapFind(map, nullptr, cs);
        if (!node) continue;
        void* entry = *(void**)(node + eng::kNodeValue);
        s.mapPut(map, nullptr, lo, &entry);
        log("vo10: '%s' found in a 1.0 script by its 1.0 key", name);
    }
}

// FUN_0073bf50, thiscall (nameRec, a2; ret 8) on the request pool: a dialog request. Game threads, per request.
inline void* __fastcall hkDlgReq(void* pool, void*, const char** nameRec, uint32_t a2)
{
    State& s = g_state;
    const char* name = nameRec ? *nameRec : nullptr;
    uint8_t* dm = s.dialogMgr;
    const uint32_t cs = name ? hash::h131Signed(name, SIZE_MAX) : 0;
    if (name) {   // a sequence entry of that name is armed: its first line's post starts the rest
        std::lock_guard<std::recursive_mutex> l(s.mx);
        auto q = s.scripts.seqs.find(cs);
        if (q != s.scripts.seqs.end()) s.armed = {q->second, GetTickCount(), true};
    }
    const uint32_t lo = name && dm ? hash::h131Signed(text::lower(name).c_str(), SIZE_MAX) : 0;
    if (name && dm && cs != lo) aliasScripts(dm, name, cs, lo);
    return s.oDlgReq(pool, nullptr, nameRec, a2);
}

}  // namespace detail

inline void configure(const Config& c)
{
    detail::State& s = detail::g_state;
    s.cfg = c;
    if (!c.rt) return;
    s.lang = (detail::LangFn)c.rt(detail::eng::kLanguage);
    s.mgr = (uint8_t*)c.rt(detail::eng::kSoundManager);
    s.mapFind = (detail::MapFindFn)c.rt(detail::eng::kSpeakerFind);
    s.mapAdd = (detail::MapAddFn)c.rt(detail::eng::kSpeakerAdd);
    s.mapPut = (detail::MapPutFn)c.rt(detail::eng::kMapPut);
    s.dialogMgr = (uint8_t*)c.rt(detail::eng::kDialogueManager);
    s.engineAlloc = (detail::AllocFn)c.rt(detail::eng::kEngineAlloc);
    s.engineFree = (detail::FreeFn)c.rt(detail::eng::kEngineFree);
}

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    const core::InstallFeatureFn installFeature = c.installFeature;
    return installFeature("vo10", {
        {"dialog line post (vo10)", 0x01948D80, "558BEC83EC105356578B7D1C", (void*)&detail::hkPostExt,
         (void**)&s.oPostExt},
        {"sound file read (vo10)", 0x0182F570, "8B44240485C0741F803800741A", (void*)&detail::hkLoadFile,
         (void**)&s.oLoadFile},
        {"dialog request (vo10)", 0x0073BF50, "53568BF1E8????????8D4618", (void*)&detail::hkDlgReq,
         (void**)&s.oDlgReq},
    });
}

inline void pickLanguage()
{
    detail::State& s = detail::g_state;
    const char* l = s.lang ? s.lang() : nullptr;
    const DWORD a = l && *l ? GetFileAttributesA((s.cfg.banksDir + "\\" + l).c_str()) : INVALID_FILE_ATTRIBUTES;
    const bool present = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    const std::string want = s.cfg.banksDir + "\\" + (present ? std::string(l) : s.cfg.defaultLanguage);
    if (want != s.reg.dir) log("vo10: 1.0 dialog language folder %s", want.c_str());
    s.reg.dir = want;
}

inline DlgBank* open(const std::string& file)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    if (s.reg.dir.empty()) pickLanguage();
    std::unique_ptr<DlgBank>& slot = s.reg.banks[text::lower(file)];
    if (!slot) {
        slot = std::make_unique<DlgBank>();
        slot->file = file;
    }
    DlgBank* b = slot.get();
    const int had = b->refs++;
    if (had > 0 && b->snd) return b;
    const std::string stem = detail::stemOf(file);
    const bool named = core::readFile(s.reg.dir + "\\" + stem + ".name", b->name) && b->idx().valid();
    if (!named) {
        log("vo10: %s: no valid .name", file.c_str());
        --b->refs;
        return nullptr;
    }
    const bool streamed = text::endsWithPastStem(stem, s.cfg.streamSuffix.c_str());
    std::unique_lock<std::recursive_mutex> gl = system::lock();
    const bool opened = detail::openSound(b, file, streamed);
    if (!opened) {
        --b->refs;
        return nullptr;
    }
    const fsbname::Index x = b->idx();
    log("vo10: %s: %u subsounds, lines %c%c%03u..%03u", file.c_str(), x.subs(), x.prefix() & 0xff, x.prefix() >> 8,
        x.lo(), x.hi());
    return b;
}

inline bool load(const std::string& file)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    DlgBank* b = open(file);
    if (!b) return false;
    const uint16_t p = b->idx().prefix();
    std::vector<DlgBank*>& v = s.reg.speakers[p].banks;
    if (std::find(v.begin(), v.end(), b) != v.end()) return true;
    v.push_back(b);
    detail::publish(p);
    return true;
}

inline void unload(const std::string& file)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    auto it = s.reg.banks.find(text::lower(file));
    if (it == s.reg.banks.end() || it->second->refs <= 0) return;
    --it->second->refs;
    if (it->second->refs > 0) return;
    DlgBank* b = it->second.get();
    const uint16_t p = b->idx().prefix();
    std::vector<DlgBank*>& v = s.reg.speakers[p].banks;
    v.erase(std::remove(v.begin(), v.end(), b), v.end());
    detail::publish(p);
    std::unique_lock<std::recursive_mutex> gl = system::lock();
    host::stopBank(b->snd);
    system::api().Sound_Release(b->snd);
    b->snd = nullptr;
    log("vo10: unloaded %s", file.c_str());
}

inline DlgBank* bank(const std::string& file)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    auto it = s.reg.banks.find(text::lower(file));
    return it != s.reg.banks.end() ? it->second.get() : nullptr;
}

inline bool media(const void* p, DlgBank** bank, int* sub)
{
    detail::State& s = detail::g_state;
    const uint8_t* at = (const uint8_t*)p;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    auto it = s.reg.headers.upper_bound(at);   // the last header starting at or before p
    if (it == s.reg.headers.begin()) return false;
    --it;
    if (at >= it->first + it->second) return false;
    memcpy(bank, p, sizeof *bank);
    memcpy(sub, at + sizeof(uint32_t), sizeof *sub);
    return *bank && (*bank)->snd;
}

// Not retried: built is set before the scan.
inline void buildEvNames()
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    if (s.scripts.evNamesBuilt) return;
    s.scripts.evNamesBuilt = true;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((s.cfg.projectsDir + "\\*.fev").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<uint8_t> d;
    for (BOOL more = TRUE; more; more = FindNextFileA(h, &fd)) {
        const bool read = core::readFile(s.cfg.projectsDir + "\\" + fd.cFileName, d);
        if (read) detail::hashStrings(d, s.scripts.evNames);
    }
    FindClose(h);
    log("vo10: %u FEV strings hashed for the 1.0 dialog scripts", (unsigned)s.scripts.evNames.size());
}

inline void seqTick()
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    const DWORD now = GetTickCount();
    for (size_t r = 0; r < s.runs.size();) {
        detail::Run& u = s.runs[r];
        const DlgSeqItem& it = u.seq.items[u.next];
        const bool due = detail::isDue(u, it, now);
        if (!due) {
            ++r;
            continue;
        }
        constexpr uint8_t kFromStart = 4;
        u.prevPid = detail::playSeqLine(it, u.obj);
        u.prevStart = u.seq.type == kFromStart ? u.prevStart + it.delay : now;
        u.prevEnd = 0;
        ++u.next;
        if (u.next >= u.seq.items.size()) {
            s.runs.erase(s.runs.begin() + r);
            continue;
        }
        ++r;
    }
}

inline std::string lineName(void* fsb, int sub)
{
    detail::State& s = detail::g_state;
    std::string n = "?";
    std::lock_guard<std::recursive_mutex> l(s.mx);
    for (auto& b : s.reg.banks) {
        const bool same = b.second && b.second->snd == fsb;
        if (same && sub >= 0) n = b.second->idx().subName(sub);
    }
    return n;
}

template <class Fn>
inline bool withSpeaker(uint16_t prefix, Fn fn)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::recursive_mutex> l(s.mx);
    auto it = s.reg.speakers.find(prefix);
    if (it == s.reg.speakers.end() || it->second.banks.empty()) return false;
    return fn(it->second.banks.front()->file, s.reg.dir);
}

}  // namespace engine::audio::dialogue

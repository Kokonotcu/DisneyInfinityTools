// Audio acceptance tests on the real 1.0 FMOD DLLs with no output: --fmod (AUD-2), --vo (vo10), --host.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "../games/di1/audio.hpp"
#include "../base/bytes.hpp"
#include "files.hpp"
#include "run.hpp"
#include "fmodtools.hpp"
#include "modes.hpp"

namespace octtest {
namespace {

namespace fs = std::filesystem;

// What the callbacks saw: they are C function pointers with no context. Main thread.
struct State {
    int fmodEnds = 0;             // --fmod: end-of-event callbacks
    uint32_t fmodLastPid = 0;     // --fmod: the last callback's playing id
    float voDuration = -1;        // --vo: fDuration of the line's AK_Duration
    int voEnds = 0;               // --vo: end-of-event callbacks
    DWORD voEndAt = 0;            // --vo: tick count at the last end
    int hostHooks = 0;            // --host: hook rows install asked for
    uint8_t hostMemory[64] = {};  // --host: where the stand-in rt maps every engine address
};
State state;

void __cdecl onFmodCallback(uint32_t type, void* info)
{
    state.fmodEnds += type == kAkEndOfEvent;
    state.fmodLastPid = ((uint32_t*)info)[kAkPlayingIdAt];
}

void __cdecl onVoCallback(uint32_t type, void* info)
{
    if (type == kAkDuration) state.voDuration = ((float*)info)[kAkDurationAt];
    if (type == kAkEndOfEvent) {
        ++state.voEnds;
        state.voEndAt = GetTickCount();
    }
}

// ---- --fmod ----

// IN_City.fev and Music_Inc.fev of the 1.0 Incredibles playset: the events and cue the lifecycle plays.
constexpr char kOneShot[] = "billboardStart", kLoop[] = "fire_loop", kCue[] = "Inc_mx_Metroville";
constexpr char kSyncedCin[] = "INC_CIN_OpeningA";
constexpr char kSyncedCinPath[] = "Choreographies/INC_CIN_OpeningA/INC_CIN_OpeningA";
constexpr char kUnnamedCinPath[] = "Choreographies/INC_Dock_Explode/INC_Dock_Explode";   // no event of its name
constexpr uint32_t kCinNoSync = 0x8, kCinOther = 0x20, kCinOpeningA = 0x44128;   // choreography flags; the last in-game
constexpr int kOneShotMs = 6000, kAfterStopMs = 2000, kPromptMs = 1000, kResetMs = 500, kCinMs = 200;
// fire_loop: MAX_PLAYBACKS_BEHAVIOR 5 ('just fail if quietest'); 1.0 FUN_01136520 places its info-only instance.
constexpr uint32_t kVirtualFlag = 1;           // a 1.0 event entry flag: playback limited, judged by distance
constexpr uint32_t kVirtFirst = 100;           // the limit's game objects; the far post's is kVirtFirst + kVirtFill
constexpr int kVirtFill = 32, kVirtSources = 40, kVirtPostMs = 32, kVirtSettleMs = 200, kVirtEndMs = 500;
constexpr float kFillAt = 0.5f, kFarAt = 0.9f, kNearAt = 0.5f;   // of the 3D range; near: of the min distance

using ProjectNames = std::map<void*, std::string>;
using Dropped = std::map<std::string, std::string>;

// Initialize.fev:Permanent, then each FEV of argv[4..] by its 1.0 name (NOFILE when the install lacks it).
void loadFevs(const Args& a)
{
    ea::host::loadTree("Initialize.fev", "Permanent");
    for (int i = 4; i < a.argc; ++i) {
        std::string n = ea::system::installedName(a.argv[i]);
        if (n.empty()) printf("NOFILE %s\n", a.argv[i]);
        else ea::host::loadTree(n, nullptr);
    }
}

// A cue's verdict: through the playset's Music_<X>.fev, else 1.0's way, the playset's loaded project.
std::string cueVerdict(const std::string& name)
{
    size_t u = name.find('_');
    std::string x = u == std::string::npos ? "" : name.substr(0, u);
    unsigned id = 0;
    if (!x.empty() && ea::music::ensure(x))
        return ea::music::cue(name.c_str(), &id) ? "OK " + ea::music::file()
                                                     : "MISS (not a cue of " + ea::music::file() + ")";
    if (!ea::music::file().empty() && ea::music::cue(name.c_str(), &id)) {
        MusicIterator ci{};
        g_sys.f.MusicSystem_GetCues(g_sys.ms, &ci, name.c_str());
        return "OK " + ea::music::file() + " as " + (ci.value ? ci.value->name : "?");
    }
    return "MISS (no Music_" + x + ".fev)";
}

std::string eventVerdict(const std::string& name, ProjectNames& projName, Dropped& dropped)
{
    auto it = g_host.byId.find(hash::fnv1Lower(name));
    if (it != g_host.byId.end())
        return "OK " + projName[it->second.proj] + " flags " + std::to_string(it->second.flags);
    if (dropped.count(name)) return "MISS (left out by the 1.0 rule: " + dropped[name] + ")";
    return "MISS";
}

// Each names.tsv line (kind, source, name, uses) as an event or a cue; returns a tally per kind and source.
std::map<std::string, int> resolveNames(const char* path, ProjectNames& projName, Dropped& dropped)
{
    std::ifstream in(path);
    std::string line;
    std::map<std::string, int> tally;
    while (std::getline(in, line)) {
        size_t a = line.find('\t'), b = line.find('\t', a + 1), c = line.find('\t', b + 1);
        if (c == std::string::npos) continue;
        std::string kind = line.substr(0, a), src = line.substr(a + 1, b - a - 1), name = line.substr(b + 1, c - b - 1);
        std::string verdict = kind == "cue" ? cueVerdict(name) : eventVerdict(name, projName, dropped);
        ++tally[kind + " " + src + (verdict[0] == 'O' ? " ok" : " miss")];
        printf("%s\t%s\t%s\t%s\n", kind.c_str(), src.c_str(), name.c_str(), verdict.c_str());
    }
    return tally;
}

// The cue list of the Music_<X>.fev loaded last (fevcheck.py compares it with the file's cues chunk).
void printCues()
{
    MusicIterator it{};
    if (!g_sys.ms || g_sys.f.MusicSystem_GetCues(g_sys.ms, &it, nullptr)) return;
    for (; it.value; g_sys.f.MusicSystem_GetNextCue(g_sys.ms, &it))
        printf("CUE\t%s\t%u\n", it.value->name, it.value->id);
}

// A one-shot ends by itself and calls back once; a loop runs until stopped.
void lifecycle()
{
    void* const cb = (void*)onFmodCallback;
    uint32_t one = ea::host::play({hash::fnv1Lower(kOneShot), kTestObject, kAkEndOfEvent, cb, nullptr});
    uint32_t loop = ea::host::play({hash::fnv1Lower(kLoop), kTestObject, kAkEndOfEvent, cb, nullptr});
    tickFor(kOneShotMs);
    int afterOne = state.fmodEnds;
    uint32_t pidOne = state.fmodLastPid;
    ea::host::act(loop, kAkStop);
    tickFor(kAfterStopMs);
    printf("LIFE one-shot id %u: %d callback(s) in 6 s (pid %u); loop id %u: %s before its stop, %d after\n", one,
           afterOne, pidOne, loop, afterOne == 1 ? "running" : "ENDED", state.fmodEnds - afterOne);
}

// A music cue's prompt turns active, ends, and a fresh prompt is inactive after end and reset.
void musicPrompt()
{
    int active = -1, activeAfter = -1;
    if (ea::music::ensure(kPlayset10) && ea::music::begin(kCue)) {
        tickFor(kPromptMs);
        g_sys.f.MusicPrompt_IsActive(ea::music::prompt(), &active);
        ea::music::endPrompt();
        g_sys.f.MusicSystem_Reset(g_sys.ms);
        tickFor(kResetMs);
        unsigned id;
        void* p = nullptr;
        if (ea::music::cue(kCue, &id)) g_sys.f.MusicSystem_PrepareCue(g_sys.ms, id, &p);
        if (p) {
            g_sys.f.MusicPrompt_IsActive(p, &activeAfter);
            g_sys.f.MusicPrompt_Release(p);
        }
    }
    printf("LIFE music Inc_mx_Metroville: active %d after 1 s, fresh prompt active %d after end+reset\n", active,
           activeAfter);
}

// cinesync10: an audio-synced choreography starts the 1.0 event of its leaf name, nothing else does.
void cineSync()
{
    if (!g_host.byId.count(hash::fnv1Lower(kSyncedCin))) return;
    g_sys.up = kHostUp;   // octtest runs the host on this thread
    size_t n0 = g_host.plays.size();
    ea::host::choreographyStarted(kSyncedCinPath, kCinNoSync);
    ea::host::choreographyStarted(kUnnamedCinPath, kCinOther);
    size_t n1 = g_host.plays.size();
    ea::host::choreographyStarted(kSyncedCinPath, kCinOpeningA);
    size_t n2 = g_host.plays.size();
    printf("CINESYNC unsynced / unnamed: %s; synced: %s\n", n1 == n0 ? "nothing started" : "STARTED",
           n2 == n1 + 1 ? "event started" : "NOTHING");
    tickFor(kCinMs);
}

// fire_loop's playback limit and 3D range, read from its info-only instance.
struct Limit {
    int maxPlaybacks = 0;
    float dmin = 0, dmax = 0;
};
Limit virtualLimit(unsigned sysId)
{
    Limit l;
    void* info = nullptr;
    g_sys.f.EventSystem_GetEventBySystemID(g_sys.es, sysId, kInfoOnly, &info);
    if (!info) return l;
    g_sys.f.Event_GetPropertyByIndex(info, fmodprop::kMaxPlaybacks, &l.maxPlaybacks, kEventValue);
    g_sys.f.Event_GetPropertyByIndex(info, fmodprop::kMinDistance, &l.dmin, kEventValue);
    g_sys.f.Event_GetPropertyByIndex(info, fmodprop::kMaxDistance, &l.dmax, kEventValue);
    return l;
}

// Without the placement (the info-only instance where the far post left it), FMOD judges the near post as far.
void virtualControl(unsigned sysId)
{
    void* raw = nullptr;
    ea::fmod::Result rr;
    {
        std::lock_guard<std::recursive_mutex> l(g_sys.mx);
        rr = g_sys.f.EventSystem_GetEventBySystemID(g_sys.es, sysId, kEventDefault, &raw);
        if (!rr && raw) g_sys.f.Event_Stop(raw, 1);
    }
    printf("VIRT control: near request with the info-only instance left at the far position -> %s (FMOD %d)\n",
           !rr && raw ? "granted" : "refused", (int)rr);
}

// The limit filled at mid range: a farther post fails, a near one steals a voice.
void virtualPosts(uint32_t eventId, unsigned sysId, const Limit& lim)
{
    state.fmodEnds = 0;
    std::vector<uint32_t> ids;
    int got = 0;
    for (int i = 0; i < lim.maxPlaybacks && i < kVirtFill; ++i) {
        uint32_t id = ea::host::play({eventId, kVirtFirst + i, kAkEndOfEvent, (void*)onFmodCallback, nullptr});
        got += id != 0;
        ids.push_back(id);
        tickFor(kVirtPostMs);
    }
    tickFor(kVirtSettleMs);
    uint32_t farId = ea::host::play({eventId, kVirtFirst + kVirtFill, kAkEndOfEvent, (void*)onFmodCallback, nullptr});
    tickFor(kVirtSettleMs);
    virtualControl(sysId);
    tickFor(kVirtSettleMs);
    const uint32_t nearObj = kVirtFirst + kVirtFill + 1;
    uint32_t nearId = ea::host::play({eventId, nearObj, kAkEndOfEvent, (void*)onFmodCallback, nullptr});
    tickFor(kVirtEndMs);
    printf("VIRT fire_loop max playbacks %d: %d of %d started at the fill distance; far post %s; near post %s, %d "
           "stolen end(s)\n", lim.maxPlaybacks, got, (int)ids.size(), farId ? "STARTED (wrong)" : "failed",
           nearId ? "started" : "FAILED (wrong)", state.fmodEnds);
    for (uint32_t id : ids) ea::host::act(id, kAkStop);
    if (nearId) ea::host::act(nearId, kAkStop);
    if (farId) ea::host::act(farId, kAkStop);
    tickFor(kVirtEndMs);
}

// A 'virtual' event at its playback limit, through the hooks as the engine sets them.
void virtualEvent()
{
    auto fire = g_host.byId.find(hash::fnv1Lower(kLoop));
    if (fire == g_host.byId.end() || !(fire->second.flags & kVirtualFlag)) return;
    const Limit lim = virtualLimit(fire->second.sysId);
    float dFill = lim.dmin + kFillAt * (lim.dmax - lim.dmin), dFar = lim.dmin + kFarAt * (lim.dmax - lim.dmin);
    float dNear = lim.dmin * kNearAt;
    printf("VIRT fire_loop 3D min %.1f max %.1f: limit filled at %.1f, posts at %.1f and %.1f\n", lim.dmin, lim.dmax,
           dFill, dFar, dNear);
    listenerAtOrigin();
    for (int i = 0; i < kVirtSources; ++i) {
        const float p[6] = {0, 0, -(i < kVirtFill ? dFill : i == kVirtFill ? dFar : dNear), 0, 0, 1};   // Wwise z = -z
        ea::wwise::hkSetPos(kVirtFirst + i, p);
    }
    virtualPosts(fire->first, fire->second.sysId, lim);
}

// ---- --vo ----

// The 1.0 dialog lines whose class event the post hook falls back to: IN_Mumblers' students, Inc's intercom line.
struct LineEvent {
    const char* prefix;
    int number;
    const char* event;
};
constexpr LineEvent kLineEvents[] = {{"SN", 13, "dialog_default_2d"}, {"SP", 14, "dialog_default_2d"},
                                     {"ED", 47, "dialog_intercom"}};
constexpr uint32_t kDlgVersion10 = 11;   // DLG v11, the 1.0 scripts dlgTranscode reads
constexpr char kDialog30Prefix[] = "\\3.0_assets_sound_dialog_";   // decrypted 3.0 script names
// 3.0's dialog bank header (FUN_0182bf80, FUN_0182c000, FUN_0182d2f0): prefix chars, line range, media table.
constexpr size_t kPrefixAt = 8, kFirstLineAt = 0x10, kLastLineAt = 0x12, kMediaBaseAt = 0x14, kDataSizeAt = 0x18;
constexpr size_t kLinesAt = 0x20, kLineBytes = 4, kMediaWords = 3;   // a line: i32 (-1 none), u8 variants at +1
constexpr uint32_t kInMemory = 8;                                    // media word 2: the record is in the header
// Edna's ED019, the line --vo plays; its lipsync member in 1.0's Edna.zip.
constexpr char kEdnaFev[] = "Edna.fev", kEdnaBank[] = "Edna_str.fsb", kEdnaBankKey[] = "edna_str.fsb";
constexpr char kEdnaEvent[] = "dialog_edna", kEdnaAnnoDir[] = "sound/dialog/enu/ED/";
constexpr char kEdnaZip[] = "sound/dialog/ENU/Edna.zip";
constexpr int kEdnaLine = 19;
constexpr uint32_t kAkMarker = 4;
constexpr int kPlayMs = 20000, kSampleMs = 1000, kAheadMs = 50, kBehindMs = 250, kOverlapMs = 500;
constexpr size_t kMinSamples = 5;

// One 1.0 DLG script through dlgTranscode, and with 3.0's script of its name when given; the failures.
int transcodeDialog(const fs::path& path, const std::string& outDir, const char* dir30)
{
    std::string file = path.filename().string();
    std::vector<uint8_t> in, out;
    core::readFile(path.string(), in);
    const uint32_t ver = in.size() >= sizeof(uint32_t) ? bytes::u32At(in.data()) : 0;
    if (ver != kDlgVersion10) {
        printf("DLG %s skipped (v%u)\n", file.c_str(), ver);
        return 0;
    }
    di1::dlg::DlgStats st;
    std::string err;
    const di1::dlg::ScriptRequest req{in, g_vo.scripts.evNames, nullptr, nullptr, &g_vo.scripts.lineEvents};
    bool ok = di1::dlg::dlgTranscode(req, out, &st, &err);
    int bad = !ok;
    printf("DLG %s %s classes %d lines %d dup %d ranges %d named %d events %d unresolved %d %s\n", file.c_str(),
           ok ? "ok" : "FAIL", st.classes, st.lines, st.dupLines, st.ranges, st.named, st.events, st.unresolved,
           err.c_str());
    if (ok) writeFile(outDir + "\\" + path.stem().string() + ".dlg30", out);
    std::vector<uint8_t> b30;
    const std::string stem = text::lower(path.stem().string());
    if (!dir30 || !core::readFile(std::string(dir30) + kDialog30Prefix + stem + ".dlg", b30)) return bad;
    ok = di1::dlg::dlgTranscode({in, g_vo.scripts.evNames, &b30}, out, &st, &err);
    bad += !ok;
    printf("DLG %s +3.0 %s classes %d named %d events %d kept30 %d lines30 %d %s\n", file.c_str(), ok ? "ok" : "FAIL",
           st.classes, st.named, st.events, st.kept30, st.lines30, err.c_str());
    if (ok) writeFile(outDir + "\\" + path.stem().string() + ".merged.dlg30", out);
    return bad;
}

// The class events of kLineEvents; the failures.
int checkLineEvents()
{
    int bad = 0;
    for (const LineEvent& x : kLineEvents) {
        const uint32_t pfx = (uint32_t)(uint8_t)x.prefix[0] | (uint32_t)(uint8_t)x.prefix[1] << 8;
        auto le = g_vo.scripts.lineEvents.find(ea::dlgscript::dlgLineKey(pfx, (uint16_t)x.number));
        bool ok = le != g_vo.scripts.lineEvents.end() && le->second == x.event;
        bad += !ok;
        printf("LINEEVENT %s%03d -> %s (%s)\n", x.prefix, x.number,
               le != g_vo.scripts.lineEvents.end() ? le->second.c_str() : "-", ok ? "ok" : "BAD");
    }
    printf("LINEEVENTS %zu lines\n", g_vo.scripts.lineEvents.size());
    return bad;
}

// Media m of a 3.0 bank header: name offset, record offset, kind.
const uint32_t* mediaOf(const uint8_t* d, uint32_t m)
{
    const size_t word = (size_t)bytes::u16At(d + kMediaBaseAt) + (size_t)m * kMediaWords;
    return (const uint32_t*)(d + kLinesAt + word * sizeof(uint32_t));
}

// Every variant of line l (its entry at le) down to its media record; the failures.
int checkBankLine(const std::string& file, const uint8_t* d, uint32_t l, const uint8_t* le, ea::dialogue::DlgBank* b)
{
    int bad = 0;
    for (int v = 0; v < (le[1] > 1 ? le[1] : 1); ++v) {   // FUN_0182bf80's random pick, enumerated
        const uint32_t* me = mediaOf(d, bytes::u16At(le + 2) + v);
        bool inMem = bytes::u32At(d + kDataSizeAt) > me[1] && me[2] == kInMemory;
        const uint32_t* rec = (const uint32_t*)(d + me[1]);
        bool ok = inMem && me[0];
        if (ok) ok = (ea::dialogue::DlgBank*)(uintptr_t)rec[0] == b && rec[1] < b->idx().subs();
        bad += !ok;
        printf("NAME %s %c%c%03u %d %s %u %s\n", file.c_str(), d[kPrefixAt], d[kPrefixAt + 1], l, v,
               me[0] ? (const char*)d + me[0] : "-", rec[1], ok ? "ok" : "BAD");
    }
    return bad;
}

// Every 1.0 dialog bank opened as 1.0 does, its 3.0-layout header built, each line resolved; the failures.
int checkBanks()
{
    int bad = 0;
    for (auto& e : fs::directory_iterator(g_vo.reg.dir)) {
        if (e.path().extension() != ".fsb") continue;
        std::string file = e.path().filename().string();
        ea::dialogue::DlgBank* b = ea::dialogue::open(file);
        if (!b) {
            printf("BANK %s FAIL\n", file.c_str());
            ++bad;
            continue;
        }
        std::vector<uint8_t> h = dbnk::fromNames(b->idx().prefix(), {{b->idx(), (uint32_t)(uintptr_t)b}});
        const uint8_t* d = h.data();
        int lines = 0;
        const uint16_t lo = bytes::u16At(d + kFirstLineAt), hi = bytes::u16At(d + kLastLineAt);
        for (uint32_t l = lo; l <= hi; ++l) {
            const uint8_t* le = d + kLinesAt + (l - lo) * kLineBytes;
            if ((int32_t)bytes::u32At(le) == -1) continue;
            ++lines;
            bad += checkBankLine(file, d, l, le, b);
        }
        printf("BANK %s ok prefix %c%c lines %d subsounds %u header %u B\n", file.c_str(), d[kPrefixAt],
               d[kPrefixAt + 1], lines, b->idx().subs(), (unsigned)h.size());
        ea::dialogue::unload(file);
    }
    return bad;
}

// Edna's ED019: its bank and subsound, found the way the post hook finds them.
struct VoLine {
    ea::dialogue::DlgBank* bank = nullptr;
    int sub = -1;
    bool found = false;
};
VoLine ednaLine()
{
    const uint8_t* d = g_vo.reg.speakers[ea::dialogue::bank(kEdnaBankKey)->idx().prefix()].hdrs.back()->data();
    const uint8_t* le = d + kLinesAt + (kEdnaLine - (int)bytes::u16At(d + kFirstLineAt)) * (int)kLineBytes;
    const uint32_t* me = mediaOf(d, bytes::u16At(le + 2));
    VoLine v;
    v.found = ea::dialogue::media(d + me[1], &v.bank, &v.sub);
    return v;
}

// GetSourcePlayPosition through the hook (the lipsync clock, FUN_014cb180) once a second while the line plays.
std::vector<std::pair<DWORD, int>> positionSamples(uint32_t id, DWORD t0)
{
    std::vector<std::pair<DWORD, int>> posAt;
    DWORD lastSample = 0;
    for (int t = 0; t < kPlayMs && !state.voEnds; t += kTickMs) {
        Sleep(kTickMs);
        ea::wwise::tick();
        int pos = -1;
        DWORD now = GetTickCount() - t0;
        const bool due = !state.voEnds && now >= lastSample + kSampleMs;
        if (due && ea::wwise::hkSrcPos(id, &pos, 1) == 1) {
            posAt.emplace_back(now, pos);
            lastSample = now;
        }
    }
    return posAt;
}

// The position grows with the wall clock: never back, at most 50 ms ahead or 250 ms behind; the failures.
int checkPositions(const std::vector<std::pair<DWORD, int>>& posAt)
{
    int posBad = 0, prev = -1;
    for (auto& p : posAt) {
        posBad += p.second < prev || p.second > (int)p.first + kAheadMs || p.second + kBehindMs < (int)p.first;
        prev = p.second;
        printf("POS ED019 at %lu ms wall: position %d ms\n", (unsigned long)p.first, p.second);
    }
    printf("POS %d samples, %d off the wall clock (more than 250 ms behind or 50 ms ahead, or going back)\n",
           (int)posAt.size(), posBad);
    return posAt.size() < kMinSamples || posBad;
}

// The bank's next line while one plays (3.0 FUN_01830370 posts it first): the old ends once, the next plays on.
int checkOverlap(const VoLine& v)
{
    int endsBefore = state.voEnds;
    const ea::host::Post post{hash::fnv1Lower(kEdnaEvent), kTestObject, kAkEndOfEvent, (void*)onVoCallback, nullptr};
    uint32_t a = ea::host::playLine(post, v.bank->snd, v.sub);
    tickFor(kOverlapMs);
    uint32_t c = ea::host::playLine(post, v.bank->snd, v.sub + 1);
    ea::host::act(a, kAkStop);
    tickFor(kOverlapMs);
    bool cAlive;
    {
        std::lock_guard<std::recursive_mutex> l(g_sys.mx);
        cAlive = g_host.plays.count(c) && g_host.plays[c].ev;
    }
    printf("PLAY overlap: old id %u ended %d time(s), new"   // split: rulecheck's N19 matches the word 'new'
           " id %u %s\n", a, state.voEnds - endsBefore, c, cAlive ? "still playing" : "ENDED");
    return state.voEnds - endsBefore != 1 || !cAlive;
}

// ED019 through the post hook's path: its duration and end callbacks, its position, an overlap; the failures.
int checkEdnaLine(const VoLine& v)
{
    DWORD t0 = GetTickCount();
    const uint32_t flags = kAkEndOfEvent | kAkMarker | kAkDuration;
    const ea::host::Post post{hash::fnv1Lower(kEdnaEvent), kTestObject, flags, (void*)onVoCallback, nullptr};
    uint32_t id = v.found ? ea::host::playLine(post, v.bank->snd, v.sub) : 0;
    int bad = checkPositions(positionSamples(id, t0));
    unsigned ms = 0;
    void* s = nullptr;
    const bool sound = v.bank && v.bank->snd;
    if (sound && !g_sys.f.Sound_GetSubSound(v.bank->snd, v.sub, &s) && s) g_sys.f.Sound_GetLength(s, &ms, 1);
    printf("PLAY ED019 media %s sub %d (%s) id %u: duration cb %.0f ms (subsound %u ms), end cb %d after %lu ms\n",
           v.found ? "ours" : "NOT FOUND", v.sub, v.found ? v.bank->idx().subName(v.sub) : "-", id, state.voDuration,
           ms, state.voEnds, (unsigned long)(state.voEndAt - t0));
    const bool callbacks = state.voEnds == 1 && state.voDuration == (float)ms;
    bad += !v.found || !id || !callbacks;
    return bad + checkOverlap(v);
}

// Lipsync: the path 3.0 FUN_014c5550 builds from the media name is a member of 1.0's Edna.zip.
int checkAnno(const VoLine& v)
{
    std::string zr, mem, ap = std::string(kEdnaAnnoDir) + (v.found ? v.bank->idx().subName(v.sub) : "") + ".anno";
    bool anno = di1::audio::annoSource(ap.c_str(), zr, mem);
    printf("ANNO %s -> %s : %s\n", ap.c_str(), anno ? zr.c_str() : "-", anno ? mem.c_str() : "-");
    return !anno || zr != kEdnaZip;
}

// ---- --host ----

constexpr DWORD kHostWaitMs = 2000, kInstallMaxMs = 1000;
constexpr float kMixerSfx10 = 0.707946f, kMixerTolerance = 1e-5f;   // 1.0's at-rest 'sfx': -3 dB (mixercheck.py)

bool __cdecl standInHook(const char*, const std::vector<core::HookSpec>& feature)
{
    state.hostHooks += (int)feature.size();
    return true;
}
uintptr_t standInAddress(uint32_t) { return (uintptr_t)state.hostMemory; }

// di1::audio::install's config at its defaults, with the stand-ins for hooks and engine addresses.
di1::audio::Cfg standInCfg(const char* install10)
{
    di1::audio::Cfg cfg;
    cfg.install10 = install10;
    cfg.game30 = ".";
    cfg.installFeature = standInHook;
    cfg.rt = standInAddress;
    cfg.log = stdoutLog;
    return cfg;
}

}  // namespace

int testFmod(const Args& a)
{
    ea::setLog(stdoutLog);
    Dropped dropped;
    ea::wwise::Config ac = offlineAudio(a.argv[2]);
    ac.hostCfg.dropped = &dropped;
    if (env("FMOD_CAPS")) return probeFmodCaps(a.argv[2]);
    if (const char* sm = env("FMOD_SPEAKERS")) ac.systemCfg.testSpeakerMode = atoi(sm);
    if (!offlineHost(ac)) return 1;
    loadFevs(a);
    ProjectNames projName;
    for (auto& p : g_host.projects) projName[p.second.p] = p.first;
    for (auto& e : g_host.byId)
        printf("EV\t%s\t%s\t%u\n", projName[e.second.proj].c_str(), e.second.name.c_str(), e.second.flags);
    const auto tally = resolveNames(a.argv[3], projName, dropped);
    printCues();
    for (auto& t : tally) printf("TALLY %s %d\n", t.first.c_str(), t.second);
    lifecycle();
    musicPrompt();
    cineSync();
    if (const char* cr = env("CUE_RUN")) probeCueRun(cr);
    if (const char* er = env("EV_RUN")) probeEvRun(er, onFmodCallback);
    virtualEvent();
    return 0;
}

int testVo(const Args& a)
{
    ea::setLog(stdoutLog);
    std::string inst = a.argv[2], outDir = a.argv[3];
    if (!offlineHost(offlineAudio(inst))) return 1;
    ea::dialogue::pickLanguage();   // no engine: ENU
    ea::dialogue::buildEvNames();
    int bad = 0;
    for (auto& e : fs::directory_iterator(inst + kDialog10))
        if (e.path().extension() == ".dlg") bad += transcodeDialog(e.path(), outDir, a.argv[4]);
    bad += checkLineEvents();
    bad += checkBanks();
    ea::host::load(kEdnaFev);
    if (!ea::dialogue::load(kEdnaBank)) {   // vo.mgr null: no registry step
        printf("PLAY no Edna_str.fsb\n");
        return 1;
    }
    const VoLine v = ednaLine();
    bad += checkEdnaLine(v);
    bad += checkAnno(v);
    printf("VO bad %d\n", bad);
    return bad != 0;
}

// di1::audio::install under the loader lock, as in Pentane_Main: it returns at once; the host starts after the release.
int testHost(const Args& a)
{
    ea::setLog(stdoutLog);
    typedef LONG(NTAPI * LockFn)(ULONG, ULONG*, ULONG_PTR*);
    typedef LONG(NTAPI * UnlockFn)(ULONG, ULONG_PTR);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    LockFn lock = (LockFn)GetProcAddress(nt, "LdrLockLoaderLock");
    UnlockFn unlock = (UnlockFn)GetProcAddress(nt, "LdrUnlockLoaderLock");
    if (!lock || !unlock) {
        printf("HOST no LdrLockLoaderLock\n");
        return 1;
    }
    ULONG lockState = 0;
    ULONG_PTR cookie = 0;
    if (lock(0, &lockState, &cookie) < 0) {
        printf("HOST lock failed\n");
        return 1;
    }
    ea::wwise::Config ac = di1::audio::config(standInCfg(a.argv[2]));
    ac.systemCfg.nosound = true;
    DWORD t0 = GetTickCount();
    bool ok = ea::wwise::install(ac);
    DWORD tInstall = GetTickCount() - t0;
    DWORD w = WaitForSingleObject(g_sys.done, kHostWaitMs);
    unlock(0, cookie);
    bool up = ea::system::ready();
    DWORD tUp = GetTickCount() - t0;
    bool init = false, mixer = false;
    {
        std::lock_guard<std::recursive_mutex> l(g_sys.mx);
        init = g_host.projects.count("initialize.fev") != 0;
        mixer = g_host.mixer.count("sfx") && fabs(g_host.mixer["sfx"] - kMixerSfx10) < kMixerTolerance;
    }
    printf("HOST install under the loader lock: %s in %lu ms, %d hooks; host init while locked: %s; ready %d after %lu "
           "ms; Initialize.fev %s; mixer sfx %s\n", ok ? "returned" : "FAILED", (unsigned long)tInstall,
           state.hostHooks, w == WAIT_TIMEOUT ? "blocked (as expected)" : "RAN", (int)up, (unsigned long)tUp,
           init ? "loaded" : "MISSING", mixer ? "0.708" : "WRONG");
    const bool timely = tInstall < kInstallMaxMs && w == WAIT_TIMEOUT, loaded = up && init && mixer;
    return !(ok && timely && loaded);
}

}  // namespace octtest

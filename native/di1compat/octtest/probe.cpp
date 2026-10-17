// Probes run on request: one archive member, read10's candidates, one dialog line, FMOD caps, cue and event runs.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include "../audio.hpp"
#include "../base/text.hpp"
#include "../games/di1/archives.hpp"
#include "../third_party/miniz/miniz.h"
#include "files.hpp"
#include "run.hpp"
#include "fmodtools.hpp"
#include "modes.hpp"

namespace octtest {
namespace {

constexpr int kNoArchive = -2;   // --read10: the archive file is not there

// read10's probe of one candidate: the member's index in the archive, -1 not there, kNoArchive.
void probeArchive(const char* install10, const std::string& zr, const std::string& member)
{
    mz_zip_archive z{};
    const std::string full = di1::archivePath(install10, zr);
    const bool there = GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES;
    bool ok = there && mz_zip_reader_init_file(&z, full.c_str(), 0);
    int i = ok ? mz_zip_reader_locate_file(&z, member.c_str(), nullptr, 0) : kNoArchive;
    printf("%s : %s -> %d\n", zr.c_str(), member.c_str(), i);
    if (ok) mz_zip_reader_end(&z);
}

// ---- --voplay ----

// FMOD_EVENT_INFO (fmod_event.h): the fields --voplay prints.
constexpr size_t kEventInfoBytes = 0x80, kInfoPositionAt = 4, kInfoLengthAt = 8, kInfoChannelsAt = 12;
constexpr size_t kInfoInstancesAt = 16, kInfoAudibilityAt = 36;
constexpr int kVoPlayMs = 15000, kVoInfoMs = 250;

int infoInt(const uint8_t* info, size_t at)
{
    int v;
    memcpy(&v, info + at, sizeof v);
    return v;
}
float infoFloat(const uint8_t* info, size_t at)
{
    float v;
    memcpy(&v, info + at, sizeof v);
    return v;
}

// The FEVs of a comma list by their 1.0 names (NOFILE for a missing one).
void loadFevList(const std::string& list)
{
    for (std::string l = list; !l.empty();) {
        size_t c = l.find(',');
        std::string n = audio::g.name10(l.substr(0, c));
        if (n.empty()) printf("NOFILE %s\n", l.substr(0, c).c_str());
        else audio::g.loadTree(n, nullptr);
        l = c == std::string::npos ? "" : l.substr(c + 1);
    }
}

// The bank's first subsound whose name starts with line, or -1.
int findLine(audio::DlgBank* b, const char* line)
{
    for (uint32_t i = 0; i < b->idx().subs(); ++i)
        if (!_strnicmp(b->idx().subName(i), line, strlen(line))) return (int)i;
    return -1;
}

// The listener at the origin facing +z; the speaker at argv[7..9] when given, else at the listener.
void placeSpeaker(const Args& a)
{
    float lp[3] = {0, 0, 0}, fwd[3] = {0, 0, 1}, up[3] = {0, 1, 0};
    audio::g.f.EventSystem_Set3DListenerAttributes(audio::g.es, 0, lp, nullptr, fwd, up);
    if (a.argc <= 9) return;
    audio::V3 p;
    for (int k = 0; k < 3; ++k) p.v[k] = (float)atof(a.argv[7 + k]);
    std::lock_guard<std::mutex> l(audio::g.posMx);
    audio::g.pos[kTestObject] = p;
}

std::string volumeLabel3(const char* name, void* cat)
{
    float cv = -1;
    audio::g.f.EventCategory_GetVolume(cat, &cv);
    char v[32];
    snprintf(v, sizeof v, " %.3f", cv);
    return name + std::string(v);
}

// The event's playback limit, mode, volume, 3D range and category chain.
void printEventProps(void* ev)
{
    auto& f = audio::g.f;
    unsigned mode = 0;
    float vol = -1, mn = -1, mx = -1;
    int pos3d = -1, pri = -1, maxpb = -1, beh = -1;
    f.Event_GetPropertyByIndex(ev, fmodprop::kMode, &mode, kEventValue);
    f.Event_GetPropertyByIndex(ev, fmodprop::kVolume, &vol, kEventValue);
    f.Event_GetPropertyByIndex(ev, fmodprop::kMinDistance, &mn, kEventValue);
    f.Event_GetPropertyByIndex(ev, fmodprop::kMaxDistance, &mx, kEventValue);
    f.Event_GetPropertyByIndex(ev, fmodprop::kPosition3d, &pos3d, kEventValue);
    f.Event_GetPropertyByIndex(ev, fmodprop::kPriority, &pri, kEventValue);
    f.Event_GetPropertyByIndex(ev, fmodprop::kMaxPlaybacks, &maxpb, kEventValue);
    // 1 steal oldest, 2 steal newest, 3 steal quietest, 4 fail, 5 fail if quietest
    f.Event_GetPropertyByIndex(ev, fmodprop::kMaxPlaybacksBehavior, &beh, kEventValue);
    printf("LIMIT priority %d max playbacks %d behavior %d\n", pri, maxpb, beh);
    void* c = nullptr;
    f.Event_GetCategory(ev, &c);
    const std::string chain = categoryChain(c, kChainDepth, volumeLabel3);
    printf("PROPS mode %x volume %.3f min %.1f max %.1f 3dpos %d categories %s\n", mode, vol, mn, mx, pos3d,
           chain.c_str());
}

// The subsound length playLine recorded as AK_Duration, read before any tick.
void printSubsoundLength(uint32_t id)
{
    unsigned subMs = 0;   // getSubSound on a streamed bank would seek it and end the line playing
    {
        std::lock_guard<std::mutex> d(audio::g.doneMx);
        for (auto& x : audio::g.durs)
            if (x.first == id) subMs = (unsigned)x.second;
    }
    printf("SUBSOUND length %u ms (the event ends with it when the subsound is what plays)\n", subMs);
}

// FMOD_EVENT_INFO each 250 ms until the line ends or 15 s pass: whether it is mixed.
void printPlayback(uint32_t id, void* ev)
{
    for (int t = 0; t <= kVoPlayMs; t += kVoInfoMs) {
        uint8_t info[kEventInfoBytes] = {};
        bool live;
        {
            std::lock_guard<std::recursive_mutex> l(audio::g.mx);
            live = audio::g.plays.count(id) && audio::g.plays[id].ev;
        }
        if (!live) {
            printf("INFO t %d ended\n", t);
            break;
        }
        audio::g.f.Event_GetInfo(ev, nullptr, nullptr, info);
        printf("INFO t %d pos %d len %d channels %d instances %d audibility %.3f\n", t, infoInt(info, kInfoPositionAt),
               infoInt(info, kInfoLengthAt), infoInt(info, kInfoChannelsAt), infoInt(info, kInfoInstancesAt),
               infoFloat(info, kInfoAudibilityAt));
        tickFor(kVoInfoMs);
    }
}

// ---- --fmod's CUE_RUN and EV_RUN ----

constexpr int kCueRunSecs = 120, kCueSampleMs = 250, kMsPerSec = 1000;
constexpr uint32_t kEvObject = 200;                       // EV_RUN's source
constexpr uint32_t kLoadFirst = 1000;                     // EV_LOAD's instances
constexpr int kLoadCount = 70, kLoadMs = 500, kEvWindowMs = 3000, kEvSampleMs = 50;
constexpr int kWaveSamples = 512, kWaveChannels = 8;      // System::getWaveData per channel
constexpr float kLoud = 0.01f;
constexpr unsigned kNoState = 0xffffffff;
using StateFn = audio::FR(__stdcall*)(void*, unsigned*);
using WaveFn = audio::FR(__stdcall*)(void*, float*, int, int);

// EV_GEOM=lx,ly,lz,fx,fy,fz,ux,uy,uz,sx,sy,sz: listener and source in engine space, as audiotrace10 logs them.
void evGeometry(const char* gm)
{
    float v[12] = {};
    if (sscanf_s(gm, "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f", v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6, v + 7, v + 8,
                 v + 9, v + 10, v + 11) != 12)
        return;
    std::lock_guard<std::mutex> p(audio::g.posMx);
    memcpy(audio::g.lpos, v, sizeof audio::g.lpos);
    memcpy(audio::g.lfwd, v + 3, sizeof audio::g.lfwd);
    memcpy(audio::g.lup, v + 6, sizeof audio::g.lup);
    memcpy(audio::g.pos[kEvObject].v, v + 9, sizeof audio::g.pos[kEvObject].v);
}

// EV_LOAD=<event>:<count>: that many instances of a loop playing 300 units away first, as in-game traffic does.
void evLoad(const std::string& s)
{
    size_t c = s.find(':');
    int cnt = c == std::string::npos ? kLoadCount : atoi(s.c_str() + c + 1), got = 0;
    const float away[6] = {0, 0, -300, 0, 0, 1};
    for (int i = 0; i < cnt; ++i) {
        audio::hkSetPos(kLoadFirst + i, away);
        got += audio::g.play(audio::fnv1(s.substr(0, c)), kLoadFirst + i, 0, nullptr, nullptr) != 0;
    }
    tickFor(kLoadMs);
    int playing = -1;
    typedef audio::FR(__stdcall * ChFn)(void*, int*);
    if (auto ch = (ChFn)GetProcAddress(GetModuleHandleA("fmodex.dll"), "FMOD_System_GetChannelsPlaying"))
        ch(audio::g.sys, &playing);
    printf("EVLOAD %s: %d of %d started at 300 units; FMOD channels playing %d\n", s.substr(0, c).c_str(), got, cnt,
           playing);
}

std::string volumeLabel4(const char* name, void* cat)
{
    float cv = -1;
    audio::g.f.EventCategory_GetVolume(cat, &cv);
    return std::string(name) + " " + std::to_string(cv).substr(0, 4);
}

// The playing event's volume, priority, 3D range and category chain; returns its FMOD event.
void* evProperties(uint32_t id, const std::string& n)
{
    void* ev = nullptr;
    std::string chain;
    float vol = -1, dmin = -1, dmax = -1;
    int prio = -1;
    {
        std::lock_guard<std::recursive_mutex> l(audio::g.mx);
        auto it = audio::g.plays.find(id);
        if (it != audio::g.plays.end()) ev = it->second.ev;
        if (ev) {
            audio::g.f.Event_GetPropertyByIndex(ev, fmodprop::kVolume, &vol, kInstanceValue);
            audio::g.f.Event_GetPropertyByIndex(ev, fmodprop::kMinDistance, &dmin, kInstanceValue);
            audio::g.f.Event_GetPropertyByIndex(ev, fmodprop::kMaxDistance, &dmax, kInstanceValue);
            audio::g.f.Event_GetPropertyByIndex(ev, fmodprop::kPriority, &prio, kInstanceValue);
            void* c = nullptr;
            audio::g.f.Event_GetCategory(ev, &c);
            chain = categoryChain(c, kChainDepth, volumeLabel4);
        }
    }
    printf("EVRUN %s: id %u, volume %.2f, priority %d, 3D %.1f..%.1f, categories %s\n", n.c_str(), id, vol, prio, dmin,
           dmax, chain.c_str());
    return ev;
}

// The event's parameters: one whose default sits where a volume curve is 0 plays in silence.
void evParams(void* ev, const std::string& n)
{
    typedef audio::FR(__stdcall * NumFn)(void*, int*);
    typedef audio::FR(__stdcall * ByIdxFn)(void*, int, void**);
    typedef audio::FR(__stdcall * InfoFn)(void*, int*, char**);
    typedef audio::FR(__stdcall * ValFn)(void*, float*);
    typedef audio::FR(__stdcall * RangeFn)(void*, float*, float*);
    HMODULE em = GetModuleHandleA("fmod_event.dll");
    auto num = (NumFn)GetProcAddress(em, "FMOD_Event_GetNumParameters");
    auto byIdx = (ByIdxFn)GetProcAddress(em, "FMOD_Event_GetParameterByIndex");
    auto info = (InfoFn)GetProcAddress(em, "FMOD_EventParameter_GetInfo");
    auto val = (ValFn)GetProcAddress(em, "FMOD_EventParameter_GetValue");
    auto range = (RangeFn)GetProcAddress(em, "FMOD_EventParameter_GetRange");
    std::lock_guard<std::recursive_mutex> l(audio::g.mx);
    int np = 0;
    const bool lists = num && byIdx, reads = info && val && range;
    const bool ready = ev && lists && reads;
    if (!ready || num(ev, &np)) return;
    for (int i = 0; i < np; ++i) {
        void* p = nullptr;
        char* pn = nullptr;
        float v = 0, lo = 0, hi = 0;
        if (byIdx(ev, i, &p) || !p) continue;
        info(p, nullptr, &pn);
        val(p, &v);
        range(p, &lo, &hi);
        printf("EVRUN %s param %s = %.2f (range %.2f..%.2f)\n", n.c_str(), pn ? pn : "?", v, lo, hi);
    }
}

// One EV_RUN event while it plays: its FMOD state, and the master output's peaks (what FMOD really mixes).
struct EvRun {
    uint32_t id;
    void* ev;
    StateFn getState;
    WaveFn wave;
    float peak = 0, ch[kWaveChannels] = {};
};

// One sample of the event; false once its play has ended.
bool sampleEvent(EvRun& r, unsigned& st)
{
    std::lock_guard<std::recursive_mutex> l(audio::g.mx);
    auto it = audio::g.plays.find(r.id);
    if (it == audio::g.plays.end() || !it->second.ev) return false;
    if (r.getState) r.getState(r.ev, &st);
    float buf[kWaveSamples];
    for (int c = 0; c < kWaveChannels && r.wave; ++c) {
        if (r.wave(audio::g.sys, buf, kWaveSamples, c)) continue;
        for (float s : buf) {
            r.ch[c] = (std::max)(r.ch[c], fabsf(s));
            r.peak = (std::max)(r.peak, fabsf(s));
        }
    }
    return true;
}

// Samples each 50 ms for EV_SECS (default 3 s): state changes, then the output peak per channel.
void evPeaks(EvRun& r, const std::string& n)
{
    unsigned was = kNoState;
    const int window = env("EV_SECS") ? atoi(env("EV_SECS")) * kMsPerSec : kEvWindowMs;
    int firstLoud = -1;   // the first time the output passes kLoud
    for (int t = 0; t <= window && r.ev; t += kEvSampleMs) {
        unsigned st = 0;
        if (!sampleEvent(r, st)) {
            printf("EVRUN %s ended at %d ms\n", n.c_str(), t);
            break;
        }
        if (st != was) {
            printf("EVRUN %s at %d ms: state %x\n", n.c_str(), t, st);
            was = st;
        }
        if (firstLoud < 0 && r.peak > kLoud) firstLoud = t;
        tickFor(kEvSampleMs);
    }
    printf("EVRUN %s output peak %.4f (first above 0.01 at %d ms)%s; per channel %.3f %.3f %.3f %.3f %.3f %.3f %.3f "
           "%.3f\n", n.c_str(), r.peak, firstLoud, r.wave ? "" : " (getWaveData missing)", r.ch[0], r.ch[1], r.ch[2],
           r.ch[3], r.ch[4], r.ch[5], r.ch[6], r.ch[7]);
}

void evRunOne(const std::string& n, audio::AkCallback cb, StateFn getState)
{
    EvRun r{audio::g.play(audio::fnv1(n), kEvObject, kAkEndOfEvent, (void*)cb, nullptr), nullptr, getState,
            nullptr};
    r.ev = evProperties(r.id, n);
    evParams(r.ev, n);
    r.wave = (WaveFn)GetProcAddress(GetModuleHandleA("fmodex.dll"), "FMOD_System_GetWaveData");
    evPeaks(r, n);
}

}  // namespace

int probeZip(const Args& a)
{
    mz_zip_archive z{};
    if (!mz_zip_reader_init_file(&z, a.argv[2], 0)) {
        printf("init FAILED (%s)\n", mz_zip_get_error_string(mz_zip_get_last_error(&z)));
        return 1;
    }
    int i = mz_zip_reader_locate_file(&z, a.argv[3], nullptr, 0);
    size_t n = 0;
    void* d = i < 0 ? nullptr : mz_zip_reader_extract_to_heap(&z, (mz_uint)i, &n, 0);
    printf("locate %d, extract %s %zu B (%s)\n", i, d ? "ok" : "FAILED", n,
           mz_zip_get_error_string(mz_zip_get_last_error(&z)));
    mz_free(d);
    return d ? 0 : 1;
}

int probeMember(const Args& a)
{
    std::string got;
    bool r = readArc30(a.argv[2], a.argv[3], got);
    printf("%s %zu B\n", r ? "ok" : "FAILED", got.size());
    return r ? 0 : 1;
}

// read10's candidates in the plugin's order (games/di1/archives.hpp, cfg defaults); the first hit is served.
int probeRead10(const Args& a)
{
    std::string p = text::norm(a.argv[3]);
    for (size_t cut = p.rfind('/'); cut != std::string::npos && cut > 0; cut = p.rfind('/', cut - 1))
        for (const std::string& zr : di1::candidateArchives(p, cut, di1::AssetBuffers::Search))
            if (!zr.empty()) probeArchive(a.argv[2], zr, p.substr(cut + 1));
    for (const char* zr : di1::kRootArchives) probeArchive(a.argv[2], zr, p);
    return 0;
}

// One dialog line through the plugin's playLine with no sound device: whether it is mixed.
int probeVoPlay(const Args& a)
{
    audio::logfn = stdoutLog;
    std::string inst = a.argv[2];
    if (!audio::g.init(inst, true)) return 1;
    audio::g.loadTree("Initialize.fev", "Permanent");
    loadFevList(a.argv[3]);
    audio::vo.root10 = inst + kDialog10;
    audio::vo.pickLanguage();
    if (!audio::vo.load(a.argv[5])) {
        printf("NOBANK %s\n", a.argv[5]);
        return 1;
    }
    audio::DlgBank* b = audio::vo.banks[audio::lower(a.argv[5])].get();
    const int sub = findLine(b, a.argv[6]);
    if (sub < 0) {
        printf("NOLINE %s\n", a.argv[6]);
        return 1;
    }
    placeSpeaker(a);
    uint32_t evId = audio::fnv1(a.argv[4]);
    auto it = audio::g.byId.find(evId);
    if (it == audio::g.byId.end()) {
        printf("NOEVENT %s\n", a.argv[4]);
        return 1;
    }
    uint32_t id = audio::g.playLine(evId, kTestObject, 0, nullptr, nullptr, b->snd, sub);
    void* ev = nullptr;
    {
        std::lock_guard<std::recursive_mutex> l(audio::g.mx);
        if (audio::g.plays.count(id)) ev = audio::g.plays[id].ev;
    }
    printf("PLAYLINE %s %s (sub %d) event %s flags %u -> id %u ev %p\n", a.argv[5], b->idx().subName(sub), sub,
           a.argv[4], it->second.flags, id, ev);
    if (!ev) return 1;
    printEventProps(ev);
    printSubsoundLength(id);
    printPlayback(id, ev);
    return 0;
}

// FMOD_CAPS: the default device's speaker mode as the plugin's init reads it, before init (nothing plays).
int probeFmodCaps(const char* install10)
{
    audio::Api& f = audio::g.f;
    void *es = nullptr, *sys = nullptr;
    unsigned caps = 0;
    int mode = -1;
    if (!f.load(install10) || f.EventSystem_Create(&es) || f.EventSystem_GetSystemObject(es, &sys)) return 1;
    printf("FMODCAPS getDriverCaps -> %d\n", (int)f.System_GetDriverCaps(sys, 0, &caps, nullptr, &mode));
    printf("FMODCAPS caps %x speaker mode %d (2 stereo, 3 quad, 4 surround, 5 5.1, 6 7.1)\n", caps, mode);
    return 0;
}

// CUE_RUN=<cue>[:seconds]: prompt that cue and report when its prompt stops being active.
void probeCueRun(const char* spec)
{
    std::string c = spec;
    int secs = kCueRunSecs;
    size_t colon = c.find(':');
    if (colon != std::string::npos) {
        secs = atoi(c.c_str() + colon + 1);
        c.resize(colon);
    }
    audio::music.endPrompt();
    audio::g.f.MusicSystem_Reset(audio::g.ms);
    if (!audio::music.ensure(kPlayset10) || !audio::music.begin(c.c_str())) {
        printf("CUERUN %s: no prompt\n", c.c_str());
        return;
    }
    int was = -1;
    for (int t = 0; t <= secs * kMsPerSec; t += kCueSampleMs) {
        tickFor(kCueSampleMs);
        int active = -1;
        audio::g.f.MusicPrompt_IsActive(audio::music.prompt, &active);
        if (active == was) continue;
        printf("CUERUN %s at %.2f s: active %d\n", c.c_str(), t / (double)kMsPerSec, active);
        was = active;
    }
    printf("CUERUN %s done after %d s, active %d\n", c.c_str(), secs, was);
}

// EV_RUN=<event>[,<event>...]: each played 15 units in front of the listener, as the dock cutscene's blasts are.
void probeEvRun(const char* events, audio::AkCallback cb)
{
    StateFn getState = (StateFn)GetProcAddress(GetModuleHandleA("fmod_event.dll"), "FMOD_Event_GetState");
    listenerAtOrigin();
    const float at[6] = {0, 0, -15, 0, 0, 1};   // Wwise z = -engine z
    audio::hkSetPos(kEvObject, at);
    if (const char* gm = env("EV_GEOM")) evGeometry(gm);
    if (const char* ld = env("EV_LOAD")) evLoad(ld);
    std::string all = events;
    for (size_t b = 0, e; b < all.size(); b = e + 1) {
        e = all.find(',', b);
        if (e == std::string::npos) e = all.size();
        evRunOne(all.substr(b, e - b), cb, getState);
    }
}

}  // namespace octtest

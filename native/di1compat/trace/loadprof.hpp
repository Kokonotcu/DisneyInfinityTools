// fpslog10 frame timing and loadprof10's load profiler with its sampler thread. core, base. Frame, loader, own threads.
#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "../base/format.hpp"
#include "../core/hooks.hpp"
#include "../core/log.hpp"
#include "../core/memcheck.hpp"
#include "../core/stack.hpp"

namespace trace::loadprof {

struct Config {
    bool fpsLog = false;    // fpslog10: each second, frame time and the main thread's wait on rendering
    bool loadProf = false;  // loadprof10: per-load timings and a sampling profile (needs the frame sync too)
    std::string gameDir;    // the sampler writes <gameDir>\di1compat_prof.tsv
};

// Installs the hooks c asks for (H1) and starts the sampler with all of loadprof10's; false when one was refused.
inline bool install(const Config& c);
// QPC milliseconds; valid once install() ran with either switch on.
inline double nowMs();
// Records the calling thread as one that opens files (the sampler watches up to 8).
inline void noteOpenThread();
// Frames the frame sync has seen.
inline LONG frameNo();

namespace detail {

namespace eng {
// the resource scheduler DAT_0213d4f8: +0x5c main, +0x60/+0x64 the Resource Loader fibers, +0x68 the screen's draw loop
constexpr uint32_t kScheduler = 0x0213D4F8;
constexpr size_t kSchedulerFibers = 0x5c;
}  // namespace eng

constexpr int kRoles = 5;
constexpr int kOtherRole = 4;               // a fiber that is none of the scheduler's four
constexpr LONG kUntracked = 5;              // the sampler's fiber column for threads other than the frame thread
constexpr int kOpenThreads = 8;
constexpr int kFiberSlots = 8;              // fibers counted per thread
constexpr int kStackCopy = 384;             // stack words copied per sample
constexpr int kCallers = 4;                 // exe return addresses kept per sample (key words 3..6)
constexpr int kBusiestSampled = 3;
constexpr int kBusiestLogged = 6;
constexpr double kCpuPeriodMs = 1000;
constexpr double kDumpPeriodMs = 5000;
constexpr LONGLONG kTimerDue = -10000;      // 1 ms, in 100 ns units, relative
constexpr DWORD kTimerWaitMs = 50;
constexpr DWORD kHighResTimer = 0x2;        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
constexpr double kFileTimeMs = 1e4;         // FILETIME units per ms
constexpr uint32_t kPathText = 0xff00ff00u; // stack words with these bits zero are UTF-16 path text

inline const char* const kFiberRole[kRoles] = {"main", "load0", "load1", "screen", "other"};

typedef void(__cdecl* FrameSyncFn)();
typedef void(__cdecl* FiberSwitchFn)(void* from, void* to, int ctx);
typedef uint32_t(__fastcall* FileWaitFn)(void* done, void* edx);
typedef int*(__cdecl* OctLoadFn)(int* out, char** path, uint32_t a3, uint32_t a4);

struct State {
    // originals: set once by hook() in install()
    FrameSyncFn g_frameSync = nullptr;
    FiberSwitchFn g_fiberSwitch = nullptr;
    FileWaitFn g_fileWait = nullptr;
    OctLoadFn g_octLoad = nullptr;
    LARGE_INTEGER freq{};                   // set once in install()
    std::string profPath;                   // set once in install(), before the sampler starts
    std::atomic<DWORD> frameTid{0};         // the frame sync's thread; read by the sampler and hkOctLoad
    std::atomic<LONG> frameNo{0};
    std::atomic<LONG> fiberRole{kOtherRole};   // the scheduler fiber the frame thread runs; read by the sampler
    double fiberMs[kRoles] = {};            // cumulative per role; frame thread only (A-45)
    double fiberSince = 0;                  // frame thread only (A-45)
    std::atomic<LONG> openTidN{0};
    std::atomic<DWORD> openTids[kOpenThreads] = {};
    uintptr_t selfLo = 0, selfHi = 0;       // di1compat.dll's image; set by the sampler thread, read by it only
    // the frame sync's per-second sums: frame thread only
    LARGE_INTEGER last{}, second{};
    double sumFrame = 0, maxFrame = 0, sumWait = 0;
    int frames = 0;
    double lastFiberMs[kRoles] = {};
};
inline State g_state;

// Per-fiber counters (fibers share their thread, so thread_local alone would mix them).
struct FiberProf {
    void* fiber;
    int depth;    // .oct loads running on this fiber
    int waits;    // file waits
    double waitMs;
};
inline thread_local FiberProf g_fibers[kFiberSlots];

inline double qpcMs()
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)g_state.freq.QuadPart;
}

// The role of fiber f in the resource scheduler (main, load0, load1, screen; 4 otherwise).
inline int fiberRole(void* f)
{
    const uint8_t* s = *(const uint8_t* const*)core::rt(eng::kScheduler);
    for (int i = 0; s && f && i < 4; ++i)
        if (*(void* const*)(s + eng::kSchedulerFibers + 4 * i) == f) return i;
    return kOtherRole;
}

// A role's cumulative ms, the running stretch included.
inline double fiberMsNow(int role)
{
    const State& s = g_state;
    const bool running = s.fiberRole == role && s.fiberSince;
    return s.fiberMs[role] + (running ? qpcMs() - s.fiberSince : 0);
}

// The slot of the fiber running now (GetCurrentFiber: on a thread that never became a fiber, a per-thread value).
inline FiberProf& fiberProf()
{
    void* f = GetCurrentFiber();
    for (auto& e : g_fibers)
        if (e.fiber == f) return e;
    for (auto& e : g_fibers) {
        if (e.depth) continue;
        e = {f, 0, 0, 0};
        return e;
    }
    return g_fibers[0];   // ponytail: 8 busy fibers per thread at most
}

// FUN_0041e480, cdecl (from, to, ctx): every scheduler fiber switch; only the scheduler's thread calls it.
inline void __cdecl hkFiberSwitch(void* from, void* to, int ctx)
{
    State& s = g_state;
    double now = qpcMs();
    if (s.fiberSince) s.fiberMs[s.fiberRole] += now - s.fiberSince;
    s.fiberSince = now;
    s.fiberRole = fiberRole(to);
    s.g_fiberSwitch(from, to, ctx);
}

// FUN_0041fc10, thiscall on a completion: the engine's file-done wait (a 1 ms wait plus a yield). Per wait.
inline uint32_t __fastcall hkFileWait(void* done, void*)
{
    double t = qpcMs();
    uint32_t r = g_state.g_fileWait(done, nullptr);
    FiberProf& p = fiberProf();   // after: the wait may switch fibers, but it returns on the one that waited
    p.waitMs += qpcMs() - t;
    ++p.waits;
    return r;
}

// One outermost .oct load's line: wall ms, frames, each scheduler fiber's ms meanwhile, the file waits.
struct OctLoad {
    bool frameThread;
    double t0, waitMs0;
    int waits0;
    LONG frame0;
    double fiber0[kRoles];
};

inline OctLoad octLoadStart(const FiberProf& fp)
{
    const State& s = g_state;
    OctLoad l{GetCurrentThreadId() == s.frameTid, qpcMs(), fp.waitMs, fp.waits, s.frameNo, {}};
    for (int i = 0; i < kRoles; ++i) l.fiber0[i] = l.frameThread ? fiberMsNow(i) : 0;
    return l;
}

inline void logOctLoad(const OctLoad& l, const char* p, const FiberProf& fq)
{
    const State& s = g_state;
    char fib[160] = "";
    if (l.frameThread)
        format::fitOrFailFast(fib, sizeof(fib), ", fibers main %.1f load0 %.1f load1 %.1f screen %.1f",
                              fiberMsNow(0) - l.fiber0[0], fiberMsNow(1) - l.fiber0[1], fiberMsNow(2) - l.fiber0[2],
                              fiberMsNow(3) - l.fiber0[3]);
    core::logf("prof oct load %s: %.1f ms, frames %ld, on %s%s, file waits %d / %.1f ms", p, qpcMs() - l.t0,
               s.frameNo - l.frame0, l.frameThread ? kFiberRole[s.fiberRole] : "other-thread", fib, fq.waits - l.waits0,
               fq.waitMs - l.waitMs0);
}

// FUN_00786d00, cdecl (out, char** path, a3, a4): one .oct asset load (Zoo::AssetFileTask). Outermost per fiber logged.
inline int* __cdecl hkOctLoad(int* out, char** path, uint32_t a3, uint32_t a4)
{
    State& s = g_state;
    FiberProf& fp = fiberProf();
    const int nested = fp.depth++;
    if (nested) {
        int* r = s.g_octLoad(out, path, a3, a4);
        --fiberProf().depth;
        return r;
    }
    OctLoad l = octLoadStart(fp);
    char p[200] = "";
    if (path) core::safeCopy(p, sizeof(p), *path);
    int* r = s.g_octLoad(out, path, a3, a4);
    FiberProf& fq = fiberProf();   // the same slot (same fiber), looked up again: never hold a slot across a switch
    --fq.depth;
    logOctLoad(l, p, fq);
    return r;
}

// The fps line for the last second, then the sums restart. Frame thread only.
inline void logSecond(const LARGE_INTEGER& a)
{
    State& s = g_state;
    char fib[120] = "";
    if (s.fiberSince) {   // loadprof10: the scheduler's fibers over the last second
        double d[kRoles];
        for (int i = 0; i < kRoles; ++i) {
            double v = fiberMsNow(i);
            d[i] = v - s.lastFiberMs[i];
            s.lastFiberMs[i] = v;
        }
        format::fitOrFailFast(fib, sizeof(fib), "; fibers main %.0f load0 %.0f load1 %.0f screen %.0f ms", d[0], d[1],
                              d[2], d[3]);
    }
    core::logf("fps %.1f: frame %.2f ms avg, %.2f max; main thread waits on rendering %.2f ms of it%s",
               1000.0 * s.frames / s.sumFrame, s.sumFrame / s.frames, s.maxFrame, s.sumWait / s.frames, fib);
    s.sumFrame = s.maxFrame = s.sumWait = 0;
    s.frames = 0;
    s.second = a;
}

// FUN_0047aab0, cdecl: once per frame on the main thread (FUN_0047f0a0); spins while rendering holds the last frame.
inline void __cdecl hkFrameSync()
{
    State& s = g_state;
    s.frameTid = GetCurrentThreadId();
    ++s.frameNo;
    LARGE_INTEGER a, b;
    QueryPerformanceCounter(&a);
    s.g_frameSync();
    QueryPerformanceCounter(&b);
    if (s.last.QuadPart) {
        double frame = 1000.0 * (a.QuadPart - s.last.QuadPart) / s.freq.QuadPart;
        s.sumFrame += frame;
        s.maxFrame = (std::max)(s.maxFrame, frame);
        ++s.frames;
        s.sumWait += 1000.0 * (b.QuadPart - a.QuadPart) / s.freq.QuadPart;
    } else {
        s.second = a;
    }
    s.last = a;
    if (s.frames && a.QuadPart - s.second.QuadPart >= s.freq.QuadPart) logSecond(a);
}

// Copies the top of a suspended thread's stack, bounded by its region: no fault can reach the vectored handlers.
inline int copyStack(const uint32_t* sp, uint32_t* out, int max)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(sp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
    size_t room = ((const uint8_t*)mbi.BaseAddress + mbi.RegionSize - (const uint8_t*)sp) / 4;
    int n = (int)(std::min)(room, (size_t)max);
    memcpy(out, sp, n * 4);
    return n;
}

inline bool afterCallSafe(uint32_t v)
{
    __try {
        return core::afterCall(v);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Whether v returns into di1compat.dll (right after a call instruction).
inline bool ourRetSafe(uint32_t v)
{
    const State& s = g_state;
    if (v < s.selfLo + core::kLongestCall || v >= s.selfHi) return false;
    __try {
        const uint8_t* p = (const uint8_t*)(uintptr_t)v;
        const bool rm = core::callRm(p - 6) || core::callRm(p - 3) || core::callRm(p - 2);
        return core::callRel32(p - 5) || rm;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 'module!export+0xoff' for a code address outside the exe; in di1compat.dll 'di1compat.dll+0x<rva>' (use the .map).
inline std::string symbolAt(uintptr_t a)
{
    HMODULE m = nullptr;
    char full[MAX_PATH];
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    const BOOL found = GetModuleHandleExA(flags, (LPCSTR)a, &m);
    if (!found || !m) return "?";
    const DWORD named = GetModuleFileNameA(m, full, MAX_PATH);
    if (!named) return "?";
    std::string s = strrchr(full, '\\') ? strrchr(full, '\\') + 1 : full;
    char off[48];
    if ((uintptr_t)m == g_state.selfLo) {
        snprintf(off, sizeof(off), "+0x%X", (unsigned)(a - g_state.selfLo));
        return s + off;
    }
    const uint8_t* b = (const uint8_t*)m;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(b + ((const IMAGE_DOS_HEADER*)b)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    uint32_t rva = (uint32_t)(a - (uintptr_t)m), best = 0;
    const char* name = nullptr;
    if (dir.VirtualAddress && dir.Size) {
        const IMAGE_EXPORT_DIRECTORY* ex = (const IMAGE_EXPORT_DIRECTORY*)(b + dir.VirtualAddress);
        const uint32_t* fns = (const uint32_t*)(b + ex->AddressOfFunctions);
        const uint32_t* names = (const uint32_t*)(b + ex->AddressOfNames);
        const uint16_t* ords = (const uint16_t*)(b + ex->AddressOfNameOrdinals);
        for (DWORD i = 0; i < ex->NumberOfNames; ++i) {
            uint32_t f = fns[ords[i]];
            if (f > rva || f <= best) continue;
            best = f;
            name = (const char*)(b + names[i]);
        }
    }
    snprintf(off, sizeof(off), "+0x%X", rva - best);
    return s + "!" + (name ? name : "") + off;
}

// A sample's key: tid, outside the exe?, EIP, 4 callers, ours (outermost own frame's RVA + 1; 0 none), fiber.
typedef std::array<uint32_t, 9> SampleKey;

// The sampler thread's own maps (sampler thread only).
struct Sampler {
    DWORD self = 0, pid = 0;
    std::map<DWORD, HANDLE> handles;
    std::map<DWORD, uint64_t> cpuLast;
    std::map<SampleKey, uint32_t> counts;
    std::map<uint32_t, std::string> syms;
    std::map<DWORD, uint32_t> samples;
    std::vector<DWORD> busy;              // the busiest other threads of the last second
    int window = 0;
};

inline HANDLE threadHandle(Sampler& sm, DWORD tid)
{
    auto it = sm.handles.find(tid);
    if (it != sm.handles.end()) return it->second;
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    return sm.handles[tid] = h;
}

// "frame", "open", "frame+open" or "busy".
inline std::string roleOf(DWORD tid)
{
    const State& s = g_state;
    std::string r = tid == s.frameTid ? "frame" : "";
    for (LONG i = 0; i < s.openTidN && i < kOpenThreads; ++i)
        if (s.openTids[i] == tid) r += r.empty() ? "open" : "+open";
    return r.empty() ? std::string("busy") : r;
}

// The leaf as text: the static EIP in the exe, else 'm<module>!<export>+<off>'.
inline std::string leafName(Sampler& sm, const SampleKey& k)
{
    char b[16];
    if (!k[1]) {
        snprintf(b, sizeof(b), "%08X", k[2]);
        return b;
    }
    auto it = sm.syms.find(k[2]);
    if (it == sm.syms.end()) it = sm.syms.emplace(k[2], symbolAt(k[2])).first;
    return "m" + it->second;
}

// A sample's key from its context and stack copy; nothing is allocated or locked while the thread is suspended.
inline SampleKey sampleKey(DWORD tid, uintptr_t eip, LONG fib, const uint32_t* stk, int n)
{
    const State& s = g_state;
    const bool inExe = core::inExeImage(eip);
    SampleKey k{tid, inExe ? 0u : 1u, inExe ? core::toStaticVa(eip) : (uint32_t)eip, 0, 0, 0, 0, 0, (uint32_t)fib};
    if (!inExe && eip >= s.selfLo && eip < s.selfHi) k[7] = (uint32_t)(eip - s.selfLo) + 1;
    for (int i = 0, f = 3; i < n && f < 3 + kCallers; ++i) {
        if ((stk[i] & kPathText) && afterCallSafe(stk[i])) {
            k[f++] = core::toStaticVa(stk[i]);
            continue;
        }
        const bool ownFrame = !inExe && f == 3;   // outermost wins
        if (ownFrame && ourRetSafe(stk[i])) k[7] = (uint32_t)(stk[i] - s.selfLo) + 1;
    }
    return k;
}

// One sample of one thread: suspend, copy EIP and the stack top, resume, then count.
inline void sampleThread(Sampler& sm, DWORD tid)
{
    const State& s = g_state;
    HANDLE h = threadHandle(sm, tid);
    if (!h || tid == sm.self) return;
    CONTEXT c{};
    c.ContextFlags = CONTEXT_CONTROL;
    uint32_t stk[kStackCopy];
    int n = 0;
    if (SuspendThread(h) == (DWORD)-1) return;
    const bool ok = GetThreadContext(h, &c) != 0;
    if (ok) n = copyStack((const uint32_t*)(uintptr_t)c.Esp, stk, kStackCopy);
    const LONG fib = tid == s.frameTid ? s.fiberRole.load() : kUntracked;   // the fiber the frame thread runs
    ResumeThread(h);
    if (!ok) return;
    ++sm.counts[sampleKey(tid, c.Eip, fib, stk, n)];
    ++sm.samples[tid];
}

// The threads sampled: the frame thread, every thread that opened files, the busiest others.
inline std::vector<DWORD> sampledThreads(const Sampler& sm)
{
    const State& s = g_state;
    std::vector<DWORD> tids;
    const DWORD frame = s.frameTid;
    if (frame) tids.push_back(frame);
    for (LONG i = 0; i < s.openTidN && i < kOpenThreads; ++i) {
        const DWORD o = s.openTids[i];
        if (std::find(tids.begin(), tids.end(), o) == tids.end()) tids.push_back(o);
    }
    for (DWORD t : sm.busy)
        if (std::find(tids.begin(), tids.end(), t) == tids.end()) tids.push_back(t);
    return tids;
}

// CPU time per thread over the last second: the busiest are logged, the 3 busiest others sampled next.
inline void cpuSecond(Sampler& sm)
{
    std::vector<std::pair<uint64_t, DWORD>> d;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != sm.pid || te.th32ThreadID == sm.self) continue;
        HANDLE h = threadHandle(sm, te.th32ThreadID);
        FILETIME a, b, k, u;
        if (!h || !GetThreadTimes(h, &a, &b, &k, &u)) continue;
        uint64_t t = ((uint64_t)k.dwHighDateTime << 32 | k.dwLowDateTime) +
                     ((uint64_t)u.dwHighDateTime << 32 | u.dwLowDateTime);
        uint64_t& last = sm.cpuLast[te.th32ThreadID];
        if (last) d.push_back({t - last, te.th32ThreadID});
        last = t;
    }
    CloseHandle(snap);
    std::sort(d.rbegin(), d.rend());
    sm.busy.clear();
    std::string line;
    uint64_t total = 0;
    for (auto& [t, tid] : d) total += t;
    for (size_t i = 0; i < d.size() && i < kBusiestLogged; ++i) {
        std::string r = roleOf(d[i].second);
        if (r == "busy" && sm.busy.size() < kBusiestSampled) sm.busy.push_back(d[i].second);
        char b[80];
        // never cut: at most 43 characters
        snprintf(b, sizeof(b), " %s %u %.0f ms;", r.c_str(), (unsigned)d[i].second, d[i].first / kFileTimeMs);
        line += b;
    }
    core::logf("prof cpu last second: process %.0f ms over %zu threads; busiest:%s", total / kFileTimeMs, d.size(),
               line.c_str());
}

// The window's counts into di1compat_prof.tsv (a new file for window 1); role 'frame/load0' = that fiber.
inline void writeWindow(Sampler& sm)
{
    FILE* f = nullptr;
    const errno_t err = fopen_s(&f, g_state.profPath.c_str(), sm.window == 1 ? "w" : "a");
    if (err || !f) return;
    if (sm.window == 1) fprintf(f, "window\ttid\trole\tleaf\tcaller\tcaller2\tcaller3\tcaller4\tours\tcount\n");
    for (auto& [k, v] : sm.counts) {
        const bool fiber = k[8] < kRoles;
        fprintf(f, "%d\t%u\t%s%s%s\t%s\t%08X\t%08X\t%08X\t%08X\t%X\t%u\n", sm.window, k[0], roleOf(k[0]).c_str(),
                fiber ? "/" : "", fiber ? kFiberRole[k[8]] : "", leafName(sm, k).c_str(), k[3], k[4], k[5], k[6],
                k[7] ? k[7] - 1 : 0xffffffffu, v);
    }
    fclose(f);
}

// Per thread: its share in the exe and in each module, and in di1compat code.
inline void logThreadShares(Sampler& sm, DWORD tid, uint32_t n)
{
    std::map<std::string, uint32_t> mods;
    uint32_t ours = 0;
    for (auto& [k, v] : sm.counts) {
        if (k[0] != tid) continue;
        std::string s = k[1] ? leafName(sm, k).substr(1) : "exe";
        mods[s.substr(0, s.find_first_of("!+"))] += v;
        if (k[7]) ours += v;
    }
    std::string line;
    for (auto& [m, v] : mods) {
        char b[MAX_PATH + 24];
        // never cut: a module name is at most MAX_PATH - 1 characters
        snprintf(b, sizeof(b), " %s %.0f%%", m.c_str(), 100.0 * v / n);
        line += b;
    }
    core::logf("prof window %d %s %u: %u samples, %.1f%% in di1compat code (its calls included):%s", sm.window,
               roleOf(tid).c_str(), (unsigned)tid, n, 100.0 * ours / n, line.c_str());
}

// Every 5 s: the TSV window and the per-thread summary; the counts restart.
inline void dumpWindow(Sampler& sm)
{
    ++sm.window;
    writeWindow(sm);
    for (auto& [tid, n] : sm.samples) logThreadShares(sm, tid, n);
    sm.counts.clear();
    sm.samples.clear();
}

// di1compat.dll's image, for the samples' 'ours' column.
inline void findSelf()
{
    State& s = g_state;
    HMODULE me = nullptr;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    GetModuleHandleExA(flags, (LPCSTR)&findSelf, &me);
    const uint8_t* b = (const uint8_t*)me;
    s.selfLo = (uintptr_t)me;
    const IMAGE_NT_HEADERS* nt = me ? (const IMAGE_NT_HEADERS*)(b + ((const IMAGE_DOS_HEADER*)b)->e_lfanew) : nullptr;
    s.selfHi = nt ? s.selfLo + nt->OptionalHeader.SizeOfImage : 0;
}

// The sampler (~1 kHz, high-resolution timer): samples, a CPU line each second, a window every 5 s; never exits.
inline DWORD WINAPI profThread(void*)
{
    HANDLE tm = CreateWaitableTimerExW(nullptr, nullptr, kHighResTimer, TIMER_ALL_ACCESS);
    core::logf("loadprof10: sampler up (%s timer)", tm ? "high-resolution" : "Sleep(1)");
    Sampler sm;
    sm.self = GetCurrentThreadId();
    sm.pid = GetCurrentProcessId();
    findSelf();
    double nextCpu = qpcMs() + kCpuPeriodMs, nextDump = qpcMs() + kDumpPeriodMs;
    for (;;) {
        if (tm) {
            LARGE_INTEGER due;
            due.QuadPart = kTimerDue;
            SetWaitableTimer(tm, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(tm, kTimerWaitMs);
        } else {
            Sleep(1);
        }
        for (DWORD tid : sampledThreads(sm)) sampleThread(sm, tid);
        double now = qpcMs();
        if (now >= nextCpu) {
            nextCpu = now + kCpuPeriodMs;
            cpuSecond(sm);
        }
        if (now >= nextDump) {
            nextDump = now + kDumpPeriodMs;
            dumpWindow(sm);
        }
    }
}

}  // namespace detail

inline double nowMs() { return detail::qpcMs(); }

inline void noteOpenThread()
{
    thread_local bool seen;
    if (seen) return;
    seen = true;
    LONG i = ++detail::g_state.openTidN - 1;
    if (i < detail::kOpenThreads) detail::g_state.openTids[i] = GetCurrentThreadId();
}

inline LONG frameNo() { return detail::g_state.frameNo; }

inline bool install(const Config& c)
{
    const bool frames = c.fpsLog || c.loadProf;
    if (!frames) return true;
    detail::State& s = detail::g_state;
    QueryPerformanceFrequency(&s.freq);
    bool ok = core::hook("frame sync (fps log)", 0x0047AAB0, "83EC105657B9????????E8", (void*)&detail::hkFrameSync,
                         (void**)&s.g_frameSync);
    if (!c.loadProf) return ok;
    const bool profiler = core::installFeature("loadprof10", {
        {"fiber switch (loadprof10)", 0x0041E480, "8B44240C85C0740C", (void*)&detail::hkFiberSwitch,
         (void**)&s.g_fiberSwitch},
        {"file-done wait (loadprof10)", 0x0041FC10, "56578B3D????????8BF1", (void*)&detail::hkFileWait,
         (void**)&s.g_fileWait},
        {".oct asset load (loadprof10)", 0x00786D00, "6AFF68????????64A100000000", (void*)&detail::hkOctLoad,
         (void**)&s.g_octLoad},
    });
    if (!profiler) return false;
    s.profPath = c.gameDir + "\\di1compat_prof.tsv";
    if (HANDLE h = CreateThread(nullptr, 0, detail::profThread, nullptr, 0, nullptr); h) CloseHandle(h);
    return ok;
}

}  // namespace trace::loadprof

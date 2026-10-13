// di1compat: Pentane plugin that serves Disney Infinity 1.0 data to the 3.0 engine (research plan
// D:\DI-Extract\_research\PLAN.md, INF-1/INF-6). The 1.0 files are read in place from the 1.0 install's own
// zips and handed to the engine as Genie::MemFile streams through its asset open, FUN_0041cb10 (worlds,
// textures, Lua ... all go through it, archive members included).
// Log: <game dir>\di1compat.log (Pentane's own log function is not used: its calling convention is unverified).
// Config: <plugin dir>\di1compat.cfg  install10 = <1.0 install> | serve10 = 0|1 | trace = 0|1 |
//         memtest = <path substring>[,...]  (serve those 3.0 files from a memory copy: proves the MemFile path)
#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include "minhook/MinHook.h"
#include "third_party/miniz/miniz.h"

// ---- Pentane plugin ABI (upstream source/plugin.hpp; DI3G strings match) ----
struct SemVer { uint16_t major, minor, patch; };
struct PluginInformation {
    char name[256];
    char author[256];
    uint8_t uuid[16];
    SemVer version;
    SemVer minimum_pentane_version;
    char reserved[484];
};
static_assert(sizeof(PluginInformation) == 0x400, "Pentane expects 0x400 bytes");

extern "C" PluginInformation Pentane_PluginInformation = {
    "di1compat", "DI3Tools",
    {0x6d, 0x1c, 0x0e, 0x5a, 0x3f, 0x41, 0x4b, 0x8e, 0x9a, 0x21, 0xd1, 0x7c, 0x0a, 0x03, 0x30, 0x10},
    {0, 1, 0}, {0, 0, 0}, {}};
extern "C" size_t Pentane_PluginDependencyCount = 0;

// ---- log ----
static CRITICAL_SECTION g_logLock;
static FILE* g_log;

static void logf(const char* fmt, ...)
{
    if (!g_log) return;
    EnterCriticalSection(&g_logLock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    vfprintf(g_log, fmt, a);
    va_end(a);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logLock);
}

// Copies a C string the engine handed us without trusting the pointer.
static void safeCopy(char* out, size_t cap, const char* src)
{
    __try {
        size_t i = 0;
        for (; src && i + 1 < cap && src[i]; ++i) out[i] = src[i];
        out[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        strcpy_s(out, cap, "<unreadable>");
    }
}

// ---- hooks ----
static uintptr_t g_base;   // runtime base of DisneyInfinity3.exe (ASLR); static addresses assume 0x400000
static uintptr_t rt(uint32_t staticVa) { return g_base + (staticVa - 0x400000u); }

// Installs a detour only if the target still holds the bytes we analysed (our exe carries other patches).
// '??' skips a byte: absolute addresses inside a prologue are relocated by ASLR.
static bool hook(const char* name, uint32_t staticVa, const char* prologueHex, void* detour, void** original)
{
    const uint8_t* target = (const uint8_t*)rt(staticVa);
    size_t i = 0;
    for (const char* p = prologueHex; p[0] && p[1]; p += 2, ++i) {
        if (p[0] == '?') continue;
        unsigned v;
        sscanf_s(p, "%2x", &v);
        if (target[i] != (uint8_t)v) break;
    }
    if (i * 2 != strlen(prologueHex)) {
        logf("hook %s @ %08X refused: prologue differs", name, staticVa);
        return false;
    }
    MH_STATUS s = MH_CreateHook((LPVOID)target, detour, original);
    if (s == MH_OK) s = MH_EnableHook((LPVOID)target);
    logf("hook %s @ %08X %s (%d)", name, staticVa, s == MH_OK ? "ok" : "refused", (int)s);
    return s == MH_OK;
}

// ---- config: <plugin dir>\di1compat.cfg, lines "key = value" ----
static struct {
    char install10[MAX_PATH] = "";   // the 1.0 install (its assets\ holds plain zips)
    bool serve10 = false;            // serve paths 3.0 lacks from the 1.0 zips
    bool trace = true;               // log every open
    char memtest[600] = "";          // comma list of path substrings served from a memory copy
} g_cfg;

static void loadConfig(const char* dir)
{
    char path[MAX_PATH];
    sprintf_s(path, "%s\\di1compat.cfg", dir);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") || !f) { logf("no %s: defaults (serve10 off)", path); return; }
    char line[600];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[MAX_PATH];
        if (line[0] == '#' || sscanf_s(line, " %63[^= ] = %259[^\r\n]", key, (unsigned)sizeof(key), val, (unsigned)sizeof(val)) != 2)
            continue;
        if (!strcmp(key, "install10")) strcpy_s(g_cfg.install10, val);
        else if (!strcmp(key, "serve10")) g_cfg.serve10 = atoi(val) != 0;
        else if (!strcmp(key, "trace")) g_cfg.trace = atoi(val) != 0;
        else if (!strcmp(key, "memtest")) strcpy_s(g_cfg.memtest, val);
    }
    fclose(f);
    logf("config: install10=%s serve10=%d trace=%d memtest=%s", g_cfg.install10, g_cfg.serve10, g_cfg.trace,
         g_cfg.memtest);
}

// ---- VFS1 v0: the 1.0 install's own zips, read in place (never copied or rewritten) ----
// An engine path <dir>/<name>/<rest> lives in assets\<dir>\<name>.zip as member <rest> (the layout extract.py
// unpacks), and deeper or shallower splits are tried too. Archives open lazily; miniz is not thread-safe.
static std::mutex g_zipLock;
static std::map<std::string, mz_zip_archive*> g_zips;   // nullptr = no such zip

static mz_zip_archive* zipFor(const std::string& rel)
{
    auto it = g_zips.find(rel);
    if (it != g_zips.end()) return it->second;
    std::string full = std::string(g_cfg.install10) + "\\assets\\" + rel;
    for (char& c : full) if (c == '/') c = '\\';
    mz_zip_archive* z = nullptr;
    if (GetFileAttributesA(full.c_str()) != INVALID_FILE_ATTRIBUTES) {
        z = new mz_zip_archive{};
        if (!mz_zip_reader_init_file(z, full.c_str(), 0)) { delete z; z = nullptr; }
    }
    g_zips[rel] = z;
    return z;
}

// -> heap buffer (mz_free it) with the member's bytes, or nullptr. where = "zip:member" for the proof line.
static void* read10(const char* enginePath, size_t* size, std::string* where)
{
    std::string p = enginePath;
    for (char& c : p) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
    if (p.rfind("assets/", 0) == 0) p.erase(0, 7);
    std::lock_guard<std::mutex> lock(g_zipLock);
    for (size_t cut = p.rfind('/'); cut != std::string::npos && cut > 0; cut = p.rfind('/', cut - 1)) {
        mz_zip_archive* z = zipFor(p.substr(0, cut) + ".zip");
        if (!z) continue;
        std::string member = p.substr(cut + 1);
        int i = mz_zip_reader_locate_file(z, member.c_str(), nullptr, 0);
        if (i < 0) continue;
        void* data = mz_zip_reader_extract_to_heap(z, (mz_uint)i, size, 0);
        if (data) { *where = p.substr(0, cut) + ".zip:" + member; return data; }
    }
    return nullptr;
}

// A Genie::MemFile holding a copy of data, built the way the engine builds one (0x0041d... reader path):
// new(0x20), ctor FUN_012ec100(0x400), vtable+0x0c setSize(n, 0), fill data (+0x18), vtable+0x08 seek(0, 0, 0).
static int* memFile(const void* data, size_t n)
{
    typedef void*(__cdecl * NewFn)(size_t);
    typedef void(__fastcall * CtorFn)(void* self, void* edx, uint32_t grow);
    typedef void(__fastcall * SetSizeFn)(void* self, void* edx, uint32_t size, uint32_t force);
    typedef uint32_t(__fastcall * SeekFn)(void* self, void* edx, uint32_t off, uint32_t hi, uint32_t whence);
    void* f = ((NewFn)rt(0x00406E80))(0x20);
    if (!f) return nullptr;
    ((CtorFn)rt(0x012EC100))(f, nullptr, 0x400);
    void** vt = *(void***)f;
    ((SetSizeFn)vt[3])(f, nullptr, (uint32_t)n, 0);
    memcpy(((void**)f)[6], data, n);
    ((SeekFn)vt[2])(f, nullptr, 0, 0, 0);
    return (int*)f;
}

// FUN_0041cb10: the engine's asset open (cdecl; path, flags, ...) -> stream object or NULL.
typedef int*(__cdecl* OpenFn)(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5);
static OpenFn g_open;

static bool memtestMatch(const char* p)
{
    char list[600];
    strcpy_s(list, g_cfg.memtest);
    char* ctx = nullptr;
    for (char* t = strtok_s(list, ",", &ctx); t; t = strtok_s(nullptr, ",", &ctx))
        if (*t && strstr(p, t)) return true;
    return false;
}

// Reads a whole engine stream through the Genie::File interface (+0x10 size, +0x14 read, +0x2c destroy) and
// returns a MemFile copy; the original stream is destroyed.
static int* copyToMemFile(int* s, size_t* outSize)
{
    typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
    typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
    typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
    void** vt = *(void***)s;
    uint32_t n = ((SizeFn)vt[4])(s, nullptr);
    std::string buf(n, '\0');
    uint32_t got = ((ReadFn)vt[5])(s, nullptr, &buf[0], n);
    ((DtorFn)vt[11])(s, nullptr, 1);
    *outSize = got;
    return got == n ? memFile(buf.data(), n) : nullptr;
}

static int* __cdecl hkOpen(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5)
{
    int* r = g_open(path, flags, a3, a4, a5);
    char p[300];
    safeCopy(p, sizeof(p), path);
    if (r && g_cfg.memtest[0] && !(flags & 1) && memtestMatch(p)) {
        size_t n = 0;
        r = copyToMemFile(r, &n);
        logf("memtest %s %u B -> %s", p, (unsigned)n, r ? "MemFile" : "FAILED");
        return r;
    }
    if (!r && g_cfg.serve10 && g_cfg.install10[0] && !(flags & 1)) {   // flags bit 0 = write (never served)
        size_t n = 0;
        std::string where;
        if (void* data = read10(p, &n, &where)) {
            r = memFile(data, n);
            logf("served %s %u B from 1.0 %s codec=raw -> %s", p, (unsigned)n, where.c_str(), r ? "ok" : "FAILED");
            mz_free(data);
            return r;
        }
    }
    if (g_cfg.trace) logf("open %s flags=%X -> %s", p, flags & 0xff, r ? "ok" : "MISSING");
    return r;
}

extern "C" void __stdcall Pentane_Main();
extern "C" void __stdcall Pentane_Main()
{
    InitializeCriticalSection(&g_logLock);
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy_s(slash + 1, path + MAX_PATH - (slash + 1), "di1compat.log");
    fopen_s(&g_log, path, "w");
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    logf("di1compat 0.2.0 loaded; exe base %08X", (unsigned)g_base);
    char dir[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&Pentane_Main, &self);
    GetModuleFileNameA(self, dir, MAX_PATH);
    if (char* s = strrchr(dir, '\\')) *s = 0;
    loadConfig(dir);

    if (MH_Initialize() != MH_OK) {
        logf("MinHook init failed; no hooks");
        return;
    }
    hook("AssetOpen", 0x0041CB10, "6AFF68????????", (void*)&hkOpen, (void**)&g_open);
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

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
#include <share.h>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include "minhook/MinHook.h"
#include "octx.hpp"
#include "bent.hpp"
#include "chd.hpp"
#include "render.hpp"
#include "arc30.hpp"
#include "datamap.hpp"
#include "luachunk.hpp"
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
        std::string dir = p.substr(0, cut), member = p.substr(cut + 1);
        // <dir>.zip (worlds/in_city.zip), a folder-named archive inside <dir> (gamedb/core/core.zip,
        // shaders/shaders.zip: the engine path leaves the archive's own name out), or a per-asset archive inside
        // <dir> named after the member (characters/inc_bob/inc_bob_ps.zip holds inc_bob_ps.oct)
        std::string stem = member.substr(0, member.find('.'));
        std::string zips[3] = {dir + ".zip", dir + "/" + dir.substr(dir.rfind('/') + 1) + ".zip", dir + "/" + stem + ".zip"};
        for (auto& zr : zips) {
            mz_zip_archive* z = zipFor(zr);
            if (!z) continue;
            int i = mz_zip_reader_locate_file(z, member.c_str(), nullptr, 0);   // case-insensitive
            if (i < 0) continue;
            void* data = mz_zip_reader_extract_to_heap(z, (mz_uint)i, size, 0);
            if (data) { *where = zr + ":" + member; return data; }
        }
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

// ---- load-time transcodes: 1.0 bytes in, the form the 3.0 reader takes out (the 1.0 file is never rewritten) ----
static bool endsWith(const char* s, const char* suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && _stricmp(s + a - b, suffix) == 0;
}

static std::string norm(const char* path)
{
    std::string p = path;
    for (char& c : p) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
    if (p.rfind("assets/", 0) == 0) p.erase(0, 7);
    return p;
}

static bool read10s(const std::string& path, std::string& out)
{
    size_t n = 0;
    std::string where;
    void* d = read10(path.c_str(), &n, &where);
    if (!d) return false;
    out.assign((const char*)d, n);
    mz_free(d);
    return true;
}

typedef int*(__cdecl* OpenFn)(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5);
static OpenFn g_open;

// A 3.0 asset through the engine's own open (archives, keys, index), read whole; the stream is destroyed.
static bool read30(const std::string& path, std::string& out)
{
    typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
    typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
    typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
    int* s = g_open(path.c_str(), 0, 0, 0, nullptr);
    if (!s) return false;
    void** vt = *(void***)s;
    uint32_t n = ((SizeFn)vt[4])(s, nullptr);
    out.assign(n, '\0');
    uint32_t got = n ? ((ReadFn)vt[5])(s, nullptr, &out[0], n) : 0;
    ((DtorFn)vt[11])(s, nullptr, 1);
    return got == n;
}

// A 3.0 asset by its extract path ('3:X/name/member' = member of <game>/assets/X/name.zip, the extract.py layout),
// read straight from the install's archive (arc30.hpp) -- the engine's own path conventions vary by asset kind.
static std::string g_gameDir;
static bool read30x(const std::string& extract, std::string& out)
{
    std::string rel = extract.substr(2);
    size_t cut = rel.rfind('/');
    if (cut == std::string::npos) return false;
    std::string arc = g_gameDir + "/assets/" + rel.substr(0, cut) + ".zip";
    for (char& c : arc) if (c == '/') c = '\\';
    return arc30::readMember(arc, rel.substr(cut + 1), out);
}

// World session: the .oct request transcodes the whole world (its .mtb/.vbuf/.ibuf decisions are coupled), and the
// engine's next requests for those files are answered from here. 1.0 textures: hash -> (zip, member).
static std::mutex g_sessLock;
static std::map<std::string, std::string> g_served;
static std::map<std::string, std::pair<std::string, std::string>> g_tex10;
static render::Table g_render;
static bool g_renderOk;

static void indexTextures10(const std::string& zipRel)
{
    std::lock_guard<std::mutex> lock(g_zipLock);
    mz_zip_archive* z = zipFor(zipRel);
    if (!z) return;
    std::lock_guard<std::mutex> l2(g_sessLock);
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(z); ++i) {
        char name[300];
        mz_zip_reader_get_filename(z, i, name, sizeof(name));
        std::string n = name;
        for (char& c : n) c = (char)tolower((unsigned char)c);
        if (n.rfind("textures/", 0) == 0 && endsWith(n.c_str(), ".tbody"))
            g_tex10.emplace(n.substr(9, n.size() - 9 - 6), std::make_pair(zipRel, std::string(name)));
    }
}

// D3D11 rejects block-compressed textures below 4x4; a 1x1/2x2 single-mip DXT file already holds one whole
// 4x4 block, so only the header size changes (conv.copy_texture).
static void padSmallDxt(std::string& d)
{
    if (d.size() < 128 || d.compare(0, 4, "DDS ") || d.compare(84, 3, "DXT")) return;
    uint32_t h = oct::u((const uint8_t*)d.data() + 12, 4), w = oct::u((const uint8_t*)d.data() + 16, 4);
    uint32_t mips = oct::u((const uint8_t*)d.data() + 28, 4);
    if ((w < 4 || h < 4) && mips <= 1) {
        uint32_t v[2] = {h < 4 ? 4u : h, w < 4 ? 4u : w};
        memcpy(&d[12], v, 8);
    }
}

// The 1.0 zip a path resolves to (read10's rule), for indexing that asset's own textures/ folder.
static std::string zip10For(const std::string& path)
{
    size_t n = 0;
    std::string where;
    void* d = read10(path.c_str(), &n, &where);
    if (!d) return "";
    mz_free(d);
    return where.substr(0, where.find(':'));
}

// The render half of a 1.0 asset with a material bundle -- a world, a mission graph, ... (render.hpp): materials
// onto 3.0 shaders and, where the asset has buffers, its vertices into formats those shaders take.
static bool transcodeAssetRender(const std::string& p, oct::File& f, std::string& note)
{
    if (!g_renderOk) { note += "render table missing "; return false; }
    size_t b = p.rfind('/');
    if (b == std::string::npos) return false;
    std::string dir = p.substr(0, b), stem = p.substr(b + 1, p.size() - b - 1 - 4);
    std::string mtb, vbuf, ibuf;
    if (!read10s(dir + "/" + stem + ".mtb", mtb)) return false;     // nothing to render
    read10s(dir + "/" + stem + "_0.vbuf", vbuf);
    read10s(dir + "/" + stem + "_0.ibuf", ibuf);
    render::IO io;
    io.read30 = read30x;
    io.read10 = read10s;
    io.log = [](const std::string& s) { logf("%s", s.c_str()); };
    render::Result r;
    if (!render::transcodeWorld(g_render, io, f, stem, mtb, vbuf, ibuf, r)) {
        for (auto& pr : r.problems) logf("render problem %s: %s", p.c_str(), pr.c_str());
        note += "render FAILED ";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_sessLock);
        g_served[dir + "/" + stem + ".mtb"] = r.mtb;
        if (!r.vbuf.empty()) g_served[dir + "/" + stem + "_0.vbuf"] = r.vbuf;
        if (!r.ibuf.empty()) g_served[dir + "/" + stem + "_0.ibuf"] = r.ibuf;
        if (!r.ibuf2.empty()) g_served[dir + "/" + stem + "_2.ibuf"] = r.ibuf2;
    }
    std::string zip = zip10For(p);
    if (!zip.empty()) indexTextures10(zip);
    char buf[200];
    sprintf_s(buf, "+render(real %d same-sh %d generic %d, %u 1.0 textures) ", r.tiers["real"], r.tiers["same-sh"],
              r.tiers["generic"], (unsigned)r.textures10.size());
    note += buf;
    return true;
}

// .oct (worlds, missions, exploders ...). Steps, each from the research plan (PLAN.md section 4):
//   NavMeshPool removed: 3.0's blob parser FUN_01909fb0 trusts the layout, and the 1.0 layout differs (in-game
//   2026-10-12: raw in_city.oct faulted at 0x190A4A7 inside it). Interim until the navmesh codec (OCT-8).
//   SubNetwork counts added (octx::addCounts, OCT-1): without them the basis arenas are sized 0 (in-game: NULL
//   DataNodeBasisConversion in FUN_004d0160).
//   World render (render.hpp, REN-5 + geometry): the 1.0 .mtb named 1.0 shaders (int3 in Shader_LookupCrc).
static bool transcodeOct(const std::string& path, oct::File& f, std::string& note)
{
    bool changed = transcodeAssetRender(path, f, note);
    if (f.dropPool("NavMeshPool")) { note += "-NavMeshPool "; changed = true; }
    if (int n = octx::addAllCounts(f)) { note += "+counts(" + std::to_string(n) + " subnetworks) "; changed = true; }
    return changed;
}

// -> codec name if out holds transcoded bytes, nullptr to serve the 1.0 bytes unchanged.
static const char* transcode(const char* path, const void* data, size_t n, std::string& out, std::string& note)
{
    if (endsWith(path, ".oct") && n >= 4 && oct::u((const uint8_t*)data, 4) == oct::MAGIC) {
        try {
            oct::File f = oct::File::parse(data, n);
            if (!transcodeOct(norm(path), f, note)) return nullptr;
            out = f.write();
            return "oct";
        } catch (std::exception& e) {
            note = std::string("oct parse failed: ") + e.what();
            return nullptr;
        }
    }
    // an entity description (.bent 4.1 -> 4.1f, bent.hpp, NPC-4): characters, playset avatars, choreographies
    if (endsWith(path, ".bent") && n >= 4 && oct::u((const uint8_t*)data, 4) == oct::MAGIC) {
        oct::File f = oct::File::parse(data, n);
        std::string why;
        if (!bent::transcode(f, norm(path), &why)) { note += "bent: " + why + " "; return nullptr; }
        out = f.write();
        return "bent";
    }
    // a material bundle outside any asset session (particles/<x>.mtb): its materials onto 3.0 shaders, generic donors
    // from the same 3.0 asset family (in-game 2026-10-12: raw particles/IN_City.mtb -> int3 in Shader_LookupCrc)
    if (endsWith(path, ".mtb") && n >= 4 && !memcmp(data, "BNDL", 4)) {
        if (!g_renderOk) { note += "render table missing "; return nullptr; }
        std::string p = norm(path);
        render::IO io;
        io.read30 = read30x;
        io.read10 = read10s;
        render::Result r;
        if (!render::transcodeBundle(g_render, io, std::string((const char*)data, n), "3:" + p.substr(0, p.find('/') + 1), r)) {
            for (auto& pr : r.problems) logf("render problem %s: %s", p.c_str(), pr.c_str());
            note += "render FAILED ";
            return nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(g_sessLock);
            g_served[p] = r.mtb;   // the engine reopens bundles; transcode once
        }
        std::string zip = zip10For(p);
        if (!zip.empty()) indexTextures10(zip);
        char buf[160];
        sprintf_s(buf, "+materials(real %d same-sh %d generic %d, %u 1.0 textures) ", r.tiers["real"], r.tiers["same-sh"],
                  r.tiers["generic"], (unsigned)r.textures10.size());
        note += buf;
        out = r.mtb;
        return "mtb";
    }
    // a playset DataMap (Lua chunk): its entries into the path forms 3.0's handlers take (datamap.hpp, PS-2)
    if (endsWith(path, "datamap.lua") && n >= 4 && !memcmp(data, "\x1bLua", 4)) {
        int k = luachunk::rewrite(std::string((const char*)data, n), out, datamap::normalise);
        note += std::to_string(k) + " entries normalised ";
        return k ? "datamap" : nullptr;
    }
    return nullptr;
}

static bool memtestMatch(const char* p)
{
    char list[600];
    strcpy_s(list, g_cfg.memtest);
    char* ctx = nullptr;
    for (char* t = strtok_s(list, ",", &ctx); t; t = strtok_s(nullptr, ",", &ctx))
        if (*t && strstr(p, t)) return true;
    return false;
}

// Reads a whole engine stream through the Genie::File interface (+0x10 size, +0x14 read, +0x2c destroy); the
// stream is destroyed. False if the read came up short.
static bool readStream(int* s, std::string& buf)
{
    typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
    typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
    typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
    void** vt = *(void***)s;
    uint32_t n = ((SizeFn)vt[4])(s, nullptr);
    buf.assign(n, '\0');
    uint32_t got = ((ReadFn)vt[5])(s, nullptr, &buf[0], n);
    ((DtorFn)vt[11])(s, nullptr, 1);
    buf.resize(got);
    return got == n;
}

// ... and returns a MemFile copy of it.
static int* copyToMemFile(int* s, size_t* outSize)
{
    std::string buf;
    bool ok = readStream(s, buf);
    *outSize = buf.size();
    return ok ? memFile(buf.data(), buf.size()) : nullptr;
}

// GameDB CHD-only callback loaders (DBFileLoader_Callback_CHD::load, FUN_018db450 -- RumpusData, FUN_00f63e70) given
// a 1.0 .lua table: the file opened during that call is the table compiled to CHD (chd.hpp), not its Lua text
// (which the CHD parser reads as a wild pointer: in-game 2026-10-12 AV in FUN_01824810 after rumpusdata.lua).
// thiscall, three stack arguments (ret 0xc), as the dispatcher FUN_018da710 calls vtable[1]
typedef uint8_t(__fastcall* ChdLoadFn)(void* self, void* edx, const char* path, uint32_t a2, uint32_t a3);
static ChdLoadFn g_loadChdCallback;
// the path whose next open compiles to CHD; process-wide because the engine opens files on other threads
static std::mutex g_chdLock;
static std::string g_chdPath;
static bool takeChdPath(const char* p)
{
    std::lock_guard<std::mutex> lock(g_chdLock);
    return !g_chdPath.empty() && norm(p) == g_chdPath;
}
static uint8_t __fastcall hkLoadChdCallback(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    char p[300];
    safeCopy(p, sizeof(p), path);
    if (!endsWith(p, ".lua")) return g_loadChdCallback(self, nullptr, path, a2, a3);
    { std::lock_guard<std::mutex> lock(g_chdLock); g_chdPath = norm(p); }
    uint8_t r = g_loadChdCallback(self, nullptr, path, a2, a3);
    { std::lock_guard<std::mutex> lock(g_chdLock); g_chdPath.clear(); }
    logf("gamedb CHD callback loader given %s -> %u", p, r);
    return r;
}

// Trace only: the actor registry's "does <name> exist" (FUN_00751750, thiscall, ret 4), which the playset avatar
// choice FUN_013a4250 asks for <avatar> + avatarSuffix, then <avatar>, before taking FallbackAvatar.
typedef uint8_t(__fastcall* ActorExistsFn)(void* self, void* edx, const char** name);
static ActorExistsFn g_actorExists;
static uint8_t __fastcall hkActorExists(void* self, void*, const char** name)
{
    uint8_t r = g_actorExists(self, nullptr, name);
    char n[200];
    safeCopy(n, sizeof(n), name && *name ? *name : "(null)");
    logf("actor exists? %s -> %u", n, r);
    return r;
}

// The open half: a .lua stream (3.0's own or served from 1.0) -> CHD bytes. nullptr leaves the open as it was.
static int* chdForLua(const char* p, int* r)
{
    std::string src;
    if (r) { if (!readStream(r, src)) return memFile(src.data(), src.size()); }
    else if (!read10s(norm(p), src)) return nullptr;
    try {
        std::string c = chd::compile(chd::lua(src));
        logf("compiled %s %u B -> CHD %u B (%s)", p, (unsigned)src.size(), (unsigned)c.size(), r ? "3.0 file" : "1.0 file");
        return memFile(c.data(), c.size());
    } catch (std::exception& e) {
        logf("CHD compile of %s failed: %s; served as it is", p, e.what());
        return memFile(src.data(), src.size());
    }
}

// GameDB tables (plan PS-4). 3.0's DataMaps point its locks / inventory loaders at compiled .chd files and the
// loader objects are built in CHD mode (byte +0x2d = 1); the 1.0 DataMap points them at the Lua tables
// (GameDB/Incredibles/locks__lua.lua), whose text read as CHD is a wild pointer (in-game 2026-10-12: AV in
// FUN_018249d0 under the locks loader). The same 3.0 loaders read Lua when +0x2d == 0 (FUN_01612fa0: Lua state +
// FUN_018d9200(path), else FUN_018d91f0), so a .lua path runs the loader in its own Lua mode for that one call.
typedef uint32_t(__fastcall* GameDbLoadFn)(void* self, void* edx, const char* path, uint32_t a2, uint32_t a3);
static GameDbLoadFn g_loadLocks, g_loadInventory;

static uint32_t gameDbLoad(GameDbLoadFn orig, const char* tag, void* self, const char* path, uint32_t a2, uint32_t a3)
{
    char p[300];
    safeCopy(p, sizeof(p), path);
    uint8_t* mode = (uint8_t*)self + 0x2d;
    if (*mode && endsWith(p, ".lua")) {
        *mode = 0;
        uint32_t r = orig(self, nullptr, path, a2, a3);
        *mode = 1;
        logf("gamedb %s %s: CHD loader run in its Lua mode -> %u", tag, p, r);
        return r;
    }
    return orig(self, nullptr, path, a2, a3);
}
static uint32_t __fastcall hkLoadLocks(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    return gameDbLoad(g_loadLocks, "locks", self, path, a2, a3);
}
static uint32_t __fastcall hkLoadInventory(void* self, void*, const char* path, uint32_t a2, uint32_t a3)
{
    return gameDbLoad(g_loadInventory, "inventory", self, path, a2, a3);
}

// __invoke_watson (0x12CA1D1): the CRT's invalid-parameter fail-fast terminates without any exception handler
// seeing it (in-game 2026-10-12: 0xC0000417 loading the 1.0 side-missions graph). Log who called it, then die.
typedef void(__cdecl* WatsonFn)(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t);
static WatsonFn g_watson;
static void __cdecl hkWatson(const wchar_t* a, const wchar_t* b, const wchar_t* c, unsigned d, uintptr_t e)
{
    void* frames[24];
    USHORT n = CaptureStackBackTrace(0, 24, frames, nullptr);
    char line[600] = "CRT invalid parameter (__invoke_watson); stack (static):";
    for (USHORT i = 0; i < n; ++i) {
        uintptr_t f = (uintptr_t)frames[i];
        char s[24];
        if (f >= g_base && f < g_base + 0x2100000) sprintf_s(s, " %08X", (unsigned)(f - g_base + 0x400000));
        else sprintf_s(s, " ext:%08X", (unsigned)f);
        strcat_s(line, s);
    }
    logf("%s", line);
    g_watson(a, b, c, d, e);
}

// FUN_0041fef0: the resident-file handlers' path normaliser, strcpy_s into a 0x80-byte stack buffer; a longer path
// is a CRT fail-fast (found through the watson hook). A too-long path is logged and truncated (the lookup misses).
typedef void(__cdecl* NormPathFn)(char* dst, unsigned size, const char* src);
static NormPathFn g_normPath;
static void __cdecl hkNormPath(char* dst, unsigned size, const char* src)
{
    size_t n = src ? strnlen(src, 4096) : 0;
    if (src && n >= size) {
        logf("path too long for the engine's %u-byte buffer (%u): %s", size, (unsigned)n, src);
        std::string cut(src, size - 1);
        g_normPath(dst, size, cut.c_str());
        return;
    }
    g_normPath(dst, size, src);
}

static int* __cdecl hkOpen(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5)
{
    int* r = g_open(path, flags, a3, a4, a5);
    char p[300];
    safeCopy(p, sizeof(p), path);
    if (!(flags & 1) && endsWith(p, ".lua") && takeChdPath(p)) return chdForLua(p, r);
    if (r && g_cfg.memtest[0] && !(flags & 1) && memtestMatch(p)) {
        size_t n = 0;
        r = copyToMemFile(r, &n);
        logf("memtest %s %u B -> %s", p, (unsigned)n, r ? "MemFile" : "FAILED");
        return r;
    }
    if (!r && g_cfg.serve10 && g_cfg.install10[0] && !(flags & 1)) {   // flags bit 0 = write (never served)
        std::string np = norm(p), hit, from;
        {
            std::lock_guard<std::mutex> lock(g_sessLock);
            auto it = g_served.find(np);                 // a world session product (.mtb/.vbuf/.ibuf)
            if (it != g_served.end()) { hit = it->second; from = "asset session"; }   // kept: the engine reopens files
            else if (np.rfind("textures/", 0) == 0 && endsWith(np.c_str(), ".tbody")) {
                auto t = g_tex10.find(np.substr(9, np.size() - 9 - 6));
                if (t != g_tex10.end()) from = t->second.first + ":" + t->second.second;
            }
        }
        if (hit.empty() && !from.empty()) {            // a 1.0 texture the transcoded bundle names
            size_t colon = from.find(':');
            if (read10s(from.substr(0, colon - 4) + "/" + from.substr(colon + 1), hit)) padSmallDxt(hit);
        }
        if (!hit.empty()) {
            r = memFile(hit.data(), hit.size());
            logf("served %s %u B from %s -> %s", p, (unsigned)hit.size(), from.c_str(), r ? "ok" : "FAILED");
            return r;
        }
        size_t n = 0;
        std::string where;
        if (void* data = read10(p, &n, &where)) {
            std::string out, note;
            const char* codec = nullptr;
            try {
                codec = transcode(p, data, n, out, note);
            } catch (std::exception& e) {   // never unwind into engine code (that ends in a silent fail-fast)
                note += std::string("TRANSCODE EXCEPTION: ") + e.what() + " ";
                codec = nullptr;
            } catch (...) {
                note += "TRANSCODE EXCEPTION ";
                codec = nullptr;
            }
            r = codec ? memFile(out.data(), out.size()) : memFile(data, n);
            logf("served %s %u B from 1.0 %s codec=%s%s%s -> %s", p, (unsigned)(codec ? out.size() : n), where.c_str(),
                 codec ? codec : "raw", note.empty() ? "" : " ", note.c_str(), r ? "ok" : "FAILED");
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
    g_log = _fsopen(path, "w", _SH_DENYNO);   // readable while the game runs
    g_base = (uintptr_t)GetModuleHandleA(nullptr);
    g_gameDir.assign(path, slash ? slash - path : 0);
    logf("di1compat 0.3.0 loaded; exe base %08X", (unsigned)g_base);
    char dir[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&Pentane_Main, &self);
    GetModuleFileNameA(self, dir, MAX_PATH);
    if (char* s = strrchr(dir, '\\')) *s = 0;
    loadConfig(dir);
    {
        std::string text;
        FILE* tf = nullptr;
        if (!fopen_s(&tf, (std::string(dir) + "\\di1compat_render.txt").c_str(), "rb") && tf) {
            char buf[65536];
            size_t got;
            while ((got = fread(buf, 1, sizeof(buf), tf)) > 0) text.append(buf, got);
            fclose(tf);
        }
        g_renderOk = g_render.load(text);
        logf("render table: %s (%u shaders, %u donors, %u materials, %u vertex plans)", g_renderOk ? "ok" : "MISSING",
             (unsigned)g_render.shaders.size(), (unsigned)g_render.donors.size(), (unsigned)g_render.matindex.size(),
             (unsigned)g_render.plans.size());
    }

    if (MH_Initialize() != MH_OK) {
        logf("MinHook init failed; no hooks");
        return;
    }
    hook("AssetOpen", 0x0041CB10, "6AFF68????????", (void*)&hkOpen, (void**)&g_open);
    hook("GameDB locks load", 0x01612FA0, "6AFF64A100000000", (void*)&hkLoadLocks, (void**)&g_loadLocks);
    hook("GameDB inventory load", 0x014C08D0, "6AFF64A100000000", (void*)&hkLoadInventory, (void**)&g_loadInventory);
    hook("GameDB CHD callback load", 0x018DB450, "6AFF64A100000000", (void*)&hkLoadChdCallback, (void**)&g_loadChdCallback);
    if (g_cfg.trace) hook("actor exists (trace)", 0x00751750, "518B4424088B00", (void*)&hkActorExists, (void**)&g_actorExists);
    hook("CRT __invoke_watson", 0x012CA1D1, "8BFF566A01", (void*)&hkWatson, (void**)&g_watson);
    hook("resident path normaliser", 0x0041FEF0, "8B44240C56", (void*)&hkNormPath, (void**)&g_normPath);
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

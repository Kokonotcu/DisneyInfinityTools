// di1compat: Pentane plugin that serves Disney Infinity 1.0 data to the 3.0 engine (research plan
// D:\DI-Extract\_research\PLAN.md, INF-1/INF-6). The 1.0 files are read in place from the 1.0 install's own
// zips and handed to the engine as Genie::MemFile streams through its asset open, FUN_0041cb10 (worlds,
// textures, Lua ... all go through it, archive members included).
// Log: <game dir>\di1compat.log (Pentane's own log function is not used: its calling convention is unverified).
// Config: <plugin dir>\di1compat.cfg  install10 = <1.0 install> | serve10 = 0|1 | trace = 0|1 |
//         memtest = <path substring>[,...]  (serve those 3.0 files from a memory copy: proves the MemFile path)
#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <map>
#include <unordered_set>
#include <mutex>
#include <string>
#include "minhook/MinHook.h"
#include "octx.hpp"
#include "bent.hpp"
#include "btd.hpp"
#include "chd.hpp"
#include "mer.hpp"
#include "render.hpp"
#include "arc30.hpp"
#include "datamap.hpp"
#include "dct.hpp"
#include "luachunk.hpp"
#include "sx.hpp"
#include "third_party/miniz/miniz.h"
#include "renderdoc_app.h"
#include <thread>

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
    // trace hooks pass values that may not be pointers; check first, so no first-chance AV reaches the vectored
    // handlers (CrabeLoader writes a dump per AV even when the __try below handles it)
    MEMORY_BASIC_INFORMATION mbi;
    if ((uintptr_t)src < 0x10000 || !VirtualQuery(src, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !mbi.Protect) {
        if (cap) out[0] = 0;
        return;
    }
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
    bool missionTrace = false;       // log IN_ mission context state changes (one cheap hook; trace's cost frames)
    bool shaders10 = false;          // real 1.0 shaders: translated at load (gen30.hpp) instead of 3.0 substitutes
    bool translate10 = true;         // shaders10's materials (0: its engine-side changes only, for bisecting)
    bool lazyLink10 = true;          // shaders10: 3.0's shaders link on first use too (0: the boot linker runs)
    bool magenta10 = false;          // test: every translated pixel shader draws solid magenta
    int bufview10 = 0;               // test: translated final passes show 1 the light buffer (t3), 2 view depth (t0), 3 t4
    int renderdoc10 = 0;             // >0: load RenderDoc (x86) and capture one frame this many seconds into the world
    bool opacityLog10 = false;       // test: log primitives the opacity test drops from the opaque list
    char memtest[600] = "";          // comma list of path substrings served from a memory copy
    bool hidePs = false;             // experiment (trace): actor registry denies <name>_PS, so avatars fall back
    char block10[600] = "";          // experiment: comma list of path prefixes never served from 1.0
    bool sx10 = true;                // 1.0 Slang scripts served in the 3.0 container (sx.hpp SX-1; 0: raw, rejected)
    bool sxNatives10 = true;         // sx10's imports bound to 1.0-semantics natives by a linker hook (SX-3/4)
    bool sxPorts10 = true;           // sxNatives10 with the ported natives (0: stack-balanced stubs only)
    bool sxBeh10 = true;             // sxPorts10 + 1.0 behavior-type numbers into 1.0 scripts' SetInitialParams
    bool bmx10 = true;               // 1.0 behavior maps in 3.0's form (sx.hpp BMX-1; 0: raw, 3.0 drops them)
    bool btx10 = true;               // 1.0 behavior trees in 3.0's form (sx.hpp BTX-1; 0: raw, 3.0 drops them)
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
        else if (!strcmp(key, "missiontrace")) g_cfg.missionTrace = atoi(val) != 0;
        else if (!strcmp(key, "shaders10")) g_cfg.shaders10 = atoi(val) != 0;
        else if (!strcmp(key, "translate10")) g_cfg.translate10 = atoi(val) != 0;
        else if (!strcmp(key, "lazylink10")) g_cfg.lazyLink10 = atoi(val) != 0;
        else if (!strcmp(key, "magenta10")) g_cfg.magenta10 = atoi(val) != 0;
        else if (!strcmp(key, "bufview10")) g_cfg.bufview10 = atoi(val);
        else if (!strcmp(key, "renderdoc10")) g_cfg.renderdoc10 = atoi(val);
        else if (!strcmp(key, "opacitylog10")) g_cfg.opacityLog10 = atoi(val) != 0;
        else if (!strcmp(key, "memtest")) strcpy_s(g_cfg.memtest, val);
        else if (!strcmp(key, "hide_ps")) g_cfg.hidePs = atoi(val) != 0;
        else if (!strcmp(key, "block10")) strcpy_s(g_cfg.block10, val);
        else if (!strcmp(key, "sx10")) g_cfg.sx10 = atoi(val) != 0;
        else if (!strcmp(key, "sxnatives10")) g_cfg.sxNatives10 = atoi(val) != 0;
        else if (!strcmp(key, "sxports10")) g_cfg.sxPorts10 = atoi(val) != 0;
        else if (!strcmp(key, "sxbeh10")) g_cfg.sxBeh10 = atoi(val) != 0;
        else if (!strcmp(key, "bmx10")) g_cfg.bmx10 = atoi(val) != 0;
        else if (!strcmp(key, "btx10")) g_cfg.btx10 = atoi(val) != 0;
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
        // 1.0 shards choreographies and exploders by the name's first two letters, one flat archive per asset:
        // choreographies\bhs_beam1\bhs_beam1.oct is choreographies/bh/bhs_beam1.zip, exploders\inc_rubble_0.vbuf
        // is exploders/in/inc_rubble.zip (in-game 2026-10-13: the avatar's DNA pulls 1.0 choreographies)
        size_t top = p.find('/'), last = dir.rfind('/');
        std::string asset = last == std::string::npos ? stem : dir.substr(last + 1);
        if (asset.size() > 2 && asset.compare(asset.size() - 2, 2, "_0") == 0 && last == std::string::npos) asset.resize(asset.size() - 2);
        std::string shard = asset.size() >= 2 ? p.substr(0, top) + "/" + asset.substr(0, 2) + "/" + asset + ".zip" : dir + ".zip";
        std::string zips[4] = {dir + ".zip", dir + "/" + dir.substr(dir.rfind('/') + 1) + ".zip", dir + "/" + stem + ".zip", shard};
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
// engine's next requests for those files are answered from here.
// 1.0 archive overlay: a 1.0 asset archive also carries files under their full asset paths -- textures/<hash>.tbody,
// and the motions its entity uses (characters/inc_bob/inc_bob_ps.zip holds characters/biped/motions/*.oct and .banm,
// which no characters/biped archive has: in-game 2026-10-12 the transcoded inc_bob_ps.bent named them and they were
// missing). The 1.0 engine sees them once the archive is loaded, so every 1.0 archive a file is served from gets its
// full-path members indexed (engine path -> "<archive without .zip>/<member>", which read10 resolves); first wins.
static std::mutex g_sessLock;
static std::map<std::string, std::string> g_served;
static std::map<std::string, std::string> g_over10;
static std::set<std::string> g_indexed10;
static render::Table g_render;
static gen30::VFormats g_vf10;              // 1.0 vertex formats (real 1.0 shaders: shaders10)
static uint8_t* shader10(const char* name, bool create);
static bool g_renderOk;
static mer::Table g_mer;
static bool g_merOk;

static void indexZip10(const std::string& zipRel)
{
    std::lock_guard<std::mutex> lock(g_zipLock);
    mz_zip_archive* z = zipFor(zipRel);
    if (!z) return;
    std::lock_guard<std::mutex> l2(g_sessLock);
    if (!g_indexed10.insert(zipRel).second) return;
    std::string base = zipRel.substr(0, zipRel.size() - 4) + "/";
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(z); ++i) {
        char name[300];
        mz_zip_reader_get_filename(z, i, name, sizeof(name));
        std::string n = name;
        for (char& c : n) c = (c == '\\') ? '/' : (char)tolower((unsigned char)c);
        if (n.find('/') != std::string::npos && n.back() != '/') g_over10.emplace(n, base + name);
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
    io.translate = g_cfg.shaders10 && g_cfg.translate10;
    io.translatable = [](const std::string& sh) { return shader10(("x" + sh).c_str(), true) != nullptr; };
    io.skinned = [](uint32_t vf) { auto f = g_vf10.find(vf); return f != g_vf10.end() && f->second.count("BLENDINDICES0"); };
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
    char buf[200];
    sprintf_s(buf, "+render(translated %d real %d same-sh %d generic %d, %u 1.0 textures) ", r.tiers["translated"], r.tiers["real"],
              r.tiers["same-sh"], r.tiers["generic"], (unsigned)r.textures10.size());
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
    if (octx::addAssetPool(f)) { note += "+AssetPool "; changed = true; }
    if (int n = octx::skin30(f)) { note += "+skin(" + std::to_string(n) + ") "; changed = true; }
    if (int n = octx::locator30(f)) { note += "+locators(" + std::to_string(n) + ") "; changed = true; }
    if (int n = octx::collision30(f)) { note += "+collision(" + std::to_string(n) + ") "; changed = true; }
    if (int n = octx::realm30w(f)) { note += "+lighting(" + std::to_string(n) + " nodes) "; changed = true; }
    if (int n = octx::addAllCounts(f)) { note += "+counts(" + std::to_string(n) + " subnetworks) "; changed = true; }
    return changed;
}

// -> codec name if out holds transcoded bytes, nullptr to serve the 1.0 bytes unchanged.
// A 1.0 mission line (<line>__missiongraph.lua, the Unlocks database) in 3.0 terms. 1.0 started a story line's
// opening mission -- the one with no Unlocks -- by itself, even with its giver far away (IN_City_DockFire: giver
// IN_Edna, who spawns at HQ ~550 m from the start; Edna's call, the compass and ledge tutorials open the 1.0 game,
// user screenshots 2026-10-13). 3.0 waits for the player to talk to a named giver unless the record says
// ForceActivateFirstTime = true (3.0 ships it on its own named-giver openers: MBA_Side_Tutorial_Quest_01,
// PSX_L1_Main04). Adds that field to story-line records with empty Unlocks. Returns the records changed.
static int missionGraph30(std::string& t)
{
    static const std::string emptyUnlocks = "\t\t\tUnlocks = \n\t\t\t{\n\t\t\t},\n\t\t\tline = \"";
    int n = 0;
    for (size_t pos = t.find(emptyUnlocks); pos != std::string::npos; pos = t.find(emptyUnlocks, pos + 1)) {
        size_t q = pos + emptyUnlocks.size(), e = t.find('"', q);
        if (e == std::string::npos) break;
        std::string line = t.substr(q, e - q);
        for (char& c : line) c = (char)tolower((unsigned char)c);
        size_t start = t.rfind("\n\t\t{\n", pos);
        if (start == std::string::npos || line.find("story") == std::string::npos) continue;
        std::string rec = t.substr(start, pos - start);
        size_t ar = rec.find("\t\t\t\tAvatarReady = ");
        if (rec.find("Type = \"MissionContext\"") == std::string::npos || rec.find("ForceActivateFirstTime") != std::string::npos ||
            ar == std::string::npos)
            continue;
        static const std::string add = "\t\t\t\tForceActivateFirstTime = true,\n";
        t.insert(start + ar, add);
        pos += add.size();
        ++n;
    }
    return n;
}

static const char* transcode(const char* path, const void* data, size_t n, std::string& out, std::string& note)
{
    if (endsWith(path, "__missiongraph.lua") && n && ((const char*)data)[0] != 0x1b) {
        std::string t((const char*)data, n);
        int k = missionGraph30(t);
        if (!k) return nullptr;
        note += "+missiongraph(" + std::to_string(k) + " opening missions force-activated) ";
        out.swap(t);
        return "missiongraph";
    }
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
    // a Meridian graph (.mer, mer.hpp, MER-2/4) and its type manifest (.mtc, rebuilt from the transcoded graph, since
    // stand-ins change the class counts)
    bool isMtc = endsWith(path, ".mtc");
    if ((endsWith(path, ".mer") || isMtc) && g_merOk) {
        std::string graph, why;
        if (isMtc) {
            std::string p = norm(path);
            if (!read10s(p.substr(0, p.size() - 4) + ".mer", graph)) return nullptr;
        } else graph.assign((const char*)data, n);
        if (graph.size() < 4 || oct::u((const uint8_t*)graph.data(), 4) != oct::MAGIC) return nullptr;
        oct::File f = oct::File::parse(graph.data(), graph.size());
        mer::Stats st;
        if (!mer::transcode(g_mer, f, &st, &why)) { note += "mer: " + why + " "; return nullptr; }
        char buf[200];
        sprintf_s(buf, "+mer(%d namespaces, %d nodes, %d params, %d pin tables copied, %d stand-ins, %d links, %d dropped) ",
                  st.namespaces, st.nodes, st.params, st.pinsCopied, st.stubs, st.links, st.linksDropped);
        note += buf;
        out = isMtc ? mer::mtc(f) : f.write();
        return isMtc ? "mtc" : "mer";
    }
    // a localisation table: keys re-hashed under 3.0's one seed (dct.hpp), names from the stringids table beside it
    if (endsWith(path, ".dct") && n >= 4 && !memcmp(data, "DICT", 4)) {
        std::string p = norm(path), ids;
        size_t us = p.rfind('_'), sl = p.rfind('/');
        if (us == std::string::npos || sl == std::string::npos || us < sl) return nullptr;
        std::string plat = endsWith(path, "_pc_con.dct") ? "_win32.dct" : p.substr(us);   // its stand-in (hkOpen)
        if (!read10s(p.substr(0, sl + 1) + "stringids" + plat, ids)) { note += "dct: no stringids beside it "; return nullptr; }
        std::string t((const char*)data, n);
        int dropped = 0, kept = dct::reseed(t, ids, dct::SEED30, &dropped);
        if (kept < 0) return nullptr;
        note += "+dct(" + std::to_string(kept) + " keys re-hashed, " + std::to_string(dropped) + " unnamed dropped) ";
        out.swap(t);
        return "dct";
    }
    // collision (.btd blocks in the 3.0 layout, btd.hpp) and its block table (.btm, rebuilt from the converted
    // sibling .btd); raw 1.0 collision gave a world without ground (in-game 2026-10-13: the avatar fell, respawned)
    bool isBtm = endsWith(path, ".btm");
    if (endsWith(path, ".btd") || isBtm) {
        std::string src, conv;
        if (isBtm) {
            std::string p = norm(path);
            if (!read10s(p.substr(0, p.size() - 4) + ".btd", src)) return nullptr;
        } else src.assign((const char*)data, n);
        if (!btd::convert(src, conv)) { note += "btd: not the 1.0 block layout "; return nullptr; }
        char buf[64];
        sprintf_s(buf, "+btd(%u blocks) ", (unsigned)btd::blocks(conv, 3).size());
        note += buf;
        out = isBtm ? btd::btm(conv) : conv;
        return isBtm ? "btm" : "btd";
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
        // texture-only bundles (UI images: ui/charactericons, flash/<screen>/*_iN.mtb) hold a named TEXB laid out as
        // in 3.0 (count, name length, name, hash) and no materials: served as they are. The material path read them
        // as unnamed TEXB and wrote garbage (in-game 2026-10-13: Edna's dialog icon fell back to the IN badge).
        bool hasMatp = false;
        for (size_t o = 16, c = 0, nc = n >= 16 ? oct::u((const uint8_t*)data + 12, 4) : 0; c < nc && o + 16 <= n; ++c) {
            if (!memcmp((const char*)data + o, "MATP", 4)) hasMatp = true;
            uint32_t cs = oct::u((const uint8_t*)data + o + 8, 4);
            if (!cs) break;
            o += cs;
        }
        if (!hasMatp) return nullptr;
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
    // a 1.0 Slang script (sx.hpp SX-1): 3.0's fix-up FUN_007366f0 takes only 0x100d containers
    if (g_cfg.sx10 && endsWith(path, ".sx") && sx::is10(data, n)) {
        std::string why;
        int k = sx::transcode(data, n, norm(path), out, &why);
        if (k < 0) { note += "sx: " + why + " "; return nullptr; }
        note += "+sx(" + std::to_string(k) + " imports) ";
        return "sx";
    }
    // a 1.0 behavior map / behavior tree (sx.hpp BMX-1 / BTX-1): 3.0's loaders drop tables without Version 763
    bool isBmx = g_cfg.bmx10 && endsWith(path, ".bmx"), isBtx = g_cfg.btx10 && endsWith(path, ".btx");
    if ((isBmx || isBtx) && n >= 4 && !memcmp(data, "\x1bLua", 4)) {
        std::string why;
        auto exists = [](const std::string& p) { std::string s; return read10s(p, s); };
        int k = isBmx ? sx::ai::bmx(data, n, exists, out, &why) : sx::ai::btx(data, n, exists, out, &why);
        if (k < 0) { note += std::string(isBmx ? "bmx: " : "btx: ") + why + " "; return nullptr; }
        note += (isBmx ? "+bmx(" : "+btx(") + std::to_string(k) + (isBmx ? " behaviors) " : " tables) ");
        return isBmx ? "bmx" : "btx";
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

// The PC virtual reader's remembered character per playset (FUN_00b7f910(key "IGP_PLAYSET_<playset>", sku), thiscall,
// ret 8) has slots for 3.0's four playsets only (Empire, InsideOut, TheCloneWars, PlaysetX) and is false for any other;
// FUN_00b83b90 then resets the current character to none every frame on such a playset world (watchpoint on the
// reader tag's toy id, 2026-10-12: written to -1 from there during the IN_City load), the playset flow finds no figure,
// keeps the proxy avatar and shows "Missing Disney Infinity Figure". A playset without a slot keeps the selected
// character; whether that figure may play here stays the playset flow's own check (Gateway[sku].playset, FUN_00ddae30).
typedef uint8_t(__fastcall* PlaysetCharFn)(void* self, void* edx, const char* key, int sku);
static PlaysetCharFn g_playsetChar;
static uint8_t __fastcall hkPlaysetChar(void* self, void*, const char* key, int sku)
{
    static const char* const slots[] = {"IGP_PLAYSET_Empire", "IGP_PLAYSET_InsideOut", "IGP_PLAYSET_TheCloneWars", "IGP_PLAYSET_PlaysetX"};
    char k[100];
    safeCopy(k, sizeof(k), key);
    for (auto s : slots)
        if (!_stricmp(k, s)) return g_playsetChar(self, nullptr, key, sku);
    return 1;
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
    if (g_cfg.hidePs && endsWith(n, "_PS") && !strncmp(n, "AV_", 3)) r = 0;
    logf("actor exists? %s -> %u", n, r);
    return r;
}

// Trace only: the playset flow's figure check FUN_00ddae30(player, sku, int* failCode, a4, a5) (thiscall, ret 0x14)
// and the figure's playset string Gateway[sku].playset, FUN_00435080(out, sku) (ret 8).
typedef uint8_t(__fastcall* FigureOkFn)(void* self, void* edx, uint32_t player, int sku, int* code, uint32_t a4, uint32_t a5);
static FigureOkFn g_figureOk;
static uint8_t __fastcall hkFigureOk(void* self, void*, uint32_t player, int sku, int* code, uint32_t a4, uint32_t a5)
{
    int mine = -1;
    uint8_t r = g_figureOk(self, nullptr, player, sku, code ? code : &mine, a4, a5);
    logf("figure check player %u sku %d (a4 %u a5 %u) -> %u code %d", player, sku, a4 & 0xff, a5 & 0xff, r, code ? *code : mine);
    return r;
}
// ... the reader item list the playset flow fills its figure slots from (FUN_00427330(out) -> count, ret 4; items
// 0x50 B from out+4: +0 type, +4 sku) and its per-item gate FUN_00deb610(type, sku, a3, a4, a5) (ret 0x14); logged
// on change
// First access violation in the exe: the faulting address and a call stack (stack words that point into the exe right
// after a call instruction: E8 rel32 whose target is plausible, or an FF /2 indirect call), static addresses. Logged
// once, then the exception continues to the game's / CrabeLoader's handlers.
static volatile LONG g_avLogged;
static bool afterCall(uintptr_t ret)
{
    if (ret < g_base + 0x1000 + 6 || ret >= g_base + 0x2100000) return false;
    const uint8_t* p = (const uint8_t*)ret;
    if (p[-5] == 0xE8) { uintptr_t t = ret + *(const int32_t*)(p - 4); return t >= g_base && t < g_base + 0x2100000; }
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;   // call [mem32] / [reg+disp32]
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;   // call [reg+disp8]
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true;   // call reg / [reg]
    return false;
}
// Appends the exe return addresses found on a stack (static VAs, call-site validated) to line.
static void stackOf(const uint32_t* sp, char* line, size_t cap, int len, int max = 24)
{
    for (int i = 0, found = 0; i < 2048 && found < max; ++i) {
        uint32_t v;
        __try { v = sp[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        bool ok = false;
        if ((v & 0xff00ff00) == 0) continue;   // two UTF-16 characters of a path buffer, not a return address
        __try { ok = afterCall(v); } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) { len += sprintf_s(line + len, cap - len, " %08X", (unsigned)(v - g_base + 0x400000)); ++found; }
    }
}
static LONG CALLBACK avHandler(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || InterlockedExchange(&g_avLogged, 1)) return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t eip = (uintptr_t)e->ExceptionRecord->ExceptionAddress;
    char line[900];
    int len = sprintf_s(line, "ACCESS VIOLATION at %08X (%s %08X), stack:", (unsigned)(eip >= g_base && eip < g_base + 0x2100000 ? eip - g_base + 0x400000 : eip),
                        e->ExceptionRecord->ExceptionInformation[0] ? "write" : "read", (unsigned)e->ExceptionRecord->ExceptionInformation[1]);
    stackOf((const uint32_t*)e->ContextRecord->Esp, line, sizeof(line), len);
    logf("%s", line);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Trace only: a hardware write watchpoint (DR0, 4 bytes) on one address in every thread; the vectored handler logs
// the writer's EIP and the exe return addresses on its stack (first 5 hits).
static volatile uintptr_t g_watchAddr;
static volatile LONG g_watchHits;
static LONG CALLBACK watchHandler(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Dr6 = 0;
    if (InterlockedIncrement(&g_watchHits) <= 10) {
        char line[700];
        uintptr_t eip = e->ContextRecord->Eip;
        int len = sprintf_s(line, "watch hit: value now %d, eip %08X, stack:", *(int*)g_watchAddr,
                            (unsigned)(eip >= g_base && eip < g_base + 0x2100000 ? eip - g_base + 0x400000 : eip));
        const uint32_t* sp = (const uint32_t*)e->ContextRecord->Esp;
        for (int i = 0, found = 0; i < 400 && found < 14; ++i) {
            uint32_t v = sp[i];
            if (v >= g_base && v < g_base + 0x2100000) { len += sprintf_s(line + len, sizeof(line) - len, " %08X", (unsigned)(v - g_base + 0x400000)); ++found; }
        }
        logf("%s", line);
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}
static DWORD WINAPI armWatch(void*)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof(te)};
    int n = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        SuspendThread(t);
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(t, &c)) {
            c.Dr0 = g_watchAddr;
            c.Dr7 = (c.Dr7 & ~0xF0003ul) | 1 | (1ul << 16) | (3ul << 18);   // L0, write, 4 bytes
            if (SetThreadContext(t, &c)) ++n;
        }
        ResumeThread(t);
        CloseHandle(t);
    }
    CloseHandle(snap);
    logf("watch armed on %08X in %d threads", (unsigned)g_watchAddr, n);
    return 0;
}
static void watch(uintptr_t addr)
{
    if (g_watchAddr) return;
    g_watchAddr = addr;
    AddVectoredExceptionHandler(1, watchHandler);
    HANDLE h = CreateThread(nullptr, 0, armWatch, nullptr, 0, nullptr);   // a thread cannot set its own context
    if (h) { WaitForSingleObject(h, 5000); CloseHandle(h); }
}

typedef int(__fastcall* ReaderListFn)(void* self, void* edx, int* out);
static ReaderListFn g_readerList;
static int __fastcall hkReaderList(void* self, void*, int* out)
{
    static std::string last;
    int n = g_readerList(self, nullptr, out);   // reader slot = this + 0x6c + index * 0x260, its toy id at +0x168
    std::string s = std::to_string(n) + ":";
    for (int i = 0; i < n && i < 12; ++i) s += " " + std::to_string(out[1 + i * 20]) + "/" + std::to_string(out[2 + i * 20]);
    if (s != last) { last = s; logf("reader list %s", s.c_str()); }
    return n;
}
// ... the playset flow's player figure slot (*(this + 0xc) + 0xc + player * 4, read by FUN_00df9fd0(out, player,
// flag), thiscall ret 0xc): the watchpoint shows who fills and who clears player 0's slot
typedef void*(__fastcall* IgpNameFn)(void* self, void* edx, void* out, uint32_t player, uint32_t flag);
static IgpNameFn g_igpName;
static void* __fastcall hkIgpName(void* self, void*, void* out, uint32_t player, uint32_t flag)
{
    if (self && *(char**)((char*)self + 0xc)) watch((uintptr_t)(*(char**)((char*)self + 0xc)) + 0xc);
    return g_igpName(self, nullptr, out, player, flag);
}
typedef uint8_t(__fastcall* ReaderItemOkFn)(void* self, void* edx, uint32_t type, int sku, uint32_t a3, uint32_t a4, uint32_t a5);
static ReaderItemOkFn g_readerItemOk;
static uint8_t __fastcall hkReaderItemOk(void* self, void*, uint32_t type, int sku, uint32_t a3, uint32_t a4, uint32_t a5)
{
    static std::string last;
    uint8_t r = g_readerItemOk(self, nullptr, type, sku, a3, a4, a5);
    std::string s = std::to_string(type) + "/" + std::to_string(sku) + " -> " + std::to_string(r);
    if (s != last) { last = s; logf("reader item %s", s.c_str()); }
    return r;
}
typedef const char** (__fastcall* FigurePlaysetFn)(void* self, void* edx, const char** out, int sku);
static FigurePlaysetFn g_figurePlayset;
static const char** __fastcall hkFigurePlayset(void* self, void*, const char** out, int sku)
{
    const char** r = g_figurePlayset(self, nullptr, out, sku);
    char s[200];
    safeCopy(s, sizeof(s), out && *out ? *out : "(null)");
    logf("Gateway[%d].playset = '%s'", sku, s);
    return r;
}

// Trace only: an entity instancing one of its assets' SubNetworks (FUN_007734f0(out, const char** subName, a3),
// thiscall ret 0xc; an empty name = the root SubNetwork).
typedef void*(__fastcall* AssetInstFn)(void* self, void* edx, void* out, const char** name, uint32_t a3);
static AssetInstFn g_assetInst;
static void* __fastcall hkAssetInst(void* self, void*, void* out, const char** name, uint32_t a3)
{
    char n[200];
    safeCopy(n, sizeof(n), name && *name ? *name : "(null)");
    logf("instance asset '%s' of entity %p", n, self);
    return g_assetInst(self, nullptr, out, name, a3);
}

// Diagnostic guard (trace): SubNetwork instancing FUN_004ff0e0(param_1, root, instanceNo) (thiscall on the template,
// ret 0xc). A template whose root handle (+0xc) no longer resolves faults in FUN_004f5900 (NULL+8, in-game
// 2026-10-12, instancing a 1.0 prop's 'Anchor'); log it and return 0 (FUN_007734f0 handles a failed instance).
typedef int(__fastcall* InstFn)(void* self, void* edx, uint32_t p1, uint32_t root, int no);
static InstFn g_inst;
static uint32_t g_instHeaders;   // header count of the template being instanced (trace)
static int __fastcall hkInst(void* self, void*, uint32_t p1, uint32_t root, int no)
{
    typedef void*(__fastcall * ResolveFn)(void* handleField, void* edx);
    void* r = ((ResolveFn)rt(0x004B46D0))((char*)self + 0xc, nullptr);
    if (!r) {
        uint32_t* t = (uint32_t*)self;
        logf("instancing template %p with a dead root handle %08X (block %08X, headers %u, conv %u, procs %u): skipped",
             self, t[3], t[0x4c / 4], t[0x5c / 4], t[100 / 4], t[0x60 / 4]);
        return 0;
    }
    g_instHeaders = ((uint32_t*)self)[0x5c / 4];
    return g_inst(self, nullptr, p1, root, no);
}

// Trace: header creation FUN_004fae90(out, name, parent, a4, a5) (thiscall on the allocator, ret 0x14). Logs the
// adapter headers ('As...') made at runtime, with the creator's stack.
typedef void*(__fastcall* HdrMakeFn)(void* self, void* edx, void* out, uint32_t name, uint32_t parent, uint32_t a4, uint32_t a5);
static HdrMakeFn g_hdrMake;
static volatile LONG g_hdrMakeLogged;
static void* __fastcall hkHdrMake(void* self, void*, void* out, uint32_t name, uint32_t parent, uint32_t a4, uint32_t a5)
{
    char n[80] = "";
    __try { if (name) safeCopy(n, sizeof(n), (const char*)(name + 0xc)); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (n[0] == 'A' && n[1] == 's' && n[2] >= 'A' && n[2] <= 'Z' && InterlockedIncrement(&g_hdrMakeLogged) <= 6) {
        char line[700];
        int len = sprintf_s(line, "make header '%s' (allocator %p, parent %08X, %u %u), stack:", n, self, parent, a4, a5);
        stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, 20);
        logf("%s", line);
    }
    return g_hdrMake(self, nullptr, out, name, parent, a4, a5);
}

// Trace: the spawn lookup StartPositions FUN_013a85d0(player, zone, out xform, out, flags) (thiscall, ret 0x14):
// 0 = no registered start (the caller then uses the origin); out[4..6] = the translation. And the
// PlayerStartPositionNode factory (vtable slot 14, FUN_014884f0, ret 4), to see whether a graph builds one.
typedef uint32_t(__fastcall* StartLookupFn)(void* self, void* edx, uint32_t player, uint32_t zone, float* out, uint32_t* o2, uint32_t flags);
static StartLookupFn g_startLookup;
static volatile LONG g_startLogged;
static uint32_t __fastcall hkStartLookup(void* self, void*, uint32_t player, uint32_t zone, float* out, uint32_t* o2, uint32_t flags)
{
    uint32_t r = g_startLookup(self, nullptr, player, zone, out, o2, flags);
    if (InterlockedIncrement(&g_startLogged) <= 20)
        logf("start lookup player %u zone %08X flags %X -> %u at (%.2f, %.2f, %.2f)", player, zone, flags, r,
             out ? out[4] : 0.f, out ? out[5] : 0.f, out ? out[6] : 0.f);
    return r;
}
// Trace: a start node's provider query FUN_0145f720(a1, player, zone, out, a5) (thiscall on the StartPositionBase,
// ret 0x14): matches when zone is 0 or its +0x88, the player's bit is in byte +0x85 and vfunc +0x68 fills the transform.
typedef uint8_t(__fastcall* StartQueryFn)(uint8_t* self, void* edx, uint32_t a1, int player, uint32_t zone, float* out, uint32_t a5);
static StartQueryFn g_startQuery;
static volatile LONG g_startQueryLogged;
static uint8_t __fastcall hkStartQuery(uint8_t* self, void*, uint32_t a1, int player, uint32_t zone, float* out, uint32_t a5)
{
    uint8_t r = g_startQuery(self, nullptr, a1, player, zone, out, a5);
    if (InterlockedIncrement(&g_startQueryLogged) <= 12)
        logf("start query node %p (zone %08X, players %02X) for player %d zone %08X -> %u at (%.2f, %.2f, %.2f)", self,
             *(uint32_t*)(self + 0x88), self[0x85], player, zone, r, out ? out[4] : 0.f, out ? out[5] : 0.f, out ? out[6] : 0.f);
    return r;
}

// Trace: the avatar spawn path. Event posts FUN_0068bbb0(name, a2, a3) (thiscall on the event manager, ret 0xc),
// first 3 of each spawn/zone name; the level loader FUN_00d00530 state (+0x2c) changes; its load-done check
// FUN_00ccc450 result changes; AvatarSpawning finalize FUN_00aa36f0 and avatar removal FUN_00a9ec70.
typedef void(__fastcall* PostFn)(void* self, void* edx, const char* name, uint32_t a2, uint32_t a3);
static PostFn g_post;
static std::mutex g_postMx;
static std::map<std::string, int> g_postSeen;
static void __fastcall hkPost(void* self, void*, const char* name, uint32_t a2, uint32_t a3)
{
    char n[64] = "";
    safeCopy(n, sizeof(n), name);
    if (strstr(n, "Zone") || strstr(n, "Avatar") || strstr(n, "Creation") || strstr(n, "Toy") || strstr(n, "Spawn") || strstr(n, "Level")) {
        std::lock_guard<std::mutex> l(g_postMx);
        if (++g_postSeen[n] <= 3) logf("event %s (%08X %08X)", n, a2, a3);
    }
    g_post(self, nullptr, name, a2, a3);
}
typedef void(__fastcall* ThisFn)(void* self, void* edx);
static ThisFn g_levelTick;
static int g_levelState = -1;
static void __fastcall hkLevelTick(void* self, void*)
{
    int st = *(int*)((char*)self + 0x2c);
    if (st != g_levelState) { logf("level loader %p state %d -> %d", self, g_levelState, st); g_levelState = st; }
    g_levelTick(self, nullptr);
    st = *(int*)((char*)self + 0x2c);
    if (st != g_levelState) { logf("level loader %p state -> %d", self, st); g_levelState = st; }
}
typedef uint8_t(__fastcall* BoolThisFn)(void* self, void* edx);
static BoolThisFn g_loadDone;
static int g_loadDoneLast = -1;
static uint8_t __fastcall hkLoadDone(void* self, void*)
{
    uint8_t r = g_loadDone(self, nullptr);
    if (r != g_loadDoneLast) {
        uint8_t* o = (uint8_t*)self;
        logf("level load-done check %p -> %u (+0x21 %u, +0xc %08X)", self, r, o[0x21], *(uint32_t*)(o + 0xc));
        g_loadDoneLast = r;
    }
    return r;
}
typedef void(__fastcall* OneArgFn)(void* self, void* edx, uint32_t a1);
static OneArgFn g_avFinal, g_avRemove;
static void __fastcall hkAvFinal(void* self, void*, uint32_t a1) { logf("AvatarSpawning finalize %p rec %08X", self, a1); g_avFinal(self, nullptr, a1); }
static void __fastcall hkAvRemove(void* self, void*, uint32_t a1)
{
    char line[400];
    int len = sprintf_s(line, "AvatarSpawning remove avatar of player %u, stack:", a1);
    stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, 12);
    logf("%s", line);
    g_avRemove(self, nullptr, a1);
}

// Trace: the spawn choreography. FUN_00a9f080 asks the creation cho for 'Cho Controls' (FUN_014835c0, thiscall 2 args)
// and only then registers FUN_00a8b670 on its 'ToyCreation_PlaceToy' event; that callback sets the per-player ready
// flag (+0xd9) the finalize step waits for.
typedef uint8_t(__fastcall* ChoHasFn)(void* self, void* edx, uint32_t a1, const char* name);
static ChoHasFn g_choHas;
static volatile LONG g_choHasLogged;
static uint8_t __fastcall hkChoHas(void* self, void*, uint32_t a1, const char* name)
{
    uint8_t r = g_choHas(self, nullptr, a1, name);
    char n[64] = "";
    safeCopy(n, sizeof(n), name);
    if (!strcmp(n, "Cho Controls") && InterlockedIncrement(&g_choHasLogged) <= 6) logf("cho %p has '%s' (a1 %08X) -> %u", self, n, a1, r);
    return r;
}
typedef void(__fastcall* AvCreateFn)(void* self, void* edx, uint32_t player, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);
static AvCreateFn g_avCreate;
static void __fastcall hkAvCreate(void* self, void*, uint32_t player, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    logf("AvatarSpawning create player %u (%08X %08X %08X %08X) ->", player, a2, a3, a4, a5);
    g_avCreate(self, nullptr, player, a2, a3, a4, a5);
    logf("AvatarSpawning create player %u returned", player);
}
// AsyncActorCreator (the queued spawn FUN_00752a90 makes when the actor's data is not resident): Execute (vtable 1,
// FUN_0075a290) builds it through FUN_00759ca0 (cdecl, 11 args, returns the out handle pointer) and calls the
// completion callback; log Execute (actor name at +0x6c).
static ThisFn g_aacExec;
static void __fastcall hkAacExec(void* self, void*)
{
    bool avatar = *(uintptr_t*)((char*)self + 0x78) == rt(0x00A9B3E0);   // its callback is the avatar spawn's
    if (avatar) logf("AsyncActorCreator %p execute (avatar spawn, mode %u)", self, *(uint32_t*)((char*)self + 0x18));
    g_aacExec(self, nullptr);
    if (avatar) logf("AsyncActorCreator %p execute returned", self);
}
// The avatar spawn's completion callback FUN_00a9b3e0(actor, record) (cdecl): sets record+0x28 = actor and the ready
// flag unless AvatarSpawning+0x88 is 0.
typedef void(__cdecl* AvSpawnedFn)(uint32_t actor, uint8_t* rec);
static AvSpawnedFn g_avSpawned;
static void __cdecl hkAvSpawned(uint32_t actor, uint8_t* rec)
{
    uint8_t* as = *(uint8_t**)rt(0x02212BC4);
    logf("avatar spawn callback actor %08X rec %p (AvatarSpawning %p +0x88 %08X, rec+0x31 %02X)", actor, rec, as,
         as ? *(uint32_t*)(as + 0x88) : 0, rec ? rec[0x31] : 0);
    g_avSpawned(actor, rec);
}
// Asset defs lookup FUN_00703340(key) (thiscall on the AssetDefsManager, ret 4): DefID -> FileName of a Cho, "" when
// no loaded assetdefs has it. A 3.0 playset's gamedb package carries assetdefs.lua (30 engine entries every playset
// ships identically, GamePlayers_ToyCreation_Fast among them, plus character entries) and the GameDB loader parses it
// when the playset loads; the Incredibles package is loaded by the older fixed-list path that never opens it, so
// AvatarSpawning got no creation cho, skipped its ToyCreation_PlaceToy setup and never finalized the avatar
// (in-game 2026-10-13). On a miss the lookup answers from gamedb/incredibles/assetdefs.lua (3.0 entries, added to that
// package), read once through the engine's own open.
typedef const char*(__fastcall* AssetDefFn)(void* self, void* edx, const char* key);
static AssetDefFn g_assetDef;
static std::map<std::string, std::string>* g_defs;   // lowercase DefID -> FileName (Cho entries)
static std::mutex g_defsMx;
static bool read30(const std::string& path, std::string& out);
static const char* fallbackDef(const char* key)
{
    std::lock_guard<std::mutex> l(g_defsMx);
    if (!g_defs) {
        g_defs = new std::map<std::string, std::string>;
        std::string t;
        if (read30("gamedb/incredibles/assetdefs.lua", t)) {
            for (size_t a = t.find('{', t.find("AssetDefs")); a != std::string::npos; a = t.find('{', a + 1)) {
                size_t b = t.find('}', a);
                if (b == std::string::npos) break;
                std::string blk = t.substr(a, b - a);
                auto field = [&](const char* n) {
                    size_t i = blk.find(n);
                    if (i == std::string::npos) return std::string();
                    size_t q = blk.find('"', i), e = q == std::string::npos ? q : blk.find('"', q + 1);
                    return e == std::string::npos ? std::string() : blk.substr(q + 1, e - q - 1);
                };
                std::string id = field("DefID"), file = field("FileName");
                if (field("AssetType") != "Cho" || id.empty()) continue;
                for (char& c : id) c = (char)tolower((unsigned char)c);
                (*g_defs)[id] = file;
            }
        }
        logf("asset defs fallback: %u Cho entries from gamedb/incredibles/assetdefs.lua", (unsigned)g_defs->size());
    }
    std::string k = key ? key : "";
    for (char& c : k) c = (char)tolower((unsigned char)c);
    auto it = g_defs->find(k);
    return it == g_defs->end() ? nullptr : it->second.c_str();
}
static volatile LONG g_assetDefLogged;
static const char* __fastcall hkAssetDef(void* self, void*, const char* key)
{
    const char* r = g_assetDef(self, nullptr, key);
    if (r && !*r) {
        const char* f = nullptr;
        __try { f = fallbackDef(key); } __except (EXCEPTION_EXECUTE_HANDLER) { f = nullptr; }
        if (f) {
            if (InterlockedIncrement(&g_assetDefLogged) <= 20) { char k[64] = ""; safeCopy(k, sizeof(k), key); logf("assetdef '%s' -> '%s' (fallback)", k, f); }
            return f;
        }
    }
    return r;
}
typedef uint32_t*(__fastcall* ChoMakeFn)(void* self, void* edx, uint32_t* out, const char* name, uint32_t a3, uint32_t a4, uint32_t a5);
static ChoMakeFn g_choMake;
static volatile LONG g_choMakeLogged;
static uint32_t* __fastcall hkChoMake(void* self, void*, uint32_t* out, const char* name, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t* r = g_choMake(self, nullptr, out, name, a3, a4, a5);
    char n[64] = "";
    safeCopy(n, sizeof(n), name);
    if ((strstr(n, "oy") || strstr(n, "pawn") || !n[0]) && InterlockedIncrement(&g_choMakeLogged) <= 20)
        logf("cho make '%s' -> %08X", n, r == out && out ? *out : 0xffffffff);
    return r;
}
typedef void(__cdecl* PlaceToyFn)(void* msg);
static PlaceToyFn g_placeToy;
static void __cdecl hkPlaceToy(void* msg) { logf("ToyCreation_PlaceToy callback (msg %p)", msg); g_placeToy(msg); }
// MissionContext update (FUN_0166ffb0, per frame per context): logs an IN_ context's activation state when it changes.
// Offsets from the context parser FUN_016748d0 and the update itself; +0x10 is the context's name string.
typedef void(__fastcall* MissionTickFn)(uint8_t* self, void* edx, float dt);
static MissionTickFn g_missionTick;
// test (opacitylog10): FUN_0044ad80 -> bit i set = primitive i is "faded" (opacity param * object fade < 1) and the
// opaque G-buffer list skips it (FUN_00461020); logs our skipped primitives with the values that decided it
typedef uint32_t(__cdecl* OpacityMaskFn)(uint32_t n, float fade, uint32_t prims, uint32_t insts);
static OpacityMaskFn g_opacityMask;
static uint32_t __cdecl hkOpacityMask(uint32_t n, float fade, uint32_t prims, uint32_t insts)
{
    uint32_t r = g_opacityMask(n, fade, prims, insts);
    static std::mutex m;
    static std::set<std::string> seen;
    uint32_t all = n >= 32 ? 0xffffffffu : (1u << n) - 1;
    if (r && (r & all) != all) {   // one object, some primitives skipped and some kept: log all of them once
        std::string line;
        for (uint32_t i = 0; i < n && i < 32; ++i) {
            uint32_t prim = *(uint32_t*)(uintptr_t)(prims + 4 * i);
            uint8_t* shd = *(uint8_t**)(uintptr_t)(prim + 0x10);
            uint32_t w = shd ? *(uint32_t*)(shd + 0x50) : 0;
            float v = w ? *(float*)(uintptr_t)((w >> 16) + *(uint32_t*)(uintptr_t)(insts + 4 * i)) : 1.0f;
            char e[96];
            sprintf_s(e, " [%u %.8s w%08x v%.3f %s]", i, shd && *(const char**)shd ? *(const char**)shd : "?", w, v, (r >> i) & 1 ? "SKIP" : "keep");
            line += e;
        }
        std::lock_guard<std::mutex> lock(m);
        if (seen.size() < 300 && seen.insert(line).second) logf("opacity mixed: fade %.3f%s", fade, line.c_str());
        static int dumped;
        for (uint32_t i = 0; i < n && i < 32 && dumped < 6; ++i) {
            if (!((r >> i) & 1)) continue;
            uint32_t prim = *(uint32_t*)(uintptr_t)(prims + 4 * i);
            uint8_t* shd = *(uint8_t**)(uintptr_t)(prim + 0x10);
            float* inst = *(float**)(uintptr_t)(insts + 4 * i);
            std::string f;
            for (int k = 0; k < 12; ++k) { char e[16]; sprintf_s(e, " %.3f", inst[k]); f += e; }
            std::string pr;
            for (int k = 0; k < 12; ++k) { char e[16]; sprintf_s(e, " %08x", ((uint32_t*)(uintptr_t)prim)[k]); pr += e; }
            logf("skipped primitive %s sizeA %u: instance floats%s | prim words%s", shd && *(const char**)shd ? *(const char**)shd : "?",
                 shd ? *(uint16_t*)(shd + 0x6a) : 0, f.c_str(), pr.c_str());
            ++dumped;
        }
    }
    for (uint32_t i = 0; r && i < n && i < 32; ++i) {
        uint32_t prim = *(uint32_t*)(uintptr_t)(prims + 4 * i);
        uint8_t* shd = *(uint8_t**)(uintptr_t)(prim + 0x10);
        const char* nm = shd ? *(const char**)shd : nullptr;
        uint32_t w = shd ? *(uint32_t*)(shd + 0x50) : 0;
        float v = w ? *(float*)(uintptr_t)((w >> 16) + *(uint32_t*)(uintptr_t)(insts + 4 * i)) : 1.0f;
        char key[160];
        sprintf_s(key, "%s %08x %.3f %.3f %u", nm ? nm : "(null)", w, v, fade, (r >> i) & 1);
        std::lock_guard<std::mutex> lock(m);
        if (seen.size() < 300 && seen.insert(key).second)
            logf("opacity mask: %s (word %08x: value %.3f, object fade %.3f) skip %u", nm ? nm : "(null)", w, v, fade, (r >> i) & 1);
    }
    return r;
}

static RENDERDOC_API_1_1_2* g_rdoc;   // renderdoc10: RenderDoc loaded in-process, one frame captured once the world runs
static void __fastcall hkMissionTick(uint8_t* self, void*, float dt)
{
    g_missionTick(self, nullptr, dt);
    const char* name = *(const char**)(self + 0x10);
    if (!name || _strnicmp(name, "IN_", 3)) return;
    static bool armed;
    if (g_rdoc && !armed) {
        armed = true;
        std::thread([] { Sleep(1000 * g_cfg.renderdoc10); g_rdoc->TriggerCapture(); logf("renderdoc: capture triggered"); }).detach();
    }
    static const uint32_t stateVa[] = {0x0228a2c0, 0x0228a2c4, 0x0228a2c8, 0x0228a2cc, 0x0228a2d0, 0x0228a2d4, 0x0228a2d8,
                                       0x0228a2dc, 0x0228a2e0, 0x0228a2e4, 0x0228a2e8, 0x0228a2ec, 0x0228a2f0, 0x0228a2f4};
    auto stateName = [](uint32_t v) {
        for (int i = 0; i < 14; ++i) if (*(uint32_t*)rt(stateVa[i]) == v) return i;
        return -1;
    };
    uint32_t state = *(uint32_t*)(self + 0x44), m = *(uint32_t*)(self + 0x198);
    char sig[256];
    sprintf_s(sig, "state=%d(%08X) mission=%08X mflags=%02X faft=%d trig=%d b159=%d b15f=%d inzone=%d ignzone=%d avready=%d reason=%d "
                   "f3f=%08X beh=%d init=%d", stateName(state), state, m, m ? *(uint8_t*)(m + 0x3d9) : 0xff, self[0x112], self[0x113],
              self[0x159], self[0x15f], self[0x114], self[0x135], self[0x137], *(int*)(self + 0x290), *(uint32_t*)(self + 0xfc),
              *(int*)(self + 0xc8), self[0x224]);
    static std::unordered_map<uint8_t*, std::string> last;
    std::string& l = last[self];
    if (l != sig) { l = sig; logf("mission %.60s: %s", name, sig); }
}
// Meridian node creators (cdecl placement constructors registered per class, FUN_006bc220): count what a graph builds.
typedef void*(__cdecl* NodeMakeFn)(void* mem, uint32_t a2, uint32_t a3);
static NodeMakeFn g_mkStart, g_mkLocator, g_mkSpawnExp;
static volatile LONG g_nStart, g_nLocator, g_nSpawnExp;
static void* __cdecl hkMkStart(void* m, uint32_t a2, uint32_t a3) { logf("MNode PlayerStartPosNode %p (#%ld)", m, InterlockedIncrement(&g_nStart)); return g_mkStart(m, a2, a3); }
static void* __cdecl hkMkLocator(void* m, uint32_t a2, uint32_t a3) { LONG n = InterlockedIncrement(&g_nLocator); if (n <= 2 || n % 100 == 0) logf("MNode LocatorNode #%ld", n); return g_mkLocator(m, a2, a3); }
static void* __cdecl hkMkSpawnExp(void* m, uint32_t a2, uint32_t a3) { LONG n = InterlockedIncrement(&g_nSpawnExp); if (n <= 2 || n % 50 == 0) logf("MNode SpawnExpendable #%ld", n); return g_mkSpawnExp(m, a2, a3); }

// Trace: instance header clone FUN_004f5900(index, a2, delta, instRoot) (thiscall on the instance header, ret 0x10).
// Header = [name, parent|rc, sibling|flags, child-or-self|slot, ...]. FUN_004b4470 gave every header of the cloned array
// a temp slot (DAT_021af260); 0x4f5a25 maps the +0xc target through it without a check, so a +0xc that leads outside
// the array reads slot 0 (NULL) and faults at 0x4cdabe. Log such headers with their target.
typedef void(__fastcall* HdrCloneFn)(void* self, void* edx, uint32_t index, uint32_t a2, int delta, uint32_t instRoot);
static HdrCloneFn g_hdrClone;
static volatile LONG g_hdrLogged;
static void hdrName(const uint32_t* h, char* out, size_t cap)
{
    __try { if (h && h[0]) safeCopy(out, cap, (const char*)(h[0] + 0xc)); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void __fastcall hkHdrClone(void* self, void*, uint32_t index, uint32_t a2, int delta, uint32_t instRoot)
{
    typedef void*(__fastcall * ResolveFn)(void* handleField, void* edx);
    ResolveFn res = (ResolveFn)rt(0x004B46D0);
    uint32_t* inst = (uint32_t*)self;
    uint32_t* tmpl = (uint32_t*)((char*)self - delta);
    uint32_t* t = (uint32_t*)res(inst + 3, nullptr);
    uint32_t slots = *(uint32_t*)rt(0x021B725C), e = 1, slot = 0;
    if (t) {
        slot = ((t[2] & 0xe001ffff) | (t[3] >> 3)) >> 17;
        e = slot < slots ? ((uint32_t*)rt(0x021AF260))[slot] : 1;
    }
    if ((!t || !e) && InterlockedIncrement(&g_hdrLogged) <= 6) {
        char name[80] = "?", tname[80] = "?", pname[80] = "?";
        hdrName(tmpl, name, sizeof(name));
        hdrName(t, tname, sizeof(tname));
        uint32_t* tp = t ? (uint32_t*)res(t + 1, nullptr) : nullptr;
        hdrName(tp, pname, sizeof(pname));
        const uint32_t* base = (const uint32_t*)((char*)tmpl - index * 0x14);
        logf("header clone %u/%u '%s' (instRoot %08X, %u slots): template %p [%08X %08X %08X %08X %08X] +0xc -> %p '%s' (array index %d, slot %u) [%08X %08X %08X %08X %08X] parent %p '%s'",
             index, g_instHeaders, name, instRoot, slots, tmpl, tmpl[0], tmpl[1], tmpl[2], tmpl[3], tmpl[4], t, tname,
             t ? (int)(((const char*)t - (const char*)base) / 0x14) : -1, slot,
             t ? t[0] : 0, t ? t[1] : 0, t ? t[2] : 0, t ? t[3] : 0, t ? t[4] : 0, tp, pname);
    }
    g_hdrClone(self, nullptr, index, a2, delta, instRoot);
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

static std::string mergeVtxf(const std::string& t30, const std::string& t10);
static int* __cdecl hkOpen(const char* path, uint32_t flags, uint32_t a3, uint32_t a4, int* a5)
{
    int* r = g_open(path, flags, a3, a4, a5);
    char p[300];
    safeCopy(p, sizeof(p), path);
    if (r && g_cfg.shaders10 && !(flags & 1) && _stricmp(p, "shaders/vformat.vtxf") == 0) {   // + 1.0's formats
        typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
        typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
        typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
        void** vt = *(void***)r;
        std::string t30, t10;
        t30.resize(((SizeFn)vt[4])(r, nullptr));
        uint32_t got = t30.empty() ? 0 : ((ReadFn)vt[5])(r, nullptr, &t30[0], (uint32_t)t30.size());
        if (got == t30.size() && read10s("shaders/vformat.vtxf", t10)) {
            std::string m = mergeVtxf(t30, t10);
            ((DtorFn)vt[11])(r, nullptr, 1);
            return memFile(m.data(), m.size());
        }
        ((DtorFn)vt[11])(r, nullptr, 1);
        return g_open(path, flags, a3, a4, a5);
    }
    if (g_cfg.trace && (_stricmp(p, "GameDB/Incredibles/bitmasks.lua") == 0 || _stricmp(p, "gamedb/theclonewars/bitmasks.lua") == 0)) {
        char line[600];
        int len = sprintf_s(line, "gamedb package open %s, stack:", p);
        stackOf((const uint32_t*)_AddressOfReturnAddress(), line, sizeof(line), len, 24);
        logf("%s", line);
    }
    if (!(flags & 1) && endsWith(p, ".lua") && takeChdPath(p)) return chdForLua(p, r);
    if (r && g_cfg.memtest[0] && !(flags & 1) && memtestMatch(p)) {
        size_t n = 0;
        r = copyToMemFile(r, &n);
        logf("memtest %s %u B -> %s", p, (unsigned)n, r ? "MemFile" : "FAILED");
        return r;
    }
    bool blocked = false;
    std::string missWhy = " (not served)";
    if (g_cfg.block10[0]) {
        std::string np = norm(p), list = g_cfg.block10;
        for (size_t a = 0, b; a < list.size(); a = b + 1) {
            b = list.find(',', a);
            if (b == std::string::npos) b = list.size();
            std::string pre = list.substr(a, b - a);
            if (!pre.empty() && np.rfind(pre, 0) == 0) blocked = true;
        }
    }
    if (!r && !blocked && g_cfg.serve10 && g_cfg.install10[0] && !(flags & 1)) {   // flags bit 0 = write (never served)
        std::string np = norm(p), hit, over;
        {
            std::lock_guard<std::mutex> lock(g_sessLock);
            auto it = g_served.find(np);                 // a world session product (.mtb/.vbuf/.ibuf)
            if (it != g_served.end()) hit = it->second;  // kept: the engine reopens files
            else {
                auto o = g_over10.find(np);              // a full-path member of a loaded 1.0 archive
                if (o != g_over10.end()) over = o->second;
            }
        }
        if (!hit.empty()) {
            r = memFile(hit.data(), hit.size());
            logf("served %s %u B from asset session -> %s", p, (unsigned)hit.size(), r ? "ok" : "FAILED");
            return r;
        }
        size_t n = 0;
        std::string where;
        missWhy = over.empty() ? " (not in the 1.0 archives, " + std::to_string(g_zips.size()) + " archives open)" : " (overlay " + over + " unreadable)";
        void* data = read10(over.empty() ? p : over.c_str(), &n, &where);
        // 3.0 registers a language table only with its controller variant <lang>_pc_con.dct beside it, which 1.0
        // never shipped: without one the whole playset table was dropped (in-game 2026-10-13, lang\IN). The WIN32
        // table stands in (same strings).
        if (!data && endsWith(p, "_pc_con.dct")) {
            std::string w = std::string(p, strlen(p) - 11) + "_WIN32.dct";
            data = read10(w.c_str(), &n, &where);
        }
        if (data) {
            indexZip10(where.substr(0, where.find(':')));
            std::string out, note;
            const char* codec = nullptr;
            try {
                if (endsWith(p, ".tbody")) {             // D3D11 refuses sub-4x4 block textures (padSmallDxt)
                    out.assign((const char*)data, n);
                    padSmallDxt(out);
                    codec = out.size() == n && memcmp(out.data(), data, n) ? "tbody" : nullptr;
                } else codec = transcode(p, data, n, out, note);
            } catch (std::exception& e) {   // never unwind into engine code (that ends in a silent fail-fast)
                note += std::string("TRANSCODE EXCEPTION: ") + e.what() + " ";
                codec = nullptr;
            } catch (...) {
                note += "TRANSCODE EXCEPTION ";
                codec = nullptr;
            }
            if (!over.empty()) note += "(archive overlay) ";
            r = codec ? memFile(out.data(), out.size()) : memFile(data, n);
            logf("served %s %u B from 1.0 %s codec=%s%s%s -> %s", p, (unsigned)(codec ? out.size() : n), where.c_str(),
                 codec ? codec : "raw", note.empty() ? "" : " ", note.c_str(), r ? "ok" : "FAILED");
            mz_free(data);
            return r;
        }
    }
    // misses always (rare and cheap; what an unexplained fallback asks for), hits only when tracing
    if (g_cfg.trace || !r) logf("open %s flags=%X -> %s%s", p, flags & 0xff, r ? "ok" : "MISSING", r ? "" : missWhy.c_str());
    return r;
}

// ---- real 1.0 shaders at runtime (gen30.hpp; plan in DI3Tools/gen30.py) ----
// The engine loads its whole shader package once, at boot (renderer init FUN_00479470: shaders.list ->
// FUN_00476180, then the linker FUN_00472050), and 3.0's index fields cap a session at 2047 programs per stage --
// 1.0's shaders do not all fit beside 3.0's, so each is registered when a material first names it ('x'<GUID>, the
// name the world transcode gives it): the shader-by-name lookup FUN_0045c820 is hooked. On a miss for an 'x' name
// the 1.0 shader is translated (programs compiled once, then from <plugin dir>\di1compat_shaders), its programs are
// added through the engine's own registration (VS FUN_0045c230 / PS FUN_0045c350: CreateVertexShader /
// CreatePixelShader, table append) under ids above every shipped one (the tables stay sorted), the .shd is
// finalized by the engine's FUN_0045cf40 (EDI = shader, ECX = &cursor, as the loader calls it) and each pass gets
// a pass-state record built as the linker builds one (records 0x38 B at 0x02173a68, count u16 at 0x02173a66).
struct Blob : IUnknown {
    virtual void* STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};
typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, Blob**, Blob**);
static D3DCompileFn g_d3dCompile;
static std::string g_shaderCache;
static std::mutex g_sh10Lock;
static std::unordered_map<std::string, uint8_t*> g_sh10;                       // 'x'<GUID> -> finalized .shd (nullptr: failed)
static std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> g_prog10;  // code -> engine id, table index

static uint64_t fnv64(const std::string& s)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

static thread_local std::string g_compiling10;   // the shader shader10 is generating (debug views name it)
static bool compile10(const std::string& hlsl0, const char* target, std::string& code, std::string& msg)
{
    std::string idcolor;
    if (g_cfg.bufview10 == 8 && target[0] == 'p') {
        // test: final passes skip, G-buffer albedo carries the shader's ID colour (3.0's own composite shows it)
        uint64_t h = fnv64(g_compiling10);
        char c[160];
        sprintf_s(c, "o.o1 = float4(sqrt(float3(%.2f, %.2f, %.2f)), 0.2236);", (h & 3) / 3.0, (h >> 2 & 3) / 3.0, (h >> 4 & 3) / 3.0);
        if (hlsl0.find("tLight") != std::string::npos)
            idcolor = "float4 main(float4 p : SV_Position) : SV_Target0 { discard; return 0; }";
        else if (size_t at = hlsl0.find("o.o1 = float4(0, 0, 0, 0);"); at != std::string::npos) {
            idcolor = hlsl0;
            idcolor.replace(at, strlen("o.o1 = float4(0, 0, 0, 0);"), c);
            logf("idcolor %s = %.2f %.2f %.2f", g_compiling10.c_str(), (h & 3) / 3.0, (h >> 2 & 3) / 3.0, (h >> 4 & 3) / 3.0);
        }
    }
    if (g_cfg.bufview10 >= 6 && g_cfg.bufview10 <= 7 && target[0] == 'p' && (g_cfg.bufview10 == 7 ? hlsl0.find("SV_Target1") == std::string::npos
                                                                           : hlsl0.find("tLight") != std::string::npos)) {
        // test: each translated final pass a solid colour of its own, logged with its shader
        uint64_t h = fnv64(g_compiling10);
        char c[200];
        sprintf_s(c, "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(%.2f, %.2f, %.2f, 1); }",
                  (h & 3) / 3.0, (h >> 2 & 3) / 3.0, (h >> 4 & 3) / 3.0);
        idcolor = c;
        logf("idcolor %s = %.2f %.2f %.2f", g_compiling10.c_str(), (h & 3) / 3.0, (h >> 2 & 3) / 3.0, (h >> 4 & 3) / 3.0);
    }
    static const std::string magenta = "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(1, 0, 1, 1); }";
    static const std::string bufview[5] = {
        "cbuffer perView : register(b1) { float4 CB1[18]; }; Texture2D t3 : register(t3); SamplerState s3 : register(s3);\n"
        "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(t3.Sample(s3, p.xy * CB1[17].zw).rgb, 1); }",
        "cbuffer perView : register(b1) { float4 CB1[18]; }; Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
        "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(t0.Sample(s0, p.xy * CB1[17].zw).xxx, 1); }",
        "cbuffer perView : register(b1) { float4 CB1[18]; }; Texture2D t4 : register(t4); SamplerState s4 : register(s4);\n"
        "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(t4.Sample(s4, p.xy * CB1[17].zw).rgb, 1); }",
        "cbuffer perInstance : register(b4) { float4 CB4[1]; };\n"
        "float4 main(float4 p : SV_Position) : SV_Target0 { return float4(CB4[0].rgb * 0.5 + float3(0.5, 0, 0) * (CB4[0].a == 0), 1); }",
        "Texture2D t5 : register(t5); SamplerState s5 : register(s5);\n"
        "struct PSIn { float4 pos : SV_Position; float4 TEXCOORD0 : TEXCOORD0; float4 TEXCOORD1 : TEXCOORD1; float4 TEXCOORD2 : TEXCOORD2; };\n"
        "float4 main(PSIn i) : SV_Target0 { return float4(t5.Sample(s5, i.TEXCOORD2.xy).rgb, 1); }"};
    const std::string& hlsl = g_cfg.magenta10 && target[0] == 'p' ? magenta
        : !idcolor.empty() ? idcolor
        : g_cfg.bufview10 && target[0] == 'p' && hlsl0.find("tLight") != std::string::npos ? bufview[(g_cfg.bufview10 - 1) % 5] : hlsl0;
    char name[40];
    sprintf_s(name, "\\%016llx.dxbc", (unsigned long long)fnv64(std::string(target) + "\n" + hlsl));
    std::string path = g_shaderCache + name;
    FILE* f = nullptr;
    if (!fopen_s(&f, path.c_str(), "rb") && f) {
        char buf[65536];
        size_t got;
        code.clear();
        while ((got = fread(buf, 1, sizeof(buf), f)) > 0) code.append(buf, got);
        fclose(f);
        if (code.size() > 32 && !code.compare(0, 4, "DXBC")) return true;
    }
    if (!g_d3dCompile) { msg = "no d3dcompiler_47"; return false; }
    Blob *c = nullptr, *e = nullptr;
    HRESULT hr = g_d3dCompile(hlsl.data(), hlsl.size(), "di1compat", nullptr, nullptr, "main", target, 0, 0, &c, &e);
    if (e) { msg.assign((const char*)e->GetBufferPointer(), e->GetBufferSize()); e->Release(); }
    if (FAILED(hr) || !c) { if (c) c->Release(); return false; }
    code.assign((const char*)c->GetBufferPointer(), c->GetBufferSize());
    c->Release();
    if (!fopen_s(&f, path.c_str(), "wb") && f) { fwrite(code.data(), 1, code.size(), f); fclose(f); }
    return true;
}

// A program into the engine's table for its stage -> engine id and table index (identical code shared).
static bool registerProgram10(const gen30::Program& p, uint32_t& id, uint32_t& index)
{
    uint64_t key = fnv64(p.code) ^ (p.ps ? 0x9e3779b97f4a7c15ull : 0);
    if (auto it = g_prog10.find(key); it != g_prog10.end()) { id = it->second.first; index = it->second.second; return true; }
    const uint32_t countVa = p.ps ? 0x0216B04C : 0x0215EFC4, tableVa = p.ps ? 0x0216B050 : 0x0215EFC8;
    uint16_t n = *(uint16_t*)rt(countVa);
    if (n >= 2047) { logf("shaders10: %s table full (%u)", p.ps ? "PS" : "VS", n); return false; }
    static uint32_t next[2] = {0xF0000000u, 0xF0000000u};
    uint32_t last = n ? *(uint32_t*)rt(tableVa + 8 * (n - 1)) : 0;
    uint32_t& nx = next[p.ps ? 1 : 0];
    if (nx <= last) nx = last + 1;
    id = nx++;
    void* data = _aligned_malloc(p.code.size(), 16);   // kept: the engine reads VS bytecode again for input layouts
    memcpy(data, p.code.data(), p.code.size());
    typedef void(__cdecl * RegFn)(uint32_t id, const void* data, uint32_t size);
    ((RegFn)rt(p.ps ? 0x0045C350 : 0x0045C230))(id, data, (uint32_t)p.code.size());
    if (*(uint16_t*)rt(countVa) != n + 1 || !*(uint32_t*)rt(tableVa + 8 * n + 4)) {
        logf("shaders10: program %08X not created", id);
        return false;
    }
    index = n;
    g_prog10[key] = {id, index};
    return true;
}

static void finalizeShd10(uint8_t* shd)
{
    uint32_t cursor[2] = {(uint32_t)(uintptr_t)(shd + 0xE0), 0};
    uint32_t* pc = cursor;
    void* fn = (void*)rt(0x0045CF40);
    __asm {
        push edi
        mov ecx, pc
        mov edi, shd
        call fn
        pop edi
    }
}

static uint32_t inputLayout10(uint32_t vfIndex, uint32_t vsIndex)
{
    void* fn = (void*)rt(0x0044D430);
    uint32_t entry = (uint32_t)rt(0x02170664) + vfIndex * 0x34, r;
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

// Pass-state records (0x38 B at 0x02173a68, u16 count at 0x02173a66): the renderer packs a record index into 12 bits
// of its draw sort key, so 4096 is a hard ceiling -- and 3.0's boot linker FUN_00472050 spends 3802 of them on every
// pass of all 836 3.0 shaders. So nothing links at boot (hkLinkAll keeps only the linker's shader-array sort) and a
// shader's passes are linked on its first lookup (every shader the engine uses comes through FUN_0045c820): the
// records go to the shaders a session uses. Fields as FUN_00472050 builds them; program indices by the engine's
// FUN_00450970 over its sorted tables (ours too: their ids sort after every shipped one).
// The render thread adds records too (FUN_0045c5c0 clones one per extra vertex format, mid-draw); with our linker
// writing from loader threads the two raced for the count and the clone chains (FUN_0045c5c0 walks them in a loop):
// the first lazy-linking run froze on the loading screen (2026-10-13). One lock for both writers.
static CRITICAL_SECTION g_recCs;
static std::unordered_set<uint8_t*> g_linked;
struct RecLock { RecLock() { EnterCriticalSection(&g_recCs); } ~RecLock() { LeaveCriticalSection(&g_recCs); } };

typedef uint32_t(__cdecl* CloneRecFn)(uint32_t rec, uint32_t vformat);
static CloneRecFn g_cloneRec;
static uint32_t __cdecl hkCloneRec(uint32_t rec, uint32_t vformat)
{
    RecLock lock;
    uint32_t r = g_cloneRec(rec, vformat);
    uint32_t* c = (uint32_t*)rt(0x02173A68) + 14 * (r & 0xffff);
    const char* nm = c[6] ? *(const char**)(uintptr_t)c[6] : nullptr;
    if (!c[7] && nm && nm[0] == 'x') {
        static int told;
        if (told++ < 40) logf("clone without input layout: %s pass %u vformat %u (record %u from %u)", nm, c[0] & 0x3f, vformat & 0xff, r & 0xffff, rec & 0xffff);
    }
    return r;
}

static bool linkShader(uint8_t* shd)
{
    RecLock lock;
    bool seen = g_linked.count(shd) != 0;
    int fresh = 0;
    typedef uint32_t(__cdecl * IndexFn)(uint32_t id, uint32_t table, uint32_t count, uint32_t fallback);
    IndexFn index = (IndexFn)rt(0x00450970);
    auto idx = [&](uint32_t id, uint32_t tableVa, uint32_t countVa, uint32_t fallback) -> uint32_t {
        return id ? index(id, (uint32_t)rt(tableVa), *(uint16_t*)rt(countVa), fallback) & 0xffff : 0;
    };
    uint32_t shaderIndex = 0x3ff;                      // the record key's shader field; ours are not in the array
    uint32_t nsh = *(uint32_t*)rt(0x0215CFC0);
    for (uint32_t k = 0; k < nsh; ++k)
        if (*(uint32_t*)rt(0x0215CFC8 + 8 * k) == (uint32_t)(uintptr_t)shd) { shaderIndex = k & 0x3ff; break; }
    uint8_t* passes = *(uint8_t**)(shd + 0x38);
    int npass = shd[0x19];
    for (int i = 0; i < npass; ++i) {
        uint8_t* p = passes + 0x50 * i;
        if (!p[0x14]) continue;                        // no stage (yet): not drawn
        uint16_t& count = *(uint16_t*)rt(0x02173A66);
        uint16_t had = *(uint16_t*)(p + 0x16);         // linked already: its record points back at this pass
        uint32_t* old = (uint32_t*)rt(0x02173A68) + 14 * had;
        if (had < count && old[6] == (uint32_t)(uintptr_t)shd && (old[0] & 0x3f) == (uint32_t)i) continue;
        ++fresh;
        if (count >= 4095) { logf("pass-state records full (%u)", count); return false; }
        uint16_t n = count;
        uint32_t* rec = (uint32_t*)rt(0x02173A68) + 14 * n;
        memset(rec, 0, 0x38);
        uint32_t word = *(uint32_t*)p;
        uint32_t vsi = idx(*(uint32_t*)(p + 0x28), 0x0215EFC8, 0x0215EFC4, 0x7ff);
        uint32_t psi = idx(*(uint32_t*)(p + 0x30), 0x0216B050, 0x0216B04C, 0x7ff);
        uint32_t s0 = idx(*(uint32_t*)(p + 0x2c), 0x0216AFCC, 0x0216AFC8, 0xf), s1 = idx(*(uint32_t*)(p + 0x38), 0x0216F0D8, 0x0216F0D4, 0xf);
        uint32_t s2 = idx(*(uint32_t*)(p + 0x3c), 0x0216F15C, 0x0216F158, 0xf), s3 = idx(*(uint32_t*)(p + 0x34), 0x0216F054, 0x0216F050, 0xf);
        rec[0] = (uint32_t)(i & 0x3f) | (shaderIndex << 6) | ((word >> 25) << 16) | ((psi & 0xff) << 24);
        rec[1] = ((psi & 0x7ff) >> 8) | ((vsi & 0x7ff) << 3) | ((uint32_t)p[0x18] << 14);
        rec[2] = (s0 & 0xf) | (s1 & 0xf) << 4 | (s2 & 0xf) << 8 | (s3 & 0xf) << 12;
        ((uint16_t*)rec)[6] = n;                       // +0x0c, +0x0e: itself (FUN_0045c5c0 chains other vformats)
        ((uint16_t*)rec)[7] = n;
        ((uint16_t*)rec)[8] = *(uint16_t*)(p + 0x18);  // +0x10
        ((uint16_t*)rec)[9] = p[0x15];                 // +0x12: run length
        rec[5] = *(uint32_t*)(p + 0x1c);
        rec[6] = (uint32_t)(uintptr_t)shd;
        rec[7] = inputLayout10(p[0x18], vsi);
        if (shaderIndex == 0x3ff)
            logf("ours: %s pass %d word %08x stage %u run %u b1c %02x layout %08x", *(const char**)shd, i, word, p[0x14], p[0x15],
                 p[0x1c], rec[7]);
        rec[8] = vsi ? *(uint32_t*)rt(0x0215EFCC + 8 * vsi) : 0;
        rec[9] = psi ? *(uint32_t*)rt(0x0216B054 + 8 * psi) : 0;
        rec[10] = s0 ? *(uint32_t*)rt(0x0216AFD0 + 8 * s0) : 0;
        rec[11] = s1 ? *(uint32_t*)rt(0x0216F0DC + 8 * s1) : 0;
        rec[12] = s2 ? *(uint32_t*)rt(0x0216F160 + 8 * s2) : 0;
        rec[13] = s3 ? *(uint32_t*)rt(0x0216F058 + 8 * s3) : 0;
        *(uint16_t*)(p + 0x16) = n;
        count = n + 1;
    }
    if (seen && fresh) logf("late link: shader %p gained %d staged passes", shd, fresh);
    if (seen) return true;
    g_linked.insert(shd);
    if (g_linked.size() % 50 == 0) logf("linked %u shaders (records %u)", (unsigned)g_linked.size(), *(uint16_t*)rt(0x02173A66));
    return true;
}

typedef void(__cdecl* LinkAllFn)();
static LinkAllFn g_linkAll;
static void __cdecl hkLinkAll()
{
    typedef void(__cdecl * SortFn)(uint32_t arr, uint32_t n);
    ((SortFn)rt(0x004703A0))((uint32_t)rt(0x0215CFC4), *(uint32_t*)rt(0x0215CFC0));
    // The shaders the engine builds in code have no name (shd+0): they are used by pointer, never looked up, so
    // they link now -- unlinked, the world drew black or white and the loading screen never closed (2026-10-13).
    unsigned builtin = 0;
    for (uint32_t k = 0, n = *(uint32_t*)rt(0x0215CFC0); k < n; ++k) {
        uint8_t* s = *(uint8_t**)rt(0x0215CFC8 + 8 * k);
        if (!*(const char**)s) { linkShader(s); ++builtin; }
    }
    logf("shader linking deferred to first use (%u shaders registered, %u built-in linked now: %u pass-state records)",
         *(uint32_t*)rt(0x0215CFC0), builtin, *(uint16_t*)rt(0x02173A66));
}

// create: generate it if new (the material transcode asks first, so a 1.0 shader that does not translate keeps its
// material on the other tiers); lookups only find ones already made, whatever the name's length
static uint8_t* shader10(const char* name, bool create)
{
    std::lock_guard<std::mutex> lock(g_sh10Lock);
    if (auto it = g_sh10.find(name); it != g_sh10.end()) return it->second;
    if (!create) return nullptr;
    uint8_t*& out = g_sh10[name];
    out = nullptr;
    gen30::Generated g;
    g_compiling10 = name;
    gen30::generate(g_render.shaders10, read10s, g_vf10, std::string(name + 1), compile10, g);
    for (auto& pr : g.problems) logf("shaders10 %s: %s", name, pr.c_str());
    if (g.shd.empty() || !g.problems.empty()) return nullptr;
    std::map<uint32_t, uint32_t> engineId, vsIndex, psIndex;   // generator id -> engine id; engine id -> index
    for (auto& p : g.programs) {
        uint32_t id = 0, index = 0;
        if (!registerProgram10(p, id, index)) return nullptr;
        engineId[p.id] = id;
        (p.ps ? psIndex : vsIndex)[id] = index;
    }
    std::string shd = g.shd;
    for (int i = 0; i < (uint8_t)shd[0x19]; ++i)
        for (size_t off : {(size_t)0x28, (size_t)0x30}) {
            uint32_t& pid = *(uint32_t*)&shd[0xE0 + 0x50 * i + off];
            if (pid) pid = engineId.at(pid);
        }
    uint8_t* mem = (uint8_t*)_aligned_malloc(shd.size(), 16);   // shaders live for the session, as the engine's do
    memcpy(mem, shd.data(), shd.size());
    char* nm = _strdup(name);
    *(uint32_t*)mem = (uint32_t)(uintptr_t)nm;
    *(uint32_t*)(mem + 4) = (uint32_t)shd.size();
    finalizeShd10(mem);
    logf("shaders10 %s: %u passes, %u programs registered (pass-state records in use: %u)", name, (unsigned)mem[0x19],
         (unsigned)g.programs.size(), *(uint16_t*)rt(0x02173A66));
    out = mem;
    return out;
}

typedef uint8_t*(__cdecl* ShaderByNameFn)(const char* name);
static ShaderByNameFn g_shaderByName;
static uint8_t* __cdecl hkShaderByName(const char* name)
{
    uint8_t* s = nullptr;
    // 'x'<1.0 shader>: hash-named ones (33 chars) are made on demand; named ones ('xfades') exist only if the
    // transcode made them -- a name it could not translate reached the engine, whose loader task waited on it
    // forever: the respawn after Edna's dialog never finished and control never came back (2026-10-13)
    if (name && name[0] == 'x') s = shader10(name, strlen(name) == 33);
    bool ours = s != nullptr;
    if (!s) s = g_shaderByName(name);
    if (s && (ours || g_cfg.lazyLink10)) linkShader(s);   // with the boot linker on, 3.0's are linked already
    return s;
}

// vformat.vtxf: 3.0's formats plus every 1.0 format 3.0 lacks, so 1.0 vertex buffers draw as they are.
static std::string mergeVtxf(const std::string& t30, const std::string& t10)
{
    size_t end30 = 0;
    auto e30 = gen30::ventries(t30, &end30);
    if (!end30) return t30;
    std::set<std::string> have;
    for (auto& e : e30) { std::string g = e.guid; for (char& c : g) c = (char)tolower((unsigned char)c); have.insert(g); }
    std::string add;
    int n = 0;
    for (auto& e : gen30::ventries(t10)) {
        std::string g = e.guid;
        for (char& c : g) c = (char)tolower((unsigned char)c);
        if (have.count(g)) continue;
        std::string block = t10.substr(e.start, e.end - e.start);
        if (block.back() != ',') block += ",";
        add += "\t\t" + block + "\n";
        ++n;
    }
    logf("shaders10: vformat.vtxf + %d 1.0 formats", n);
    return t30.substr(0, end30) + add + t30.substr(end30);
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
    if (g_cfg.renderdoc10 > 0) {   // before the renderer creates its device, so RenderDoc's hooks see it
        HMODULE rd = LoadLibraryExA("C:\\Program Files\\RenderDoc\\x86\\renderdoc.dll", nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        auto get = rd ? (pRENDERDOC_GetAPI)GetProcAddress(rd, "RENDERDOC_GetAPI") : nullptr;
        if (get && get(eRENDERDOC_API_Version_1_1_2, (void**)&g_rdoc) == 1) {
            g_rdoc->SetCaptureFilePathTemplate((g_gameDir + "\\di3cap").c_str());
            logf("renderdoc: loaded; capture %d s after the first mission tick (missiontrace)", g_cfg.renderdoc10);
        } else logf("renderdoc: not loaded (%lu)", GetLastError());
    }
    _setmaxstdio(8192);   // every 1.0 archive used stays open (miniz holds a FILE*); the CRT default is 512
    auto slurpFile = [&](const char* name) {
        std::string text;
        FILE* tf = nullptr;
        if (!fopen_s(&tf, (std::string(dir) + "\\" + name).c_str(), "rb") && tf) {
            char buf[65536];
            size_t got;
            while ((got = fread(buf, 1, sizeof(buf), tf)) > 0) text.append(buf, got);
            fclose(tf);
        }
        return text;
    };
    g_renderOk = g_render.load(slurpFile("di1compat_render.txt"));
    logf("render table: %s (%u shaders, %u donors, %u materials, %u vertex plans)", g_renderOk ? "ok" : "MISSING",
         (unsigned)g_render.shaders.size(), (unsigned)g_render.donors.size(), (unsigned)g_render.matindex.size(),
         (unsigned)g_render.plans.size());
    octx::g_slack = 4096;   // instanced 1.0 SubNetworks must fit their arenas (octx.hpp)
    g_merOk = g_mer.load(slurpFile("di1compat_mer.txt"));
    logf("meridian table: %s (%u registered classes, %u described)", g_merOk ? "ok" : "MISSING",
         (unsigned)g_mer.registered.size(), (unsigned)g_mer.classes.size());
    if (g_cfg.shaders10) {
        HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
        g_d3dCompile = dc ? (D3DCompileFn)GetProcAddress(dc, "D3DCompile") : nullptr;
        g_shaderCache = std::string(dir) + "\\di1compat_shaders";
        CreateDirectoryA(g_shaderCache.c_str(), nullptr);
        std::string v10;
        if (read10s("shaders/vformat.vtxf", v10)) g_vf10 = gen30::vformats(v10);
        logf("shaders10: compiler %s, %u 1.0 vertex formats, %u 3.0 globals, cache %s", g_d3dCompile ? "ok" : "MISSING",
             (unsigned)g_vf10.size(), (unsigned)g_render.shaders10.globals.size(), g_shaderCache.c_str());
    }

    AddVectoredExceptionHandler(1, avHandler);
    if (MH_Initialize() != MH_OK) {
        logf("MinHook init failed; no hooks");
        return;
    }
    hook("AssetOpen", 0x0041CB10, "6AFF68????????", (void*)&hkOpen, (void**)&g_open);
    hook("GameDB locks load", 0x01612FA0, "6AFF64A100000000", (void*)&hkLoadLocks, (void**)&g_loadLocks);
    hook("GameDB inventory load", 0x014C08D0, "6AFF64A100000000", (void*)&hkLoadInventory, (void**)&g_loadInventory);
    hook("GameDB CHD callback load", 0x018DB450, "6AFF64A100000000", (void*)&hkLoadChdCallback, (void**)&g_loadChdCallback);
    hook("VR playset character slot", 0x00B7F910, "6AFF68????????64A100000000", (void*)&hkPlaysetChar, (void**)&g_playsetChar);
    if (g_cfg.trace) {
        hook("actor exists (trace)", 0x00751750, "518B4424088B00", (void*)&hkActorExists, (void**)&g_actorExists);
        hook("figure check (trace)", 0x00DDAE30, "64A1000000006AFF68", (void*)&hkFigureOk, (void**)&g_figureOk);
        hook("figure playset (trace)", 0x00435080, "6AFF68????????64A100000000", (void*)&hkFigurePlayset, (void**)&g_figurePlayset);
        hook("reader list (trace)", 0x00427330, "83EC08535556", (void*)&hkReaderList, (void**)&g_readerList);
        hook("reader item gate (trace)", 0x00DEB610, "538B5C2408", (void*)&hkReaderItemOk, (void**)&g_readerItemOk);
        hook("playset figure slot (trace)", 0x00DF9FD0, "6AFF68????????64A100000000", (void*)&hkIgpName, (void**)&g_igpName);
        hook("asset instance (trace)", 0x007734F0, "6AFF68????????64A100000000", (void*)&hkAssetInst, (void**)&g_assetInst);
        hook("SubNetwork instance guard (trace)", 0x004FF0E0, "6AFF68????????64A100000000", (void*)&hkInst, (void**)&g_inst);
        hook("header clone (trace)", 0x004F5900, "5153555657", (void*)&hkHdrClone, (void**)&g_hdrClone);
        hook("start lookup (trace)", 0x013A85D0, "6AFF68????????64A100000000", (void*)&hkStartLookup, (void**)&g_startLookup);
        hook("AsyncActorCreator execute (trace)", 0x0075A290, "6AFF68????????64A100000000", (void*)&hkAacExec, (void**)&g_aacExec);
        hook("avatar spawn callback (trace)", 0x00A9B3E0, "8B0D????????83B988000000", (void*)&hkAvSpawned, (void**)&g_avSpawned);
        hook("cho make (trace)", 0x014523B0, "83EC1C33C0", (void*)&hkChoMake, (void**)&g_choMake);
        hook("AvatarSpawning create (trace)", 0x00A9F080, "6AFF68????????64A100000000", (void*)&hkAvCreate, (void**)&g_avCreate);
        hook("cho has (trace)", 0x014835C0, "53568B74240C", (void*)&hkChoHas, (void**)&g_choHas);
        hook("PlaceToy callback (trace)", 0x00A8B670, "8B4424048B4004", (void*)&hkPlaceToy, (void**)&g_placeToy);
        hook("event post (trace)", 0x0068BBB0, "56578BF98B07", (void*)&hkPost, (void**)&g_post);
        hook("level loader tick (trace)", 0x00D00530, "6AFF64A100000000", (void*)&hkLevelTick, (void**)&g_levelTick);
        hook("level load-done check (trace)", 0x00CCC450, "568BF1807E2100", (void*)&hkLoadDone, (void**)&g_loadDone);
        hook("AvatarSpawning finalize (trace)", 0x00AA36F0, "64A1000000006AFF68", (void*)&hkAvFinal, (void**)&g_avFinal);
        hook("AvatarSpawning remove (trace)", 0x00A9EC70, "6AFF68????????64A100000000", (void*)&hkAvRemove, (void**)&g_avRemove);
        hook("start query (trace)", 0x0145F720, "8B44240C83EC30", (void*)&hkStartQuery, (void**)&g_startQuery);
        hook("MNode PlayerStartPosNode (trace)", 0x0148A790, "518B442410", (void*)&hkMkStart, (void**)&g_mkStart);
        hook("MNode LocatorNode (trace)", 0x01480090, "518B442410", (void*)&hkMkLocator, (void**)&g_mkLocator);
        hook("MNode SpawnExpendable (trace)", 0x017BF020, "518B442410", (void*)&hkMkSpawnExp, (void**)&g_mkSpawnExp);
        hook("header make (trace)", 0x004FAE90, "6AFF68????????64A100000000", (void*)&hkHdrMake, (void**)&g_hdrMake);
    }
    if (g_cfg.shaders10) {
        InitializeCriticalSection(&g_recCs);
        hook("shader by name", 0x0045C820, "8B44240450E8????????8B0D", (void*)&hkShaderByName, (void**)&g_shaderByName);
        if (g_cfg.lazyLink10)
            hook("shader linker (deferred)", 0x00472050, "83EC50A1????????535657", (void*)&hkLinkAll, (void**)&g_linkAll);
        hook("pass-state clone (locked)", 0x0045C5C0, "83EC4856578B7C2454", (void*)&hkCloneRec, (void**)&g_cloneRec);
    }
    if (g_cfg.missionTrace)
        hook("MissionContext update (trace)", 0x0166FFB0, "6AFF68????????64A100000000", (void*)&hkMissionTick, (void**)&g_missionTick);
    if (g_cfg.opacityLog10)
        hook("opacity mask (log)", 0x0044AD80, "83EC088B442418F30F105424", (void*)&hkOpacityMask, (void**)&g_opacityMask);
    hook("asset defs lookup", 0x00703340, "8B44240456", (void*)&hkAssetDef, (void**)&g_assetDef);
    hook("CRT __invoke_watson", 0x012CA1D1, "8BFF566A01", (void*)&hkWatson, (void**)&g_watson);
    hook("resident path normaliser", 0x0041FEF0, "8B44240C56", (void*)&hkNormPath, (void**)&g_normPath);
    sx::g_log = logf;   // also names the key ids BMX-1/BTX-1 give unpaired 1.0 key names
    if (g_cfg.sx10 && g_cfg.sxNatives10) {
        sx::g_exe = g_base;
        sx::init(g_cfg.sxPorts10);
        hook("Slang linker (1.0 natives)", 0x00736DA0, "51538B5C240C568BF18BCBE8", (void*)&sx::hkLink, (void**)&sx::g_link);
        if (g_cfg.sxPorts10 && g_cfg.sxBeh10)
            hook("Slang call by name (1.0 behavior type)", 0x00739090, "56578B7C240C8BF18B4E04", (void*)&sx::hkCallByName,
                 (void**)&sx::g_callByName);
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }

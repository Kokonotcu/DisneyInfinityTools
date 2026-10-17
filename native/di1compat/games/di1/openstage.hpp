// One AssetOpen as hkOpen's stages see it, and the stream helpers they share. core, engine/assets. Open threads.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "../../core/engine_file.hpp"
#include "../../engine/assets.hpp"
#include "config.hpp"

namespace di1::openstage {

constexpr uint32_t kOpenWrite = 1;   // AssetOpen flags bit 0: a write, never served or rewritten
constexpr char kLuaChunk = 0x1b;     // a compiled Lua chunk's first byte ("\x1bLua")
constexpr size_t kPathCap = 300;     // the copied path's buffer; longer paths are cut

// One open as the stages see it: FUN_0041cb10's arguments, the engine's own stream and the path copied.
struct Open {
    const Config* cfg;                    // the cfg keys the stages read
    const char* path;
    uint32_t flags, a3, a4;
    int* a5;
    const uint32_t* sp;                   // hkOpen's return address slot: its stack walks start there
    bool write;                           // flags & kOpenWrite
    bool prof;                            // loadprof10: the served and missing lines carry timings
    double tIn = 0, t30 = 0, tRead = 0;   // loadprof10: start, the engine's own open, the 1.0 read
    int* r = nullptr;                     // the engine's stream, null when 3.0 has no such file
    char p[kPathCap] = "";
    bool blocked = false;                 // block10 matched
    std::string missWhy = " (not served)";

    // A 3.0 file opened for reading: the stages that rewrite 3.0 files take these.
    bool edits() const { return r && !write; }
};

// A stage answers (true, out) or passes the open on. Each one's comment says what it does with r.
typedef bool (*Stage)(Open& o, int*& out);

// The engine's own open with the same arguments (a stage that consumed r answers this).
inline int* reopen(const Open& o) { return engine::assets::open(o.path, o.flags, o.a3, o.a4, o.a5); }

// r read whole as read30 reads it: no Read when empty, the full size kept on a short read.
inline bool readKeep(int* r, std::string& t)
{
    typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
    typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
    void** vt = *(void***)r;
    t.resize(((SizeFn)vt[core::eng::kFileSize])(r, nullptr));
    uint32_t got = t.empty() ? 0 : ((ReadFn)vt[core::eng::kFileRead])(r, nullptr, &t[0], (uint32_t)t.size());
    return got == t.size();
}

inline void destroyStream(int* r)
{
    typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
    ((DtorFn)(*(void***)r)[core::eng::kFileDestroy])(r, nullptr, core::eng::kDestroyFree);
}

}  // namespace di1::openstage

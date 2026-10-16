// Serving bytes to the engine: a Genie::MemFile stream built from memory, and an engine stream read whole.
// core/hooks.hpp (rt). Any thread that opens files; the engine functions it calls are not prologue-checked (H4).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include "hooks.hpp"

namespace core {

namespace eng {
constexpr uint32_t kEngineNew = 0x00406E80;     // FUN_00406e80: the engine's operator new (cdecl)
constexpr uint32_t kMemFileCtor = 0x012EC100;   // FUN_012ec100: Genie::MemFile(grow) (thiscall)
constexpr size_t kMemFileBytes = 0x20;          // a Genie::MemFile's size, as the engine allocates it
constexpr uint32_t kMemFileGrow = 0x400;        // the grow step the engine's reader path passes
constexpr int kMemFileData = 6;                 // MemFile word 6 (+0x18): its buffer
// Genie::File vtable slots
constexpr int kFileSeek = 2;      // +0x08 (off, hi, whence)
constexpr int kFileSetSize = 3;   // +0x0c (size, force)
constexpr int kFileSize = 4;      // +0x10
constexpr int kFileRead = 5;      // +0x14 (dst, n) -> bytes read
constexpr int kFileDestroy = 11;  // +0x2c (flags)
constexpr uint32_t kDestroyFree = 1;   // kFileDestroy's flag: also free the object
}  // namespace eng

// A Genie::MemFile holding a copy of data, built the way the engine builds one (0x0041d... reader path):
// new(0x20), ctor FUN_012ec100(0x400), vtable+0x0c setSize(n, 0), fill data (+0x18), vtable+0x08 seek(0, 0, 0).
inline int* memFile(const void* data, size_t n)
{
    typedef void*(__cdecl * NewFn)(size_t);
    typedef void(__fastcall * CtorFn)(void* self, void* edx, uint32_t grow);
    typedef void(__fastcall * SetSizeFn)(void* self, void* edx, uint32_t size, uint32_t force);
    typedef uint32_t(__fastcall * SeekFn)(void* self, void* edx, uint32_t off, uint32_t hi, uint32_t whence);
    void* f = ((NewFn)rt(eng::kEngineNew))(eng::kMemFileBytes);
    if (!f) return nullptr;
    ((CtorFn)rt(eng::kMemFileCtor))(f, nullptr, eng::kMemFileGrow);
    void** vt = *(void***)f;
    ((SetSizeFn)vt[eng::kFileSetSize])(f, nullptr, (uint32_t)n, 0);
    memcpy(((void**)f)[eng::kMemFileData], data, n);
    ((SeekFn)vt[eng::kFileSeek])(f, nullptr, 0, 0, 0);
    return (int*)f;
}

// Reads a whole engine stream through the Genie::File interface (+0x10 size, +0x14 read, +0x2c destroy); the
// stream is destroyed. False if the read came up short.
inline bool readStream(int* s, std::string& buf)
{
    typedef uint32_t(__fastcall * SizeFn)(void* self, void* edx);
    typedef uint32_t(__fastcall * ReadFn)(void* self, void* edx, void* dst, uint32_t n);
    typedef void(__fastcall * DtorFn)(void* self, void* edx, uint32_t flags);
    void** vt = *(void***)s;
    uint32_t n = ((SizeFn)vt[eng::kFileSize])(s, nullptr);
    buf.assign(n, '\0');
    uint32_t got = ((ReadFn)vt[eng::kFileRead])(s, nullptr, &buf[0], n);
    ((DtorFn)vt[eng::kFileDestroy])(s, nullptr, eng::kDestroyFree);
    buf.resize(got);
    return got == n;
}

// ... and returns a MemFile copy of it.
inline int* copyToMemFile(int* s, size_t* outSize)
{
    std::string buf;
    bool ok = readStream(s, buf);
    *outSize = buf.size();
    return ok ? memFile(buf.data(), buf.size()) : nullptr;
}

}  // namespace core

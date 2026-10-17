// An engine CString made and released by the engine's own functions (XF-03). core/hooks (rt). Any engine thread.
#pragma once
#include <cstdint>
#include "hooks.hpp"

namespace core {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kCStringCtor = 0x00418310;     // FUN_00418310: thiscall (const char*) on a CString
constexpr uint32_t kCStringRelease = 0x00418230;  // FUN_00418230: thiscall on a CString
}  // namespace eng

// For engine calls that add-ref their argument's header (path - 0xc, FUN_00caa0d0): a plain char* would be written.
class EngineString {
public:
    explicit EngineString(const char* s)
    {
        typedef void*(__fastcall * CtorFn)(void* str, void* edx, const char* s);
        ((CtorFn)rt(eng::kCStringCtor))(&p, nullptr, s);
    }
    ~EngineString()
    {
        typedef void(__fastcall * ReleaseFn)(void* str, void* edx);
        ((ReleaseFn)rt(eng::kCStringRelease))(&p, nullptr);
    }
    EngineString(const EngineString&) = delete;
    EngineString& operator=(const EngineString&) = delete;

    char* p = nullptr;   // the engine's text; valid while this lives
};

}  // namespace core

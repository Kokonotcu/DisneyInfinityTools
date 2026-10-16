// A hook as data, and the installer type for all-or-none feature hooks (CLAUDE.md H2). Standard library only.
#pragma once
#include <cstdint>
#include <vector>

namespace core {

struct HookSpec {
    const char* name;          // stable: it appears in the log
    uint32_t staticVa;         // address in the exe at image base 0x400000
    const char* prologueHex;   // the bytes the target must start with; "??" skips one byte
    void* detour;
    void** original;
};

// Installs every hook of the feature, or none of them. Returns whether the feature is on.
typedef bool (*InstallFeatureFn)(const char* feature, const std::vector<HookSpec>& hooks);

}  // namespace core

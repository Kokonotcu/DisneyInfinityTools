// tools10 (TL-1): in 1.0 zones a tool 3.0 refuses is judged by its 1.0 level list. toolset, vfs. Main thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../core/zip.hpp"
#include "../../engine/zones.hpp"
#include "../../third_party/miniz/miniz.h"
#include "config.hpp"
#include "playsets.hpp"
#include "toolset.hpp"
#include "vfs.hpp"

namespace di1::tools {

// Installs the tool level gate hook when c has tools10 (H1); false when it was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// H4 open: called without a prologue check (X-12)
constexpr uint32_t kToolSetTable = 0x0228AA3C;   // *: the ToolSetTable
constexpr uint32_t kToolSetFind = 0x015F6A60;    // FUN_015f6a60: thiscall (section, name) -> the record
constexpr size_t kRecordLevels = 4;              // the gate's list: record + 4, as FUN_0151b960 passes it
constexpr size_t kVerdict = 0x74;                // the tool's cached gate result, -1 unset (FUN_01520e00 resets it)
}  // namespace eng

typedef uint32_t(__fastcall* ToolGateFn)(uint8_t* tool, void* edx, const uint8_t* prefixes);
typedef uint8_t*(__fastcall* ToolSetFindFn)(void* table, void* edx, const char* section, const char* name);
using Levels = std::map<std::string, std::vector<std::string>>;

struct State {
    ToolGateFn g_toolGate = nullptr;   // set once by hook() in install()
    std::once_flag levelsRead;         // levels are read on the first refused gate: the read logs, on the game thread
    Levels levels;                     // written once under levelsRead, read-only after
};
inline State g_state;

// The 1.0 tool records, from the 1.0 install (startup5.zip, mounted at assets\ in 1.0).
inline Levels readLevels()
{
    Levels m;
    vfs::withArchive("startup5.zip", [&m](mz_zip_archive* z) {
        size_t n = 0;
        void* d = z ? core::extractZipMember(z, "var/toolset/toolset.bin", &n) : nullptr;
        if (d) {
            m = toolset::toolLevels10((const uint8_t*)d, n);
            mz_free(d);
        }
        core::logf("tools10: %u 1.0 tool records from startup5.zip:var/toolset/toolset.bin", (unsigned)m.size());
    });
    return m;
}

inline const Levels& levels10()
{
    State& s = g_state;
    std::call_once(s.levelsRead, [&s] { s.levels = readLevels(); });
    return s.levels;
}

// FUN_0150f7d0, thiscall (prefixes; ret 4), main thread, per uncached tool per refresh. 3.0 dropped in_ from the lists.
inline uint32_t __fastcall hkToolGate(uint8_t* tool, void* edx, const uint8_t* prefixes)
{
    uint32_t r = g_state.g_toolGate(tool, edx, prefixes);
    if (r || !playsets::currentZone10()) return r;
    void* zm = engine::zones::manager();
    if (!zm) return r;   // GP-14: currentZone10 found it a moment ago
    const char* zone = engine::zones::zoneName(zm, engine::zones::currentZone(zm));
    void* table = *(void**)core::rt(eng::kToolSetTable);
    for (auto& kv : levels10()) {
        if (kv.second.empty() || !toolset::levelAllows(kv.second, zone)) continue;
        const uint8_t* rec = ((ToolSetFindFn)core::rt(eng::kToolSetFind))(table, nullptr, "Tools", kv.first.c_str());
        if (!rec || rec + eng::kRecordLevels != prefixes) continue;
        *(uint32_t*)(tool + eng::kVerdict) = 1;
        core::logf("tools10: %s allowed in %s by its 1.0 level list", kv.first.c_str(), zone ? zone : "?");
        return 1;
    }
    return r;
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.tools10) return true;
    return core::hook("tool level gate (1.0 lists)", 0x0150F7D0, "538BD9837B74FF", (void*)&detail::hkToolGate,
                      (void**)&detail::g_state.g_toolGate);
}

}  // namespace di1::tools

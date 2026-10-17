// sxnatives10: 1.0 Slang scripts' imports bound to 1.0-semantics natives by a linker hook. core, sx. Boot thread.
#pragma once
#include <vector>
#include "../../core/hooks.hpp"
#include "../../core/log.hpp"
#include "../../sx.hpp"
#include "config.hpp"

namespace di1::sxnatives {

// Sets sx's log in every config (BMX-1 and BTX-1 log unpaired key ids through it); hooks the linker (H1).
inline bool install(const Config& c)
{
    sx::g_log = core::logf;
    if (!c.sx10 || !c.sxNatives10) return true;
    sx::g_exe = core::g_base;
    sx::init(c.sxPorts10);
    std::vector<core::HookSpec> natives = {
        {"Slang linker (1.0 natives)", 0x00736DA0, "51538B5C240C568BF18BCBE8", (void*)&sx::hkLink,
         (void**)&sx::g_link}};
    if (c.sxPorts10 && c.sxBeh10)
        natives.push_back({"Slang call by name (1.0 behavior type)", 0x00739090, "56578B7C240C8BF18B4E04",
                           (void*)&sx::hkCallByName, (void**)&sx::g_callByName});
    return core::installFeature("sxnatives10", natives);
}

}  // namespace di1::sxnatives

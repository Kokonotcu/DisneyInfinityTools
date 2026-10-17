// The folder of this DLL, as the loader reports it. Windows only. Any thread.
#pragma once
#include <windows.h>
#include <cstring>
#include <string>

namespace core {

// The folder of the DLL this code is linked into (the plugin's: its cfg and tables).
inline std::string pluginDir()
{
    char dir[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&pluginDir, &self);
    GetModuleFileNameA(self, dir, MAX_PATH);
    if (char* s = strrchr(dir, '\\')) *s = 0;
    return dir;
}

}  // namespace core

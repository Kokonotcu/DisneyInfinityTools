// D3DCompile for octtest's shader modes: the compiler from d3dcompiler_47.dll, ID3DBlob reads. Main thread.
#pragma once
#include <windows.h>
#include <string>

namespace octtest {

typedef HRESULT(WINAPI* CompileFn)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, void**,
                                   void**);

// ID3DBlob's vtable (d3dcommon.h): IUnknown's Release, then GetBufferPointer and GetBufferSize.
inline constexpr int kBlobRelease = 2, kBlobPointer = 3, kBlobSize = 4;
inline constexpr size_t kCachedMin = 32;   // a cache file this short is no compiled shader

// D3DCompile, or null without d3dcompiler_47.dll.
inline CompileFn d3dCompiler()
{
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    return dc ? (CompileFn)GetProcAddress(dc, "D3DCompile") : nullptr;
}

inline const char* blobPointer(void* b)
{
    void** vt = *(void***)b;
    return (const char*)((void*(__stdcall*)(void*))vt[kBlobPointer])(b);
}
inline SIZE_T blobSize(void* b)
{
    void** vt = *(void***)b;
    return ((SIZE_T(__stdcall*)(void*))vt[kBlobSize])(b);
}
inline void releaseBlob(void* b)
{
    void** vt = *(void***)b;
    ((ULONG(__stdcall*)(void*))vt[kBlobRelease])(b);
}

}  // namespace octtest

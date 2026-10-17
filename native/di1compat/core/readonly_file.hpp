// A file opened for positional reads (no shared file pointer), for archive readers. Windows only; one owner.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace core {

class ReadOnlyFile {
public:
    explicit ReadOnlyFile(const std::string& path)
        : h_(CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr))
    {
    }
    ~ReadOnlyFile()
    {
        if (open()) CloseHandle(h_);
    }
    ReadOnlyFile(const ReadOnlyFile&) = delete;
    ReadOnlyFile& operator=(const ReadOnlyFile&) = delete;

    bool open() const { return h_ != INVALID_HANDLE_VALUE; }
    // The size in bytes. GetFileSizeEx's result is not checked (CO-24, a behaviour change left open).
    uint64_t size() const
    {
        LARGE_INTEGER sz;
        GetFileSizeEx(h_, &sz);
        return (uint64_t)sz.QuadPart;
    }
    // n bytes at offset at into dst; false unless all n were read.
    bool readAt(uint64_t at, void* dst, size_t n) const
    {
        OVERLAPPED o{};
        o.Offset = (DWORD)at;
        o.OffsetHigh = (DWORD)(at >> 32);
        DWORD got = 0;
        return ReadFile(h_, dst, (DWORD)n, &got, &o) && got == n;
    }

private:
    HANDLE h_;   // INVALID_HANDLE_VALUE when the file did not open
};

}  // namespace core

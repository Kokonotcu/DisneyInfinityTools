// Zip archives read in place (miniz). One cursor per archive: callers serialise each archive's use. Windows, miniz.
#pragma once
#include <windows.h>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include "../third_party/miniz/miniz.h"

namespace core {

// Absent is a search's normal miss; Transient may pass on a later try; Broken will not.
enum class ZipOpenFailure { None, Absent, Transient, Broken };

struct ZipOpen {
    std::unique_ptr<mz_zip_archive> zip;   // null unless failure is None
    ZipOpenFailure failure = ZipOpenFailure::None;
    std::string why;                       // a Transient or Broken failure's reason
};

// The zip at path, or why it did not open.
inline ZipOpen openZip(const std::string& path);

// A member's bytes on the heap (mz_free them) and size; null if missing or not inflating. Names ignore case.
inline void* extractZipMember(mz_zip_archive* z, const char* member, size_t* size)
{
    int i = mz_zip_reader_locate_file(z, member, nullptr, 0);
    return i < 0 ? nullptr : mz_zip_reader_extract_to_heap(z, (mz_uint)i, size, 0);
}

namespace detail {

// Windows errors that can pass: another process holds the file, or handles or memory ran out.
inline bool transientWinError(unsigned long err)
{
    switch (err) {
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_TOO_MANY_OPEN_FILES:
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return true;
    default:
        return false;
    }
}

// A failed mz_zip_reader_init_file; errno and _doserrno were cleared before it.
inline ZipOpen initFailure(mz_zip_archive* z)
{
    const int err = errno;
    const unsigned long win = _doserrno;
    const mz_zip_error e = mz_zip_get_last_error(z);
    ZipOpen r;
    r.why = mz_zip_get_error_string(e);
    if (e == MZ_ZIP_ALLOC_FAILED) {
        r.failure = ZipOpenFailure::Transient;
        return r;
    }
    if (e != MZ_ZIP_FILE_OPEN_FAILED) {
        r.failure = ZipOpenFailure::Broken;
        return r;
    }
    r.why += " (errno " + std::to_string(err) + ", Windows error " + std::to_string(win) + ")";
    const bool outOfResources = err == EMFILE || err == ENFILE || err == ENOMEM;
    r.failure = outOfResources || transientWinError(win) ? ZipOpenFailure::Transient : ZipOpenFailure::Broken;
    return r;
}

}  // namespace detail

inline ZipOpen openZip(const std::string& path)
{
    ZipOpen r;
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const unsigned long win = GetLastError();
        const bool absent = win == ERROR_FILE_NOT_FOUND || win == ERROR_PATH_NOT_FOUND || win == ERROR_INVALID_NAME;
        if (absent) {
            r.failure = ZipOpenFailure::Absent;
            return r;
        }
        r.failure = detail::transientWinError(win) ? ZipOpenFailure::Transient : ZipOpenFailure::Broken;
        r.why = "file attributes unreadable (Windows error " + std::to_string(win) + ")";
        return r;
    }
    auto z = std::make_unique<mz_zip_archive>();
    errno = 0;
    _doserrno = 0;
    if (!mz_zip_reader_init_file(z.get(), path.c_str(), 0)) return detail::initFailure(z.get());
    r.zip = std::move(z);
    return r;
}

}  // namespace core

// Zip archives read in place (miniz): open one, find and inflate a member. miniz reads an archive through one
// cursor, so callers serialise every use of one archive. Windows and miniz only.
#pragma once
#include <windows.h>
#include <cstddef>
#include <memory>
#include <string>
#include "../third_party/miniz/miniz.h"

namespace core {

// The zip at path, or null when the file is absent or not a zip.
inline std::unique_ptr<mz_zip_archive> openZip(const std::string& path)
{
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    auto z = std::make_unique<mz_zip_archive>();
    if (!mz_zip_reader_init_file(z.get(), path.c_str(), 0)) return nullptr;
    return z;
}

// A member's bytes on the heap (mz_free them) and their size, or null when there is no such member or it does not
// inflate. Member names match case-insensitively.
inline void* extractZipMember(mz_zip_archive* z, const char* member, size_t* size)
{
    int i = mz_zip_reader_locate_file(z, member, nullptr, 0);
    return i < 0 ? nullptr : mz_zip_reader_extract_to_heap(z, (mz_uint)i, size, 0);
}

}  // namespace core

// Octane tuple checks octtest repeats: parse from bytes, the magic test, a name test, blob data. Main thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "../formats/oct.hpp"

namespace octtest {

inline constexpr size_t kMagicBytes = sizeof(oct::MAGIC);     // a shorter file is no tuple
inline constexpr size_t kNameIndexBytes = sizeof(uint16_t);   // a name index or key (FORMATS.md 3)

inline oct::File parse(const std::string& b) { return oct::File::parse(b.data(), b.size()); }

// True for an Octane tuple: other formats share its extensions.
inline bool isTuple(const std::string& b)
{
    return b.size() >= kMagicBytes && oct::u((const uint8_t*)b.data(), (int)kMagicBytes) == oct::MAGIC;
}

// True when b holds name with its terminating NUL: a cheap test before parsing.
inline bool hasName(const std::string& b, const char* name)
{
    return b.find(name, 0, strlen(name) + 1) != std::string::npos;
}

// A blob record's bytes after its element count (ClipDataBlock, Data).
inline std::string blobData(const oct::Rec& r) { return r.pay.substr(oct::spec(r.type).cw); }

}  // namespace octtest

// Overflow-safe bounds checks for file data. 32-bit size_t wraps, so off + n <= size can pass wrongly.
#pragma once
#include <cstddef>

namespace bytes {

// True when [off, off + n) lies inside a buffer of size bytes. Cannot overflow.
inline bool fits(size_t size, size_t off, size_t n) { return n <= size && off <= size - n; }

}  // namespace bytes

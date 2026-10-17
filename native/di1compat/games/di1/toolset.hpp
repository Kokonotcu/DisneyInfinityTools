// 1.0 tool level lists from ToolSet.bin (plan TL-1), and 3.0's level-list test. base/bytes, formats/oct. Any thread.
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "../../base/bytes.hpp"
#include "../../formats/oct.hpp"

namespace di1::toolset {

// Each 1.0 tool's level prefixes (toolgate.py: 177 records, 78 with a list); damaged layouts stop the read early.
inline std::map<std::string, std::vector<std::string>> toolLevels10(const uint8_t* d, size_t n);
// 3.0's test (FUN_0150f7d0): an empty list allows every level, else the lower-cased zone starts with one.
inline bool levelAllows(const std::vector<std::string>& prefixes, const char* zone);

namespace detail {

// ToolSet.bin as 1.0's ToolSetInfo (FUN_00f768f0) reads it: sections of [u32 size] + data.
constexpr size_t kSizeWord = 4;
constexpr size_t kMinSection = 8;   // the section walk stops where fewer bytes remain
constexpr size_t kPoolSection = 0, kListSection = 1, kToolSection = 11;   // strings, -1-ended lists, Tools
constexpr size_t kToolsHead = 8;    // the Tools section's size and count
constexpr size_t kToolBytes = 76;   // a Tools record: +0 name offset, +0x10 LevelPrefix list index (-1: none)
constexpr size_t kToolList = 0x10;

// The pool string at off, "" when off is outside the pool.
inline std::string poolString(const char* pool, size_t poolN, int32_t off)
{
    if (off < 0 || (size_t)off >= poolN) return std::string();
    return std::string(pool + off, strnlen(pool + off, poolN - off));
}

}  // namespace detail

inline std::map<std::string, std::vector<std::string>> toolLevels10(const uint8_t* d, size_t n)
{
    namespace k = detail;
    std::map<std::string, std::vector<std::string>> out;
    std::vector<std::pair<size_t, size_t>> secs;   // offset, size
    for (size_t o = 0; bytes::fits(n, o, k::kMinSection);) {
        uint32_t sz = oct::u(d + o, 4);
        if (sz < k::kSizeWord || !bytes::fits(n, o, sz)) return out;
        secs.push_back({o, sz});
        o += sz;
    }
    if (secs.size() <= k::kToolSection) return out;
    const auto& poolSec = secs[k::kPoolSection];
    const auto& listSec = secs[k::kListSection];
    const char* pool = (const char*)d + poolSec.first + k::kSizeWord;
    const size_t poolN = poolSec.second - k::kSizeWord, lists = listSec.first + k::kSizeWord;
    const size_t listWords = (listSec.second - k::kSizeWord) / 4;   // u32 words in the list section
    const size_t so = secs[k::kToolSection].first, sz = secs[k::kToolSection].second;
    const size_t cnt = oct::u(d + so + k::kSizeWord, 4);
    const size_t body = sz - k::kToolsHead;   // checked before use: cnt * kToolBytes cannot wrap
    if (sz < k::kToolsHead || body % k::kToolBytes || cnt != body / k::kToolBytes) return out;
    for (size_t r = 0; r < cnt; ++r) {
        const uint8_t* rec = d + so + k::kToolsHead + r * k::kToolBytes;
        std::vector<std::string>& lv = out[k::poolString(pool, poolN, (int32_t)oct::u(rec, 4))];
        for (int32_t i = (int32_t)oct::u(rec + k::kToolList, 4); i >= 0 && bytes::fits(listWords, (size_t)i, 1); ++i) {
            int32_t w = (int32_t)oct::u(d + lists + 4 * i, 4);
            if (w == -1) break;
            lv.push_back(k::poolString(pool, poolN, w));
        }
    }
    return out;
}

inline bool levelAllows(const std::vector<std::string>& prefixes, const char* zone)
{
    if (prefixes.empty()) return true;
    std::string z = zone ? zone : "";
    for (char& c : z) c = (char)tolower((unsigned char)c);
    for (const std::string& p : prefixes)
        if (!p.empty() && z.compare(0, p.size(), p) == 0) return true;   // CString Find == 0
    return false;
}

}  // namespace di1::toolset

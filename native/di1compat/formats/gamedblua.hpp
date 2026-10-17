// Generated GameDB tables as Lua text: scanning, and keeping only some records verbatim. std only. Any thread.
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

namespace gamedblua {

inline constexpr size_t npos = std::string::npos;

// t[i] is a quote: the index after its closing quote, backslash escapes skipped; npos if unterminated.
inline size_t skipString(const std::string& t, size_t i)
{
    char q = t[i++];
    while (i < t.size() && t[i] != q) i += t[i] == '\\' ? 2 : 1;
    return i < t.size() ? i + 1 : npos;
}

namespace detail {
// "--" up to the line end; --[[ ]] long comments and [[ ]] strings are not recognised (SY-11).
inline bool comment(const std::string& t, size_t i, size_t to) { return t[i] == '-' && i + 1 < to && t[i + 1] == '-'; }
inline bool quote(char c) { return c == '\'' || c == '"'; }
inline bool blank(char c) { return c == ' ' || c == '\t'; }
inline bool space(char c) { return blank(c) || c == '\r' || c == '\n'; }
inline bool identChar(char c) { return isalnum((unsigned char)c) || c == '_'; }
}  // namespace detail

// t[i] == '{': the index of its matching '}', strings and comments skipped (stateData strings hold any byte).
inline size_t matchBrace(const std::string& t, size_t i)
{
    int depth = 0;
    while (i < t.size()) {
        char c = t[i];
        if (detail::quote(c)) {
            i = skipString(t, i);
            if (i == npos) return npos;
            continue;
        }
        if (detail::comment(t, i, t.size())) {
            i = t.find('\n', i);
            if (i == npos) return npos;
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) return i;
        }
        ++i;
    }
    return npos;
}

// The first `key =` in t[from, to) at brace depth 0 from `from`: the index after '=', npos if absent.
inline size_t findKey(const std::string& t, const char* key, size_t from = 0, size_t to = npos)
{
    if (to > t.size()) to = t.size();
    size_t n = strlen(key);
    int depth = 0;
    for (size_t i = from; i < to;) {
        char c = t[i];
        if (detail::quote(c)) {
            i = skipString(t, i);
            if (i == npos) return npos;
            continue;
        }
        if (detail::comment(t, i, to)) {
            i = t.find('\n', i);
            if (i == npos) return npos;
            continue;
        }
        const bool wordStart = i == 0 || !detail::identChar(t[i - 1]);
        if (c == '{') ++depth;
        else if (c == '}') --depth;
        else if (depth == 0 && !t.compare(i, n, key) && wordStart) {
            size_t j = i + n;
            while (j < to && detail::space(t[j])) ++j;
            if (j < to && t[j] == '=') return j + 1;
        }
        ++i;
    }
    return npos;
}

namespace detail {
// The record t[i, e] with a UID keep() accepts, appended verbatim to out; a record without a UID is dropped.
inline bool keepRecord(const std::string& t, size_t i, size_t e, const std::function<bool(uint32_t)>& keep,
                       std::string& out)
{
    size_t u = findKey(t, "UID", i + 1, e);
    if (u == npos) return false;
    while (u < e && blank(t[u])) ++u;
    uint32_t uid = (uint32_t)strtoul(t.c_str() + u, nullptr, 10);
    if (!keep(uid)) return false;
    out += "\t\t";
    out.append(t, i, e + 1 - i);
    out += ",\n";
    return true;
}
}  // namespace detail

// keep()'s records of a customization_csv2game.lua table (Data = {.., Records = {{UID = n, ..}}}), verbatim.
// False: no such layout.
inline bool keepRecords(const std::string& t, const std::function<bool(uint32_t)>& keep, std::string& out, int& kept,
                        int& total)
{
    kept = total = 0;
    size_t data = findKey(t, "Data");
    if (data == npos) return false;
    size_t dOpen = t.find('{', data), dClose = dOpen == npos ? npos : matchBrace(t, dOpen);
    if (dClose == npos) return false;
    size_t rec = findKey(t, "Records", dOpen + 1, dClose);
    if (rec == npos) return false;
    size_t open = t.find('{', rec), close = open == npos ? npos : matchBrace(t, open);
    if (close == npos || close > dClose) return false;
    out.assign(t, 0, open + 1);
    out += "\n";
    for (size_t i = open + 1; i < close;) {
        if (t[i] == '{') {
            size_t e = matchBrace(t, i);
            if (e == npos || e > close) return false;
            ++total;
            kept += detail::keepRecord(t, i, e, keep, out) ? 1 : 0;
            i = e + 1;
        } else if (detail::comment(t, i, close)) {
            i = t.find('\n', i);
            if (i == npos) return false;
        } else {
            ++i;
        }
    }
    out += "\t";
    out.append(t, close, npos);
    return true;
}

}  // namespace gamedblua

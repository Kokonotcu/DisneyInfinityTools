// Lua source text: tokens outside strings and comments, assignments, tables, renames. std only; any thread.
#pragma once
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lualex {

// A token: an identifier (kind 'i', with its brace depth) or a brace ('{', '}').
struct Tok {
    size_t at, len;
    char kind;
    int depth;
};

// The tokens of s. Leveled long brackets ([==[, --[==[) are read as plain text (GP-28).
inline std::vector<Tok> tokens(const std::string& s);

// Whether 'name =' follows: the next non-space after `after` is '=' and not '=='.
inline bool assignedAt(const std::string& s, size_t after)
{
    size_t j = s.find_first_not_of(" \t\r\n", after);
    if (j == std::string::npos || s[j] != '=') return false;
    return j + 1 >= s.size() || s[j + 1] != '=';
}

namespace detail {

inline bool identChar(char c) { return isalnum((unsigned char)c) || c == '_'; }
inline bool numberChar(char c) { return isalnum((unsigned char)c) || c == '.'; }
inline bool spaceOrTab(char c) { return c == ' ' || c == '\t'; }

// After the "]]" that closes a long string or comment, or s.size().
inline size_t longEnd(const std::string& s, size_t from)
{
    size_t e = s.find("]]", from);
    return e == std::string::npos ? s.size() : e + 2;
}

// i at "--": where the comment ends (a line comment at its '\n').
inline size_t commentEnd(const std::string& s, size_t i)
{
    if (i + 3 < s.size() && s[i + 2] == '[' && s[i + 3] == '[') return longEnd(s, i + 4);
    size_t e = s.find('\n', i);
    return e == std::string::npos ? s.size() : e;
}

// i at a quote: one past the closing quote (escapes skipped); past s.size() when unclosed.
inline size_t quotedEnd(const std::string& s, size_t i)
{
    const char q = s[i];
    size_t j = i + 1;
    while (j < s.size() && s[j] != q) j += s[j] == '\\' ? 2 : 1;
    return j + 1;
}

inline size_t identEnd(const std::string& s, size_t i)
{
    while (i < s.size() && identChar(s[i])) ++i;
    return i;
}

inline size_t numberEnd(const std::string& s, size_t i)
{
    while (i < s.size() && numberChar(s[i])) ++i;
    return i;
}

// p starts a top-level entry line: one tab, then neither a tab nor a brace.
inline bool entryLine(const std::string& t, size_t p)
{
    if (t[p] != '\t' || p + 1 >= t.size()) return false;
    const char c = t[p + 1];
    return c != '\t' && c != '{' && c != '}';
}

}  // namespace detail

inline std::vector<Tok> tokens(const std::string& s)
{
    std::vector<Tok> out;
    int depth = 0;
    for (size_t i = 0; i < s.size();) {
        const char c = s[i];
        const bool comment = c == '-' && i + 1 < s.size() && s[i + 1] == '-';
        const bool longString = c == '[' && i + 1 < s.size() && s[i + 1] == '[';
        if (comment) {
            i = detail::commentEnd(s, i);
            continue;
        }
        if (c == '"' || c == '\'') {
            i = detail::quotedEnd(s, i);
            continue;
        }
        if (longString) {
            i = detail::longEnd(s, i + 2);
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            const size_t j = detail::identEnd(s, i);
            out.push_back({i, j - i, 'i', depth});
            i = j;
            continue;
        }
        if (isdigit((unsigned char)c)) {
            i = detail::numberEnd(s, i);
            continue;
        }
        if (c == '{') out.push_back({i, 1, '{', depth++});
        else if (c == '}') out.push_back({i, 1, '}', --depth});
        ++i;
    }
    return out;
}

// Renames table keys and globals ('Old =' outside strings and comments). -> the number renamed.
inline int renameKeys(std::string& s, const std::vector<std::pair<std::string, std::string>>& map)
{
    auto toks = tokens(s);
    int n = 0;
    for (size_t k = toks.size(); k-- > 0;) {
        const Tok& t = toks[k];
        if (t.kind != 'i' || !assignedAt(s, t.at + t.len)) continue;
        for (auto& m : map) {
            if (t.len != m.first.size() || s.compare(t.at, t.len, m.first)) continue;
            s.replace(t.at, t.len, m.second);
            ++n;
            break;
        }
    }
    return n;
}

// Replaces whole quoted string values "Old" -> "New". -> the count.
inline int renameStrings(std::string& s, const std::vector<std::pair<std::string, std::string>>& map)
{
    int n = 0;
    for (auto& m : map) {
        std::string a = "\"" + m.first + "\"", b = "\"" + m.second + "\"";
        for (size_t p = s.find(a); p != std::string::npos; p = s.find(a, p + b.size())) {
            s.replace(p, a.size(), b);
            ++n;
        }
    }
    return n;
}

// The table assigned to global `name`: [open brace, one past its close); {npos, npos} if absent.
inline std::pair<size_t, size_t> globalTable(const std::string& s, const std::vector<Tok>& toks,
                                             const std::string& name)
{
    for (size_t k = 0; k + 1 < toks.size(); ++k) {
        const bool global = toks[k].kind == 'i' && toks[k].depth == 0;
        const bool named = global && !s.compare(toks[k].at, toks[k].len, name);
        if (!named || toks[k + 1].kind != '{') continue;
        for (size_t e = k + 2; e < toks.size(); ++e)
            if (toks[e].kind == '}' && toks[e].depth == 0) return {toks[k + 1].at, toks[e].at + 1};
    }
    return {std::string::npos, std::string::npos};
}

using Entries = std::vector<std::pair<std::string, std::pair<size_t, size_t>>>;

// Entries 'Key = { ... }' directly in the table opened at `open`: key -> [key, past its '}').
inline Entries tableEntries(const std::string& s, const std::vector<Tok>& toks, size_t open)
{
    Entries out;
    size_t k = 0;
    while (k < toks.size() && toks[k].at != open) ++k;
    if (k == toks.size()) return out;
    int d = toks[k].depth + 1;
    for (++k; k + 1 < toks.size(); ++k) {
        if (toks[k].kind == '}' && toks[k].depth < d) break;
        const bool key = toks[k].kind == 'i' && toks[k].depth == d;
        const bool table = key && toks[k + 1].kind == '{';
        if (!table || !assignedAt(s, toks[k].at + toks[k].len)) continue;
        for (size_t e = k + 2; e < toks.size(); ++e) {
            if (toks[e].kind != '}' || toks[e].depth != d) continue;
            out.push_back({s.substr(toks[k].at, toks[k].len), {toks[k].at, toks[e].at + 1}});
            k = e;
            break;
        }
    }
    return out;
}

// ---- tables in the shipped line layout: "name = \n{\n", entries indented one tab, "}" closes ----

struct LineEntry {
    std::string key, text;   // a top-level entry's key and its lines, verbatim
};
struct LineTable {
    std::vector<LineEntry> entries;
    size_t begin = 0, end = 0;   // the body's line range in the text
};

// Top-level table `name` of t in the line layout; nullopt if absent or unclosed.
inline std::optional<LineTable> lineTable(const std::string& t, const char* name)
{
    const std::string head = std::string(name) + " = ";
    const size_t last = t.rfind("\n" + head);
    size_t at = last == std::string::npos ? last : last + 1;   // the last one at a line start
    if (t.compare(0, head.size(), head) == 0) at = 0;           // unless the text starts with one
    if (at == std::string::npos) return std::nullopt;
    size_t o = t.find("\n{", at);
    if (o == std::string::npos) return std::nullopt;
    LineTable out;
    out.begin = t.find('\n', o + 1);
    if (out.begin == std::string::npos) return std::nullopt;
    ++out.begin;
    for (size_t p = out.begin; p < t.size();) {
        size_t nl = t.find('\n', p);
        size_t end = nl == std::string::npos ? t.size() : nl + 1;
        if (t[p] == '}') {
            out.end = p;
            return out;
        }
        if (detail::entryLine(t, p)) {
            // the key runs to the next '=', on a later line when this one has none (GP-30)
            std::string k = t.substr(p + 1, t.find('=', p) - p - 1);
            while (!k.empty() && detail::spaceOrTab(k.back())) k.pop_back();
            if (k.size() > 4 && k[0] == '[' && k[1] == '"') k = k.substr(2, k.size() - 4);
            out.entries.push_back({k, ""});
        }
        if (!out.entries.empty()) out.entries.back().text.append(t, p, end - p);
        p = end;
    }
    return std::nullopt;
}

}  // namespace lualex

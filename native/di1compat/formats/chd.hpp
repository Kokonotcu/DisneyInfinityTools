// GameDB tables: a generated gamedb .lua (Data = { ... }) compiled to .chd (FORMATS.md 10.1, chd.py). Any thread.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"

namespace chd {

struct V {
    enum T { INT, FLT, STR, BOOL, TAB } t = TAB;
    int32_t i = 0;
    float f = 0;
    std::string s;                                   // string bytes
    std::vector<std::pair<std::string, V>> named;    // a table holds named entries or items, never both
    std::vector<V> items;
};

// A Lua token and its group, as chd.py's _LUA regex numbers them.
enum Group { kString = 1, kFloat = 3, kInt = 4, kName = 5, kSymbol = 6 };
struct Tok { int g; std::string v; };

namespace detail {

inline bool digit(char c) { return c >= '0' && c <= '9'; }
inline bool word(char c) { return isalnum((unsigned char)c) || c == '_'; }
inline bool octalAt(const std::string& t, size_t j) { return j < t.size() && t[j] >= '0' && t[j] <= '7'; }

// The escape after the backslash at t[j] onto s, as Python's unicode_escape over Latin-1 bytes. -> the next index.
inline size_t unescape(const std::string& t, size_t j, std::string& s)
{
    static constexpr char kFrom[] = "ntrabfv";
    static constexpr char kTo[] = "\n\t\r\a\b\f\v";
    const size_t n = t.size();
    const char e = t[j + 1];
    j += 2;
    if (const char* k = e ? strchr(kFrom, e) : nullptr) {
        s.push_back(kTo[k - kFrom]);
        return j;
    }
    if (e == '\n') return j;   // a continued line
    if (e == 'x') {
        const bool hex = bytes::fits(n, j, 2) && isxdigit((unsigned char)t[j]) && isxdigit((unsigned char)t[j + 1]);
        if (!hex) s += "\\x";
        else s.push_back((char)strtol(t.substr(j, 2).c_str(), nullptr, 16));
        return hex ? j + 2 : j;
    }
    if (e >= '0' && e <= '7') {   // up to 3 octal digits
        int v = e - '0';
        for (int k = 0; k < 2 && octalAt(t, j); ++k) v = v * 8 + (t[j++] - '0');
        s.push_back((char)v);
        return j;
    }
    if (e != '\\' && e != '"' && e != '\'') s.push_back('\\');
    s.push_back(e);
    return j;
}

// The string literal opened by t[i]: its bytes, and the index after its closing quote.
inline std::pair<std::string, size_t> quoted(const std::string& t, size_t i)
{
    std::string s;
    size_t j = i + 1;
    while (j < t.size() && t[j] != t[i]) {
        if (t[j] != '\\' || j + 1 >= t.size()) s.push_back(t[j++]);
        else j = unescape(t, j, s);
    }
    return {s, j + 1};
}

// The number at t[i] (-?\d+\.\d* or -?\d+), or none: {group, end}; group 0 when there is no number.
inline std::pair<int, size_t> number(const std::string& t, size_t i)
{
    size_t j = i + (t[i] == '-' ? 1 : 0);
    if (j >= t.size() || !digit(t[j])) return {0, i};
    while (j < t.size() && digit(t[j])) ++j;
    if (j >= t.size() || t[j] != '.') return {kInt, j};
    ++j;
    while (j < t.size() && digit(t[j])) ++j;
    return {kFloat, j};
}
}  // namespace detail

namespace detail {
// The token at t[i] (a string, number, name or one symbol character) and the index after it.
inline std::pair<Tok, size_t> token(const std::string& t, size_t i)
{
    if (t[i] == '"' || t[i] == '\'') {
        auto [s, end] = quoted(t, i);
        return {Tok{kString, s}, end};
    }
    if (auto [g, end] = number(t, i); g) return {Tok{g, t.substr(i, end - i)}, end};
    if (!word(t[i])) return {Tok{kSymbol, std::string(1, t[i])}, i + 1};
    size_t j = i;
    while (j < t.size() && word(t[j])) ++j;
    return {Tok{kName, t.substr(i, j - i)}, j};
}
}  // namespace detail

// The tokens of t, -- comments and blanks dropped, closed by a '}' as if t were one table's body.
inline std::vector<Tok> tokens(const std::string& t)
{
    std::vector<Tok> out;
    size_t i = 0, n = t.size();
    while (i < n) {
        const bool comment = t[i] == '-' && i + 1 < n && t[i + 1] == '-';
        if (isspace((unsigned char)t[i])) ++i;
        else if (comment) i = (std::min)(t.find('\n', i), n);
        else {
            auto [tok, end] = detail::token(t, i);
            out.push_back(std::move(tok));
            i = end;
        }
    }
    out.push_back({kSymbol, "}"});
    return out;
}

inline V parse(const std::vector<Tok>& tok, size_t& i);

namespace detail {
// A repeated key keeps its first place with the last value (a Python dict).
inline void setNamed(V& tab, const std::string& key, const V& v)
{
    auto it = std::find_if(tab.named.begin(), tab.named.end(), [&](const auto& e) { return e.first == key; });
    if (it != tab.named.end()) it->second = v;
    else tab.named.emplace_back(key, v);
}

inline bool isSymbol(const std::vector<Tok>& tok, size_t k, const char* sym)
{
    return k < tok.size() && tok[k].g == kSymbol && tok[k].v == sym;
}

// The value token t (already consumed); a '{' parses the table that follows it.
inline V value(const std::vector<Tok>& tok, size_t& i, const Tok& t)
{
    V v;
    switch (t.g) {
    case kSymbol:
        if (t.v == "{") return parse(tok, i);
        break;
    case kName:
        if (t.v != "true" && t.v != "false") throw std::runtime_error("lua: bare name " + t.v);
        v.t = V::BOOL;
        v.i = t.v == "true";
        return v;
    case kString:
        v.t = V::STR;
        v.s = t.v;
        return v;
    case kFloat:
        v.t = V::FLT;
        v.f = (float)strtod(t.v.c_str(), nullptr);
        return v;
    case kInt:
        v.t = V::INT;
        v.i = (int32_t)strtoll(t.v.c_str(), nullptr, 10);
        return v;
    default:
        break;
    }
    throw std::runtime_error("lua: unexpected '" + t.v + "'");
}
}  // namespace detail

// The table whose '{' was just consumed, up to and past its '}'.
inline V parse(const std::vector<Tok>& tok, size_t& i)
{
    V tab;
    while (i < tok.size() && !detail::isSymbol(tok, i, "}")) {
        std::string key;
        bool named = false;
        if (detail::isSymbol(tok, i, "[")) {
            key = tok[i + 1].v;
            i += 4;
            named = true;
        } else if (detail::isSymbol(tok, i + 1, "=")) {
            key = tok[i].v;
            i += 2;
            named = true;
        }
        if (i >= tok.size()) throw std::runtime_error("lua: unexpected end");
        const Tok& t = tok[i++];
        V v = detail::value(tok, i, t);
        if (named) detail::setNamed(tab, key, v);
        else tab.items.push_back(v);
        if (detail::isSymbol(tok, i, ",")) ++i;
    }
    ++i;
    if (!tab.named.empty()) tab.items.clear();   // chd.lua: named or items
    return tab;
}

inline V lua(const std::string& text)
{
    std::vector<Tok> tok = tokens(text);
    size_t i = 0;
    return parse(tok, i);
}

namespace detail {

inline constexpr uint32_t kMagic = 0x0123abcd, kVersion = 0x3000, kHeaderSize = 0x20;
inline constexpr uint32_t kRootEntry = 0xD0000000u;   // root entry: an array-item table at data + 8
inline constexpr uint16_t kTableMagic = 0xfeed;
inline constexpr uint32_t kArrayKey = 0xffffff, kArrayItem = 0x80;   // an array item's key and type flag

// An entry's type code (FORMATS.md 10.1).
inline uint32_t typeCode(V::T t)
{
    switch (t) {
    case V::BOOL: return 0x40;
    case V::INT: return 0x00;
    case V::FLT: return 0x20;
    case V::STR: return 0x30;
    default: return 0x50;
    }
}

// Each name once, bytewise order, as u16 length + bytes + NUL; at gets each one's offset.
inline std::string namePool(const std::set<std::string>& names, std::map<std::string, uint32_t>& at)
{
    std::string out;
    for (const std::string& s : names) {
        at[s] = (uint32_t)out.size();
        bytes::appendLe(out, (uint16_t)s.size(), 2);
        out += s;
        out.push_back('\0');
    }
    return out;
}

// The key names (true / false for bools: the compiler interns them) and the string values of the tree.
inline void collectNames(const V& tree, std::set<std::string>& keys, std::set<std::string>& strs)
{
    std::vector<const V*> stack{&tree};
    while (!stack.empty()) {
        const V* v = stack.back();
        stack.pop_back();
        if (v->t == V::BOOL) keys.insert(v->i ? "true" : "false");
        else if (v->t == V::STR) strs.insert(v->s);
        if (v->t != V::TAB) continue;
        for (const auto& e : v->named) {
            keys.insert(e.first);
            stack.push_back(&e.second);
        }
        for (const V& x : v->items) stack.push_back(&x);
    }
}

// The data section: every table in depth-first pre-order, each u16 0xfeed, u16 n, then n 8-byte entries.
struct Compiler {
    std::map<std::string, uint32_t> keyAt, strAt;
    std::string data;

    uint32_t value(const V& x)
    {
        uint32_t val = 0;
        switch (x.t) {
        case V::FLT:
            memcpy(&val, &x.f, 4);
            return val;
        case V::STR: return strAt[x.s];
        case V::TAB: return table(x);
        default: return (uint32_t)x.i;
        }
    }
    uint32_t table(const V& v)
    {
        std::vector<std::pair<const std::string*, const V*>> es;
        for (const auto& e : v.named) es.emplace_back(&e.first, &e.second);
        std::sort(es.begin(), es.end(), [](const auto& a, const auto& b) { return *a.first < *b.first; });
        for (const V& x : v.items) es.emplace_back(nullptr, &x);
        const uint32_t at = (uint32_t)data.size();
        bytes::appendLe(data, kTableMagic, 2);
        bytes::appendLe(data, (uint16_t)es.size(), 2);
        data.append(8 * es.size(), '\0');
        for (size_t k = 0; k < es.size(); ++k) {
            const V& x = *es[k].second;
            const uint32_t t = typeCode(x.t);
            const uint32_t w = es[k].first ? (keyAt[*es[k].first] | t << 24) : (kArrayKey | (t | kArrayItem) << 24);
            const uint32_t val = value(x);   // a nested table appends after this one's entries
            bytes::setLe32(data, at + 4 + 8 * k, w);
            bytes::setLe32(data, at + 8 + 8 * k, val);
        }
        return at;
    }
};
}  // namespace detail

inline std::string compile(const V& tree)
{
    std::set<std::string> keys, strs;
    detail::collectNames(tree, keys, strs);
    detail::Compiler c;
    const std::string kt = detail::namePool(keys, c.keyAt), st = detail::namePool(strs, c.strAt);
    c.data.assign(8, '\0');
    bytes::setLe32(c.data, 0, detail::kRootEntry);
    bytes::setLe32(c.data, 4, 8);
    c.table(tree);
    const size_t strings = detail::kHeaderSize + kt.size(), pad = (4 - (strings + st.size()) % 4) % 4;
    const uint32_t h[8] = {detail::kMagic, detail::kVersion, detail::kHeaderSize, (uint32_t)kt.size(),
                           (uint32_t)strings, (uint32_t)st.size(), (uint32_t)(strings + st.size() + pad),
                           (uint32_t)c.data.size()};
    std::string head(32, '\0');
    for (size_t k = 0; k < 8; ++k) bytes::setLe32(head, 4 * k, h[k]);
    return head + kt + st + std::string(pad, '\0') + c.data;
}

}  // namespace chd

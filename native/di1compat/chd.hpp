// GameDB table compiler: a generated gamedb .lua (Data = { ... }) -> .chd, in memory. C++ port of DI3Tools chd.py
// lua() + dump() (layout documented there; that pair compiles 29 of the 30 shipped 3.0 sources byte-exact, octtest
// --chd checks this port the same way). Needed because 3.0's RumpusData loader is a DBFileLoader_Callback_CHD
// (FUN_00f63e70) that parses only CHD, and the 1.0 DataMap names rumpusdata.lua (research playset G4).
#pragma once
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace chd {

struct V {
    enum T { INT, FLT, STR, BOOL, TAB } t = TAB;
    int32_t i = 0;
    float f = 0;
    std::string s;                                   // string bytes
    std::vector<std::pair<std::string, V>> named;    // a table holds named entries or items, never both
    std::vector<V> items;
};

struct Tok { int g; std::string v; };   // g: 1 string, 3 float, 4 int, 5 name, 6 symbol (chd._LUA groups)

inline std::vector<Tok> tokens(const std::string& t)
{
    std::vector<Tok> out;
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    auto word = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
    size_t i = 0, n = t.size();
    while (i < n) {
        char c = t[i];
        if (isspace((unsigned char)c)) { ++i; continue; }
        if (c == '-' && i + 1 < n && t[i + 1] == '-') { while (i < n && t[i] != '\n') ++i; continue; }
        if (c == '"' || c == '\'') {   // Python unicode_escape over Latin-1 bytes
            std::string s;
            size_t j = i + 1;
            while (j < n && t[j] != c) {
                if (t[j] != '\\' || j + 1 >= n) { s.push_back(t[j++]); continue; }
                char e = t[j + 1];
                j += 2;
                switch (e) {
                case 'n': s.push_back('\n'); break;
                case 't': s.push_back('\t'); break;
                case 'r': s.push_back('\r'); break;
                case 'a': s.push_back('\a'); break;
                case 'b': s.push_back('\b'); break;
                case 'f': s.push_back('\f'); break;
                case 'v': s.push_back('\v'); break;
                case '\n': break;
                case 'x':
                    if (j + 2 <= n && isxdigit((unsigned char)t[j]) && isxdigit((unsigned char)t[j + 1])) {
                        s.push_back((char)strtol(t.substr(j, 2).c_str(), nullptr, 16));
                        j += 2;
                    } else s += "\\x";
                    break;
                default:
                    if (e >= '0' && e <= '7') {   // up to 3 octal digits
                        int v = e - '0', k = 0;
                        while (k < 2 && j < n && t[j] >= '0' && t[j] <= '7') { v = v * 8 + (t[j++] - '0'); ++k; }
                        s.push_back((char)v);
                    } else if (e == '\\' || e == '"' || e == '\'') s.push_back(e);
                    else { s.push_back('\\'); s.push_back(e); }
                }
            }
            out.push_back({1, s});
            i = j + 1;
            continue;
        }
        size_t j = i + (c == '-' ? 1 : 0);
        if (j < n && digit(t[j])) {   // -?\d+\.\d*  |  -?\d+
            while (j < n && digit(t[j])) ++j;
            if (j < n && t[j] == '.') {
                ++j;
                while (j < n && digit(t[j])) ++j;
                out.push_back({3, t.substr(i, j - i)});
            } else out.push_back({4, t.substr(i, j - i)});
            i = j;
            continue;
        }
        if (word(c)) {
            j = i;
            while (j < n && word(t[j])) ++j;
            out.push_back({5, t.substr(i, j - i)});
            i = j;
            continue;
        }
        out.push_back({6, std::string(1, c)});
        ++i;
    }
    out.push_back({6, "}"});
    return out;
}

inline V parse(const std::vector<Tok>& tok, size_t& i)
{
    auto is = [&](size_t k, const char* sym) { return k < tok.size() && tok[k].g == 6 && tok[k].v == sym; };
    V tab;
    while (i < tok.size() && !is(i, "}")) {
        std::string key;
        bool named = false;
        if (is(i, "[")) { key = tok[i + 1].v; i += 4; named = true; }
        else if (is(i + 1, "=")) { key = tok[i].v; i += 2; named = true; }
        if (i >= tok.size()) throw std::runtime_error("lua: unexpected end");
        const Tok& t = tok[i++];
        V v;
        if (t.g == 6 && t.v == "{") v = parse(tok, i);
        else if (t.g == 5) {
            if (t.v != "true" && t.v != "false") throw std::runtime_error("lua: bare name " + t.v);
            v.t = V::BOOL; v.i = t.v == "true";
        } else if (t.g == 1) { v.t = V::STR; v.s = t.v; }
        else if (t.g == 3) { v.t = V::FLT; v.f = (float)strtod(t.v.c_str(), nullptr); }
        else if (t.g == 4) { v.t = V::INT; v.i = (int32_t)strtoll(t.v.c_str(), nullptr, 10); }
        else throw std::runtime_error("lua: unexpected '" + t.v + "'");
        if (named) {   // a repeated key keeps its first place with the last value (Python dict)
            auto it = std::find_if(tab.named.begin(), tab.named.end(), [&](const auto& e) { return e.first == key; });
            if (it != tab.named.end()) it->second = v;
            else tab.named.emplace_back(key, v);
        } else tab.items.push_back(v);
        if (is(i, ",")) ++i;
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

inline std::string compile(const V& tree)
{
    std::set<std::string> keys, strs;
    std::vector<const V*> stack{&tree};
    while (!stack.empty()) {
        const V* v = stack.back();
        stack.pop_back();
        if (v->t == V::TAB) {
            for (auto& e : v->named) { keys.insert(e.first); stack.push_back(&e.second); }
            for (auto& x : v->items) stack.push_back(&x);
        } else if (v->t == V::BOOL) keys.insert(v->i ? "true" : "false");
        else if (v->t == V::STR) strs.insert(v->s);
    }
    auto pool = [](const std::set<std::string>& names, std::map<std::string, uint32_t>& at) {   // bytewise order
        std::string out;
        for (auto& s : names) {
            at[s] = (uint32_t)out.size();
            uint16_t n = (uint16_t)s.size();
            out.append((const char*)&n, 2);
            out += s;
            out.push_back('\0');
        }
        return out;
    };
    std::map<std::string, uint32_t> ko, so;
    std::string kt = pool(keys, ko), st = pool(strs, so);
    auto put32 = [](std::string& d, size_t at, uint32_t v) { memcpy(&d[at], &v, 4); };
    std::string data(8, '\0');
    put32(data, 0, 0xD0000000u);   // root entry: an array-item table at data + 8
    put32(data, 4, 8);
    std::function<uint32_t(const V&)> table = [&](const V& v) -> uint32_t {
        std::vector<std::pair<const std::string*, const V*>> es;
        for (auto& e : v.named) es.emplace_back(&e.first, &e.second);
        std::sort(es.begin(), es.end(), [](const auto& a, const auto& b) { return *a.first < *b.first; });
        for (auto& x : v.items) es.emplace_back(nullptr, &x);
        uint32_t at = (uint32_t)data.size();
        uint16_t hdr[2] = {0xfeed, (uint16_t)es.size()};
        data.append((const char*)hdr, 4);
        data.append(8 * es.size(), '\0');
        for (size_t k = 0; k < es.size(); ++k) {
            const V& x = *es[k].second;
            uint32_t t = x.t == V::BOOL ? 0x40 : x.t == V::INT ? 0x00 : x.t == V::FLT ? 0x20 : x.t == V::STR ? 0x30 : 0x50;
            uint32_t w = es[k].first ? (ko[*es[k].first] | t << 24) : (0xffffffu | (t | 0x80) << 24);
            uint32_t val;
            if (x.t == V::FLT) memcpy(&val, &x.f, 4);
            else if (x.t == V::STR) val = so[x.s];
            else if (x.t == V::TAB) val = table(x);
            else val = (uint32_t)x.i;
            put32(data, at + 4 + 8 * k, w);
            put32(data, at + 8 + 8 * k, val);
        }
        return at;
    };
    table(tree);
    size_t pad = (4 - (0x20 + kt.size() + st.size()) % 4) % 4;
    uint32_t h[8] = {0x0123abcd, 0x3000, 0x20, (uint32_t)kt.size(), (uint32_t)(0x20 + kt.size()), (uint32_t)st.size(),
                     (uint32_t)(0x20 + kt.size() + st.size() + pad), (uint32_t)data.size()};
    return std::string((const char*)h, 32) + kt + st + std::string(pad, '\0') + data;
}

}  // namespace chd

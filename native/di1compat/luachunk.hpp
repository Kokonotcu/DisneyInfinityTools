// Lua 5.1 bytecode (x86: little-endian, int/size_t/instruction 4 bytes, number 8): rewrite string constants in place
// of a chunk, every function included. Debug strings (source, locals, upvalues) are left alone. Constant strings are
// length-prefixed, so changing one moves nothing else. Also: run a data-only chunk (eval) and write a value back as Lua
// text (text).
#pragma once
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "base/bytes.hpp"

namespace luachunk {

struct Reader {
    const uint8_t* p;
    size_t n, at = 0;
    bool big = false;   // eval: the expendables packages (both games) are big-endian chunks
    uint32_t u32() { need(4); uint32_t v; memcpy(&v, p + at, 4); at += 4; return big ? _byteswap_ulong(v) : v; }
    uint8_t u8() { need(1); return p[at++]; }
    void skip(size_t k) { need(k); at += k; }
    void need(size_t k) const { if (!bytes::fits(n, at, k)) throw std::runtime_error("lua chunk truncated"); }
    // A u32 count of items of at least `each` bytes, checked against the bytes left before allocating (CO-26).
    uint32_t count(size_t each)
    {
        const uint32_t c = u32();
        if (c > (n - at) / each) throw std::runtime_error("lua chunk truncated");
        return c;
    }
};

// fn(s) -> true if it changed s (s without the trailing NUL). Returns how many constants changed.
inline int rewrite(const std::string& in, std::string& out, const std::function<bool(std::string&)>& fn)
{
    static const uint8_t HDR[12] = {0x1b, 'L', 'u', 'a', 0x51, 0, 1, 4, 4, 4, 8, 0};
    if (in.size() < 12 || memcmp(in.data(), HDR, 12)) throw std::runtime_error("not a Lua 5.1 x86 chunk");
    Reader r{(const uint8_t*)in.data(), in.size(), 12};
    out.assign(in, 0, 12);
    size_t copied = 12;   // in[copied..r.at) is still to be copied verbatim
    int changed = 0;
    auto flush = [&]() { out.append(in, copied, r.at - copied); copied = r.at; };
    auto str = [&]() { uint32_t len = r.u32(); r.skip(len); };
    std::function<void()> function = [&]() {
        str();                                   // source
        r.skip(4 + 4 + 4);                       // linedefined, lastlinedefined, nups/params/vararg/maxstack
        r.skip((size_t)r.u32() * 4);             // code
        uint32_t nk = r.u32();
        for (uint32_t i = 0; i < nk; ++i) {
            uint8_t t = r.u8();
            if (t == 1) r.skip(1);
            else if (t == 3) r.skip(8);
            else if (t == 4) {
                flush();
                uint32_t len = r.u32();
                r.skip(len);
                std::string s = len ? in.substr(r.at - len, len - 1) : std::string();
                if (len && fn(s)) {
                    uint32_t nl = (uint32_t)s.size() + 1;
                    out.append((const char*)&nl, 4);
                    out += s;
                    out.push_back('\0');
                    ++changed;
                } else out.append(in, r.at - len - 4, len + 4);
                copied = r.at;
            } else if (t != 0) throw std::runtime_error("bad constant type");
        }
        uint32_t np = r.u32();
        for (uint32_t i = 0; i < np; ++i) function();
        r.skip((size_t)r.u32() * 4);             // lineinfo
        uint32_t nl = r.u32();
        for (uint32_t i = 0; i < nl; ++i) { str(); r.skip(8); }   // locvars
        uint32_t nu = r.u32();
        for (uint32_t i = 0; i < nu; ++i) str();                 // upvalue names
    };
    function();
    r.at = in.size();
    flush();
    return changed;
}

// ---- data chunks: a compiled table constructor (1.0 gamedb/core/zonelist.lua: NEWTABLE, SETTABLE, SETLIST, LOADK,
// LOADBOOL, SETGLOBAL, RETURN and nothing else) run without a Lua VM ----
struct Value {
    enum T { NIL, BOOL, NUM, STR, TAB } t = NIL;
    bool b = false;
    double n = 0;
    std::string s;
    std::shared_ptr<std::vector<std::pair<Value, Value>>> tab;   // entries in assignment order, keys unique (shared like Lua's)
    bool operator==(const Value& o) const { return t == o.t && b == o.b && n == o.n && s == o.s && tab == o.tab; }
    const Value* get(const Value& k) const
    {
        if (t == TAB)
            for (auto& e : *tab) if (e.first == k) return &e.second;
        return nullptr;
    }
    const Value* get(const std::string& k) const { Value v; v.t = STR; v.s = k; return get(v); }
    std::string str(const std::string& k) const { const Value* v = get(k); return v && v->t == STR ? v->s : ""; }
};

namespace detail {
// Register x of eval's frame; a damaged b, c or a + k reaches 766 of 306 (CO-26, NP-N1).
inline Value& reg(std::vector<Value>& frame, uint32_t x)
{
    if (x >= frame.size()) throw std::runtime_error("lua chunk: register " + std::to_string(x) + " out of range");
    return frame[x];
}
inline Value num(double n)
{
    Value v;
    v.t = Value::NUM;
    v.n = n;
    return v;
}
}  // namespace detail

// The globals a data-only chunk sets (little- or big-endian). Throws on an opcode a table constructor does not use.
inline std::map<std::string, Value> eval(const std::string& in)
{
    static const uint8_t HDR[12] = {0x1b, 'L', 'u', 'a', 0x51, 0, 1, 4, 4, 4, 8, 0};
    if (in.size() < 12 || memcmp(in.data(), HDR, 6) || (uint8_t)in[6] > 1 || memcmp(in.data() + 7, HDR + 7, 5))
        throw std::runtime_error("not a Lua 5.1 chunk with 4-byte ints");
    Reader r{(const uint8_t*)in.data(), in.size(), 12, in[6] == 0};
    auto str = [&]() { uint32_t len = r.u32(); r.skip(len); return len ? in.substr(r.at - len, len - 1) : std::string(); };
    str();
    r.skip(12);
    std::vector<uint32_t> code(r.count(4));
    for (auto& c : code) c = r.u32();
    std::vector<Value> K(r.count(1));
    for (auto& k : K) {
        uint8_t t = r.u8();
        if (t == 1) { k.t = Value::BOOL; k.b = r.u8() != 0; }
        else if (t == 3) {
            k.t = Value::NUM;
            r.need(8);
            uint8_t d[8];
            for (int i = 0; i < 8; ++i) d[i] = r.p[r.at + (r.big ? 7 - i : i)];
            memcpy(&k.n, d, 8);
            r.skip(8);
        }
        else if (t == 4) { k.t = Value::STR; k.s = str(); }
        else if (t != 0) throw std::runtime_error("bad constant type");
    }
    std::map<std::string, Value> G;
    std::vector<Value> R(256 + 50);
    auto rk = [&](uint32_t x) -> const Value& { return x & 0x100 ? K.at(x & 0xff) : R[x]; };
    auto set = [](Value& t, const Value& k, const Value& v) {
        if (t.t != Value::TAB || k.t == Value::NIL) throw std::runtime_error("bad table store");
        auto& e = *t.tab;
        auto it = std::find_if(e.begin(), e.end(), [&](const std::pair<Value, Value>& x) { return x.first == k; });
        if (v.t == Value::NIL) { if (it != e.end()) e.erase(it); }
        else if (it != e.end()) it->second = v;
        else e.push_back({k, v});
    };
    for (size_t pc = 0; pc < code.size(); ++pc) {
        uint32_t i = code[pc], op = i & 0x3f, a = i >> 6 & 0xff, c = i >> 14 & 0x1ff, b = i >> 23 & 0x1ff, bx = i >> 14;
        switch (op) {
        case 0: R[a] = detail::reg(R, b); break;                                          // MOVE
        case 1: R[a] = K.at(bx); break;                                                   // LOADK
        case 2: R[a] = Value(); R[a].t = Value::BOOL; R[a].b = b != 0; pc += c ? 1 : 0; break;   // LOADBOOL
        case 3: for (uint32_t k = a; k <= b; ++k) detail::reg(R, k) = Value(); break;     // LOADNIL
        case 5: { auto g = G.find(K.at(bx).s); R[a] = g == G.end() ? Value() : g->second; break; }   // GETGLOBAL
        case 7: G[K.at(bx).s] = R[a]; break;                                              // SETGLOBAL
        case 9: set(R[a], rk(b), rk(c)); break;                                           // SETTABLE
        case 10: R[a] = Value(); R[a].t = Value::TAB; R[a].tab = std::make_shared<std::vector<std::pair<Value, Value>>>(); break;   // NEWTABLE
        case 34: {                                                                        // SETLIST, 50 per batch
            if (!c) c = code.at(++pc);
            if (!b) throw std::runtime_error("SETLIST to top");
            for (uint32_t k = 1; k <= b; ++k) set(R[a], detail::num((c - 1) * 50.0 + k), detail::reg(R, a + k));
            break;
        }
        case 30: return G;                                                                // RETURN
        default: throw std::runtime_error("not a data chunk (opcode " + std::to_string(op) + ")");
        }
    }
    return G;
}

// Lua text of a value: tables as "{ " + array part, then named keys sorted (type, then text) + "}", each entry
// followed by ", " (DI3Tools zonereg.py lua_text, so a record reads as it did when written offline).
inline std::string numText(double n)
{
    char b[32];
    if (n == std::floor(n) && std::fabs(n) < 1e15) snprintf(b, sizeof(b), "%lld", (long long)n);
    else *std::to_chars(b, b + sizeof(b) - 1, n).ptr = 0;   // shortest text that reads back the same double
    return b;
}
inline std::string quote(const std::string& s)
{
    std::string o = "\"";
    for (char c : s) o += c == '\\' ? "\\\\" : c == '"' ? "\\\"" : c == '\n' ? "\\n" : std::string(1, c);
    return o + "\"";
}
inline bool ident(const std::string& k)   // usable as a bare key: a name that is no Lua keyword
{
    static const char* const kw[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in",
                                     "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"};
    if (k.empty() || isdigit((unsigned char)k[0])) return false;
    for (char c : k) if (!isalnum((unsigned char)c) && c != '_') return false;
    for (auto w : kw) if (k == w) return false;
    return true;
}
// Lua 5.1 nests constructors at most 200 deep (LUAI_MAXCCALLS); a damaged SETTABLE can make a table hold itself.
inline constexpr int kMaxTableNesting = 200;
inline std::string text(const Value& v, int depth = 0)
{
    switch (v.t) {
    case Value::NIL: return "nil";
    case Value::BOOL: return v.b ? "true" : "false";
    case Value::NUM: return numText(v.n);
    case Value::STR: return quote(v.s);
    default: break;
    }
    if (depth >= kMaxTableNesting)
        throw std::runtime_error("lua chunk: tables nested more than " + std::to_string(kMaxTableNesting) + " deep");
    size_t n = 0;   // the array part: keys 1..n
    for (;; ++n) { Value k; k.t = Value::NUM; k.n = (double)(n + 1); if (!v.get(k)) break; }
    std::string o = "{ ";
    for (size_t i = 1; i <= n; ++i) {
        Value k;
        k.t = Value::NUM;
        k.n = (double)i;
        o += text(*v.get(k), depth + 1) + ", ";
    }
    std::vector<const std::pair<Value, Value>*> rest;
    for (auto& e : *v.tab)
        if (!(e.first.t == Value::NUM && e.first.n >= 1 && e.first.n <= n && e.first.n == std::floor(e.first.n))) rest.push_back(&e);
    auto order = [depth](const Value& k) {
        return std::make_pair(k.t == Value::BOOL ? 0 : k.t == Value::NUM ? 1 : 2,
                              k.t == Value::STR ? k.s : k.t == Value::NUM ? numText(k.n) : text(k, depth + 1));
    };
    std::sort(rest.begin(), rest.end(), [&](auto* a, auto* b) { return order(a->first) < order(b->first); });
    for (auto* e : rest) {
        const bool bare = e->first.t == Value::STR && ident(e->first.s);
        o += (bare ? e->first.s : "[" + text(e->first, depth + 1) + "]") + " = " + text(e->second, depth + 1) + ", ";
    }
    return o + "}";
}

}  // namespace luachunk

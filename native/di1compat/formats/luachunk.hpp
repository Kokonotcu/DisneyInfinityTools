// Lua 5.1 chunks (FORMATS.md 11): rewrite string constants, run data-only chunks, write Lua text. Any thread.
#pragma once
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"

namespace luachunk {

struct Reader {
    const uint8_t* p;
    size_t n, at = 0;
    bool big = false;   // eval: the expendables packages (both games) are big-endian chunks

    uint32_t u32()
    {
        need(4);
        const uint32_t v = bytes::le(p + at, 4);
        at += 4;
        return big ? _byteswap_ulong(v) : v;
    }
    uint8_t u8()
    {
        need(1);
        return p[at++];
    }
    void skip(size_t k)
    {
        need(k);
        at += k;
    }
    void need(size_t k) const
    {
        if (!bytes::fits(n, at, k)) throw std::runtime_error("lua chunk truncated");
    }
    // A u32 count of items of at least `each` bytes, checked against the bytes left before allocating (CO-26).
    uint32_t count(size_t each)
    {
        const uint32_t c = u32();
        if (c > (n - at) / each) throw std::runtime_error("lua chunk truncated");
        return c;
    }
};

namespace detail {
// The Lua 5.1 header of an x86 chunk: little-endian, int / size_t / instruction 4 bytes, number 8.
inline constexpr uint8_t kHeader[12] = {0x1b, 'L', 'u', 'a', 0x51, 0, 1, 4, 4, 4, 8, 0};
inline constexpr size_t kEndianAt = 6;   // 1 little-endian, 0 big-endian
enum Constant : uint8_t { kNil = 0, kBool = 1, kNumber = 3, kString = 4 };

// rewrite's walk: the chunk copied to out, each string constant that fn changes written anew.
struct Rewriter {
    const std::string& in;
    std::string& out;
    const std::function<bool(std::string&)>& fn;
    Reader r;
    size_t copied = 12;   // in[copied, r.at) is still to be copied verbatim
    int changed = 0;

    void flush()
    {
        out.append(in, copied, r.at - copied);
        copied = r.at;
    }
    void skipString() { r.skip(r.u32()); }
    // A string constant: length-prefixed, so a changed one moves nothing else.
    void stringConstant()
    {
        flush();
        const uint32_t len = r.u32();
        r.skip(len);
        std::string s = len ? in.substr(r.at - len, len - 1) : std::string();   // without the trailing NUL
        if (len && fn(s)) {
            bytes::appendLe(out, (uint32_t)s.size() + 1, 4);
            out += s;
            out.push_back('\0');
            ++changed;
        } else out.append(in, r.at - len - 4, len + 4);
        copied = r.at;
    }
    void constant()
    {
        switch (r.u8()) {
        case kNil: return;
        case kBool: return r.skip(1);
        case kNumber: return r.skip(8);
        case kString: return stringConstant();
        default: throw std::runtime_error("bad constant type");
        }
    }
    // A function prototype and those nested in it; debug strings (source, locals, upvalues) stay as they are.
    void function()
    {
        skipString();                    // source
        r.skip(4 + 4 + 4);               // linedefined, lastlinedefined, nups/params/vararg/maxstack
        r.skip((size_t)r.u32() * 4);     // code
        for (uint32_t i = 0, nk = r.u32(); i < nk; ++i) constant();
        for (uint32_t i = 0, np = r.u32(); i < np; ++i) function();
        r.skip((size_t)r.u32() * 4);     // lineinfo
        for (uint32_t i = 0, nl = r.u32(); i < nl; ++i) {
            skipString();                // a local's name, then its startpc and endpc
            r.skip(8);
        }
        for (uint32_t i = 0, nu = r.u32(); i < nu; ++i) skipString();   // upvalue names
    }
};
}  // namespace detail

// The chunk in with each string constant fn changes (fn gets it without its NUL) -> how many changed.
inline int rewrite(const std::string& in, std::string& out, const std::function<bool(std::string&)>& fn)
{
    if (in.size() < 12 || memcmp(in.data(), detail::kHeader, 12)) throw std::runtime_error("not a Lua 5.1 x86 chunk");
    out.assign(in, 0, 12);
    detail::Rewriter w{in, out, fn, Reader{(const uint8_t*)in.data(), in.size(), 12}};
    w.function();
    w.r.at = in.size();
    w.flush();
    return w.changed;
}

// ---- data chunks: compiled table constructors (1.0 zonelist.lua) run without a Lua VM ----
struct Value {
    enum T { NIL, BOOL, NUM, STR, TAB } t = NIL;
    bool b = false;
    double n = 0;
    std::string s;
    // entries in assignment order, keys unique; shared, like a Lua table
    std::shared_ptr<std::vector<std::pair<Value, Value>>> tab;

    bool operator==(const Value& o) const
    {
        const bool scalar = t == o.t && b == o.b && n == o.n;
        return scalar && s == o.s && tab == o.tab;
    }
    const Value* get(const Value& k) const
    {
        if (t != TAB) return nullptr;
        for (const auto& e : *tab)
            if (e.first == k) return &e.second;
        return nullptr;
    }
    const Value* get(const std::string& k) const
    {
        Value v;
        v.t = STR;
        v.s = k;
        return get(v);
    }
    // The same lookups on a table being edited (SY-10).
    Value* get(const Value& k) { return const_cast<Value*>(static_cast<const Value&>(*this).get(k)); }
    Value* get(const std::string& k) { return const_cast<Value*>(static_cast<const Value&>(*this).get(k)); }
    std::string str(const std::string& k) const
    {
        const Value* v = get(k);
        return v && v->t == STR ? v->s : "";
    }
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

// The Lua 5.1 opcodes a table constructor uses (lopcodes.h).
enum Op { kMove = 0, kLoadK = 1, kLoadBool = 2, kLoadNil = 3, kGetGlobal = 5, kSetGlobal = 7, kSetTable = 9,
          kNewTable = 10, kReturn = 30, kSetList = 34 };
inline constexpr uint32_t kFieldsPerFlush = 50;   // LFIELDS_PER_FLUSH: SETLIST's batch
inline constexpr size_t kFrame = 256 + 50;        // registers: a is 8 bits, SETLIST reaches a + 50

// Stores t[k] = v, keeping assignment order; a nil value removes the key.
inline void store(Value& t, const Value& k, const Value& v)
{
    if (t.t != Value::TAB || k.t == Value::NIL) throw std::runtime_error("bad table store");
    auto& e = *t.tab;
    auto it = std::find_if(e.begin(), e.end(), [&](const std::pair<Value, Value>& x) { return x.first == k; });
    if (v.t == Value::NIL) {
        if (it != e.end()) e.erase(it);
    } else if (it != e.end()) it->second = v;
    else e.push_back({k, v});
}

// eval's machine: the function's code and constants, its registers and the globals it sets.
struct Machine {
    std::vector<uint32_t> code;
    std::vector<Value> K;
    std::map<std::string, Value> G;
    std::vector<Value> R = std::vector<Value>(kFrame);

    const Value& rk(uint32_t x) { return x & 0x100 ? K.at(x & 0xff) : R[x]; }
    // Runs the instruction at pc (op 6 bits, A 8, C 9, B 9; Bx = B:C); false at RETURN.
    bool step(size_t& pc)
    {
        const uint32_t i = code[pc], op = i & 0x3f, a = i >> 6 & 0xff, c = i >> 14 & 0x1ff, b = i >> 23 & 0x1ff;
        const uint32_t bx = i >> 14;
        switch (op) {
        case kMove:
            R[a] = reg(R, b);
            return true;
        case kLoadK:
            R[a] = K.at(bx);
            return true;
        case kLoadBool:
            R[a] = Value();
            R[a].t = Value::BOOL;
            R[a].b = b != 0;
            pc += c ? 1 : 0;
            return true;
        case kLoadNil:
            for (uint32_t k = a; k <= b; ++k) reg(R, k) = Value();
            return true;
        case kGetGlobal: {
            auto g = G.find(K.at(bx).s);
            R[a] = g == G.end() ? Value() : g->second;
            return true;
        }
        case kSetGlobal:
            G[K.at(bx).s] = R[a];
            return true;
        case kSetTable:
            store(R[a], rk(b), rk(c));
            return true;
        default:
            return stepTable(pc, op, a, b, c);
        }
    }
    // NEWTABLE, SETLIST and RETURN; anything else is no data chunk.
    bool stepTable(size_t& pc, uint32_t op, uint32_t a, uint32_t b, uint32_t c)
    {
        switch (op) {
        case kNewTable:
            R[a] = Value();
            R[a].t = Value::TAB;
            R[a].tab = std::make_shared<std::vector<std::pair<Value, Value>>>();
            return true;
        case kSetList:
            if (!c) c = code.at(++pc);
            if (!b) throw std::runtime_error("SETLIST to top");
            for (uint32_t k = 1; k <= b; ++k) store(R[a], num((c - 1) * (double)kFieldsPerFlush + k), reg(R, a + k));
            return true;
        case kReturn:
            return false;
        default:
            throw std::runtime_error("not a data chunk (opcode " + std::to_string(op) + ")");
        }
    }
};

// A number constant (8 bytes, the chunk's byte order).
inline double number(Reader& r)
{
    r.need(8);
    uint8_t d[8];
    for (int i = 0; i < 8; ++i) d[i] = r.p[r.at + (r.big ? 7 - i : i)];
    double v;
    memcpy(&v, d, 8);
    r.skip(8);
    return v;
}
inline std::string string(Reader& r, const std::string& in)
{
    const uint32_t len = r.u32();
    r.skip(len);
    return len ? in.substr(r.at - len, len - 1) : std::string();
}
inline Value constant(Reader& r, const std::string& in)
{
    Value k;
    switch (r.u8()) {
    case kNil: break;
    case kBool:
        k.t = Value::BOOL;
        k.b = r.u8() != 0;
        break;
    case kNumber:
        k.t = Value::NUM;
        k.n = number(r);
        break;
    case kString:
        k.t = Value::STR;
        k.s = string(r, in);
        break;
    default: throw std::runtime_error("bad constant type");
    }
    return k;
}
}  // namespace detail

// The globals a data-only chunk sets (little- or big-endian). Throws on an opcode a table constructor does not use.
inline std::map<std::string, Value> eval(const std::string& in)
{
    const uint8_t* h = detail::kHeader;
    const bool header = in.size() >= 12 && !memcmp(in.data(), h, 6) && !memcmp(in.data() + 7, h + 7, 5);
    if (!header || (uint8_t)in[detail::kEndianAt] > 1) throw std::runtime_error("not a Lua 5.1 chunk with 4-byte ints");
    Reader r{(const uint8_t*)in.data(), in.size(), 12, in[detail::kEndianAt] == 0};
    detail::string(r, in);   // source
    r.skip(12);
    detail::Machine m;
    m.code.resize(r.count(4));
    for (auto& c : m.code) c = r.u32();
    m.K.resize(r.count(1));
    for (auto& k : m.K) k = detail::constant(r, in);
    for (size_t pc = 0; pc < m.code.size(); ++pc)
        if (!m.step(pc)) break;
    return m.G;
}

// ---- Lua text of a value (DI3Tools zonereg.py lua_text, so a record reads as it did when written offline) ----

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
    for (char c : s) {
        switch (c) {
        case '\\':
            o += "\\\\";
            break;
        case '"':
            o += "\\\"";
            break;
        case '\n':
            o += "\\n";
            break;
        default:
            o += c;
        }
    }
    return o + "\"";
}

namespace detail {
inline constexpr const char* kKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function",
                                            "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
                                            "true", "until", "while"};
}  // namespace detail

// Usable as a bare key: a name that is no Lua keyword.
inline bool ident(const std::string& k)
{
    if (k.empty() || isdigit((unsigned char)k[0])) return false;
    for (char c : k)
        if (!isalnum((unsigned char)c) && c != '_') return false;
    for (const char* w : detail::kKeywords)
        if (k == w) return false;
    return true;
}

// Lua 5.1 nests constructors at most 200 deep (LUAI_MAXCCALLS); a damaged SETTABLE can make a table hold itself.
inline constexpr int kMaxTableNesting = 200;

inline std::string text(const Value& v, int depth = 0);

namespace detail {
// The array part's length: keys 1..n are all there.
inline size_t arrayLength(const Value& v)
{
    size_t n = 0;
    for (;; ++n)
        if (!v.get(num((double)(n + 1)))) return n;
}
// A named key's sort key: booleans, then numbers, then the rest, each by its text (so "10" < "9").
inline std::pair<int, std::string> keyOrder(const Value& k, int depth)
{
    const int rank = k.t == Value::BOOL ? 0 : k.t == Value::NUM ? 1 : 2;
    return {rank, k.t == Value::STR ? k.s : k.t == Value::NUM ? numText(k.n) : text(k, depth + 1)};
}
inline bool inArray(const Value& k, size_t n)
{
    const bool whole = k.n == std::floor(k.n), inRange = k.n >= 1 && k.n <= n;
    return k.t == Value::NUM && inRange && whole;
}
// "{ " + the array part, then the named keys sorted by keyOrder, each followed by ", ", then "}".
inline std::string tableText(const Value& v, int depth)
{
    const size_t n = arrayLength(v);
    std::string o = "{ ";
    for (size_t i = 1; i <= n; ++i) o += text(*v.get(num((double)i)), depth + 1) + ", ";
    std::vector<const std::pair<Value, Value>*> rest;
    for (const auto& e : *v.tab)
        if (!inArray(e.first, n)) rest.push_back(&e);
    std::sort(rest.begin(), rest.end(),
              [&](auto* a, auto* b) { return keyOrder(a->first, depth) < keyOrder(b->first, depth); });
    for (const auto* e : rest) {
        const bool bare = e->first.t == Value::STR && ident(e->first.s);
        o += (bare ? e->first.s : "[" + text(e->first, depth + 1) + "]") + " = " + text(e->second, depth + 1) + ", ";
    }
    return o + "}";
}
}  // namespace detail

// Lua text of a value; a table as tableText writes it.
inline std::string text(const Value& v, int depth)
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
    return detail::tableText(v, depth);
}

}  // namespace luachunk

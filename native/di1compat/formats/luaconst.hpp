// Lua 5.1 table-constructor chunks read without a VM, and quoted Lua strings. base. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"

namespace luaconst {

// s as a Lua string literal of printable ASCII, other bytes as \ddd. Not luachunk::quote (SY-08).
inline std::string quoteAscii(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += (char)c;
        } else if (c < 32 || c > 126) {
            char e[8];
            snprintf(e, sizeof e, "\\%03u", c);
            o += e;
        } else {
            o += (char)c;
        }
    }
    return o + "\"";
}

// evalStrict's values: kind, a number (a boolean as 0 / 1), a string, a table index.
struct StrictVal {
    int kind = 0;
    double num = 0;
    std::string str;
    int tab = -1;
};
constexpr int kNil = 0;
constexpr int kNumber = 1;
constexpr int kString = 2;
constexpr int kTable = 3;
constexpr int kBool = 4;
// A table's entries in first-store order; a stored key keeps its place, the last value wins.
struct StrictTable {
    std::vector<std::pair<StrictVal, StrictVal>> kv;
};
// evalStrict's result: every table the chunk built, and the one it stored in the expected global.
struct StrictChunk {
    std::vector<StrictTable> tabs;
    StrictVal global;
};

namespace detail {

// The Lua 5.1 header of an x86 chunk: little-endian; int, size_t, instruction 4 bytes; number 8.
inline constexpr uint8_t kHeader[12] = {0x1b, 'L', 'u', 'a', 0x51, 0, 1, 4, 4, 4, 8, 0};
constexpr size_t kFunctionBytes = 12;   // linedefined, lastlinedefined, nups, numparams, is_vararg, maxstacksize
constexpr size_t kInstruction = 4;
constexpr size_t kNumberBytes = 8;
// Constant tags and opcodes (lundump.c, lopcodes.h)
enum Tag : uint8_t { kTagNil = 0, kTagBool = 1, kTagNumber = 3, kTagString = 4 };
enum Op : uint32_t { kLoadK = 1, kLoadBool = 2, kSetGlobal = 7, kSetTable = 9, kNewTable = 10, kReturn = 30,
                     kSetList = 34 };
constexpr uint32_t kFieldsPerFlush = 50;   // LFIELDS_PER_FLUSH: SETLIST's batch
constexpr uint32_t kRkConstant = 0x100;    // an RK operand naming a constant
constexpr uint32_t kRegisters = 256;       // A is 8 bits

inline bool fail(std::string* why, const char* w)
{
    if (why) *why = w;
    return false;
}

// One instruction: op 6 bits, A 8, C 9, B 9; Bx = B:C.
struct Ins {
    uint32_t op, a, b, c, bx;
};
inline Ins decode(const uint8_t* code, uint32_t pc)
{
    uint32_t i;
    memcpy(&i, code + kInstruction * pc, kInstruction);
    return {i & 0x3f, (i >> 6) & 0xff, i >> 23, (i >> 14) & 0x1ff, i >> 14};
}

// A read position in a chunk of n bytes; it never passes n.
struct Cursor {
    const uint8_t* b;
    size_t n, at;
    bool u32(uint32_t& v)
    {
        if (!bytes::fits(n, at, 4)) return false;
        memcpy(&v, b + at, 4);
        at += 4;
        return true;
    }
};

struct Code {
    const uint8_t* code = nullptr;
    uint32_t ncode = 0;
};

// The header, source name and function bytes, then the code; any shortfall is "truncated".
inline bool strictCode(Cursor& c, Code& out, std::string* why)
{
    if (c.n < sizeof kHeader || memcmp(c.b, kHeader, sizeof kHeader)) return fail(why, "not a Lua 5.1 x86 chunk");
    c.at = sizeof kHeader;
    uint32_t len;
    if (!c.u32(len) || len > c.n - c.at || c.n - c.at - len < kFunctionBytes) return fail(why, "truncated");
    c.at += len + kFunctionBytes;
    if (!c.u32(out.ncode) || (uint64_t)kInstruction * out.ncode > c.n - c.at) return fail(why, "truncated");
    out.code = c.b + c.at;
    c.at += kInstruction * out.ncode;
    return true;
}

// One constant into k: nullptr, or why it fails.
inline const char* strictConstant(Cursor& c, StrictVal& k)
{
    if (c.at >= c.n) return "truncated";
    const uint8_t t = c.b[c.at++];
    if (t == kTagNil) return nullptr;
    if (t == kTagBool && c.at < c.n) {
        k.kind = kBool;
        k.num = c.b[c.at++] ? 1 : 0;
        return nullptr;
    }
    if (t == kTagNumber && bytes::fits(c.n, c.at, kNumberBytes)) {
        k.kind = kNumber;
        memcpy(&k.num, c.b + c.at, kNumberBytes);
        c.at += kNumberBytes;
        return nullptr;
    }
    uint32_t len;
    if (t != kTagString || !c.u32(len)) return "constant kind";
    if (!len || len > c.n - c.at) return "constant kind";
    k.kind = kString;
    k.str.assign((const char*)c.b + c.at, len - 1);
    c.at += len;
    return nullptr;
}

inline bool strictConstants(Cursor& c, std::vector<StrictVal>& K, std::string* why)
{
    uint32_t nk;
    // every constant takes at least one byte
    if (!c.u32(nk) || !bytes::fits(c.n, c.at, nk)) return fail(why, "truncated");
    K.resize(nk);
    for (StrictVal& k : K)
        if (const char* bad = strictConstant(c, k)) return fail(why, bad);
    return true;
}

inline bool sameKey(const StrictVal& a, const StrictVal& k)
{
    return a.kind == k.kind && (k.kind == kString ? a.str == k.str : a.num == k.num);
}

// evalStrict's registers and the tables they name.
struct StrictRun {
    std::vector<StrictTable>& tabs;
    const std::vector<StrictVal>& K;
    std::vector<StrictVal> R = std::vector<StrictVal>(kRegisters);

    const StrictVal* rk(uint32_t x) const
    {
        if (!(x & kRkConstant)) return &R[x & 0xff];
        return (x & 0xff) < K.size() ? &K[x & 0xff] : nullptr;
    }
    void store(const StrictVal& t, const StrictVal& k, const StrictVal& v)
    {
        auto& kv = tabs[t.tab].kv;
        for (auto& e : kv) {
            if (!sameKey(e.first, k)) continue;
            e.second = v;
            return;
        }
        kv.emplace_back(k, v);
    }
    void newTable(uint32_t a)
    {
        R[a] = StrictVal();
        R[a].kind = kTable;
        R[a].tab = (int)tabs.size();
        tabs.emplace_back();
    }
    // SETTABLE R[a][RK(b)] = RK(c): a table, and a key that is neither nil nor a table.
    bool setTable(const Ins& in)
    {
        const StrictVal *k = rk(in.b), *v = rk(in.c);
        const bool operands = k && v;
        const bool usableKey = operands && k->kind != kNil && k->kind != kTable;
        if (R[in.a].kind != kTable || !usableKey) return false;
        store(R[in.a], *k, *v);
        return true;
    }
    void loadBool(const Ins& in)
    {
        R[in.a] = StrictVal();
        R[in.a].kind = kBool;
        R[in.a].num = in.b ? 1 : 0;
    }
    bool isGlobal(uint32_t bx, const char* global) const
    {
        return bx < K.size() && K[bx].kind == kString && K[bx].str == global;
    }
};

// SETLIST; C == 0 takes C from the next word, skipped by pc. B == 0 (to the top) fails.
inline const char* strictSetList(StrictRun& m, const Code& code, uint32_t& pc, const Ins& in)
{
    uint32_t c = in.c;
    if (!c) {
        ++pc;
        if (pc >= code.ncode) return "SETLIST";
        memcpy(&c, code.code + kInstruction * pc, kInstruction);
    }
    if (m.R[in.a].kind != kTable || !in.b || in.a + in.b >= kRegisters) return "SETLIST operands";
    for (uint32_t j = 1; j <= in.b; ++j) {
        StrictVal k;
        k.kind = kNumber;
        k.num = (c - 1) * (double)kFieldsPerFlush + j;
        m.store(m.R[in.a], k, m.R[in.a + j]);
    }
    return nullptr;
}

// Runs the constructor ops until RETURN; SETGLOBAL of `global` sets out, any other global or op fails.
inline bool strictRun(const Code& code, const char* global, StrictRun& m, StrictVal& out, std::string* why)
{
    for (uint32_t pc = 0; pc < code.ncode; ++pc) {
        const Ins in = decode(code.code, pc);
        switch (in.op) {
        case kNewTable:
            m.newTable(in.a);
            break;
        case kSetTable:
            if (!m.setTable(in)) return fail(why, "SETTABLE operands");
            break;
        case kLoadK:
            if (in.bx >= m.K.size()) return fail(why, "LOADK index");
            m.R[in.a] = m.K[in.bx];
            break;
        case kLoadBool:
            m.loadBool(in);
            pc += in.c ? 1 : 0;
            break;
        case kSetList:
            if (const char* bad = strictSetList(m, code, pc, in)) return fail(why, bad);
            break;
        case kSetGlobal:
            if (!m.isGlobal(in.bx, global)) return fail(why, "an unexpected global");
            out = m.R[in.a];
            break;
        case kReturn:
            return true;
        default:
            return fail(why, "opcode outside table constructors");
        }
    }
    return true;
}

}  // namespace detail

// The table a one-function constructor chunk stores in `global`. Strict: nil or table keys, other globals fail.
inline bool evalStrict(const uint8_t* b, size_t n, const char* global, StrictChunk& out, std::string* why)
{
    detail::Cursor c{b, n, 0};
    detail::Code code;
    std::vector<StrictVal> K;
    if (!detail::strictCode(c, code, why) || !detail::strictConstants(c, K, why)) return false;
    detail::StrictRun m{out.tabs, K};
    if (!detail::strictRun(code, global, m, out.global, why)) return false;
    if (out.global.kind != kTable) return detail::fail(why, "no table in the expected global");
    return true;
}

}  // namespace luaconst

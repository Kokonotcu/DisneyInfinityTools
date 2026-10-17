// BMX-1 / BTX-1: 1.0 behavior maps and trees as the Lua text 3.0's loaders take. luaconst, sxkeys. Any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "../../base/text.hpp"
#include "../../formats/luaconst.hpp"
#include "sxkeys.hpp"

namespace di1::aitables {

// Whether the 1.0 install has that file: 1.0's own search decides where a bare script name lives.
typedef std::function<bool(const std::string&)> Exists;

// A .bmx as 3.0's loader FUN_015473f0 reads it -> behaviors written, or -1 (why).
inline int bmx(const void* data, size_t n, const Exists& exists, std::string& out, std::string* why);
// A .btx as 3.0's loader FUN_015459a0 reads it -> tables written, or -1 (why).
inline int btx(const void* data, size_t n, const Exists& exists, std::string& out, std::string* why);

namespace detail {

using Val = luaconst::StrictVal;
using Table = luaconst::StrictTable;
constexpr int kMaxDepth = 64;   // nested tables written; deeper ones are written as nil

inline void luaNum(std::string& o, double v)
{
    char e[40];
    snprintf(e, sizeof e, "%.17g", v);
    o += e;
}

// A behavior Method or Method_Custom as 1.0 resolves it: scripts/<m> if present, else characters/<m> (FUN_00f53380).
inline std::string method(const std::string& m, const Exists& exists)
{
    if (m.empty() || m == "none") return m;
    std::string s = text::slashLower(m);
    return (exists("scripts/" + s) ? "scripts/" : "characters/") + s;
}

// The 3.0 text of the tables one chunk built.
struct Writer {
    std::string& o;
    const std::vector<Table>& tabs;
    const Exists& exists;

    // 1.0 {Name = value} as 3.0's {{key id, value}, ...} (FUN_0152d380); numbers and strings, all the data has.
    void keyed(const Val& t)
    {
        o += "{";
        for (auto& [k, x] : tabs[t.tab].kv) {
            const bool scalar = x.kind == luaconst::kNumber || x.kind == luaconst::kString;
            if (k.kind != luaconst::kString || !scalar) continue;
            o += "{" + std::to_string(sxkeys::keyId(k.str.c_str())) + ", ";
            if (x.kind == luaconst::kNumber) luaNum(o, x.num);
            else o += luaconst::quoteAscii(k.str == "Method_Custom" ? method(x.str, exists) : x.str);
            o += "}, ";
        }
        o += "}";
    }
    // 1.0 resolves RoutingScript as Characters\<m> (FUN_00fee740), InteractionScript as Scripts\<m> (FUN_00ffb440).
    void stringValue(const Val& v, const std::string& key)
    {
        const bool name = !v.str.empty();
        if (name && key == "RoutingScript") o += luaconst::quoteAscii("characters/" + text::slashLower(v.str));
        else if (name && key == "InteractionScript") o += luaconst::quoteAscii("scripts/" + text::slashLower(v.str));
        else o += luaconst::quoteAscii(v.str);
    }
    void tableValue(const Val& v, const std::string& key, int depth)
    {
        if (key == "HashOverrides") return keyed(v);
        if (depth >= kMaxDepth) {
            o += "nil";
            return;
        }
        o += "{";
        for (auto& [k, x] : tabs[v.tab].kv) {
            o += "[";
            if (k.kind == luaconst::kString) o += luaconst::quoteAscii(k.str);
            else if (k.kind == luaconst::kBool) o += k.num ? "true" : "false";
            else luaNum(o, k.num);
            o += "] = ";
            value(x, k.kind == luaconst::kString ? k.str : std::string(), depth + 1);
            o += ", ";
        }
        o += "}";
    }
    // Any value; key is the field it is stored under.
    void value(const Val& v, const std::string& key, int depth)
    {
        switch (v.kind) {
        case luaconst::kNumber:
            luaNum(o, v.num);
            break;
        case luaconst::kBool:
            o += v.num ? "true" : "false";
            break;
        case luaconst::kString:
            stringValue(v, key);
            break;
        case luaconst::kTable:
            tableValue(v, key, depth);
            break;
        default:
            o += "nil";
        }
    }
};

// One BehaviorMap entry's fields 3.0 reads: Method as a path, Data as a keyed table.
inline void bmxEntry(Writer& w, const Val& e)
{
    for (auto& [f, v] : w.tabs[e.tab].kv) {
        const bool named = f.kind == luaconst::kString;
        if (named && f.str == "Method" && v.kind == luaconst::kString) {
            w.o += "Method = " + luaconst::quoteAscii(method(v.str, w.exists)) + ", ";
        } else if (named && f.str == "Data" && v.kind == luaconst::kTable) {
            w.o += "Data = ";
            w.keyed(v);
            w.o += ", ";
        }
    }
}

}  // namespace detail

// 3.0's loaders build a table only at Version 763 (0x2fb); 1.0's read none, so 3.0 dropped them all.
inline int bmx(const void* data, size_t n, const Exists& exists, std::string& out, std::string* why)
{
    luaconst::StrictChunk c;
    if (!luaconst::evalStrict((const uint8_t*)data, n, "BehaviorMap", c, why)) return -1;
    out = "BehaviorMap = {\n\tVersion = 763,\n";
    detail::Writer w{out, c.tabs, exists};
    int entries = 0;
    for (auto& [type, e] : c.tabs[c.global.tab].kv) {
        if (type.kind != luaconst::kString || e.kind != luaconst::kTable) continue;
        out += "\t[" + luaconst::quoteAscii(type.str) + "] = {";
        detail::bmxEntry(w, e);
        out += "},\n";
        ++entries;
    }
    out += "}\n";
    return entries;
}

inline int btx(const void* data, size_t n, const Exists& exists, std::string& out, std::string* why)
{
    luaconst::StrictChunk c;
    if (!luaconst::evalStrict((const uint8_t*)data, n, "BehaviorTree", c, why)) return -1;
    out = "BehaviorTree = {\n\tVersion = 763,\n";
    detail::Writer w{out, c.tabs, exists};
    for (auto& [k, x] : c.tabs[c.global.tab].kv) {
        if (k.kind != luaconst::kString || k.str == "Version") continue;
        out += "\t[" + luaconst::quoteAscii(k.str) + "] = ";
        w.value(x, k.str, 1);
        out += ",\n";
    }
    out += "}\n";
    return (int)c.tabs.size();
}

}  // namespace di1::aitables

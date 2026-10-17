// Queries the choreography tests share: corpus indexes, SubNetwork header paths, node children. Main thread.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "../convert/records.hpp"
#include "../formats/oct.hpp"
#include "files.hpp"
#include "tuples.hpp"

namespace octtest {

using Corpus = std::map<std::string, std::filesystem::path>;   // asset name -> its .oct

inline constexpr const char* kChoSubs[] = {"choreographies", "exploders"};   // the corpus folders, in walk order

// <sub>/<xx>/<name>/<name>.oct below an extract's assets dir, by name.
inline Corpus choIndex(const std::filesystem::path& assets, const char* sub = "choreographies")
{
    Corpus m;
    for (auto& xx : std::filesystem::directory_iterator(assets / sub)) {
        if (!xx.is_directory()) continue;
        for (auto& d : std::filesystem::directory_iterator(xx.path())) {
            auto p = d.path() / (d.path().filename().string() + ".oct");
            if (std::filesystem::exists(p)) m[d.path().filename().string()] = p;
        }
    }
    return m;
}

// Every file of index, each followed by its motions/*.oct.
inline std::vector<std::filesystem::path> withMotions(const Corpus& index)
{
    std::vector<std::filesystem::path> files;
    for (auto& [name, p] : index) {
        files.push_back(p);
        const auto md = p.parent_path() / "motions";
        if (!std::filesystem::is_directory(md)) continue;
        for (auto& e : std::filesystem::directory_iterator(md))
            if (e.path().extension() == ".oct") files.push_back(e.path());
    }
    return files;
}

// Every choreography and exploder of an extract's assets dir, then every world's <name>.oct.
inline std::vector<std::filesystem::path> choAndWorldFiles(const std::filesystem::path& assets)
{
    std::vector<std::filesystem::path> v;
    for (const char* sub : kChoSubs)
        for (auto& [n, p] : choIndex(assets, sub)) v.push_back(p);
    for (auto& w : std::filesystem::directory_iterator(assets / "worlds")) {
        auto p = w.path() / (w.path().filename().string() + ".oct");
        if (w.is_directory() && std::filesystem::exists(p)) v.push_back(p);
    }
    return v;
}

// fn(name, 1.0 path, 1.0 file, 3.0 file) for each tuple of a with a tuple twin in b.
template <class Fn>
inline void forEachTupleTwin(const Corpus& a, const Corpus& b, Fn fn)
{
    for (auto& [n, p] : a) {
        auto t = b.find(n);
        if (t == b.end()) continue;
        std::string b10 = slurp(p), b30 = slurp(t->second);
        if (!isTuple(b10) || !isTuple(b30)) continue;
        oct::File f = parse(b10), g = parse(b30);
        fn(n, p, f, g);
    }
}

// A SubNetwork: its record, header paths and DataNode / ProcessorNode record indices.
struct SubNet {
    size_t at = 0;
    std::vector<std::string> paths;   // header number -> 'a/b/c' (0 = root, "")
    std::vector<size_t> dataNodes, processors;
};

namespace detail {

// Header paths from HeaderStrings, HeaderStringIndices and HeaderLayout ('?' where an index is out of range).
inline std::vector<std::string> headerPaths(const std::vector<std::string>& strs, const std::vector<int32_t>& idx,
                                            const std::vector<int32_t>& lay)
{
    std::vector<std::string> paths = {""};
    size_t k = 0;
    for (size_t p = 0; p + 1 < lay.size(); p += 2)
        for (int32_t c = 0; c < lay[p + 1]; ++c, ++k) {
            std::string nm = k < idx.size() && idx[k] >= 0 && (size_t)idx[k] < strs.size() ? strs[idx[k]] : "?";
            std::string par = lay[p] >= 0 && (size_t)lay[p] < paths.size() ? paths[lay[p]] : "?";
            paths.push_back(par.empty() ? nm : par + "/" + nm);
        }
    return paths;
}

inline SubNet readSubNet(const oct::File& f, size_t i)
{
    SubNet s;
    s.at = i;
    std::vector<std::string> strs;
    std::vector<int32_t> idx, lay;
    std::string pool;
    for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
        const oct::Rec& r = f.recs[j];
        const std::string& n = f.name(r);
        if (r.level == 3) {
            pool = n;
            if (n == "HeaderStrings") strs = f.nameValues(r);
            else if (n == "HeaderStringIndices") idx = f.ints(r);
            else if (n == "HeaderLayout") lay = f.ints(r);
        } else if (r.level == 4 && pool == "DataNodePool") s.dataNodes.push_back(j);
        else if (r.level == 4 && pool == "ProcessorNodePool") s.processors.push_back(j);
    }
    s.paths = headerPaths(strs, idx, lay);
    return s;
}

// 'level|name|type|' of record r, its level relative to node's.
inline std::string recordHead(const oct::File& f, size_t node, const oct::Rec& r)
{
    char b[16];
    snprintf(b, sizeof b, "|%03x|", r.type);
    return std::to_string(r.level - f.recs[node].level) + "|" + f.name(r) + b;
}

// A record's value: names comma-terminated, other payloads in hex.
inline std::string recordValue(const oct::File& f, const oct::Rec& r)
{
    std::string l;
    if (oct::spec(r.type).kind == oct::NAME) {
        for (auto& s : f.nameValues(r)) l += s + ",";
        return l;
    }
    char b[16];
    for (unsigned char c : r.pay) {
        snprintf(b, sizeof b, "%02x", c);
        l += b;
    }
    return l;
}

}  // namespace detail

// Every SubNetwork of f in record order.
inline std::vector<SubNet> subnets(const oct::File& f)
{
    std::vector<SubNet> out;
    for (size_t i = 0; i < f.recs.size(); ++i)
        if (f.recs[i].level == 2 && f.name(f.recs[i]) == "SubNetwork") out.push_back(detail::readSubNet(f, i));
    return out;
}

// The child record of node i, one level down, named n; -1 if none.
inline long child(const oct::File& f, size_t i, const char* n)
{
    for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j)
        if (f.recs[j].level == f.recs[i].level + 1 && f.name(f.recs[j]) == n) return (long)j;
    return -1;
}

// The first name value of child n of node i, or "".
inline std::string childName(const oct::File& f, size_t i, const char* n)
{
    long k = child(f, i, n);
    auto v = k < 0 ? std::vector<std::string>{} : f.nameValues(f.recs[k]);
    return v.empty() ? "" : v[0];
}

// A node's header path through its Header record; '?' when it has none in range.
inline std::string headerPath(const oct::File& f, const SubNet& s, size_t node)
{
    long h = child(f, node, "Header");
    int k = h < 0 ? -1 : octx::firstInt(f, f.recs[h], -1);
    return k >= 0 && (size_t)k < s.paths.size() ? s.paths[k] : "?";
}

// The node's records except Header: 'level|name|type|' lines, with their values or without.
enum class NodeDump { Shape, Values };
inline std::vector<std::string> nodeDump(const oct::File& f, size_t node, NodeDump what)
{
    std::vector<std::string> v;
    for (size_t j = node + 1, e = f.blockEnd(node); j < e; ++j) {
        const oct::Rec& r = f.recs[j];
        if (f.name(r) == "Header") continue;
        std::string l = detail::recordHead(f, node, r);
        if (what == NodeDump::Values) l += detail::recordValue(f, r);
        v.push_back(l);
    }
    return v;
}

// The 'Input' DataNode record of a ClipProcessor / EventList, or -1.
inline long inputNode(const oct::File& f, const SubNet& s, size_t pn)
{
    long a = child(f, pn, "DataNodeAttributes"), r = child(f, pn, "DataNodeRefs");
    if (a < 0 || r < 0) return -1;
    auto at = f.nameValues(f.recs[a]);
    auto rf = f.ints(f.recs[r]);
    for (size_t k = 0; k < at.size() && k < rf.size(); ++k)
        if (at[k] == "Input") return rf[k] >= 0 && (size_t)rf[k] < s.dataNodes.size() ? (long)s.dataNodes[rf[k]] : -1;
    return -1;
}

// The float payload of record rec; 0 when rec is -1.
inline float floatAt(const oct::File& f, long rec)
{
    float x = 0;
    if (rec >= 0) memcpy(&x, f.recs[rec].pay.data(), sizeof x);
    return x;
}

}  // namespace octtest

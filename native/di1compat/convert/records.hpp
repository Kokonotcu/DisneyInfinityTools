// Record builders and pool edits for tuple files in 3.0's form (formats/oct.hpp). Data in, data out; any thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include "../base/bytes.hpp"
#include "../formats/oct.hpp"

namespace octx {

namespace detail {
// Lends f's name table to g while it lives, and takes it back even on an exception (editPoolChildren).
struct LentNames {
    oct::File& f;
    oct::File& g;
    LentNames(oct::File& from, oct::File& to) : f(from), g(to) { swap(); }
    ~LentNames() { swap(); }
    void swap()
    {
        f.names.swap(g.names);
        f.nameIndex.swap(g.nameIndex);
        std::swap(f.nameIndexed, g.nameIndexed);
    }
};

// Moves records [first, last) of from onto the end of out.
inline void moveAppend(std::vector<oct::Rec>& out, std::vector<oct::Rec>& from, size_t first, size_t last)
{
    out.insert(out.end(), std::make_move_iterator(from.begin() + first), std::make_move_iterator(from.begin() + last));
}
}  // namespace detail

// A byte scalar (blob) holding data; the count width is the narrowest that fits.
inline oct::Rec blobRec(oct::File& f, uint16_t level, const std::string& n, const std::string& data)
{
    const int cw = oct::countWidth(data.size());
    oct::Rec r{level, oct::makeType(oct::SCALAR, 0, oct::BYTE, cw), f.addName(n), {}};
    oct::File::put(r.pay, (uint32_t)data.size(), cw);
    r.pay += data;
    return r;
}

// A name list record's values replaced: it keeps its key, and its count width only grows.
inline void setNames(oct::File& f, oct::Rec& r, const std::vector<std::string>& v)
{
    oct::Spec s = oct::spec(r.type);
    int cw = s.cw < 1 ? 1 : s.cw;
    while (v.size() >= ((size_t)1 << (8 * cw))) ++cw;
    r.type = oct::makeType(oct::LIST, s.keyed, oct::NAME, cw);
    std::string pay = r.pay.substr(0, 2 * s.keyed);
    oct::File::put(pay, (uint32_t)v.size(), cw);
    for (const std::string& x : v) oct::File::put(pay, f.addName(x), 2);
    r.pay = pay;
}
inline oct::Rec nameList(oct::File& f, uint16_t level, const std::string& n, const std::vector<std::string>& v)
{
    oct::Rec r{level, oct::makeType(oct::LIST, 0, oct::NAME), f.addName(n), {}};
    setNames(f, r, v);
    return r;
}
// A float list with a u8 count: callers pass at most 16 floats (CO-31).
inline oct::Rec floatList(oct::File& f, uint16_t level, const std::string& n, const std::vector<float>& v)
{
    oct::Rec r{level, oct::makeType(oct::LIST, 0, oct::FLOAT), f.addName(n), {}};
    oct::File::put(r.pay, (uint32_t)v.size(), 1);
    r.pay.append((const char*)v.data(), v.size() * 4);
    return r;
}
// A keyed node: name n, key the decimal text of key (pool entries: DataNode = ['3']).
inline oct::Rec keyedNode(oct::File& f, uint16_t level, const std::string& n, int key)
{
    oct::Rec r{level, oct::makeType(oct::NODE, 1, 0), f.addName(n), {}};
    oct::File::put(r.pay, f.addName(std::to_string(key)), 2);
    return r;
}

// Edits a top-level pool's children one at a time, each alone in a File, then splices the pool once (C8).
template <class Edit>
inline void editPoolChildren(oct::File& f, long pool, const std::vector<size_t>& kids,
                             const std::vector<size_t>& order, Edit edit)
{
    // kids: the children's record indices in file order; edit(g, k) runs for each k of order (no duplicates)
    if (order.empty()) return;
    std::vector<size_t> ends(kids.size());
    for (size_t k = 0; k < kids.size(); ++k) ends[k] = f.blockEnd(kids[k]);
    const size_t poolEnd = f.blockEnd(pool);
    std::vector<std::vector<oct::Rec>> edited(kids.size());
    std::vector<char> done(kids.size(), 0);
    {
        oct::File g;
        // edit sees only g, which holds f's names: they are added in edit order (CO-02, CO-11)
        detail::LentNames lent(f, g);
        for (size_t k : order) {
            detail::moveAppend(g.recs, f.recs, kids[k], ends[k]);
            edit(g, k);
            edited[k].swap(g.recs);
            done[k] = 1;
        }
    }
    std::vector<oct::Rec> out;
    size_t grown = 0;
    for (size_t k = 0; k < kids.size(); ++k) grown += done[k] ? edited[k].size() : 0;
    out.reserve(f.recs.size() + grown);
    detail::moveAppend(out, f.recs, 0, pool + 1);
    size_t at = pool + 1;
    for (size_t k = 0; k < kids.size(); ++k) {
        detail::moveAppend(out, f.recs, at, kids[k]);
        if (done[k]) detail::moveAppend(out, edited[k], 0, edited[k].size());
        else detail::moveAppend(out, f.recs, kids[k], ends[k]);
        at = ends[k];
    }
    detail::moveAppend(out, f.recs, at, poolEnd);
    detail::moveAppend(out, f.recs, poolEnd, f.recs.size());
    f.recs.swap(out);
}

// A pool entry field for readEntryFields: a name (name0) into text, or an int (int0) into number.
template <class Out>
struct EntryField {
    const char* name;
    std::string Out::*text;
    int32_t Out::*number;
};

// Reads every record of entry's block named in fields, in record order: a repeated field's last value wins.
template <class Out, size_t N>
inline void readEntryFields(const oct::File& f, size_t entry, const EntryField<Out> (&fields)[N], Out& out)
{
    for (size_t j = entry + 1, end = f.blockEnd(entry); j < end; ++j) {
        const std::string& n = f.name(f.recs[j]);
        for (const EntryField<Out>& x : fields) {
            if (n != x.name) continue;
            if (x.text) out.*x.text = f.name0(f.recs[j]);
            else out.*x.number = f.int0(f.recs[j]);
        }
    }
}

// True when entry's block has a record called n.
inline bool hasField(const oct::File& f, size_t entry, const char* n)
{
    for (size_t j = entry + 1, end = f.blockEnd(entry); j < end; ++j)
        if (f.name(f.recs[j]) == n) return true;
    return false;
}

// Calls fn(NodeName record, SubNetworkRef) for every association of this Type.
template <class Fn>
inline void eachAssociation(oct::File& f, const std::string& type, Fn fn)
{
    long a = f.pool("AssociationPool");
    if (a < 0) return;
    for (size_t i = a + 1, end = f.blockEnd(a); i < end; i = f.blockEnd(i)) {
        std::string t;
        long nn = -1;
        int sn = -1;
        for (size_t j = i + 1, e = f.blockEnd(i); j < e; ++j) {
            const std::string& n = f.name(f.recs[j]);
            if (n == "Type") t = f.name0(f.recs[j]);
            else if (n == "NodeName") nn = (long)j;
            else if (n == "SubNetworkRef") sn = f.int0(f.recs[j]);
        }
        if (t == type && nn >= 0 && sn >= 0) fn(f.recs[nn], sn);
    }
}

}  // namespace octx

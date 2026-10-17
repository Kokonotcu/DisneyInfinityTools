// .bent 4.1 (1.0) -> 4.1f (3.0), in memory: 3.0's gate FUN_0076fb50 takes only 4.1f. formats/oct.hpp; any thread.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "../../formats/oct.hpp"

namespace di1::bent {

// dir 'characters/inc_bob' + '../biped/motions/X.banm' -> 'characters/biped/motions/x.banm' (3.0 writes lowercase)
inline std::string resolve(const std::string& dir, const std::string& rel);

namespace detail {

// 1.0: Version, Channels{Channel}, FileSets{FileSet{Files{File{Type, Filename, Bases{Base{..}}, ..}}}}, Layouts
inline constexpr uint16_t kFileLevel = 4;   // FileSets/FileSet/Files/File
inline constexpr uint16_t kBaseLevel = 6;   // File/Bases/Base

// One path's segments onto parts: '..' drops the last, '' and '.' are skipped.
inline void pushSegments(std::vector<std::string>& parts, const std::string& s)
{
    for (size_t a = 0; a <= s.size();) {
        size_t b = s.find_first_of("/\\", a);
        if (b == std::string::npos) b = s.size();
        const std::string seg = s.substr(a, b - a);
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!seg.empty() && seg != ".") parts.push_back(seg);
        a = b + 1;
    }
}

// Where the version, the channels and the files are: one Model file, the others by name (583/583 1.0 bents).
struct Layout {
    long version = -1, channels = -1, model = -1;
    std::vector<std::pair<std::string, size_t>> files;   // non-model File entries: name, record
};
// True when the File entry's Type is Model (the last Type wins).
inline bool isModelFile(const oct::File& f, size_t file)
{
    bool model = false;
    for (size_t j = file + 1, e = f.blockEnd(file); j < e; ++j)
        if (f.recs[j].level == kFileLevel + 1 && f.name(f.recs[j]) == "Type") model = f.name0(f.recs[j]) == "Model";
    return model;
}
inline Layout layout(const oct::File& f)
{
    Layout l;
    for (size_t i = 0; i < f.recs.size(); ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& n = f.name(r);
        if (r.level == 1 && n == "Version") l.version = (long)i;
        else if (r.level == 1 && n == "Channels") l.channels = (long)i;
        if (r.level != kFileLevel || n != "File") continue;
        if (isModelFile(f, i)) l.model = (long)i;
        else l.files.emplace_back(f.name0(r), i);
    }
    return l;
}

// The model's non-default Base connection maps by channel: 3.0 reads ConnectionMap per Channel (FUN_00787200).
inline std::map<std::string, std::string> channelMaps(const oct::File& f, long model)
{
    std::map<std::string, std::string> maps;
    for (size_t j = model + 1, end = f.blockEnd(model); j < end; ++j) {
        if (f.recs[j].level != kBaseLevel || f.name(f.recs[j]) != "Base") continue;
        std::string ch, cm;
        for (size_t k = j + 1, e = f.blockEnd(j); k < e; ++k) {
            const std::string& n = f.name(f.recs[k]);
            if (n == "Channel") ch = f.name0(f.recs[k]);
            else if (n == "ConnectionMap") cm = f.name0(f.recs[k]);
        }
        if (!ch.empty() && !cm.empty() && cm != "Anchor-Main") maps[ch] = cm;
    }
    return maps;
}

// The 4.1f records being written: 3.0 has Version, Model{Filename, Channels, Layers}, Files{File{..}}.
struct Writer {
    oct::File& f;
    std::string dir;   // the .bent's folder: 1.0 Filenames are relative to it
    std::vector<oct::Rec> out;

    // records [from, to) copied with their level moved by shift
    void copy(size_t from, size_t to, int shift)
    {
        for (size_t k = from; k < to; ++k) {
            oct::Rec r = f.recs[k];
            r.level = (uint16_t)(r.level + shift);
            out.push_back(r);
        }
    }
    // a File's children: Filename as a full path, Type and Bases dropped, the rest as they are
    void children(size_t file, int shift)
    {
        for (size_t k = file + 1; k < f.blockEnd(file); k = f.blockEnd(k)) {
            const oct::Rec& r = f.recs[k];
            const std::string& n = f.name(r);
            if (n == "Type" || n == "Bases") continue;
            if (n != "Filename") {
                copy(k, f.blockEnd(k), shift);
                continue;
            }
            out.push_back(f.nameScalar((uint16_t)(r.level + shift), r.name, resolve(dir, f.name0(r))));
        }
    }
    // Channels after the model's Filename, as 3.0 writes them; each takes its Base's ConnectionMap
    void channels(size_t at, size_t modelAt, const std::map<std::string, std::string>& maps)
    {
        std::vector<oct::Rec> rest(out.begin() + modelAt + 1, out.end());
        out.resize(modelAt + 1);
        out.push_back(f.recs[at]);
        out.back().level = 2;
        for (size_t k = at + 1; k < f.blockEnd(at); k = f.blockEnd(k)) {
            copy(k, f.blockEnd(k), 1);
            auto cm = maps.find(f.name0(f.recs[k]));
            if (cm == maps.end()) continue;
            out.push_back(f.nameScalar((uint16_t)(f.recs[k].level + 2), f.addName("ConnectionMap"), cm->second));
        }
        out.insert(out.end(), rest.begin(), rest.end());
    }
    // Files in ASCII name order (400/400 shipped); 3.0 tells blended files by '.banm' (FUN_00787200)
    void files(std::vector<std::pair<std::string, size_t>> list, uint16_t fileLevel)
    {
        out.push_back(f.node(1, "Files"));
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [name, at] : list) {
            out.push_back(f.recs[at]);
            out.back().level = 2;
            children(at, 2 - fileLevel);
        }
    }
};

inline bool fail(std::string* why, const char* reason)
{
    if (why) *why = reason;
    return false;
}
}  // namespace detail

inline std::string resolve(const std::string& dir, const std::string& rel)
{
    std::vector<std::string> parts;
    detail::pushSegments(parts, dir);
    detail::pushSegments(parts, rel);
    std::string out;
    for (const std::string& p : parts) out += (out.empty() ? "" : "/") + p;
    for (char& c : out) c = (char)tolower((unsigned char)c);
    return out;
}

// path: the engine path of the .bent ('characters/inc_bob/inc_bob_ps.bent'). False (f unchanged) if it is not 4.1.
inline bool transcode(oct::File& f, const std::string& path, std::string* why = nullptr)
{
    const detail::Layout l = detail::layout(f);
    const bool v41 = l.version >= 0 && f.nameValues(f.recs[l.version]) == std::vector<std::string>{"4.1"};
    if (!v41) return detail::fail(why, "not 4.1");
    if (l.model < 0) return detail::fail(why, "no Model file");
    const size_t slash = path.find_last_of('/');
    detail::Writer w{f, path.substr(0, slash == std::string::npos ? 0 : slash), {}};
    const std::map<std::string, std::string> maps = detail::channelMaps(f, l.model);
    const uint16_t fileLevel = f.recs[l.model].level;
    w.out.push_back(f.recs[0]);
    w.out.push_back(f.nameScalar(1, f.recs[l.version].name, "4.1f"));
    w.out.push_back(f.node(1, "Model"));
    const size_t modelAt = w.out.size();
    w.children(l.model, 2 - (fileLevel + 1));
    if (l.channels >= 0) w.channels(l.channels, modelAt, maps);
    if (!l.files.empty()) w.files(l.files, fileLevel);
    f.recs = std::move(w.out);
    return true;
}

}  // namespace di1::bent

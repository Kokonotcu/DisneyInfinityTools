// .bent 4.1 (1.0) -> 4.1f (3.0), in memory (plan NPC-4). 3.0's gate FUN_0076fb50 rejects any Version but "4.1f"
// (in-game 2026-10-12: the 1.0 playset avatars inc_*_ps.bent -> "Missing Disney Infinity Figure").
//   1.0: Version, DefaultInstanceName, SceneFilename, DefaultLayout, Channels{Channel},
//        FileSets{FileSet{Files{File{Type, Filename (relative to the .bent's folder), Bases{Base{Anchor, ConnectionMap,
//        Channel}}, Layers, MetaData}}}}, Layouts{Layout{Instances{Instance{FileSet, File}}}}
//   3.0: Version, Model{Filename (full path), Channels{Channel{..., ConnectionMap}}, Layers},
//        Files{File{Filename (full path), Layers, MetaData}} in ASCII name order (400/400 shipped checked)
// Over all 583 1.0 character .bent (bentvals.py): one Model file, one layout with one instance (the model), no File
// name twice; Type follows the extension (Model .oct, Blended .banm, clips .oct without Type), and 3.0 tells blended
// files by the '.banm' extension (FUN_00787200). Bases are Anchor / Anchor-Main except 4 props; 3.0 reads
// ConnectionMap per Channel (FUN_00787200, absent = default), so a non-default Base map goes onto its channel.
// DefaultInstanceName / SceneFilename / DefaultLayout / Layouts have no reader in the 3.0 exe.
#pragma once
#include <algorithm>
#include <map>
#include "formats/oct.hpp"

namespace bent {

// dir 'characters/inc_bob' + '../biped/motions/X.banm' -> 'characters/biped/motions/x.banm' (3.0 writes lowercase)
inline std::string resolve(const std::string& dir, const std::string& rel)
{
    std::vector<std::string> parts;
    auto push = [&](const std::string& s) {
        size_t a = 0;
        while (a <= s.size()) {
            size_t b = s.find_first_of("/\\", a);
            if (b == std::string::npos) b = s.size();
            std::string seg = s.substr(a, b - a);
            if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
            else if (!seg.empty() && seg != ".") parts.push_back(seg);
            a = b + 1;
        }
    };
    push(dir);
    push(rel);
    std::string out;
    for (auto& p : parts) out += (out.empty() ? "" : "/") + p;
    for (char& c : out) c = (char)tolower((unsigned char)c);
    return out;
}

// path: the engine path of the .bent ('characters/inc_bob/inc_bob_ps.bent'). False (f unchanged) if it is not 4.1.
inline bool transcode(oct::File& f, const std::string& path, std::string* why = nullptr)
{
    long version = -1, channels = -1, model = -1;
    std::vector<std::pair<std::string, size_t>> files;
    for (size_t i = 0; i < f.recs.size(); ++i) {
        const oct::Rec& r = f.recs[i];
        const std::string& n = f.name(r);
        if (r.level == 1 && n == "Version") version = (long)i;
        else if (r.level == 1 && n == "Channels") channels = (long)i;
        else if (r.level == 4 && n == "File") {   // FileSets/FileSet/Files/File
            bool isModel = false;
            for (size_t j = i + 1; j < f.blockEnd(i); ++j)
                if (f.recs[j].level == 5 && f.name(f.recs[j]) == "Type") isModel = f.name0(f.recs[j]) == "Model";
            if (isModel) model = (long)i;
            else files.emplace_back(f.name0(r), i);
        }
    }
    if (version < 0 || f.nameValues(f.recs[version]) != std::vector<std::string>{"4.1"}) { if (why) *why = "not 4.1"; return false; }
    if (model < 0) { if (why) *why = "no Model file"; return false; }
    std::string dir = path.substr(0, path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/'));

    // the model's Base connection maps, by channel, where not the default
    std::map<std::string, std::string> cmaps;
    for (size_t j = model + 1; j < f.blockEnd(model); ++j) {
        if (f.recs[j].level != 6 || f.name(f.recs[j]) != "Base") continue;
        std::string ch, cm;
        for (size_t k = j + 1; k < f.blockEnd(j); ++k) {
            const std::string& n = f.name(f.recs[k]);
            if (n == "Channel") ch = f.name0(f.recs[k]);
            else if (n == "ConnectionMap") cm = f.name0(f.recs[k]);
        }
        if (!ch.empty() && !cm.empty() && cm != "Anchor-Main") cmaps[ch] = cm;
    }

    std::vector<oct::Rec> out;
    auto copy = [&](size_t from, size_t to, int shift) {
        for (size_t k = from; k < to; ++k) { oct::Rec r = f.recs[k]; r.level = (uint16_t)(r.level + shift); out.push_back(r); }
    };
    // a File's children: Filename resolved, Type and Bases dropped, the rest as they are
    auto children = [&](size_t file, int shift) {
        for (size_t k = file + 1; k < f.blockEnd(file); k = f.blockEnd(k)) {
            const oct::Rec& r = f.recs[k];
            const std::string& n = f.name(r);
            if (n == "Type" || n == "Bases") continue;
            if (n == "Filename") out.push_back(f.nameScalar((uint16_t)(r.level + shift), r.name, resolve(dir, f.name0(r))));
            else copy(k, f.blockEnd(k), shift);
        }
    };
    uint16_t fileLevel = f.recs[model].level;
    out.push_back(f.recs[0]);
    out.push_back(f.nameScalar(1, f.recs[version].name, "4.1f"));
    out.push_back(f.node(1, "Model"));
    size_t modelAt = out.size();
    children(model, 2 - (fileLevel + 1));
    if (channels >= 0) {   // after the model's Filename, as 3.0 writes it
        std::vector<oct::Rec> rest(out.begin() + modelAt + 1, out.end());
        out.resize(modelAt + 1);
        out.push_back(f.recs[channels]);
        out.back().level = 2;
        for (size_t k = channels + 1; k < f.blockEnd(channels); k = f.blockEnd(k)) {
            copy(k, f.blockEnd(k), 1);
            auto cm = cmaps.find(f.name0(f.recs[k]));
            if (cm != cmaps.end()) out.push_back(f.nameScalar((uint16_t)(f.recs[k].level + 2), f.addName("ConnectionMap"), cm->second));
        }
        out.insert(out.end(), rest.begin(), rest.end());
    }
    if (!files.empty()) {
        out.push_back(f.node(1, "Files"));
        std::stable_sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& [name, at] : files) {
            out.push_back(f.recs[at]);
            out.back().level = 2;
            children(at, 2 - fileLevel);
        }
    }
    f.recs = std::move(out);
    return true;
}

}  // namespace bent

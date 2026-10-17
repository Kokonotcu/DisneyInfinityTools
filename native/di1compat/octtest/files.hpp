// File reads and writes for octtest: whole files, 3.0 archive members, extract readers, pairs lists. Main thread.
#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>
#include "../core/readonly_file.hpp"
#include "../formats/arc30.hpp"

namespace octtest {

// A reader of engine paths, as the plugin's transcode steps take one.
using Read = std::function<bool(const std::string& path, std::string& out)>;

inline constexpr size_t kPrefix30 = 2;   // "3:", the plugin's mark on a 3.0 path

// The whole file, or "" when it cannot be read.
inline std::string slurp(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline void writeFile(const std::filesystem::path& p, const std::string& d)
{
    std::ofstream(p, std::ios::binary).write(d.data(), (std::streamsize)d.size());
}
inline void writeFile(const std::filesystem::path& p, const std::vector<uint8_t>& d)
{
    std::ofstream(p, std::ios::binary).write((const char*)d.data(), (std::streamsize)d.size());
}

// arc30::readMember of a member of the 3.0 archive file at path.
inline bool readArc30(const std::string& path, const std::string& member, std::string& out)
{
    core::ReadOnlyFile file(path);
    if (!file.open()) return false;
    const arc30::Source src{file.size(), [&file](uint64_t at, void* dst, size_t n) { return file.readAt(at, dst, n); }};
    return arc30::readMember(src, member, out);
}

// Reads engine paths below an extract's assets dir; false for a missing or empty file.
inline Read readerAt(const std::filesystem::path& assets)
{
    return [assets](const std::string& p, std::string& o) {
        o = slurp(assets / p);
        return !o.empty();
    };
}

// Reads the plugin's '3:<path>' below a 3.0 extract's assets dir.
inline Read reader30At(const std::filesystem::path& assets)
{
    return [assets](const std::string& p, std::string& o) {
        o = slurp(assets / p.substr(kPrefix30));
        return !o.empty();
    };
}

// An extract's vformat.vtxf: the engine's vertex formats.
inline std::string vformatFile(const std::filesystem::path& extract)
{
    return slurp(extract / "assets/shaders/shaders/vformat.vtxf");
}

// The lines of a pairs file ('<1.0 file>|<3.0 file>...') that have a '|'.
inline std::vector<std::string> pairLines(const char* path)
{
    std::ifstream in(path);
    std::vector<std::string> out;
    for (std::string line; std::getline(in, line);)
        if (line.find('|') != std::string::npos) out.push_back(line);
    return out;
}

}  // namespace octtest

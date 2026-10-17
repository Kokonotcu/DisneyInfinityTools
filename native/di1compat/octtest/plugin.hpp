// The plugin's 1.0 pipeline as octtest runs it: cfg defaults, extract paths, no game. Main thread.
#pragma once
#include <filesystem>
#include <string>
#include "../games/di1/options.hpp"
#include "../base/text.hpp"
#include "files.hpp"

namespace octtest {

// An extract file's engine path: what follows the last "/assets/", lower case, '/' separators.
inline std::string enginePath(const std::filesystem::path& p)
{
    constexpr char kAssets[] = "/assets/";
    const std::string s = text::lower(p.generic_string());
    size_t a = s.rfind(kAssets);
    return a == std::string::npos ? s : s.substr(a + sizeof(kAssets) - 1);
}

// The plugin's .oct steps at cfg defaults, without render, 3.0 install or surface table; transforms from this run.
inline di1::OctSteps pluginOctSteps(const octx::Options& transforms)
{
    di1::OctSteps o = di1::octStepsOf(di1::Config{});
    o.transforms = transforms;
    return o;
}

// One file through the plugin's transcode dispatch at cfg defaults, with no readers; "-" when 1.0's bytes are served.
inline std::string pluginTranscode(const std::filesystem::path& in, const octx::Options& transforms, std::string& out,
                                   std::string& note)
{
    std::string b = slurp(in), ep = enginePath(in);
    di1::TranscodeIO io;
    io.read10 = [](const std::string&, std::string&) { return false; };   // rules reading files beside it serve raw
    io.read30x = io.read10;
    io.read30 = io.read10;
    io.oct = pluginOctSteps(transforms);
    const char* codec = di1::transcode({ep.c_str(), b.data(), b.size()}, {out, note, nullptr},
                                       di1::transcodeOptionsOf(di1::Config{}), io);
    return codec ? codec : "-";
}

}  // namespace octtest

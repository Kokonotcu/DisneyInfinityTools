// One octtest run: its arguments, its environment switches, and the check tallies modes print. Main thread.
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include "../convert/records.hpp"

namespace octtest {

// The command line, and the .oct transform settings main reads from OCTX_SLACK.
struct Args {
    int argc = 0;
    char** argv = nullptr;
    octx::Options octx;
};

// A mode: its exit code.
using Mode = int (*)(const Args& a);

inline constexpr int kUsageError = 2;   // the exit code of a command line octtest cannot run

// Check name -> how often it held, printed in name order.
using Tally = std::map<std::string, size_t>;

// getenv: octtest reads only its own switches (OT-18), so MSVC's C4996 advice does not apply.
inline const char* env(const char* name)
{
#pragma warning(suppress : 4996)
    return getenv(name);
}

// One line per tally entry: the count right-aligned in width, then the name.
inline void printTally(const Tally& c, int width)
{
    for (auto& [k, v] : c) printf("%*zu %s\n", width, v, k.c_str());
}

// True when any of keys was tallied.
template <size_t N>
inline bool anyTallied(const Tally& c, const char* const (&keys)[N])
{
    for (const char* k : keys)
        if (c.count(k)) return true;
    return false;
}

}  // namespace octtest

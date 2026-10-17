// Old names for di1compat.cpp's transcode() (A-02, 263 lines: its call lines wait for that split).
#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include "formats/btd.hpp"
#include "games/di1/btd.hpp"

namespace btd {

using di1::btd::convert;

// blocks() by game number, as the old header had it: 1 is the 1.0 layout, anything else 3.0's.
inline std::vector<std::pair<size_t, size_t>> blocks(const std::string& b, int game)
{
    return blocks(b, game == 1 ? di1::btd::kLayout10 : kLayout30);
}

}  // namespace btd

// What a game's dialog script transcode and the dialogue bridge share: requests, stats, line sequences. std only.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::audio::dlgscript {

typedef std::unordered_map<uint32_t, std::string> EventNames;   // a name hash -> the FMOD project string

// One line of a sequence: speaker prefix, line number, its FMOD event, delay ms.
struct DlgSeqItem {
    uint16_t prefix, line;
    std::string event;
    uint32_t delay;
};
// Type 4 times each delay from the previous line's start, type 3 from its end (1.0 FUN_00715fc0).
struct DlgSeq {
    uint8_t type;
    std::vector<DlgSeqItem> items;
};
typedef std::unordered_map<uint32_t, DlgSeq> SeqMap;               // by the entry's name hash
typedef std::unordered_map<uint32_t, std::string> LineEventMap;    // dlgLineKey -> its line class's event

struct DlgStats {
    int classes = 0, lines = 0, named = 0, ranges = 0, events = 0, unresolved = 0, dupLines = 0, kept30 = 0,
        lines30 = 0;
};

// A line's key: the prefix's two letters upper-cased, and its number.
inline uint32_t dlgLineKey(uint32_t pfx, uint16_t num)
{
    constexpr uint32_t kUpper = 0xdfdf;   // clears ASCII bit 5 of both letters (FUN_0182bc20's key)
    return (pfx & kUpper) << 16 | num;
}

// A script to transcode. seqs and lineEvents are written as entries parse, even when a later one fails.
struct ScriptRequest {
    const std::vector<uint8_t>& source;
    const EventNames& evNames;
    const std::vector<uint8_t>* base = nullptr;   // the game's own script of the same name, if any
    SeqMap* seqs = nullptr;
    LineEventMap* lineEvents = nullptr;
};

// false: out and st untouched, err says why.
typedef std::function<bool(const ScriptRequest& r, std::vector<uint8_t>& out, DlgStats* st, std::string* err)>
    Transcode;

}  // namespace engine::audio::dlgscript

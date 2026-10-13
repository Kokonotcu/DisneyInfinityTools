// 1.0 mission data in 3.0 terms, at load time (plan MIS-*; notes _research\impl-mis.md).
// Presentation events. Both engines key a presentation list (mission .lua Presentations[].Data.Event, the
// MissionPresentationList node's 'Event' parameter) by an int of the mission event enum, and the enum changed:
//   1.0 FUN_00fa7220 (presentationData name -> int): Available 0, Activate 1, Update 2, Encounter 3, Generic 4,
//       Success 5, Fail 6, Abort 7, Requirement 8, CelebrationAudio 9, FailedAudio 10, ForceActivate 11,
//       CheatActivate 12, CheatSuccess 13
//   3.0 FUN_015e5dc0 / FUN_0157c3b0: Available 0, Intro 1, Activate 2, Update 3, Encounter 4, Generic 5, Interact 6,
//       Success 7, Fail 8, Abort 9, Complete 10, Requirement 11, CelebrationAudio 12, FailedAudio 13,
//       ForceActivate 14, CheatActivate 15, CheatSuccess 16
// 3.0 reads the int as it is (Lua: FUN_01592720; node: FUN_01747d50 -> int getter FUN_006b2700). Corpus, 1.0
// Incredibles: .lua 1 x288, 5 x281, 6 x60, 0 x11, 7 x6 (= Activate/Success/Fail/Available/Abort); every one of the
// 646 'Event' keys sits in Mission.Presentations[].Data, one per 'Event = N,' line. Read raw, 3.0 took them as
// Intro/Generic/Interact: a mission's Activate list (DockFire's spawns, triggers, set-piece caches) never fired.
#pragma once
#include <string>

namespace mission {

inline int presentEvent30(int e10)
{
    static const int map[] = {0, 2, 3, 4, 5, 7, 8, 9, 11, 12, 13, 14, 15, 16};
    return e10 >= 0 && e10 < (int)(sizeof(map) / sizeof(map[0])) ? map[e10] : e10;
}

// A mission .lua (text): every 'Event = N,' line remapped. Returns the lines changed.
inline int luaEvents30(std::string& t)
{
    static const std::string key = "Event = ";
    int n = 0;
    for (size_t p = t.find(key); p != std::string::npos; p = t.find(key, p + 1)) {
        size_t ls = t.rfind('\n', p);
        ls = ls == std::string::npos ? 0 : ls + 1;
        if (t.find_first_not_of(" \t", ls) != p) continue;   // the key opens its line
        size_t q = p + key.size(), e = q;
        while (e < t.size() && isdigit((unsigned char)t[e])) ++e;
        if (e == q || e >= t.size() || t[e] != ',') continue;
        std::string v = std::to_string(presentEvent30(atoi(t.c_str() + q)));
        t.replace(q, e - q, v);
        ++n;
    }
    return n;
}

}  // namespace mission

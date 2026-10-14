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
#include <cstdint>
#include <string>

namespace mission {

// Meridian classes 1.0 graphs use that 3.0 no longer registers, but whose 1.0 registration template 3.0 still has
// under another name: registered again with that 3.0 template after the class registry is built (FUN_0178e330),
// and transcoded with the 3.0 twin's pin/parameter table. 1.0 FUN_01098250 registers
// MissionFocusPoint_Rule_ProximityCheck_ActorToLocator and MissionTask_ProximityCheck_ActorToLocator with the same
// template FUN_0108ce60; 3.0 FUN_0178e330 has the MissionTask one (template FUN_0177a8b0, cdecl(name)) and not the
// rule. Pins match by name (1.0 ObjectActorList..ObjectUndelivered = the 3.0 twin's). 6 nodes / 3 missions
// (hotdogsforall, gpmayordelivery2, activity deliverblockheadcarbside01).
struct Alias { const char* name; uint32_t templateVa; const char* twin; };
static const Alias kAliases[] = {
    {"MissionFocusPoint_Rule_ProximityCheck_ActorToLocator", 0x0177A8B0, "MissionTask_ProximityCheck_ActorToLocator"},
};

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

// MissionBehavior in a mission line (<line>__missiongraph.lua). 1.0 knew 0 Activity, 1 Alert, else 'Mission' (1.0
// 0x010284c0), and its progress log listed every 'Mission' record (1.0 missionlogdatahelpers ActiveMissions /
// CompletedMissions; FUN_00f8df90 only drops copies with m_playerGroupIndex +0x3c >= 1). 3.0 names 2 'Mission', 3
// 'Story Mission', 4 'Side Mission' (FUN_01678ef0) and its MissionLog lists only 3 and 4 (3.0 missionlogdatahelpers
// PlaysetSortedMissions; FUN_015ac790 keeps the same +0x3c filter); 3/4 also pick the giver tags
// MissionGivers_*_Story/_Side (FUN_015d22b0) and isStoryMission/isSideMission of MissionTaskNotify (FUN_015bfd10).
// Corpus: every 1.0 Incredibles record is 0, 1 or 2; 3.0 ships its story/side missions as 3 (125) / 4 (143).
// So 2 -> 3 on a story line, 4 on any other. Returns the records changed.
// Not the records 1.0's log never listed as 'Mission' (its builder FUN_01028180): GoldStarContext 1 (+0x160 = 2,
// Type 'GoldStar'; 1.0 FUN_01030a80, 3.0 FUN_016748d0 ctx[0x7f] = 2 -> FUN_01678ef0 'GoldStar') and MissionType
// EvalTrigger (skipped by 1.0 FUN_01028180 and 3.0 FUN_0167df10). 3.0 data has 0 records of either kind, so as 3/4
// they would only reach the story/side paths (guidance FUN_015cc820, celebration FUN_015bd040, beacon FUN_015c30a0).
// IN: 13 GoldStar + 4 EvalTrigger records stay 2.
inline int behaviour30(std::string& t)
{
    static const std::string key = "\t\t\t\tMissionBehavior = 2,\n", line = "\t\t\tline = \"";
    int n = 0;
    for (size_t p = t.find(key); p != std::string::npos; p = t.find(key, p + 1)) {
        size_t next = t.find("\n\t\t{\n", p), l = t.find(line, p);   // this record's line = "...", before the next record
        if (l == std::string::npos || (next != std::string::npos && l > next)) continue;
        size_t rs = t.rfind("\n\t\t{\n", p), end = next == std::string::npos ? t.size() : next;
        auto has = [&](const char* s) { size_t h = t.find(s, rs == std::string::npos ? 0 : rs); return h < end; };
        if (has("\t\t\t\tGoldStarContext = 1,\n") || has("\t\t\t\tMissionType = \"EvalTrigger\",\n")) continue;
        size_t q = l + line.size(), e = t.find('"', q);
        if (e == std::string::npos) break;
        std::string ln = t.substr(q, e - q);
        for (char& c : ln) c = (char)tolower((unsigned char)c);
        t[p + key.size() - 3] = ln.find("story") != std::string::npos ? '3' : '4';
        ++n;
    }
    return n;
}

}  // namespace mission

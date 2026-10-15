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
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

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

// The mission's objective line (Mission Log 'Mission Objectives'). 1.0's builder FUN_00f97b60: a mission whose
// Parms.UseMissionGeneratedProgressStringNew is true (FUN_00fa4100 -> +0x2a8 bit 1) lists one ACTIVE entry,
// Localize(key, 1, 0) with key = +0x168 or, when that is empty, "<MissionName>_Desc" (+0x168 is "" from the ctor
// FUN_00fa3210 on and nothing else writes it); else it asks the eval tree (+0x1dc). 3.0's builder FUN_015cff90 lists
// the same one ACTIVE entry (ctor FUN_015ca690), Localize(+0x350, 1, 0), when +0x350 is set, else asks the eval tree
// (+0x248); its mission loader FUN_015bddb0 sets +0x350 = "<MissionName>_Desc" from Parms.ProgressString =
// "<default>". 3.0 has no UseMissionGeneratedProgressStringNew, so a 1.0 mission fell to the eval tree, whose 1.0
// evals 3.0 hides (eval loader FUN_01722b70: ProgressString absent or "" -> HideProgress): 0 entries, an empty pane.
// So a Parms block with the 1.0 flag and no ProgressString gets ProgressString = "<default>". Both Localize
// (1.0 FUN_0067f4b0, 3.0 FUN_006952d0) return the key itself on a miss. Corpus: every 1.0 mission but 3 Toy Story
// ones (buildalot, customizealiens, powertower: flag false, eval tree in 1.0 too). Returns 1 if changed.
// The two logs show it differently: 1.0's progresslog.lua listed ProgressData only for HasTasks (= flag false;
// GetMissionObjectives) and showed "<MissionName>_Desc" as the details text (GetMissionDetails); 3.0's
// missiondetailspanel.lua lists every non-empty ProgressString under Mission Objectives and shows the same _Desc key
// as the description except on an AVAILABLE mission (missionlog.lua SelectedItemChanged: HideDescription).
// cfg progress10.
inline int progress30(std::string& t)
{
    size_t p = t.find("\n\tParms = "), e = p == std::string::npos ? p : t.find("\n\t}", p);
    if (e == std::string::npos) return 0;
    size_t f = t.find("\n\t\tUseMissionGeneratedProgressStringNew = true,", p);
    if (f > e || t.find("\n\t\tProgressString = ", p) < e) return 0;
    t.insert(f, "\n\t\tProgressString = \"<default>\",");
    return 1;
}

// MissionBehavior in a mission line (<line>__missiongraph.lua). 1.0 knew 0 Activity, 1 Alert, 3 Adventure, else
// 'Mission' (1.0 FUN_01028180, 0x010284c0; IN uses 0/1/2 only), and its progress log listed every 'Mission' record (1.0 missionlogdatahelpers ActiveMissions /
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

// beacon10: PlaysetProperties.<playset>.MissionParams.BeaconMap rows (zonelist.lua; the Incredibles record is 1.0's,
// verbatim through zonereg). Both exes keep one 16-byte entry per row {name, cho, placement, float} and spawn mission
// / focus-point beacons from it (3.0 lookup FUN_01591570 -> FUN_015cce20 givers, FUN_015c30a0 focus points), but the
// row layout changed:
//   1.0 FUN_00fa1b10: [1] name, [2] cho, [3] placement, [4] float only if a number (entry ctor FUN_00fa11a0: 0)
//   3.0 FUN_015e2b40: [1] name, Chos[1] cho, [2] placement, [3] float (default -1)
// Read raw, every IN entry had no cho (3.0 reads no Chos table) and the cho name as its placement: no focus-point or
// giver beacon spawned. A row without Chos whose array is 3 strings (+ a number) becomes
// { name, placement, float (0 when 1.0 gave none), Chos = { cho } }. 3.0 also resolves '<default>' on a side
// mission (behaviour 4) to 'Beacon_Arrow_Blue' (FUN_015c30a0 0x015C33DB); 1.0 resolved it to FocusPointDefault for every
// mission, so a converted map without that name gets it with FocusPointDefault's beacon. Rows already in 3.0 form
// (every 3.0 playset's) are untouched. Returns the rows converted; *aliases = rows added.
inline int beaconMap30(std::string& t, int* aliases = nullptr)
{
    auto skipQuoted = [&](size_t i) {   // i at a quote -> index after the closing one
        char q = t[i];
        for (++i; i < t.size() && t[i] != q; ++i) if (t[i] == '\\') ++i;
        return i < t.size() ? i + 1 : t.size();
    };
    auto closeOf = [&](size_t open) -> size_t {   // matching '}' of the '{' at open, strings and comments skipped
        int d = 0;
        for (size_t i = open; i < t.size();) {
            char c = t[i];
            if (c == '"' || c == '\'') { i = skipQuoted(i); continue; }
            if (c == '-' && i + 1 < t.size() && t[i + 1] == '-') { size_t e = t.find('\n', i); i = e == std::string::npos ? t.size() : e; continue; }
            if (c == '{') ++d;
            else if (c == '}' && --d == 0) return i;
            ++i;
        }
        return std::string::npos;
    };
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    auto isStr = [](const std::string& e) { return e.size() >= 2 && (e[0] == '"' || e[0] == '\'') && e.back() == e[0]; };
    auto isNum = [](const std::string& e) { char* end = nullptr; strtod(e.c_str(), &end); return !e.empty() && end && !*end; };
    struct Edit { size_t at, len; std::string text; };
    std::vector<Edit> edits;
    int n = 0, na = 0;
    static const std::string key = "BeaconMap";
    for (size_t p = t.find(key); p != std::string::npos; p = t.find(key, p + key.size())) {
        if (p && (isalnum((unsigned char)t[p - 1]) || t[p - 1] == '_')) continue;
        size_t q = p + key.size();
        if (q < t.size() && (isalnum((unsigned char)t[q]) || t[q] == '_')) continue;
        q = t.find_first_not_of(" \t\r\n", q);
        if (q == std::string::npos || t[q] != '=' || (q + 1 < t.size() && t[q + 1] == '=')) continue;
        size_t open = t.find_first_not_of(" \t\r\n", q + 1), close = open == std::string::npos || t[open] != '{' ? std::string::npos : closeOf(open);
        if (close == std::string::npos) continue;
        int converted = 0;
        bool hasBlue = false;
        std::string fpd;   // FocusPointDefault's converted tail: , placement, float, Chos = { cho, }
        for (size_t i = open + 1; i < close;) {
            char c = t[i];
            if (c == '-' && i + 1 < t.size() && t[i + 1] == '-') { size_t e = t.find('\n', i); i = e == std::string::npos ? close : e; continue; }
            if (c != '{') { ++i; continue; }
            size_t re = closeOf(i);
            if (re == std::string::npos || re > close) break;
            std::string body = t.substr(i + 1, re - i - 1);
            std::vector<std::string> el;
            bool plain = true;
            size_t s = 0;
            for (size_t k = 0; k <= body.size(); ++k) {
                if (k < body.size() && (body[k] == '"' || body[k] == '\'')) {
                    char qc = body[k];
                    for (++k; k < body.size() && body[k] != qc; ++k) if (body[k] == '\\') ++k;
                    continue;
                }
                if (k < body.size() && (body[k] == '{' || body[k] == '=')) { plain = false; break; }
                if (k == body.size() || body[k] == ',') { el.push_back(trim(body.substr(s, k - s))); s = k + 1; }
            }
            if (!el.empty() && el.back().empty()) el.pop_back();
            std::string name = !el.empty() && isStr(el[0]) ? el[0].substr(1, el[0].size() - 2) : "";
            if (name == "Beacon_Arrow_Blue") hasBlue = true;
            if (plain && (el.size() == 3 || el.size() == 4) && isStr(el[0]) && isStr(el[1]) && isStr(el[2]) && (el.size() == 3 || isNum(el[3]))) {
                std::string tail = ", " + el[2] + ", " + (el.size() == 4 ? el[3] : std::string("0")) + ", Chos = { " + el[1] + ", }, ";
                edits.push_back({i, re + 1 - i, "{ " + el[0] + tail + "}"});
                if (name == "FocusPointDefault") fpd = tail;
                ++converted;
            }
            i = re + 1;
        }
        if (converted && !hasBlue && !fpd.empty()) {
            size_t last = t.find_last_not_of(" \t\r\n", close - 1);
            std::string sep = last != std::string::npos && t[last] != ',' && t[last] != '{' ? ", " : " ";
            edits.push_back({close, 0, sep + "{ \"Beacon_Arrow_Blue\"" + fpd + "}, "});
            ++na;
        }
        n += converted;
    }
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.at > b.at; });
    for (auto& e : edits) t.replace(e.at, e.len, e.text);
    if (aliases) *aliases = na;
    return n;
}

}  // namespace mission

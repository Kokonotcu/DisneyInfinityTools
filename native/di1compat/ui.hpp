// UI, HUD, text and FMV (track ui): the 1.0 rules behind what the player sees, as engine-free functions (octtest
// --ui checks them); the hooks and serve gates that apply them are in di1compat.cpp (cfg loadscreen10, uiscope10,
// fmv10, text10, tips10).
#pragma once
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ui {

// The loading screen 1.0 showed for a zone whose LoadingScreen (1.0 key) is key10 (dump10 FUN_00b4e8c0): key10 only
// when it holds "Intro", else LoadingScreen_%.2d from the profile stat LastLoadingScreen, which gains 1 per load
// (FUN_00675ef0; a new stat starts at the increment, 1) and wraps past 19 to 1 (cmp esi,0x13 at 0x00b4e98d). 1.0
// ships loadingscreen_01..19 (flash/loadingscreen/loadingscreen.zip); its loadingscreen_inc.gfx was never picked
// on PC. counter = the stat (0: not created yet).
inline std::string loadingScreen10(const std::string& key10, int& counter)
{
    if (key10.find("Intro") != std::string::npos) return key10;
    if (++counter > 19) counter = 1;
    char b[24];
    snprintf(b, sizeof(b), "LoadingScreen_%.2d", counter);
    return b;
}

// p (normalised: lower case, '/') = flash/<dir>/<movie>_i<hex>.mtb -> "flash/<dir>/<movie>", else "". 3.0's movie
// loader also asks <movie>_i<hex id> for character ids that are no external image: 3.0's missioncomplete.gfx has
// none (DefineExternalImage2 count 0) yet asked missioncomplete_i5.mtb (in-game 2026-10-13) and got 1.0's.
inline std::string flashImageMovie(const std::string& p)
{
    if (p.rfind("flash/", 0) != 0 || p.size() < 12 || p.compare(p.size() - 4, 4, ".mtb")) return "";
    size_t i = p.rfind("_i"), e = p.size() - 4;
    if (i == std::string::npos || i + 2 >= e || p.find('/', i) != std::string::npos) return "";
    for (size_t k = i + 2; k < e; ++k)
        if (!isxdigit((unsigned char)p[k])) return "";
    return p.substr(0, i);
}

// p = flash/<dir>/<movie>.gfx or .gfxmanifest -> "flash/<dir>/<movie>", else "".
inline std::string flashMovie(const std::string& p)
{
    if (p.rfind("flash/", 0) != 0) return "";
    for (const char* x : {".gfx", ".gfxmanifest"}) {
        size_t n = strlen(x);
        if (p.size() > n && !p.compare(p.size() - n, n, x)) return p.substr(0, p.size() - n);
    }
    return "";
}

// The clip the 3.0 FMV player resolves (FUN_0067e460(out, size, name): "FMV\%s.bik"; it asks "PC1\X" and "X") ->
// "X", or "" for a stream URL. 1.0 keeps its clips loose under assets\fmv\PC1 (9 IN_COM_*, the HOH__Info_* Hall of
// Heroes clips, cinematics); its assets\fmv root holds only the 1.0 front-end videos (AvLogo, Copyright, DIS,
// EscLogo, FE_Attract, LOGO_HI_Wiu), never a fallback: 3.0 asks for its own logos by the same names at boot.
inline std::string fmvClip(const char* name)
{
    std::string n = name ? name : "";
    if (n.compare(0, 4, "http") == 0 || n.compare(0, 4, "HTTP") == 0) return "";
    size_t s = n.find_last_of("\\/");
    if (s != std::string::npos) n.erase(0, s + 1);
    if (n.size() > 4 && (n.compare(n.size() - 4, 4, ".bik") == 0 || n.compare(n.size() - 4, 4, ".BIK") == 0)) n.resize(n.size() - 4);
    return n;
}

// ---- data/tips.lua: TipCounts{category = n} and ZoneTipCategories{zone or playset = {categories}} ----
// Both exes pick loading-screen tips the same way (3.0 FUN_00b70da0 = 1.0 FUN_00b4af50, 3.0 adds UIPlaysetFilter):
// ZoneTipCategories[zone] or [playset] (lower case), weights TipCounts, text "Load_Tips_%s_%.2d". 3.0's table has no
// 'incredibles' and no Incredibles category, so IN_City got none of the 28 Incredibles tips.
struct LuaEntry { std::string key, text; };   // one top-level entry of a table, its lines verbatim
// The entries of the top-level table `name` in t (the shipped files' layout: "name = \n{\n", entries indented one tab,
// "}" closes). [b, e) = the body's line range in t. false if absent.
inline bool luaTable(const std::string& t, const char* name, std::vector<LuaEntry>& out, size_t& b, size_t& e)
{
    std::string head = std::string(name) + " = ";
    size_t h = t.rfind("\n" + head, std::string::npos), at = t.compare(0, head.size(), head) == 0 ? 0 : h == std::string::npos ? h : h + 1;
    if (at == std::string::npos) return false;
    size_t o = t.find("\n{", at);
    if (o == std::string::npos) return false;
    b = t.find('\n', o + 1);
    if (b == std::string::npos) return false;
    ++b;
    out.clear();
    for (size_t p = b; p < t.size();) {
        size_t nl = t.find('\n', p);
        size_t end = nl == std::string::npos ? t.size() : nl + 1;
        if (t[p] == '}') { e = p; return true; }
        bool top = t[p] == '\t' && p + 1 < t.size() && t[p + 1] != '\t' && t[p + 1] != '{' && t[p + 1] != '}';
        if (top) {
            std::string k = t.substr(p + 1, t.find('=', p) - p - 1);
            while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
            if (k.size() > 4 && k[0] == '[' && k[1] == '"') k = k.substr(2, k.size() - 4);
            out.push_back({k, ""});
        }
        if (!out.empty()) out.back().text.append(t, p, end - p);
        p = end;
    }
    return false;
}

// t30 with the entries of the 1.0 table t10 that t30 lacks (by key) added verbatim to TipCounts and
// ZoneTipCategories; a ZoneTipCategories key that names a registered zone (zone(key) true) stays 3.0's: of the 55
// 1.0-only keys only 'rumpusroom' does (3.0's RumpusRoom). -> entries added, -1 if either file lacks the tables.
template <class IsZone>
inline int mergeTips(std::string& t30, const std::string& t10, IsZone zone)
{
    int added = 0;
    for (const char* name : {"TipCounts", "ZoneTipCategories"}) {
        std::vector<LuaEntry> a, b;
        size_t ab, ae, bb, be;
        if (!luaTable(t30, name, a, ab, ae) || !luaTable(t10, name, b, bb, be)) return -1;
        bool zones = name[0] == 'Z';
        std::string add;
        for (auto& x : b) {
            bool have = zones && zone(x.key);
            for (auto& y : a) have |= y.key == x.key;
            if (have) continue;
            std::string s = x.text;
            size_t z = s.find_last_not_of("\r\n\t ");
            std::string eol = s.find("\r\n") != std::string::npos ? "\r\n" : "\n";
            s.resize(z + 1);
            if (s.back() != ',') s += ',';
            add += s + eol;
            ++added;
        }
        // the last 3.0 entry ends with ',' in every shipped tips.lua; one without gets it
        size_t last = t30.find_last_not_of("\r\n\t ", ae - 1);
        if (!add.empty() && last != std::string::npos && last >= ab && t30[last] != ',') { t30.insert(last + 1, ","); ++ae; }
        t30.insert(ae, add);
    }
    return added;
}

}  // namespace ui

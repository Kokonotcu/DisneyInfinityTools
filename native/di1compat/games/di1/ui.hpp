// 1.0's UI rules: loading screens, Flash and FMV names, context lists, tips, the Play Sets menu. base/lua_lex.
// Any thread.
#pragma once
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "../../base/lua_lex.hpp"

namespace di1::ui {

// The screen 1.0 picks for a zone whose LoadingScreen is key10 (FUN_00b4e8c0): key10 if it holds Intro, else rotating.
inline std::string loadingScreen10(const std::string& key10, int& counter);
// p (normalised) = flash/<dir>/<movie>_i<hex>.mtb -> "flash/<dir>/<movie>", else "".
inline std::string flashImageMovie(const std::string& p);
// p = flash/<dir>/<movie>.gfx or .gfxmanifest -> "flash/<dir>/<movie>", else "".
inline std::string flashMovie(const std::string& p);
// The clip FUN_0067e460 resolves ("FMV\%s.bik"; asked as "PC1\X" and "X") -> "X"; "" for a stream URL.
inline std::string fmvClip(const char* name);
// A ControllerContextNode list (FUN_01224880) keeping each 3.0 context that gates what a kept 1.0 one gates; "" same.
inline std::string contextList10(const std::string& s);
// t30 gains the TipCounts / ZoneTipCategories entries of t10 it lacks; zone(key) keys stay 3.0's. -> added, -1
template <class IsZone>
inline int mergeTips(std::string& t30, const std::string& t10, IsZone zone);
// presentation/playsetlistwin32.lua as Lua text: 3.0's chunk unchanged plus an entry per {name, start zone}.
inline std::string playsetMenu10(const std::string& chunk,
                                 const std::vector<std::pair<std::string, std::string>>& playsets);
// presentation/player_hud_combined.lua as Lua text: 3.0's chunk unchanged, a 1.0 currency icon resting and counted.
inline std::string hudCurrency10(const std::string& chunk);

namespace detail {

// counter is the profile stat LastLoadingScreen: +1 per load (FUN_00675ef0), past 19 back to 1 (0x00b4e98d).
constexpr int kLastScreen = 19;
constexpr size_t kImageFixed = sizeof("flash/") - 1 + sizeof("_i") - 1 + sizeof(".mtb") - 1;

inline bool negation(char c) { return c == '-' || c == '!' || c == '~'; }

// One comma-list item, blanks trimmed.
inline std::string trimmed(const std::string& s, size_t b, size_t e)
{
    std::string t = s.substr(b, e - b);
    size_t i = t.find_first_not_of(" \t"), j = t.find_last_not_of(" \t");
    return i == std::string::npos ? "" : t.substr(i, j - i + 1);
}

// The Lua chunk as a quoted Lua string: printable ASCII kept, every other byte as a 3-digit escape.
inline void appendQuotedChunk(std::string& s, const std::string& chunk)
{
    for (unsigned char c : chunk) {
        const bool printable = c >= 0x20 && c < 0x7f;
        if (printable && c != '"' && c != '\\') {
            s += (char)c;
            continue;
        }
        char b[6];
        snprintf(b, sizeof(b), "\\%03u", (unsigned)c);   // always 3 digits: a digit after it never joins the escape
        s += b;
    }
}

}  // namespace detail

inline std::string loadingScreen10(const std::string& key10, int& counter)
{
    if (key10.find("Intro") != std::string::npos) return key10;
    if (++counter > detail::kLastScreen) counter = 1;
    char b[24];
    snprintf(b, sizeof(b), "LoadingScreen_%.2d", counter);
    return b;
}

inline std::string flashImageMovie(const std::string& p)
{
    // 3.0's movie loader also asks ids no image has: missioncomplete_i5.mtb, which only 1.0 ships
    if (p.rfind("flash/", 0) != 0 || p.size() < detail::kImageFixed || p.compare(p.size() - 4, 4, ".mtb")) return "";
    size_t i = p.rfind("_i"), e = p.size() - 4;
    if (i == std::string::npos || i + 2 >= e || p.find('/', i) != std::string::npos) return "";
    for (size_t k = i + 2; k < e; ++k)
        if (!isxdigit((unsigned char)p[k])) return "";
    return p.substr(0, i);
}

inline std::string flashMovie(const std::string& p)
{
    if (p.rfind("flash/", 0) != 0) return "";
    for (const char* x : {".gfx", ".gfxmanifest"}) {
        size_t n = strlen(x);
        if (p.size() > n && !p.compare(p.size() - n, n, x)) return p.substr(0, p.size() - n);
    }
    return "";
}

inline std::string fmvClip(const char* name)
{
    // only http/HTTP and .bik/.BIK are matched (GP-33); 1.0 keeps its clips loose in assets\fmv\PC1
    std::string n = name ? name : "";
    if (n.compare(0, 4, "http") == 0 || n.compare(0, 4, "HTTP") == 0) return "";
    size_t s = n.find_last_of("\\/");
    if (s != std::string::npos) n.erase(0, s + 1);
    const std::string ext = n.size() > 4 ? n.substr(n.size() - 4) : "";
    if (ext == ".bik" || ext == ".BIK") n.resize(n.size() - 4);
    return n;
}

inline std::string contextList10(const std::string& s)
{
    // 3.0's ToolSelectionUI also needs TUT_DISABLE_WEAPONWHEEL, a context no 1.0 controls table has
    static const char* const kept[][2] = {{"TUT_DISABLE_SELECT_ITEM", "TUT_DISABLE_WEAPONWHEEL"}};
    std::vector<std::string> neg;
    for (size_t b = 0; b <= s.size();) {
        size_t e = s.find(',', b);
        if (e == std::string::npos) e = s.size();
        std::string t = detail::trimmed(s, b, e);
        if (!t.empty() && detail::negation(t[0])) neg.push_back(t.substr(1));
        b = e + 1;
    }
    std::string out = s;
    for (auto& k : kept)
        for (auto& n : neg)
            if (n == k[0] && out.find(k[1]) == std::string::npos) out += std::string(",-") + k[1];
    return out == s ? "" : out;
}

// Both exes pick tips alike (3.0 FUN_00b70da0 = 1.0 FUN_00b4af50); 3.0's table had no Incredibles tips at all.
template <class IsZone>
inline int mergeTips(std::string& t30, const std::string& t10, IsZone zone)
{
    int added = 0;
    for (const char* name : {"TipCounts", "ZoneTipCategories"}) {
        const std::optional<lualex::LineTable> a = lualex::lineTable(t30, name);
        const std::optional<lualex::LineTable> b = a ? lualex::lineTable(t10, name) : std::nullopt;
        if (!a || !b) return -1;   // after editing TipCounts when only ZoneTipCategories is missing (GP-31)
        bool zones = name[0] == 'Z';
        std::string add;
        for (auto& x : b->entries) {
            bool have = zones && zone(x.key);
            for (auto& y : a->entries) have |= y.key == x.key;
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
        size_t end = a->end;
        const size_t last = t30.find_last_not_of("\r\n\t ", end - 1);
        const bool inBody = last != std::string::npos && last >= a->begin;
        if (!add.empty() && inBody && t30[last] != ',') {
            t30.insert(last + 1, ",");
            ++end;
        }
        t30.insert(end, add);
    }
    return added;
}

// PlaysetListWin32 hard-codes six entries; "@" .. id texts find 1.0 playset names in both games' string tables.
inline std::string playsetMenu10(const std::string& chunk,
                                 const std::vector<std::pair<std::string, std::string>>& playsets)
{
    auto q = [](const std::string& x) {
        std::string o = "\"";
        for (char c : x) {
            if (c == '"' || c == '\\') o += '\\';
            o += c;
        }
        return o + "\"";
    };
    std::string list = "local P = { ";
    for (auto& p : playsets) list += "{ " + q(p.first) + ", " + q(p.second) + " }, ";
    std::string s = "-- di1compat playsetmenu10: 3.0's PlaysetListWin32 (embedded unchanged) + the 1.0 playsets\n";
    s += list + "}\nlocal C = assert(loadstring(\"";
    s.reserve(chunk.size() * 3 + 1500);   // speed only: an escaped byte takes up to 4 (GP-32)
    detail::appendQuotedChunk(s, chunk);
    // SelectItem launches as UI_LaunchLevel does (the Virtual Reader); "world" undoes the VR's saveTarget 2
    s += R"LUA(", "=PlaysetListWin32"))()
local populate, selectItem = C.PopulateWorldCatagoryList, C.SelectItem
C.PopulateWorldCatagoryList = function(self, ...)
  populate(self, ...)
  if self.worldFilter ~= "Play" or UI_IsOnboardTutorialActive() then return end
  local at = #self.listData + 1
  for i, t in ipairs(self.listData) do if t.id == "PlaysetX" then at = i + 1 end end
  for k, p in ipairs(P) do
    self:AddListButton(p[1])
    table.insert(self.listData, at + k - 1, table.remove(self.listData))
  end
end
C.SelectItem = function(self, ...)
  local t, zone = self.listData[self.selectedIndex]
  for _, p in ipairs(P) do if t and t.id == p[1] then zone = p[2] end end
  if not zone then return selectItem(self, ...) end
  if self.cloudLoadStarted then return end
  self:CallFlashFunction("_root.CloseScreen")
  ClearNewsPaper()
  Network_SetJoinSession(false)
  UI_LaunchLevel(zone, "world", true, true, false)
  local h = UI.GetHandleFromName("VirtualReaderPC_Loadout", 0)
  local vr = h ~= UI.INVALID_UI_HANDLE and UI.GetScreenFromScreenHandle(h)
  if vr and vr.launchLevelInfo then vr.launchLevelInfo.saveTarget = "world" end
  self:SetFocus(false)
end
return C
)LUA";
    return s;
}

// The HUD knows 4 icons: GetPlayerData('currency') gives nil for others, CloseEconomy rests on defaultCurrency.
inline std::string hudCurrency10(const std::string& chunk)
{
    std::string s = "-- di1compat currency10: 3.0's Player_HUD_Combined (embedded unchanged) + 1.0 zone currencies\n";
    s += "local C = assert(loadstring(\"";
    s.reserve(chunk.size() * 3 + 1000);   // speed only: an escaped byte takes up to 4 (GP-32)
    detail::appendQuotedChunk(s, chunk);
    s += R"LUA(", "=Player_HUD_Combined"))()
local known = { SPARKS = true, ScrapCurrency = true, OUT_Balloons = true, Empire_Currency = true }
local get, changed = C.GetPlayerData, C.PlayerDataChanged
local function own(icon) return icon ~= nil and icon ~= "" and not known[icon] end
C.GetPlayerData = function(self, key, ...)
  local info = self.PlayerData.PlayerInfo
  if key == "currency" and own(info.currencyIcon) and info.currencyAmt >= 0 then return info.currencyAmt end
  return get(self, key, ...)
end
C.PlayerDataChanged = function(self, ...)
  changed(self, ...)
  local icon = self.PlayerData.PlayerInfo.currencyIcon
  if own(icon) then self.defaultCurrency = icon end
end
return C
)LUA";
    return s;
}

}  // namespace di1::ui

// 1.0 mission data in 3.0 terms (FORMATS.md 10.3): events, objective and task lines, behaviours, beacon maps.
// Std only; any thread.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace di1::mission {

namespace eng {
// 3.0 FUN_0177a8b0, cdecl(name): registers MissionTask_ProximityCheck_ActorToLocator's class
constexpr uint32_t kProximityCheckTemplate = 0x0177A8B0;
}  // namespace eng

// A 1.0 Meridian class 3.0 lacks, registered with its 3.0 twin's template and transcoded with its table
struct Alias {
    const char* name;
    uint32_t templateVa;   // static VA, called after FUN_0178e330 built the registry
    const char* twin;
};
inline constexpr Alias kAliases[] = {
    {"MissionFocusPoint_Rule_ProximityCheck_ActorToLocator", eng::kProximityCheckTemplate,
     "MissionTask_ProximityCheck_ActorToLocator"},
};

namespace detail {

// 1.0 presentation event (FUN_00fa7220) -> the 3.0 one (FUN_015e5dc0), by 1.0 value
inline constexpr int kPresentEvent30[] = {0, 2, 3, 4, 5, 7, 8, 9, 11, 12, 13, 14, 15, 16};

// The mission's Parms block: its '\n\tParms = ' and its closing '\n\t}' (npos when either is missing)
inline std::pair<size_t, size_t> parmsBlock(const std::string& t)
{
    size_t p = t.find("\n\tParms = "), e = p == std::string::npos ? p : t.find("\n\t}", p);
    return {p, e};
}

inline bool isLineBreak(char c) { return c == '\n' || c == '\r'; }

// The line at a without its line break; next = where the following line starts
inline std::string lineAt(const std::string& t, size_t a, size_t& next)
{
    size_t e = t.find('\n', a);
    next = e == std::string::npos ? t.size() : e + 1;
    std::string l = t.substr(a, next - a);
    while (!l.empty() && isLineBreak(l.back())) l.pop_back();
    return l;
}

// A '<tabs>k = v,' line: v without its comma and quotes; false for another key
inline bool luaField(const std::string& l, const char* k, std::string& v)
{
    size_t i = l.find_first_not_of('\t');
    if (i == std::string::npos || l.compare(i, strlen(k), k) || l.compare(i + strlen(k), 3, " = ")) return false;
    v = l.substr(i + strlen(k) + 3);
    if (!v.empty() && v.back() == ',') v.pop_back();
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return true;
}

// An eval's 'Data = { ... },' block: where it sits and the fields taskLines30 reads
struct EvalBlock {
    size_t node, open, close;   // its node's '{', its 'Data = ' line, its closing '},' line
    int ind;                    // the tabs before 'Data = '
    std::string type, str, sfx, prio;
    bool gen = false, hide = false, has = false;
};

using EvalField = void (*)(EvalBlock& b, const std::string& v);
inline void hasProgressString(EvalBlock& b, const std::string&) { b.has = true; }
inline constexpr std::pair<const char*, EvalField> kEvalFields[] = {
    {"Type", [](EvalBlock& b, const std::string& v) { b.type = v; }},
    {"ProgressStr", [](EvalBlock& b, const std::string& v) { b.str = v; }},
    {"ProgressGeneratedSuffix", [](EvalBlock& b, const std::string& v) { b.sfx = v; }},
    {"EvalPriority", [](EvalBlock& b, const std::string& v) { b.prio = v; }},
    {"UseGeneratedProgressStringNew", [](EvalBlock& b, const std::string& v) { b.gen = v == "true"; }},
    {"HideProgress", [](EvalBlock& b, const std::string& v) { b.hide = v == "true"; }},
    {"ProgressString", hasProgressString},
    {"ProgressStringCustom", hasProgressString},
};

// One line of a Data block into b (the keys are exact, so at most one row matches)
inline void evalField(EvalBlock& b, const std::string& line)
{
    std::string v;
    for (auto& [key, read] : kEvalFields) {
        if (!luaField(line, key, v)) continue;
        read(b, v);
        return;
    }
}

// Data block at line a (ind tabs). next: in, the line after a; out, where scanning resumes, npos to stop
inline std::optional<EvalBlock> evalBlock(const std::string& t, size_t a, size_t ind, size_t& next)
{
    EvalBlock b{};
    b.ind = (int)ind;
    size_t n2;
    if (lineAt(t, next, n2) != std::string(ind, '\t') + "{") return std::nullopt;
    const std::string closeLine = std::string(ind, '\t') + "},";
    size_t c = n2, cn = n2;
    for (; c < t.size(); c = cn) {
        std::string fl = lineAt(t, c, cn);
        if (fl == closeLine) break;
        evalField(b, fl);
    }
    if (c >= t.size()) {
        next = std::string::npos;   // unclosed: the scan ends
        return std::nullopt;
    }
    b.open = a;
    b.close = c;
    const std::string nodeOpen = "\n" + std::string(ind - 1, '\t') + "{\n";   // its node's '{': the nearest at ind - 1
    size_t no = t.rfind(nodeOpen, a);
    b.node = no == std::string::npos ? 0 : no;
    next = cn;
    return b;
}

// Every eval's Data block from the Tasks table on (to the end of the file, GR-28)
inline std::vector<EvalBlock> evalBlocks(const std::string& t)
{
    std::vector<EvalBlock> bs;
    for (size_t a = t.find("\tTasks = "), next; a != std::string::npos && a < t.size(); a = next) {
        std::string l = lineAt(t, a, next);
        size_t ind = l.find_first_not_of('\t');
        if (ind == std::string::npos || ind < 1 || l.compare(ind, std::string::npos, "Data = ")) continue;
        std::optional<EvalBlock> b = evalBlock(t, a, ind, next);
        if (b) bs.push_back(*b);
        else if (next == std::string::npos) break;
    }
    return bs;
}

// The ProgressString lines block b gets in 3.0 ("" for none); evals below an override get none
inline std::string progressLines(const EvalBlock& b, const std::vector<EvalBlock>& bs)
{
    if (b.hide || b.has) return "";
    const std::string tab(b.ind + 1, '\t');
    if (b.type == "Eval_LogicProgressOverride" && !b.str.empty())
        return tab + "ProgressString = \"<custom>\",\n" + tab + "ProgressStringCustom = \"" + b.str + "\",\n";
    if (!b.gen) return "";
    for (const EvalBlock& o : bs) {
        const bool isOverride = o.type == "Eval_LogicProgressOverride" && o.ind < b.ind;
        if (isOverride && o.node < b.open && b.open < o.open) return "";
    }
    const std::string task = "Task" + (b.prio.empty() ? std::string("0") : b.prio);
    return tab + "ProgressString = \"_" + (b.sfx.empty() ? task : b.sfx) + "\",\n";
}

// True when s occurs in t at or after from (npos: 0) and starts before end
inline bool foundIn(const std::string& t, const char* s, size_t from, size_t end)
{
    size_t h = t.find(s, from == std::string::npos ? 0 : from);
    return h < end;
}

inline constexpr char kBeaconMap[] = "BeaconMap";
inline bool isIdentChar(char c) { return isalnum((unsigned char)c) || c == '_'; }
inline bool isQuote(char c) { return c == '"' || c == '\''; }

// i at a quote: the index after the closing one (escapes skipped), t.size() if unclosed
inline size_t skipQuoted(const std::string& t, size_t i)
{
    char q = t[i];
    for (++i; i < t.size() && t[i] != q; ++i)
        if (t[i] == '\\') ++i;
    return i < t.size() ? i + 1 : t.size();
}

// The matching '}' of the '{' at open, strings and '--' comments to the end of the line skipped (GR-13)
inline size_t closeOf(const std::string& t, size_t open)
{
    int d = 0;
    for (size_t i = open; i < t.size();) {
        char c = t[i];
        if (isQuote(c)) {
            i = skipQuoted(t, i);
            continue;
        }
        if (c == '-' && i + 1 < t.size() && t[i + 1] == '-') {
            size_t e = t.find('\n', i);
            i = e == std::string::npos ? t.size() : e;
            continue;
        }
        if (c == '{') ++d;
        else if (c == '}') {
            --d;
            if (d == 0) return i;
        }
        ++i;
    }
    return std::string::npos;
}

inline std::string trimSpace(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
inline bool isLuaString(const std::string& e)
{
    if (e.size() < 2) return false;
    return isQuote(e[0]) && e.back() == e[0];
}
inline bool isLuaNumber(const std::string& e)
{
    char* end = nullptr;
    strtod(e.c_str(), &end);
    return !e.empty() && end && !*end;
}

// A BeaconMap row's comma-separated elements; plain = no nested table or assignment
struct Row {
    std::vector<std::string> el;
    bool plain = true;
};
inline Row rowElements(const std::string& body)
{
    Row row;
    size_t s = 0;
    for (size_t k = 0; k <= body.size(); ++k) {
        const bool more = k < body.size();
        const char ch = more ? body[k] : '\0';
        if (more && isQuote(ch)) {
            for (++k; k < body.size() && body[k] != ch; ++k)
                if (body[k] == '\\') ++k;
            continue;   // an unclosed string ends the row without its last element
        }
        const bool nested = ch == '{' || ch == '=';
        if (more && nested) {
            row.plain = false;
            break;
        }
        if (more && ch != ',') continue;
        row.el.push_back(trimSpace(body.substr(s, k - s)));
        s = k + 1;
    }
    if (!row.el.empty() && row.el.back().empty()) row.el.pop_back();
    return row;
}

// A 1.0 row: plain, three strings (name, cho, placement) and maybe a number
inline bool row10(const Row& row)
{
    if (!row.plain) return false;
    const size_t n = row.el.size();
    if (n != 3 && n != 4) return false;
    if (!isLuaString(row.el[0]) || !isLuaString(row.el[1]) || !isLuaString(row.el[2])) return false;
    return n == 3 || isLuaNumber(row.el[3]);
}

// The '{' and '}' of a 'BeaconMap = { ... }' assignment named at p, nullopt for anything else
inline std::optional<std::pair<size_t, size_t>> beaconMapAt(const std::string& t, size_t p)
{
    if (p && isIdentChar(t[p - 1])) return std::nullopt;
    size_t q = p + sizeof(kBeaconMap) - 1;
    if (q < t.size() && isIdentChar(t[q])) return std::nullopt;
    q = t.find_first_not_of(" \t\r\n", q);
    if (q == std::string::npos || t[q] != '=') return std::nullopt;
    if (q + 1 < t.size() && t[q + 1] == '=') return std::nullopt;   // '==': a comparison
    size_t open = t.find_first_not_of(" \t\r\n", q + 1);
    if (open == std::string::npos || t[open] != '{') return std::nullopt;
    size_t close = closeOf(t, open);
    if (close == std::string::npos) return std::nullopt;
    return std::make_pair(open, close);
}

struct Edit {
    size_t at, len;
    std::string text;
};

// One map's rows between open and close: the 1.0 ones rewritten into edits
struct MapRows {
    int converted = 0;
    bool hasBlue = false;   // it has a Beacon_Arrow_Blue row
    std::string fpd;        // FocusPointDefault's converted tail: , placement, float, Chos = { cho, }
};
inline MapRows convertRows(const std::string& t, size_t open, size_t close, std::vector<Edit>& edits)
{
    MapRows m;
    for (size_t i = open + 1; i < close;) {
        char c = t[i];
        if (c == '-' && i + 1 < t.size() && t[i + 1] == '-') {
            size_t e = t.find('\n', i);
            i = e == std::string::npos ? close : e;
            continue;
        }
        if (c != '{') {
            ++i;
            continue;
        }
        size_t re = closeOf(t, i);
        if (re == std::string::npos || re > close) break;
        const Row row = rowElements(t.substr(i + 1, re - i - 1));
        const std::vector<std::string>& el = row.el;
        std::string name = !el.empty() && isLuaString(el[0]) ? el[0].substr(1, el[0].size() - 2) : "";
        if (name == "Beacon_Arrow_Blue") m.hasBlue = true;
        if (row10(row)) {
            const std::string number = el.size() == 4 ? el[3] : std::string("0");
            std::string tail = ", " + el[2] + ", " + number + ", Chos = { " + el[1] + ", }, ";
            edits.push_back({i, re + 1 - i, "{ " + el[0] + tail + "}"});
            if (name == "FocusPointDefault") m.fpd = tail;
            ++m.converted;
        }
        i = re + 1;
    }
    return m;
}

}  // namespace detail

// A 1.0 presentation event as 3.0's (events10); a value outside 1.0's enum stays as it is
inline int presentEvent30(int e10)
{
    const int n = (int)(sizeof(detail::kPresentEvent30) / sizeof(detail::kPresentEvent30[0]));
    return e10 >= 0 && e10 < n ? detail::kPresentEvent30[e10] : e10;
}

// luaEvents30 (events10): every 'Event = N,' line of a mission .lua (text) remapped. Returns the lines changed.
inline int luaEvents30(std::string& t)
{
    static constexpr char kKey[] = "Event = ";
    const size_t keyLen = sizeof(kKey) - 1;
    int n = 0;
    for (size_t p = t.find(kKey); p != std::string::npos; p = t.find(kKey, p + 1)) {
        size_t ls = t.rfind('\n', p);
        ls = ls == std::string::npos ? 0 : ls + 1;
        if (t.find_first_not_of(" \t", ls) != p) continue;   // the key opens its line
        size_t q = p + keyLen, e = q;
        while (e < t.size() && isdigit((unsigned char)t[e])) ++e;
        if (e == q || e >= t.size() || t[e] != ',') continue;
        std::string v = std::to_string(presentEvent30(atoi(t.c_str() + q)));
        t.replace(q, e - q, v);
        ++n;
    }
    return n;
}

// progress30 (progress10): ProgressString "<default>" where 1.0 generated the objective line (FORMATS.md 10.3).
// Returns 1 if changed.
inline int progress30(std::string& t)
{
    const auto [p, e] = detail::parmsBlock(t);
    if (e == std::string::npos) return 0;
    size_t f = t.find("\n\t\tUseMissionGeneratedProgressStringNew = true,", p);
    if (f > e || t.find("\n\t\tProgressString = ", p) < e) return 0;
    t.insert(f, "\n\t\tProgressString = \"<default>\",");
    return 1;
}

// taskLines30 (progress10): 3.0 ProgressString lines for a mission without the generated line (FORMATS.md 10.3).
// Returns the evals changed.
inline int taskLines30(std::string& t)
{
    const auto [pp, pe] = detail::parmsBlock(t);
    if (pe == std::string::npos || t.find("\n\t\tUseMissionGeneratedProgressStringNew = false,", pp) > pe) return 0;
    const std::vector<detail::EvalBlock> bs = detail::evalBlocks(t);
    std::vector<std::pair<size_t, std::string>> ins;
    for (const detail::EvalBlock& b : bs) {
        std::string add = detail::progressLines(b, bs);
        if (!add.empty()) ins.emplace_back(b.close, add);
    }
    std::sort(ins.begin(), ins.end(), [](auto& x, auto& y) { return x.first > y.first; });
    for (auto& i : ins) t.insert(i.first, i.second);
    return (int)ins.size();
}

// behaviour30 (behav10): MissionBehavior 2 -> 3 (story line) or 4; not GoldStar or EvalTrigger records.
// Returns the count.
inline int behaviour30(std::string& t)
{
    static constexpr char kKey[] = "\t\t\t\tMissionBehavior = 2,\n";
    static constexpr char kLine[] = "\t\t\tline = \"";
    const size_t keyLen = sizeof(kKey) - 1, lineLen = sizeof(kLine) - 1;
    const size_t digitAt = keyLen - 3;   // the '2' before ",\n"
    int n = 0;
    for (size_t p = t.find(kKey); p != std::string::npos; p = t.find(kKey, p + 1)) {
        size_t next = t.find("\n\t\t{\n", p), l = t.find(kLine, p);   // its line = "...", before the next record
        const bool afterNext = next != std::string::npos && l > next;
        if (l == std::string::npos || afterNext) continue;
        size_t rs = t.rfind("\n\t\t{\n", p), end = next == std::string::npos ? t.size() : next;
        const bool goldStar = detail::foundIn(t, "\t\t\t\tGoldStarContext = 1,\n", rs, end);
        if (goldStar || detail::foundIn(t, "\t\t\t\tMissionType = \"EvalTrigger\",\n", rs, end)) continue;
        size_t q = l + lineLen, e = t.find('"', q);
        if (e == std::string::npos) break;
        std::string ln = t.substr(q, e - q);
        for (char& c : ln) c = (char)tolower((unsigned char)c);
        t[p + digitAt] = ln.find("story") != std::string::npos ? '3' : '4';
        ++n;
    }
    return n;
}

// beaconMap30 (beacon10): 1.0 BeaconMap rows in 3.0's row form (FORMATS.md 10.3). Returns the rows converted;
// *aliases = Beacon_Arrow_Blue rows added.
inline int beaconMap30(std::string& t, int* aliases = nullptr)
{
    std::vector<detail::Edit> edits;
    int n = 0, na = 0;
    const size_t keyLen = sizeof(detail::kBeaconMap) - 1;
    for (size_t p = t.find(detail::kBeaconMap); p != std::string::npos; p = t.find(detail::kBeaconMap, p + keyLen)) {
        const auto map = detail::beaconMapAt(t, p);
        if (!map) continue;
        const size_t close = map->second;
        const detail::MapRows m = detail::convertRows(t, map->first, close, edits);
        if (m.converted && !m.hasBlue && !m.fpd.empty()) {
            size_t last = t.find_last_not_of(" \t\r\n", close - 1);
            std::string sep = last != std::string::npos && t[last] != ',' && t[last] != '{' ? ", " : " ";
            edits.push_back({close, 0, sep + "{ \"Beacon_Arrow_Blue\"" + m.fpd + "}, "});
            ++na;
        }
        n += m.converted;
    }
    std::sort(edits.begin(), edits.end(), [](const detail::Edit& a, const detail::Edit& b) { return a.at > b.at; });
    for (auto& e : edits) t.replace(e.at, e.len, e.text);
    if (aliases) *aliases = na;
    return n;
}

}  // namespace di1::mission

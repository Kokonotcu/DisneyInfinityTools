// di1debug.txt: every watch's counts, rewritten every few seconds by its own thread. The watches.
#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "../core/log.hpp"
#include "files.hpp"
#include "flash.hpp"
#include "lua.hpp"
#include "sx.hpp"

namespace dbg::report {

// Starts the writer thread; it writes <gameDir>\di1debug.txt every kPeriodMs. Never waits (H10).
inline bool start(const std::string& gameDir);

namespace eng {
constexpr DWORD kPeriodMs = 5000;
}  // namespace eng

namespace detail {

struct State {
    std::string path, tmp;   // set once in start()
};
inline State g_state;

// A Lua chunk name as the files watch keys paths: no leading '@'.
inline bool chunkServed(const std::string& chunk)
{
    const size_t at = !chunk.empty() && chunk[0] == '@' ? 1 : 0;
    return files::served(files::norm(chunk.c_str() + at));
}

inline void writeFiles(FILE* f)
{
    std::vector<files::Row> rows = files::rows();
    uint32_t engine = 0, compat = 0, missing = 0;
    for (const files::Row& r : rows) {
        engine += r.n.engine;
        compat += r.n.compat;
        missing += r.n.missing;
    }
    fprintf(f, "== FILES  opens: %u from 3.0, %u served by di1compat, %u missing\n", engine, compat, missing);
    std::sort(rows.begin(), rows.end(), [](const files::Row& a, const files::Row& b) { return a.path < b.path; });
    fprintf(f, "-- missing (opens, path)\n");
    for (const files::Row& r : rows)
        if (r.n.missing) fprintf(f, "%7u  %s\n", r.n.missing, r.path.c_str());
    fprintf(f, "-- served by di1compat (opens, path)\n");
    for (const files::Row& r : rows)
        if (r.n.compat) fprintf(f, "%7u  %s\n", r.n.compat, r.path.c_str());
}

inline void writeLua(FILE* f)
{
    std::vector<lua::CallRow> rows = lua::calls();
    std::sort(rows.begin(), rows.end(), [](const lua::CallRow& a, const lua::CallRow& b) { return a.calls > b.calls; });
    fprintf(f, "\n== LUA -> GAME CALLS  (calls, unreturned, function, calling chunk; * = served by di1compat)\n");
    for (const lua::CallRow& r : rows) {
        const char* mark = chunkServed(r.caller) ? "* " : "";
        fprintf(f, "%9u %6u  %-40s %s%s\n", r.calls, r.unreturned, r.function.c_str(), mark, r.caller.c_str());
    }
    std::vector<lua::ErrorRow> errs = lua::errors();
    std::sort(errs.begin(), errs.end(),
              [](const lua::ErrorRow& a, const lua::ErrorRow& b) { return a.count > b.count; });
    fprintf(f, "\n== LUA ERRORS  (count, message)\n");
    for (const lua::ErrorRow& e : errs) fprintf(f, "%7u  %s\n", e.count, e.message.c_str());
}

inline void writeSx(FILE* f)
{
    std::vector<sx::Row> rows = sx::rows();
    std::sort(rows.begin(), rows.end(), [](const sx::Row& a, const sx::Row& b) { return a.calls > b.calls; });
    fprintf(f, "\n== 1.0 SCRIPT NATIVES  (calls, import bindings, answered by, name)\n");
    for (const sx::Row& r : rows)
        fprintf(f, "%9u %6u  %-8s %s\n", r.calls, r.imports, sx::answerText(r.answer), r.name.c_str());
}

inline void writeFlash(FILE* f)
{
    std::vector<flash::Row> rows = flash::rows();
    std::sort(rows.begin(), rows.end(), [](const flash::Row& a, const flash::Row& b) { return a.calls > b.calls; });
    fprintf(f, "\n== FLASH -> GAME CALLS  (calls, Lua errors during, screen, name)\n");
    for (const flash::Row& r : rows)
        fprintf(f, "%9u %6u  %-32s %s\n", r.calls, r.errors, r.screen.c_str(), r.name.c_str());
}

// One report into tmp, then moved over path: a reader never sees half a file.
inline void writeOnce() noexcept
{
    try {
        const State& s = g_state;
        FILE* f = nullptr;
        if (fopen_s(&f, s.tmp.c_str(), "w") || !f) return;
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(f, "di1debug report %02d:%02d:%02d  (counts since the game started)\n\n", t.wHour, t.wMinute,
                t.wSecond);
        writeFiles(f);
        writeLua(f);
        writeSx(f);
        writeFlash(f);
        fclose(f);
        MoveFileExA(s.tmp.c_str(), s.path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) {
    }
}

inline DWORD WINAPI writer(LPVOID)
{
    for (;;) {
        Sleep(eng::kPeriodMs);
        writeOnce();
    }
}

}  // namespace detail

inline bool start(const std::string& gameDir)
{
    detail::State& s = detail::g_state;
    s.path = gameDir + "\\di1debug.txt";
    s.tmp = s.path + ".tmp";
    HANDLE h = CreateThread(nullptr, 0, &detail::writer, nullptr, 0, nullptr);
    if (!h) {
        core::logf("report: writer thread not started (%lu)", GetLastError());
        return false;
    }
    CloseHandle(h);
    return true;
}

}  // namespace dbg::report

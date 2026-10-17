// renderdoc10: RenderDoc in-process; one frame captured some seconds after it is armed. core. Arming: one thread.
#pragma once
#include <windows.h>
#include <string>
#include <thread>
#include "../core/log.hpp"
#include "../renderdoc_app.h"

namespace trace::renderdoc {

struct Config {
    int seconds = 0;              // renderdoc10: > 0 loads RenderDoc; the capture's delay once armed
    std::string captureTemplate;  // the capture files' path template
};

// Loads RenderDoc before the renderer creates its device, so its hooks see the device; false when it did not load.
inline bool install(const Config& c);
// Whether RenderDoc is loaded.
inline bool loaded();
// Starts the capture's countdown, the first time only; one thread arms (B-48: unverified).
inline void arm();

namespace detail {

namespace eng {
constexpr const char* kDll = "C:\\Program Files\\RenderDoc\\x86\\renderdoc.dll";
constexpr DWORD kMsPerSecond = 1000;
}  // namespace eng

struct State {
    RENDERDOC_API_1_1_2* api = nullptr;   // set once in install()
    int seconds = 0;                      // set once in install()
    bool armed = false;                   // the arming thread only (B-48: unverified)
};
inline State g_state;

}  // namespace detail

inline bool install(const Config& c)
{
    detail::State& s = detail::g_state;
    if (c.seconds <= 0) return true;
    s.seconds = c.seconds;
    HMODULE rd = LoadLibraryExA(detail::eng::kDll, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    auto get = rd ? (pRENDERDOC_GetAPI)GetProcAddress(rd, "RENDERDOC_GetAPI") : nullptr;
    if (!get || get(eRENDERDOC_API_Version_1_1_2, (void**)&s.api) != 1) {
        core::logf("renderdoc: not loaded (%lu)", GetLastError());
        return false;
    }
    s.api->SetCaptureFilePathTemplate(c.captureTemplate.c_str());
    core::logf("renderdoc: loaded; capture %d s after the first mission tick (missiontrace)", c.seconds);
    return true;
}

inline bool loaded()
{
    return detail::g_state.api != nullptr;
}

inline void arm()
{
    detail::State& s = detail::g_state;
    if (s.armed) return;
    s.armed = true;
    std::thread([] {
        detail::State& st = detail::g_state;
        Sleep(detail::eng::kMsPerSecond * st.seconds);
        st.api->TriggerCapture();
        core::logf("renderdoc: capture triggered");
    }).detach();
}

}  // namespace trace::renderdoc

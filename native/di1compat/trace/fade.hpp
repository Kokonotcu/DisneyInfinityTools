// fadeprobe10 probe: where object fades come from; mode 2 draws faded objects unfaded. core. Render-path threads.
#pragma once
#include <windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include "../core/hooks.hpp"
#include "../core/log.hpp"

namespace trace::fade {

struct Config {
    int mode = 0;   // fadeprobe10: 0 off, 1 log faded nodes' fade inputs, 2 also draw them unfaded (not log-only)
};

// Installs the probe when c asks for it (H1); false when its hook was refused.
inline bool install(const Config& c);

namespace detail {

namespace eng {
// FUN_0044b8f0's render node and view parameters (FUN_00459af0 calls it for nodes with flag 8)
constexpr uint32_t kNodeId = 0x34;          // node +0x34: its id
constexpr uint32_t kNodeFlags = 0xc;        // node +0xc u16: bit 0x80 = distance fade (start +0x10, end +0x14)
constexpr uint32_t kNodeScreenFade = 0xf;   // node +0xf: bit 0 = screen-size fade
constexpr uint32_t kNodeFadeStart = 0x10;   // node +0x10 float: distance fade start
constexpr uint32_t kNodeFadeEnd = 0x14;     // node +0x14 float: distance fade end
constexpr size_t kViewZx = 0x50;            // view +0x50/+0x70/+0x90/+0xb0: the matrix row giving view z
constexpr size_t kViewZy = 0x70;
constexpr size_t kViewZz = 0x90;
constexpr size_t kViewZw = 0xb0;
constexpr size_t kViewK = 0xd0;             // view +0xd0: screen size s = pi * radius / (z^2 * k)
constexpr size_t kViewPop = 0x130;          // view +0x130: culled under this screen size
constexpr size_t kViewFadeEnd = 0x150;      // view +0x150: fade (s - pop) * scale reaches 1 here
constexpr size_t kViewScale = 0x170;        // view +0x170: the fade scale
}  // namespace eng

constexpr float kOpaque = 0.999f;         // out[1] at or above this counts as unfaded
constexpr float kPi = 3.1415927f;
constexpr ULONGLONG kNodeQuietMs = 3000;  // a node is logged again after 3 s
constexpr int kLinesLogged = 800;
constexpr int kModeUnfade = 2;

typedef uint8_t(__cdecl* VisTestFn)(uint8_t* node, float* sphere, void* a3, uint8_t* view, uint8_t mask, float* out);

struct State {
    VisTestFn g_visTest = nullptr;                   // set once by hook() in install()
    bool unfade = false;                             // set once in install(): fadeprobe10 == 2
    std::mutex mx;                                   // guards last and lines
    std::unordered_map<uint8_t*, ULONGLONG> last;    // node -> tick of its last line
    int lines = 0;
};
inline State g_state;

// Whether node gets a line now: at most kLinesLogged, each node at most once per kNodeQuietMs.
inline bool throttle(uint8_t* node)
{
    State& s = g_state;
    ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(s.mx);
    if (s.lines >= kLinesLogged) return false;
    auto it = s.last.find(node);
    if (it != s.last.end() && now - it->second < kNodeQuietMs) return false;
    s.last[node] = now;
    ++s.lines;
    return true;
}

// Its fade inputs: both terms, as FUN_0044b8f0 computes them.
inline void logFade(uint8_t* node, const float* sphere, uint8_t* view, const float* out)
{
    auto F = [&](size_t o) { return *(float*)(view + o); };
    float z = F(eng::kViewZy) * sphere[1] + F(eng::kViewZx) * sphere[0] + F(eng::kViewZz) * sphere[2] + F(eng::kViewZw);
    float s = sphere[3] * kPi / (z * z * F(eng::kViewK));
    core::logf("fade probe: node %p id %08X flags %04X f %02X start %.2f end %.2f | sphere (%.2f %.2f %.2f) %.4f "
               "z %.2f | k %.6g pop %.6g fadeEnd %.6g scale %.6g -> screen %.6g fade %.3f", node,
               *(uint32_t*)(node + eng::kNodeId), *(uint16_t*)(node + eng::kNodeFlags), node[eng::kNodeScreenFade],
               *(float*)(node + eng::kNodeFadeStart), *(float*)(node + eng::kNodeFadeEnd), sphere[0], sphere[1],
               sphere[2], sphere[3], z, F(eng::kViewK), F(eng::kViewPop), F(eng::kViewFadeEnd), F(eng::kViewScale), s,
               out[1]);
}

// FUN_0044b8f0, cdecl: main-view visibility test, out[1] = object fade. Render path, thread unverified; per node.
inline uint8_t __cdecl hkVisTest(uint8_t* node, float* sphere, void* a3, uint8_t* view, uint8_t mask, float* out)
{
    uint8_t r = g_state.g_visTest(node, sphere, a3, view, mask, out);
    if (!(r & 1) || !(mask & 1) || out[1] >= kOpaque) return r;
    // mode 2: every return below leaves the object unfaded
    struct Unfade {
        float* o;
        bool on;
        ~Unfade()
        {
            if (on) o[1] = 1.0f;
        }
    } unfade{out, g_state.unfade};
    if (!throttle(node)) return r;
    logFade(node, sphere, view, out);
    return r;
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.mode) return true;
    detail::g_state.unfade = c.mode == detail::kModeUnfade;
    return core::hook("visibility test (fadeprobe10)", 0x0044B8F0, "8B5424088A442414F30F10620C",
                      (void*)&detail::hkVisTest, (void**)&detail::g_state.g_visTest);
}

}  // namespace trace::fade

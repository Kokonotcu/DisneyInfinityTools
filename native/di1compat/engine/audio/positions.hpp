// Where Wwise game objects and the listener are, in engine space, for the FMOD host. std only. Any thread.
#pragma once
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace engine::audio::positions {

struct V3 {
    float v[3];
};

// The listener as last set: position, forward, up; set is false until the game set it.
struct Listener {
    float pos[3], fwd[3], up[3];
    bool set;
};

// Wwise SetPosition's position (z negated: 3.0 FUN_0182f170 negates it for Wwise).
inline void set(uint32_t obj, const float* wwisePos);
// Wwise SetListenerPosition's {front, top, position} (z negated by 3.0 FUN_0182d340).
inline void setListener(const float* wwise);
// The object's position; the listener's when none is known.
inline V3 at(uint32_t obj);
inline Listener listener();
inline void erase(uint32_t obj);
// fn(the object's position or null, the listener) under the lock; audiotrace10 logs from it.
template <class Fn>
inline void inspect(uint32_t obj, Fn fn);

namespace detail {
// Its own lock: game-thread SetPosition calls never wait for FMOD's update, which runs under system's lock.
struct State {
    std::mutex mx;                          // guards every member below
    std::unordered_map<uint32_t, V3> pos;   // game object -> position
    float lpos[3] = {}, lfwd[3] = {0, 0, 1}, lup[3] = {0, 1, 0};
    bool lset = false;
};
inline State g_state;
}  // namespace detail

inline void set(uint32_t obj, const float* wwisePos)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> l(s.mx);
    V3& v = s.pos[obj];
    v.v[0] = wwisePos[0];
    v.v[1] = wwisePos[1];
    v.v[2] = -wwisePos[2];
}

inline void setListener(const float* wwise)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> l(s.mx);
    for (int i = 0; i < 3; ++i) {
        const float sign = i == 2 ? -1.0f : 1.0f;
        s.lfwd[i] = wwise[i] * sign;
        s.lup[i] = wwise[3 + i] * sign;
        s.lpos[i] = wwise[6 + i] * sign;
    }
    s.lset = true;
}

inline V3 at(uint32_t obj)
{
    detail::State& s = detail::g_state;
    V3 p;
    std::lock_guard<std::mutex> l(s.mx);
    auto it = s.pos.find(obj);
    memcpy(p.v, it != s.pos.end() ? it->second.v : s.lpos, sizeof p.v);
    return p;
}

inline Listener listener()
{
    detail::State& s = detail::g_state;
    Listener out;
    std::lock_guard<std::mutex> l(s.mx);
    memcpy(out.pos, s.lpos, sizeof out.pos);
    memcpy(out.fwd, s.lfwd, sizeof out.fwd);
    memcpy(out.up, s.lup, sizeof out.up);
    out.set = s.lset;
    return out;
}

inline void erase(uint32_t obj)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> l(s.mx);
    s.pos.erase(obj);
}

template <class Fn>
inline void inspect(uint32_t obj, Fn fn)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> l(s.mx);
    auto it = s.pos.find(obj);
    Listener ls;
    memcpy(ls.pos, s.lpos, sizeof ls.pos);
    memcpy(ls.fwd, s.lfwd, sizeof ls.fwd);
    memcpy(ls.up, s.lup, sizeof ls.up);
    ls.set = s.lset;
    fn(it != s.pos.end() ? it->second.v : nullptr, ls);
}

}  // namespace engine::audio::positions

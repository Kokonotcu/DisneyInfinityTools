// Playing ids that ended and dialog line lengths, queued for the host tick to deliver. std only. Any thread.
#pragma once
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace engine::audio::finished {

typedef std::vector<std::pair<uint32_t, float>> Lengths;   // (playing id, length ms)

inline void push(uint32_t id);
inline void push(const std::vector<uint32_t>& ids);
inline void length(uint32_t id, float ms);
// Everything queued, moved out in one hold of the lock.
inline void take(std::vector<uint32_t>& ids, Lengths& lengths);

namespace detail {
// A leaf lock, never held across an FMOD call: FMOD's callback pushes from whatever thread it runs on.
struct State {
    std::mutex mx;                  // guards done and durs
    std::vector<uint32_t> done;     // ended playing ids, delivered outside system's lock
    Lengths durs;                   // dialog lines started, for AK_Duration
};
inline State g_state;
}  // namespace detail

inline void push(uint32_t id)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> l(s.mx);
    s.done.push_back(id);
}

inline void push(const std::vector<uint32_t>& ids)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> l(s.mx);
    s.done.insert(s.done.end(), ids.begin(), ids.end());
}

inline void length(uint32_t id, float ms)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> l(s.mx);
    s.durs.emplace_back(id, ms);
}

inline void take(std::vector<uint32_t>& ids, Lengths& lengths)
{
    detail::State& s = detail::g_state;
    std::lock_guard<std::mutex> l(s.mx);
    ids.swap(s.done);
    lengths.swap(s.durs);
}

}  // namespace engine::audio::finished

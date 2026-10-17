// First-match dispatch over a rule table; a rule may pass a request on. Standard library only. Any thread.
#pragma once
#include <cstddef>
#include <optional>

namespace dispatch {

// A rule's answer: a value ends the dispatch with it; kNext lets the rules below see the request.
template <class T>
using Answer = std::optional<T>;
inline constexpr std::nullopt_t kNext = std::nullopt;

template <class Req, class T>
struct Rule {
    bool (*matches)(const Req&);
    Answer<T> (*run)(Req&);
};

// The rules in table order: the first answer wins; fallback when no rule answers.
template <class Req, class T, size_t N>
T firstMatch(const Rule<Req, T> (&rules)[N], Req& req, T fallback)
{
    for (const Rule<Req, T>& r : rules) {
        if (!r.matches(req)) continue;
        if (Answer<T> a = r.run(req)) return *a;
    }
    return fallback;
}

}  // namespace dispatch

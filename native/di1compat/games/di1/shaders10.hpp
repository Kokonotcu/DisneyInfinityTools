// shaders10's hooks: 1.0's generator and test views, then linking by 1.0 names. engine, render10. Boot thread.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "../../engine/shaderlinks.hpp"
#include "../../engine/shaders.hpp"
#include "config.hpp"
#include "render10.hpp"

namespace di1::shaders10 {

// Installs engine/shaders with 1.0's generator, then engine/shaderlinks with 1.0's names, when c has shaders10.
inline bool install(const Config& c);

namespace detail {

constexpr size_t kShaderHashName = 33;   // 'x' and a hash-named 1.0 shader's name, as the world transcode writes it

// 'x' names are 1.0 shaders: hash names are made on demand, others only found (an unmade name hangs the loader).
inline uint8_t* shader10(const char* name)
{
    if (!name || name[0] != 'x') return nullptr;
    return strlen(name) == kShaderHashName ? engine::shaders::make(name) : engine::shaders::find(name);
}

}  // namespace detail

inline bool install(const Config& c)
{
    if (!c.shaders10) return true;
    engine::shaders::Config made;
    made.compiler = render10::compiler();
    made.bufView = c.bufview10;
    made.magenta = c.magenta10;
    made.generate = render10::generateShader10;
    engine::shaders::install(made);
    engine::shaderlinks::Config links;
    links.lazyLink = c.lazyLink10;
    links.fadeProbe = c.fadeProbe10 != 0;
    links.gameShader = detail::shader10;
    return engine::shaderlinks::install(links);
}

}  // namespace di1::shaders10

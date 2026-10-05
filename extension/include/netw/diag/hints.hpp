#pragma once

#if defined(DEBUG_ENABLED)

#include <cstdint>

#include "godot/variant.hpp"

namespace netw::diag {

enum Hint : uint32_t {
    HINT_PREDICTION = 1u << 0,
};

struct HintRequest {
    uint32_t mask = 0;
    godot::PackedStringArray unknown;
};

HintRequest parse_hints(
    const godot::String &p_environment,
    const godot::PackedStringArray &p_args
);
void configure_hints();
uint32_t hints();

} // namespace netw::diag

#define NETW_HINTS(...) __VA_ARGS__
#else
#define NETW_HINTS(...) ((void)0)
#endif

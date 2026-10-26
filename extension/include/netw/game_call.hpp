#pragma once

#include "godot/callable.hpp"
#include "godot/variant.hpp"
#include "netw/subsystems.hpp"

#include <cstdint>

namespace netw {

struct GameCall {
    SubsystemName system;
    const char *what;
    const char *signature;
    int64_t argument_count;
};

bool game_call_accepts(const godot::Callable &p_fn, int64_t p_count);

bool game_call_declared(const godot::Callable &p_fn, const GameCall &p_site);

godot::Variant game_call(
    const godot::Callable &p_fn,
    const godot::Array &p_args,
    const GameCall &p_site,
    bool *r_called = nullptr
);

} // namespace netw

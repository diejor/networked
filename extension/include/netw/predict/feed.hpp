#pragma once

#include "godot/callable.hpp"

namespace netw::predict {

struct Feed {
    godot::Callable state_on_applied;
    godot::Callable input_on_applied;
    bool state_write_gate = true;
    bool input_write_gate = true;
    bool input_volatile_external = false;
};

} // namespace netw::predict

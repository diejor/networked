#pragma once

#include "netw/predict/arrival.hpp"

namespace netw::predict {

struct Feed {
    Listener state_listener = Listener::NONE;
    Listener input_listener = Listener::NONE;
    bool state_write_gate = true;
    bool input_write_gate = true;
    bool input_volatile_external = false;
};

} // namespace netw::predict

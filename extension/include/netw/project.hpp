#pragma once

#include "godot/variant.hpp"

namespace netw::project {

godot::Variant forward(
    const godot::Variant &value,
    const godot::Variant &velocity,
    double age
);
bool supports(int type);

} // namespace netw::project

#pragma once

#include "godot/variant.hpp"

namespace netw {

struct SceneDecl {
    bool declared = false;
    godot::StringName label;
    int isolation = 0;
};

} // namespace netw

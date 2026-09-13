#pragma once

#include "godot/variant.hpp"

namespace netw_test {

inline godot::PackedStringArray &published_classes() {
    static godot::PackedStringArray names;
    return names;
}

} // namespace netw_test

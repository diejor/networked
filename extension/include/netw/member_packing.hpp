#pragma once

#include "godot/script.hpp"
#include "godot/variant.hpp"

namespace netw {

bool declared_count_fits_arity(
    int p_declared_count,
    const godot::Array &p_types,
    const godot::String &p_verb,
    const godot::StringName &p_member,
    const godot::Ref<godot::Script> &p_script
);

bool declared_are_quantizers(
    const godot::Array &p_declared,
    const godot::String &p_verb,
    const godot::StringName &p_member
);

bool declared_are_interpolators(
    const godot::Array &p_declared,
    const godot::String &p_verb,
    const godot::StringName &p_member
);

bool quantizers_fit_types(
    const godot::Array &p_quantizers,
    const godot::Array &p_types,
    const godot::StringName &p_member,
    const godot::Ref<godot::Script> &p_script
);

bool interpolators_fit_types(
    const godot::Array &p_interpolators,
    const godot::Array &p_types,
    const godot::StringName &p_member,
    const godot::Ref<godot::Script> &p_script
);

} // namespace netw

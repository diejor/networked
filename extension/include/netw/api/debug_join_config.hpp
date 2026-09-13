#pragma once

#include "godot/resource.hpp"
#include "godot/variant.hpp"

namespace netw {

class DebugJoinConfig : public godot::Resource {
    GDCLASS(DebugJoinConfig, godot::Resource)

    godot::StringName username = godot::StringName("DebugPlayer");
    godot::Array join_args;

protected:
    static void _bind_methods();

public:
    void set_username(const godot::StringName &p_username) {
        username = p_username;
    }
    godot::StringName get_username() const {
        return username;
    }

    void set_join_args(const godot::Array &p_join_args);
    godot::Array get_join_args() const;
};

} // namespace netw

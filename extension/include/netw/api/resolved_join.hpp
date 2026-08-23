#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class ResolvedJoin : public godot::RefCounted {
    GDCLASS(ResolvedJoin, godot::RefCounted)

    int64_t peer_id = 0;
    godot::StringName username;
    godot::Array arg_values;
    bool is_debug = false;

protected:
    static void _bind_methods();

public:
    int64_t get_peer_id() const;
    void set_peer_id(int64_t value);
    godot::StringName get_username() const;
    void set_username(const godot::StringName &value);
    godot::Array get_arg_values() const;
    void set_arg_values(const godot::Array &value);
    bool get_is_debug() const;
    void set_is_debug(bool value);

    godot::PackedByteArray serialize() const;
    static godot::Ref<ResolvedJoin> deserialize(
        const godot::PackedByteArray &bytes
    );
};

} // namespace netw

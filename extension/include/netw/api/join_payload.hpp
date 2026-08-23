#pragma once

#include <cstdint>

#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/api/resolved_join.hpp"

namespace netw {

class JoinPayload : public godot::Resource {
    GDCLASS(JoinPayload, godot::Resource)

    godot::StringName username;
    godot::Array arg_values;
    godot::PackedByteArray arg_bytes;
    int64_t schema_hash = 0;
    int64_t peer_id = 0;
    bool is_debug = false;

protected:
    static void _bind_methods();

public:
    godot::StringName get_username() const;
    void set_username(const godot::StringName &value);
    godot::Array get_arg_values() const;
    void set_arg_values(const godot::Array &value);
    godot::PackedByteArray get_arg_bytes() const;
    void set_arg_bytes(const godot::PackedByteArray &value);
    int64_t get_schema_hash() const;
    void set_schema_hash(int64_t value);
    int64_t get_peer_id() const;
    void set_peer_id(int64_t value);
    bool get_is_debug() const;
    void set_is_debug(bool value);

    godot::Ref<ResolvedJoin> resolve() const;
    godot::PackedByteArray serialize() const;
    bool deserialize(const godot::PackedByteArray &bytes);
};

} // namespace netw

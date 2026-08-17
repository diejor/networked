#pragma once

#include <cstdint>

#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/resolved_join.hpp"

namespace netw {

using namespace godot;

class JoinPayload : public Resource {
    GDCLASS(JoinPayload, Resource)

    StringName username;
    Array arg_values;
    PackedByteArray arg_bytes;
    int64_t schema_hash = 0;
    int64_t peer_id = 0;
    bool is_debug = false;

protected:
    static void _bind_methods();

public:
    StringName get_username() const;
    void set_username(const StringName &value);
    Array get_arg_values() const;
    void set_arg_values(const Array &value);
    PackedByteArray get_arg_bytes() const;
    void set_arg_bytes(const PackedByteArray &value);
    int64_t get_schema_hash() const;
    void set_schema_hash(int64_t value);
    int64_t get_peer_id() const;
    void set_peer_id(int64_t value);
    bool get_is_debug() const;
    void set_is_debug(bool value);

    Ref<ResolvedJoin> resolve() const;
    PackedByteArray serialize() const;
    bool deserialize(const PackedByteArray &bytes);
};

} // namespace netw

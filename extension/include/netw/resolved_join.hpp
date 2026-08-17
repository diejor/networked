#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class ResolvedJoin : public RefCounted {
    GDCLASS(ResolvedJoin, RefCounted)

    int64_t peer_id = 0;
    StringName username;
    Array arg_values;
    bool is_debug = false;

protected:
    static void _bind_methods();

public:
    int64_t get_peer_id() const;
    void set_peer_id(int64_t value);
    StringName get_username() const;
    void set_username(const StringName &value);
    Array get_arg_values() const;
    void set_arg_values(const Array &value);
    bool get_is_debug() const;
    void set_is_debug(bool value);

    PackedByteArray serialize() const;
    static Ref<ResolvedJoin> deserialize(const PackedByteArray &bytes);
};

} // namespace netw

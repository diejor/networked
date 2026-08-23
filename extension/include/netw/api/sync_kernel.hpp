#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/staged_writes.hpp"

namespace netw {

class NetwSyncKernel : public godot::RefCounted {
    GDCLASS(NetwSyncKernel, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    static const int RESERVED = 0;

    static godot::PackedByteArray encode_volatile(
        int64_t p_ordinal,
        const godot::Array &p_values
    );

    static godot::Ref<NetwStagedWrites> decode_volatile(
        const godot::PackedByteArray &p_payload,
        const godot::Array &p_keys
    );

    static godot::PackedByteArray encode_retained(
        int64_t p_ordinal,
        int64_t p_mask,
        const godot::Array &p_values
    );

    static godot::Ref<NetwStagedWrites> decode_retained(
        const godot::PackedByteArray &p_payload,
        const godot::Array &p_keys
    );
};

} // namespace netw

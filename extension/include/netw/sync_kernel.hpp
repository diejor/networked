#pragma once

/* The payload a consumed MultiplayerSynchronizer frames its row through.
 *
 * A consumed set has no declared schema, so its values self-describe and the
 * caller supplies only the keys they land under. The RESERVED byte is what
 * keeps that readable across versions: this grammar is the only one, so a
 * sender that writes anything else there is speaking a newer one and its frame
 * is refused rather than read against the wrong layout.
 *
 *   volatile  [ordinal varint][reserved u8][values]
 *   retained  [ordinal varint][mask varint][values]
 */

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/staged_writes.hpp"

namespace netw {

class NetwSyncKernel : public godot::RefCounted {
    GDCLASS(NetwSyncKernel, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    // The byte between a volatile frame's ordinal and its values. Every frame
    // this version writes carries zero.
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

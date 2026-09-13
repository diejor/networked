#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/staged_writes.hpp"
#include "netw/wire/describe.hpp"
#include "netw/wire/stream.hpp"

namespace netw::sync_kernel {

inline constexpr int RESERVED = 0;

inline constexpr int WATCH_MASK_BYTES = 10;

struct VolatileHead {
    uint64_t ordinal = 0;
    uint64_t flags = RESERVED;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&VolatileHead::ordinal>(
            "ordinal",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&VolatileHead::flags>("flags", netw::wire::bits(8))
    );
};

struct RetainedHead {
    uint64_t ordinal = 0;
    uint64_t mask = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&RetainedHead::ordinal>(
            "ordinal",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&RetainedHead::mask>(
            "mask",
            netw::wire::varuint(WATCH_MASK_BYTES)
        )
    );
};

godot::PackedByteArray encode_volatile(
    int64_t p_ordinal,
    const godot::Array &p_values
);

StagedWrites decode_volatile(
    const godot::PackedByteArray &p_payload,
    const godot::Array &p_keys
);

godot::PackedByteArray encode_retained(
    int64_t p_ordinal,
    int64_t p_mask,
    const godot::Array &p_values
);

StagedWrites decode_retained(
    const godot::PackedByteArray &p_payload,
    const godot::Array &p_keys
);

godot::Dictionary frame_spec_records();

} // namespace netw::sync_kernel

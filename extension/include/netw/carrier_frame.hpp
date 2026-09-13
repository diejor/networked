#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/wire/describe.hpp"

namespace netw {

struct DatagramHead {
    uint64_t magic = 0;
    uint64_t seq = 0;
    uint64_t ack = 0;
    uint64_t history = 0;
    uint64_t base_tick = 0;

    static constexpr auto reliable_wire = wire::describe(
        wire::field<&DatagramHead::magic>("magic", wire::bits(8)),
        wire::field<&DatagramHead::base_tick>("base_tick", wire::varuint(5))
    );

    static constexpr auto unreliable_wire = wire::describe(
        wire::field<&DatagramHead::magic>("magic", wire::bits(8)),
        wire::field<&DatagramHead::seq>("seq", wire::bits(16)),
        wire::field<&DatagramHead::base_tick>("base_tick", wire::varuint(5))
    );

    static constexpr auto acked_wire = wire::describe(
        wire::field<&DatagramHead::magic>("magic", wire::bits(8)),
        wire::field<&DatagramHead::seq>("seq", wire::bits(16)),
        wire::field<&DatagramHead::ack>("ack", wire::bits(16)),
        wire::field<&DatagramHead::history>("history", wire::bits(32)),
        wire::field<&DatagramHead::base_tick>("base_tick", wire::varuint(5))
    );
};

struct NetwCarrierFrame {
    enum Magic {
        MAGIC_RELIABLE = 0x57,
        MAGIC_UNRELIABLE = 0x77,
        MAGIC_UNRELIABLE_ACKED = 0x97,
    };

    enum Kind {
        FOREIGN,
        MALFORMED,
        RELIABLE,
        UNRELIABLE,
        UNRELIABLE_ACKED,
    };

    static bool magic_is_retired(uint8_t p_magic);

    static godot::PackedByteArray build(
        const godot::PackedByteArray &p_payload,
        bool p_reliable,
        int64_t p_seq,
        int64_t p_ack,
        uint32_t p_history,
        int64_t p_tick
    );

    static NetwCarrierFrame read(const godot::PackedByteArray &p_packet);

    static godot::Dictionary spec_records();

    int64_t kind = FOREIGN;
    int64_t seq = 0;
    int64_t ack = 0;
    uint32_t history = 0;
    int64_t tick = -1;
    int64_t payload_offset = 0;
};

struct NetwCarrierDatagram {
    godot::PackedByteArray bytes;
    int64_t seq = -1;
};

} // namespace netw

#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/wire/describe.hpp"

namespace netw::wire {

enum class ControlTag : uint8_t {
    OPEN = 0,
    READY = 1,
    ACCEPT = 2,
    RESET = 3,
    CLOSE = 4,
};

enum class StreamFamily : uint8_t {
    VOLATILE = 0,
    RETAINED = 1,
    WINDOW = 2,
};

constexpr uint8_t STREAM_FAMILY_CEILING = 2;

constexpr uint32_t ACCEPT_MIN_ENTRIES = 1;
constexpr uint32_t ACCEPT_MAX_ENTRIES = 32;
constexpr uint32_t ACCEPT_MAX_PAYLOAD_BYTES = 256;

struct ControlOpen {
    uint64_t tag = uint64_t(ControlTag::OPEN);
    uint64_t request = 0;
    uint64_t route = 0;
    uint64_t ordinal = 0;
    uint64_t family = 0;
    uint64_t epoch = 0;
    uint64_t tenure = 0;
    uint64_t schema = 0;

    static constexpr auto wire = describe(
        field<&ControlOpen::tag>("tag", bits(8)),
        field<&ControlOpen::request>("request", varuint(10)),
        field<&ControlOpen::route>("route", varuint(5)),
        field<&ControlOpen::ordinal>("ordinal", bits(8)),
        field<&ControlOpen::family>("family", bits(8)),
        field<&ControlOpen::epoch>("epoch", varuint(3)),
        field<&ControlOpen::tenure>("tenure", varuint(5)),
        field<&ControlOpen::schema>("schema", bits(32))
    );
};

struct ControlReady {
    uint64_t tag = uint64_t(ControlTag::READY);
    uint64_t request = 0;
    uint64_t token = 0;

    static constexpr auto wire = describe(
        field<&ControlReady::tag>("tag", bits(8)),
        field<&ControlReady::request>("request", varuint(10)),
        field<&ControlReady::token>("token", varuint(10))
    );
};

struct ControlReset {
    uint64_t tag = uint64_t(ControlTag::RESET);
    uint64_t request = 0;
    uint64_t token = 0;

    static constexpr auto wire = describe(
        field<&ControlReset::tag>("tag", bits(8)),
        field<&ControlReset::request>("request", varuint(10)),
        field<&ControlReset::token>("token", varuint(10))
    );
};

struct ControlClose {
    uint64_t tag = uint64_t(ControlTag::CLOSE);
    uint64_t token = 0;

    static constexpr auto wire = describe(
        field<&ControlClose::tag>("tag", bits(8)),
        field<&ControlClose::token>("token", varuint(10))
    );
};

struct ControlAcceptHead {
    uint64_t tag = uint64_t(ControlTag::ACCEPT);
    int64_t count = int64_t(ACCEPT_MIN_ENTRIES);

    static constexpr auto wire = describe(
        field<&ControlAcceptHead::tag>("tag", bits(8)),
        field<&ControlAcceptHead::count>(
            "count",
            int_range(int64_t(ACCEPT_MIN_ENTRIES), int64_t(ACCEPT_MAX_ENTRIES))
        )
    );
};

struct ControlAcceptEntry {
    uint64_t token = 0;
    uint64_t revision = 0;

    static constexpr auto wire = describe(
        field<&ControlAcceptEntry::token>("token", varuint(10)),
        field<&ControlAcceptEntry::revision>("revision", varuint(10))
    );
};

struct ControlRecord {
    ControlTag tag = ControlTag::OPEN;
    uint64_t request = 0;
    uint64_t token = 0;
    int64_t route = 0;
    uint8_t ordinal = 0;
    StreamFamily family = StreamFamily::VOLATILE;
    uint64_t epoch = 0;
    uint64_t tenure = 0;
    uint32_t schema = 0;
    godot::LocalVector<ControlAcceptEntry> receipts;
};

godot::PackedByteArray write_control_record(const ControlRecord &p_record);

bool read_control_record(
    const godot::PackedByteArray &p_bytes,
    ControlRecord &r_record
);

godot::Dictionary control_spec_records();

} // namespace netw::wire

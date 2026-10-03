#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
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

constexpr uint32_t CONTROL_BASIS_CAPACITY = 4096;

struct ControlOpenHead {
    uint64_t tag = uint64_t(ControlTag::OPEN);
    int64_t request_step = 0;
    uint64_t route = 0;
    uint64_t family = 0;
    bool has_ordinal = false;
    bool has_epoch = false;
    bool has_tenure = false;
    bool has_anchor = false;
    bool has_anchor_author = false;
    bool has_schema = false;

    static constexpr auto wire = describe(
        field<&ControlOpenHead::tag>("tag", bits(8)),
        field<&ControlOpenHead::request_step>("request_step", svarint(10)),
        field<&ControlOpenHead::route>("route", varuint(5)),
        field<&ControlOpenHead::family>("family", bits(2)),
        field<&ControlOpenHead::has_ordinal>("has_ordinal", bool1()),
        field<&ControlOpenHead::has_epoch>("has_epoch", bool1()),
        field<&ControlOpenHead::has_tenure>("has_tenure", bool1()),
        field<&ControlOpenHead::has_anchor>("has_anchor", bool1()),
        field<&ControlOpenHead::has_anchor_author>(
            "has_anchor_author",
            bool1()
        ),
        field<&ControlOpenHead::has_schema>("has_schema", bool1())
    );
};

struct ControlReady {
    uint64_t tag = uint64_t(ControlTag::READY);
    int64_t request_step = 0;
    int64_t token_step = 0;

    static constexpr auto wire = describe(
        field<&ControlReady::tag>("tag", bits(8)),
        field<&ControlReady::request_step>("request_step", svarint(10)),
        field<&ControlReady::token_step>("token_step", svarint(10))
    );
};

struct ControlReset {
    uint64_t tag = uint64_t(ControlTag::RESET);
    int64_t request_step = 0;
    uint64_t token = 0;

    static constexpr auto wire = describe(
        field<&ControlReset::tag>("tag", bits(8)),
        field<&ControlReset::request_step>("request_step", svarint(10)),
        field<&ControlReset::token>("token", varuint(10))
    );
};

struct ControlClose {
    uint64_t tag = uint64_t(ControlTag::CLOSE);
    int64_t token_step = 0;

    static constexpr auto wire = describe(
        field<&ControlClose::tag>("tag", bits(8)),
        field<&ControlClose::token_step>("token_step", svarint(10))
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

struct ControlAcceptStep {
    int64_t token_step = 0;
    uint64_t revision = 0;

    static constexpr auto wire = describe(
        field<&ControlAcceptStep::token_step>("token_step", svarint(10)),
        field<&ControlAcceptStep::revision>("revision", varuint(10))
    );
};

struct ControlAcceptEntry {
    uint64_t token = 0;
    uint64_t revision = 0;
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
    uint64_t anchor = 0;
    uint64_t anchor_author = 0;
    uint32_t schema = 0;
    bool resolved = true;
    godot::LocalVector<ControlAcceptEntry> receipts;
};

struct ControlOpenBasis {
    uint64_t epoch = 0;
    uint64_t tenure = 0;
    uint64_t anchor = 0;
    uint64_t anchor_author = 0;
    uint32_t schema = 0;

    uint8_t check() const;
};

class ControlBasis {
    godot::HashMap<uint64_t, ControlOpenBasis> opens;
    godot::LocalVector<uint64_t> arrivals;
    uint32_t oldest = 0;

public:
    static uint64_t key_of(int64_t p_route, uint8_t p_ordinal) {
        return (uint64_t(p_route) << 8) | uint64_t(p_ordinal);
    }

    const ControlOpenBasis *find(uint64_t p_key) const {
        return opens.getptr(p_key);
    }

    void note(const ControlRecord &p_record);

    uint32_t size() const {
        return opens.size();
    }

    void clear();
};

struct ControlCursor {
    uint64_t opened = 0;
    uint64_t answered = 0;
    uint64_t minted = 0;
    uint64_t closed = 0;
};

class ControlFrameWriter {
    ControlBasis &basis;
    ControlCursor cursor;
    WriteStream stream;
    uint32_t records = 0;
    bool healthy = true;

public:
    explicit ControlFrameWriter(ControlBasis &p_basis) : basis(p_basis) {}

    int64_t cost(const ControlRecord &p_record) const;

    bool append(const ControlRecord &p_record);

    uint32_t count() const {
        return records;
    }

    godot::PackedByteArray bytes() const;
};

bool read_control_frame(
    const godot::PackedByteArray &p_bytes,
    ControlBasis &p_basis,
    godot::LocalVector<ControlRecord> &r_records
);

godot::PackedByteArray write_control_record(const ControlRecord &p_record);

bool read_control_record(
    const godot::PackedByteArray &p_bytes,
    ControlRecord &r_record
);

godot::Dictionary control_spec_records();

} // namespace netw::wire

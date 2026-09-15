#include "netw/repl/snapshot_frame.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/repl/row_codec.hpp"
#include "netw/wire/stream.hpp"

namespace netw::repl {

using namespace godot;
using wire::ReadStream;
using wire::WriteStream;

namespace {

bool naming_is_legal(uint64_t p_revision, uint64_t p_distance) {
    if (p_revision < 1 || p_distance > SNAPSHOT_DISTANCE_CEILING) {
        return false;
    }
    return p_distance < p_revision;
}

template <class Stream>
bool carry_naming(
    Stream &p_stream,
    uint64_t &r_token,
    uint64_t &r_revision,
    uint64_t &r_distance
) {
    return p_stream.varuint(r_token, 10) && p_stream.varuint(r_revision, 10)
        && p_stream.bits(r_distance, SNAPSHOT_DISTANCE_BITS);
}

} // namespace

PackedByteArray write_snapshot_row(
    const SnapshotHeader &p_header,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row,
    const wire::CodeRow *p_baseline
) {
    NETW_ZONE_NC("Snapshot row write", colors::WIRE);
    PackedByteArray out;
    if (!p_plan.valid() || !p_row.valid_for(p_plan)
        || !naming_is_legal(p_header.revision, p_header.distance)) {
        return out;
    }
    const bool stepping = !p_header.absolute();
    if (stepping
        && (p_baseline == nullptr || !p_baseline->valid_for(p_plan)
            || (p_header.mask & ~p_plan.full_mask()) != 0)) {
        return out;
    }
    const uint64_t mask = stepping ? p_header.mask : p_plan.full_mask();

    WriteStream stream;
    uint64_t token = p_header.token;
    uint64_t revision = p_header.revision;
    uint64_t distance = p_header.distance;
    uint64_t carried_mask = p_header.mask;
    if (!carry_naming(stream, token, revision, distance)
        || !write_ack_flag(stream, p_base_tick, p_header.reconcile_ack)
        || !write_stamp_flag(stream, p_header.tick)
        || (stepping && !stream.bits(carried_mask, int(p_plan.mask_width())))
        || !write_ack_step(stream, p_base_tick, p_header.reconcile_ack)
        || !write_stamp(stream, p_base_tick, p_header.tick)
        || !write_row_columns(
            stream,
            p_plan,
            p_row,
            mask,
            stepping ? p_baseline : nullptr
        )) {
        return PackedByteArray();
    }
    if (!stream.align_verify() || !stream.ok()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool name_snapshot_row(
    const PackedByteArray &p_bytes,
    SnapshotHeader &r_header
) {
    ReadStream stream(p_bytes);
    uint64_t token = 0;
    uint64_t revision = 0;
    uint64_t distance = 0;
    if (!carry_naming(stream, token, revision, distance)
        || !naming_is_legal(revision, distance)) {
        return false;
    }
    r_header.token = token;
    r_header.revision = revision;
    r_header.distance = distance;
    return true;
}

bool read_snapshot_row(
    const PackedByteArray &p_bytes,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    SnapshotHeader &r_header,
    wire::CodeRow &r_row,
    const wire::CodeRow *p_baseline,
    SnapshotRefusal *r_refusal
) {
    NETW_ZONE_NC("Snapshot row read", colors::WIRE);
    if (r_refusal != nullptr) {
        *r_refusal = SnapshotRefusal::MALFORMED;
    }
    if (!p_plan.valid()) {
        NETW_DEBUG(
            sys::WIRE,
            "A snapshot row arrived for a plan this peer cannot decode with."
        );
        return false;
    }
    ReadStream stream(p_bytes);
    uint64_t token = 0;
    uint64_t revision = 0;
    uint64_t distance = 0;
    bool has_ack = false;
    bool stamped = false;
    if (!carry_naming(stream, token, revision, distance)
        || !naming_is_legal(revision, distance)) {
        NETW_DEBUG(sys::WIRE, "A snapshot row ended inside its naming.");
        return false;
    }
    const bool stepping = distance != 0;
    uint64_t mask = p_plan.full_mask();
    int64_t ack = -1;
    int64_t tick = -1;
    if (!read_ack_flag(stream, has_ack) || !read_stamp_flag(stream, stamped)
        || (stepping && !stream.bits(mask, int(p_plan.mask_width())))
        || !read_ack_step(stream, has_ack, p_base_tick, ack)
        || !read_stamp(stream, stamped, p_base_tick, tick)) {
        NETW_DEBUG(sys::WIRE, "A snapshot row ended inside its header.");
        return false;
    }
    if ((mask & ~p_plan.full_mask()) != 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A snapshot row carries mask %d, which its plan does not declare.",
            int(mask)
        );
        return false;
    }
    if (stepping && (p_baseline == nullptr || !p_baseline->valid_for(p_plan))) {
        NETW_DEBUG(
            sys::WIRE,
            "A snapshot row at revision %d steps from revision %d, which this "
            "peer no longer holds.",
            int(revision),
            int(revision - distance)
        );
        if (r_refusal != nullptr) {
            *r_refusal = SnapshotRefusal::BASELINE_UNKNOWN;
        }
        return false;
    }

    wire::CodeRow staged = wire::CodeRow::for_plan(p_plan);
    if (stepping) {
        staged.copy_from(*p_baseline);
    }
    if (!read_row_columns(
            stream,
            p_plan,
            staged,
            mask,
            stepping ? p_baseline : nullptr
        )) {
        NETW_DEBUG(sys::WIRE, "A snapshot row ran out inside its columns.");
        return false;
    }
    if (!stream.align_verify() || !stream.ok()
        || stream.bits_remaining() != 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A snapshot row has %d bits left after its columns.",
            int(stream.bits_remaining())
        );
        return false;
    }

    r_header.token = token;
    r_header.revision = revision;
    r_header.distance = distance;
    r_header.tick = tick;
    r_header.reconcile_ack = ack;
    r_header.mask = mask;
    r_row = staged;
    if (r_refusal != nullptr) {
        *r_refusal = SnapshotRefusal::NONE;
    }
    return true;
}

namespace {

template <class Stream>
bool carry_window_naming(
    Stream &p_stream,
    uint64_t &r_token,
    uint64_t &r_revision
) {
    return p_stream.varuint(r_token, 10) && p_stream.varuint(r_revision, 10);
}

} // namespace

PackedByteArray write_snapshot_window(
    const SnapshotHeader &p_header,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    const LocalVector<WindowSample> &p_samples
) {
    NETW_ZONE_NC("Snapshot window write", colors::WIRE);
    PackedByteArray out;
    if (!p_plan.valid() || p_samples.is_empty() || p_samples.size() > 255
        || p_base_tick < 0 || p_header.revision < 1) {
        return out;
    }
    int64_t newest = p_samples[0].tick;
    for (uint32_t at = 1; at < p_samples.size(); ++at) {
        newest = MAX(newest, p_samples[at].tick);
    }
    if (newest < 0) {
        return out;
    }

    WriteStream stream;
    uint64_t token = p_header.token;
    uint64_t revision = p_header.revision;
    int64_t count = int64_t(p_samples.size());
    if (!carry_window_naming(stream, token, revision)
        || !stream.int_range(count, 1, 255)
        || !write_ack_flag(stream, p_base_tick, p_header.reconcile_ack)
        || !write_ack_step(stream, p_base_tick, p_header.reconcile_ack)
        || !write_stamp(stream, p_base_tick, newest)) {
        return out;
    }
    for (uint32_t at = 0; at < p_samples.size(); ++at) {
        const WindowSample &sample = p_samples[at];
        if (sample.tick > newest || !sample.row.valid_for(p_plan)) {
            return PackedByteArray();
        }
        uint64_t age = uint64_t(newest - sample.tick);
        if (!stream.varuint(age, 3)
            || !write_row_columns(
                stream,
                p_plan,
                sample.row,
                p_plan.full_mask(),
                nullptr
            )) {
            return PackedByteArray();
        }
    }
    if (!stream.align_verify() || !stream.ok()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool name_snapshot_window(
    const PackedByteArray &p_bytes,
    SnapshotHeader &r_header
) {
    ReadStream stream(p_bytes);
    uint64_t token = 0;
    uint64_t revision = 0;
    if (!carry_window_naming(stream, token, revision) || revision < 1) {
        return false;
    }
    r_header.token = token;
    r_header.revision = revision;
    r_header.distance = 0;
    return true;
}

bool read_snapshot_window(
    const PackedByteArray &p_bytes,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    SnapshotHeader &r_header,
    LocalVector<WindowSample> &r_samples
) {
    NETW_ZONE_NC("Snapshot window read", colors::WIRE);
    if (!p_plan.valid() || p_base_tick < 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A window arrived for a plan this peer cannot decode with, or on "
            "a datagram that names no tick."
        );
        return false;
    }
    ReadStream stream(p_bytes);
    uint64_t token = 0;
    uint64_t revision = 0;
    int64_t count = 0;
    int64_t ack = -1;
    int64_t newest = -1;
    bool has_ack = false;
    if (!carry_window_naming(stream, token, revision) || revision < 1
        || !stream.int_range(count, 1, 255)
        || !read_ack_flag(stream, has_ack)
        || !read_ack_step(stream, has_ack, p_base_tick, ack)
        || !read_stamp(stream, true, p_base_tick, newest)) {
        NETW_DEBUG(sys::WIRE, "A window ended inside its header.");
        return false;
    }

    LocalVector<WindowSample> read;
    for (int64_t at = 0; at < count; ++at) {
        uint64_t age = 0;
        if (!stream.varuint(age, 3)) {
            NETW_DEBUG(
                sys::WIRE,
                "A window ended before sample %d of %d.",
                int(at),
                int(count)
            );
            return false;
        }
        WindowSample sample;
        sample.tick = newest - int64_t(age);
        sample.row = wire::CodeRow::for_plan(p_plan);
        if (!read_row_columns(
                stream,
                p_plan,
                sample.row,
                p_plan.full_mask(),
                nullptr
            )) {
            NETW_DEBUG(
                sys::WIRE,
                "Sample %d of a window ran out inside its columns.",
                int(at)
            );
            return false;
        }
        read.push_back(sample);
    }
    if (!stream.align_verify() || !stream.ok()
        || stream.bits_remaining() != 0) {
        NETW_DEBUG(sys::WIRE, "A window has bits left after its samples.");
        return false;
    }
    r_header.token = token;
    r_header.revision = revision;
    r_header.distance = 0;
    r_header.tick = newest;
    r_header.reconcile_ack = ack;
    r_header.mask = p_plan.full_mask();
    r_samples = read;
    return true;
}

} // namespace netw::repl

#include "netw/repl/row_frame.hpp"

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

bool life_is_writable(int64_t p_life) {
    return p_life >= 0 && p_life < ROW_LIFE_WRAP;
}

bool life_admits(int64_t p_expected, uint64_t p_declared) {
    return p_expected < 0
        || (p_expected % ROW_LIFE_WRAP) == int64_t(p_declared);
}

} // namespace

PackedByteArray write_window_frame(
    const RowFrameHeader &p_header,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    const LocalVector<WindowSample> &p_samples
) {
    NETW_ZONE_NC("Window frame write", colors::WIRE);
    PackedByteArray out;
    if (!p_plan.valid() || p_samples.is_empty() || p_samples.size() > 255
        || p_base_tick < 0 || !life_is_writable(p_header.life)) {
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
    uint64_t life = uint64_t(p_header.life);
    int64_t count = int64_t(p_samples.size());
    if (!stream.bits(life, ROW_LIFE_BITS) || !stream.int_range(count, 1, 255)
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
                at == 0 ? nullptr : &p_samples[at - 1].row
            )) {
            return PackedByteArray();
        }
    }
    if (!stream.align_verify() || !stream.ok()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool read_window_frame(
    const PackedByteArray &p_bytes,
    int64_t p_base_tick,
    int64_t p_expected_life,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    LocalVector<WindowSample> &r_samples
) {
    NETW_ZONE_NC("Window frame read", colors::WIRE);
    if (!p_plan.valid() || p_base_tick < 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A window frame arrived for a plan this peer cannot decode with, "
            "or on a datagram that names no tick."
        );
        return false;
    }
    ReadStream stream(p_bytes);
    uint64_t life = 0;
    int64_t count = 0;
    int64_t ack = -1;
    int64_t newest = -1;
    bool has_ack = false;
    if (!stream.bits(life, ROW_LIFE_BITS) || !stream.int_range(count, 1, 255)
        || !read_ack_flag(stream, has_ack)
        || !read_ack_step(stream, has_ack, p_base_tick, ack)
        || !read_stamp(stream, true, p_base_tick, newest)) {
        NETW_DEBUG(sys::WIRE, "A window frame ended inside its header.");
        return false;
    }
    if (!life_admits(p_expected_life, life)) {
        NETW_DEBUG(
            sys::WIRE,
            "A window frame declares life %d against a route living at %d.",
            int(life),
            int(p_expected_life)
        );
        return false;
    }

    LocalVector<WindowSample> read;
    for (int64_t at = 0; at < count; ++at) {
        uint64_t age = 0;
        if (!stream.varuint(age, 3)) {
            NETW_DEBUG(
                sys::WIRE,
                "A window frame ended before sample %d of %d.",
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
                read.is_empty() ? nullptr : &read[read.size() - 1].row
            )) {
            NETW_DEBUG(
                sys::WIRE,
                "Sample %d of a window frame ran out inside its columns.",
                int(at)
            );
            return false;
        }
        read.push_back(sample);
    }
    if (!stream.align_verify() || !stream.ok()
        || stream.bits_remaining() != 0) {
        return false;
    }

    r_header.life = int64_t(life);
    r_header.tick = newest;
    r_header.reconcile_ack = ack;
    r_header.mask = p_plan.full_mask();
    r_samples = read;
    return true;
}

} // namespace netw::repl

#include "netw/repl/row_frame.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/stream.hpp"

namespace netw::repl {

using namespace godot;
using wire::ReadStream;
using wire::WriteStream;

namespace {

// Header fields ride in one order both sides walk. varuint byte-aligns, so the
// route is written first and the rest follow it packed.
bool head(WriteStream &p_stream, RowFrameHeader &p_header, uint32_t p_width) {
    uint64_t route = uint64_t(p_header.route);
    uint64_t comp = p_header.comp;
    uint64_t channel = p_header.channel;
    int64_t tick = p_header.tick;
    int64_t ack = p_header.reconcile_ack;
    return p_stream.varuint(route)
        && p_stream.bits(comp, 8)
        && p_stream.bits(channel, 8)
        && p_stream.svarint(tick)
        && p_stream.svarint(ack)
        && p_stream.bits(p_header.mask, int(p_width));
}

bool write_columns(
    WriteStream &p_stream,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row
) {
    for (uint32_t index = 0; index < p_plan.column_count(); ++index) {
        const wire::ColumnPlan &slot = p_plan.column(index);
        for (int element = 0; element < slot.stride; ++element) {
            const int64_t at = slot.offset + int64_t(element) * slot.width;
            uint64_t code = p_row.read_bits(at, slot.width);
            if (!p_stream.bits(code, slot.width)) {
                return false;
            }
        }
    }
    return true;
}

bool read_columns(
    ReadStream &p_stream,
    const wire::WirePlan &p_plan,
    wire::CodeRow &r_row
) {
    for (uint32_t index = 0; index < p_plan.column_count(); ++index) {
        const wire::ColumnPlan &slot = p_plan.column(index);
        for (int element = 0; element < slot.stride; ++element) {
            uint64_t code = 0;
            if (!p_stream.bits(code, slot.width)) {
                return false;
            }
            const int64_t at = slot.offset + int64_t(element) * slot.width;
            if (!r_row.write_bits(at, slot.width, code)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

PackedByteArray write_row_frame(
    const RowFrameHeader &p_header,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row
) {
    NETW_ZONE_NC("Row frame write", colors::WIRE);
    PackedByteArray out;
    if (!p_plan.valid() || !p_row.valid_for(p_plan) || p_header.mask == 0) {
        return out;
    }
    if ((p_header.mask & ~p_plan.full_mask()) != 0) {
        return out;
    }

    WriteStream stream;
    RowFrameHeader staged = p_header;
    if (!head(stream, staged, p_plan.mask_width())) {
        return out;
    }
    for (uint32_t index = 0; index < p_plan.column_count(); ++index) {
        if ((p_header.mask & (uint64_t(1) << index)) == 0) {
            continue;
        }
        const wire::ColumnPlan &slot = p_plan.column(index);
        for (int element = 0; element < slot.stride; ++element) {
            const int64_t at = slot.offset + int64_t(element) * slot.width;
            uint64_t code = p_row.read_bits(at, slot.width);
            if (!stream.bits(code, slot.width)) {
                return PackedByteArray();
            }
        }
    }
    if (!stream.align_verify() || !stream.ok()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool read_row_frame(
    const PackedByteArray &p_bytes,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    wire::CodeRow &r_row
) {
    NETW_ZONE_NC("Row frame read", colors::WIRE);
    if (!p_plan.valid() || !r_row.valid_for(p_plan)) {
        NETW_DEBUG(
            sys::WIRE,
            "A row frame arrived for a plan this peer cannot decode with."
        );
        return false;
    }
    ReadStream stream(p_bytes);
    uint64_t route = 0;
    uint64_t comp = 0;
    uint64_t channel = 0;
    int64_t tick = -1;
    int64_t ack = -1;
    uint64_t mask = 0;
    if (!stream.varuint(route) || !stream.bits(comp, 8)
        || !stream.bits(channel, 8) || !stream.svarint(tick)
        || !stream.svarint(ack)
        || !stream.bits(mask, int(p_plan.mask_width()))) {
        NETW_DEBUG(sys::WIRE, "A row frame ended inside its header.");
        return false;
    }
    if (mask == 0 || (mask & ~p_plan.full_mask()) != 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A row frame on route %d carries mask %d, which its plan does not "
            "declare.",
            int(route),
            int(mask)
        );
        return false;
    }

    wire::CodeRow staged = r_row;
    for (uint32_t index = 0; index < p_plan.column_count(); ++index) {
        if ((mask & (uint64_t(1) << index)) == 0) {
            continue;
        }
        const wire::ColumnPlan &slot = p_plan.column(index);
        for (int element = 0; element < slot.stride; ++element) {
            uint64_t code = 0;
            if (!stream.bits(code, slot.width)) {
                NETW_DEBUG(
                    sys::WIRE,
                    "A row frame on route %d ran out inside column %d.",
                    int(route),
                    int(index)
                );
                return false;
            }
            const int64_t at = slot.offset + int64_t(element) * slot.width;
            if (!staged.write_bits(at, slot.width, code)) {
                NETW_DEBUG(
                    sys::WIRE,
                    "Column %d of route %d does not fit the staged row.",
                    int(index),
                    int(route)
                );
                return false;
            }
        }
    }
    if (!stream.align_verify() || !stream.ok() || stream.bits_remaining() != 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A row frame on route %d has %d bits left after its columns.",
            int(route),
            int(stream.bits_remaining())
        );
        return false;
    }

    r_header.route = int64_t(route);
    r_header.comp = uint8_t(comp);
    r_header.channel = uint8_t(channel);
    r_header.tick = tick;
    r_header.reconcile_ack = ack;
    r_header.mask = mask;
    r_row = staged;
    return true;
}

bool peek_row_frame(const PackedByteArray &p_bytes, RowFrameHeader &r_header) {
    NETW_ZONE_NC("Row frame peek", colors::WIRE);
    ReadStream stream(p_bytes);
    uint64_t route = 0;
    uint64_t comp = 0;
    uint64_t channel = 0;
    int64_t tick = -1;
    int64_t ack = -1;
    if (!stream.varuint(route) || !stream.bits(comp, 8)
        || !stream.bits(channel, 8) || !stream.svarint(tick)
        || !stream.svarint(ack)) {
        return false;
    }
    r_header.route = int64_t(route);
    r_header.comp = uint8_t(comp);
    r_header.channel = uint8_t(channel);
    r_header.tick = tick;
    r_header.reconcile_ack = ack;
    r_header.mask = 0;
    return true;
}

PackedByteArray write_plain_frame(
    const RowFrameHeader &p_header,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row
) {
    NETW_ZONE_NC("Plain frame write", colors::WIRE);
    PackedByteArray out;
    if (!p_plan.valid() || !p_row.valid_for(p_plan)) {
        return out;
    }
    WriteStream stream;
    RowFrameHeader staged = p_header;
    staged.mask = 0;
    if (!head(stream, staged, 0) || !write_columns(stream, p_plan, p_row)) {
        return out;
    }
    if (!stream.align_verify() || !stream.ok()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool read_plain_frame(
    const PackedByteArray &p_bytes,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    wire::CodeRow &r_row
) {
    NETW_ZONE_NC("Plain frame read", colors::WIRE);
    if (!p_plan.valid()) {
        NETW_DEBUG(
            sys::WIRE,
            "A plain frame arrived for a plan this peer cannot decode with."
        );
        return false;
    }
    ReadStream stream(p_bytes);
    uint64_t route = 0;
    uint64_t comp = 0;
    uint64_t channel = 0;
    int64_t tick = -1;
    int64_t ack = -1;
    if (!stream.varuint(route) || !stream.bits(comp, 8)
        || !stream.bits(channel, 8) || !stream.svarint(tick)
        || !stream.svarint(ack)) {
        NETW_DEBUG(sys::WIRE, "A plain frame ended inside its header.");
        return false;
    }

    wire::CodeRow staged = wire::CodeRow::for_plan(p_plan);
    if (!read_columns(stream, p_plan, staged)) {
        NETW_DEBUG(
            sys::WIRE,
            "A plain frame on route %d ran out inside its columns.",
            int(route)
        );
        return false;
    }
    if (!stream.align_verify() || !stream.ok() || stream.bits_remaining() != 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A plain frame on route %d has %d bits left after its columns.",
            int(route),
            int(stream.bits_remaining())
        );
        return false;
    }

    r_header.route = int64_t(route);
    r_header.comp = uint8_t(comp);
    r_header.channel = uint8_t(channel);
    r_header.tick = tick;
    r_header.reconcile_ack = ack;
    r_header.mask = p_plan.full_mask();
    r_row = staged;
    return true;
}

PackedByteArray write_window_frame(
    const RowFrameHeader &p_header,
    const wire::WirePlan &p_plan,
    const LocalVector<WindowSample> &p_samples
) {
    NETW_ZONE_NC("Window frame write", colors::WIRE);
    PackedByteArray out;
    if (!p_plan.valid() || p_samples.is_empty()) {
        return out;
    }

    WriteStream stream;
    RowFrameHeader staged = p_header;
    staged.mask = 0;
    if (!head(stream, staged, 0)) {
        return out;
    }
    uint64_t count = p_samples.size();
    if (!stream.varuint(count)) {
        return out;
    }
    for (uint32_t at = 0; at < p_samples.size(); ++at) {
        const WindowSample &sample = p_samples[at];
        if (sample.tick > p_header.tick || !sample.row.valid_for(p_plan)) {
            return PackedByteArray();
        }
        uint64_t age = uint64_t(p_header.tick - sample.tick);
        if (!stream.varuint(age)
            || !write_columns(stream, p_plan, sample.row)) {
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
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    LocalVector<WindowSample> &r_samples
) {
    NETW_ZONE_NC("Window frame read", colors::WIRE);
    if (!p_plan.valid()) {
        NETW_DEBUG(
            sys::WIRE,
            "A window frame arrived for a plan this peer cannot decode with."
        );
        return false;
    }
    ReadStream stream(p_bytes);
    uint64_t route = 0;
    uint64_t comp = 0;
    uint64_t channel = 0;
    int64_t tick = -1;
    int64_t ack = -1;
    uint64_t count = 0;
    if (!stream.varuint(route) || !stream.bits(comp, 8)
        || !stream.bits(channel, 8) || !stream.svarint(tick)
        || !stream.svarint(ack) || !stream.varuint(count) || count == 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A window frame ended inside its header or declares no samples."
        );
        return false;
    }

    LocalVector<WindowSample> read;
    for (uint64_t at = 0; at < count; ++at) {
        uint64_t age = 0;
        if (!stream.varuint(age)) {
            NETW_DEBUG(
                sys::WIRE,
                "A window frame on route %d ended before sample %d of %d.",
                int(route),
                int(at),
                int(count)
            );
            return false;
        }
        WindowSample sample;
        sample.tick = tick - int64_t(age);
        sample.row = wire::CodeRow::for_plan(p_plan);
        if (!read_columns(stream, p_plan, sample.row)) {
            NETW_DEBUG(
                sys::WIRE,
                "Sample %d of a window frame on route %d ran out inside its "
                "columns.",
                int(at),
                int(route)
            );
            return false;
        }
        read.push_back(sample);
    }
    if (!stream.align_verify() || !stream.ok() || stream.bits_remaining() != 0) {
        return false;
    }

    r_header.route = int64_t(route);
    r_header.comp = uint8_t(comp);
    r_header.channel = uint8_t(channel);
    r_header.tick = tick;
    r_header.reconcile_ack = ack;
    r_header.mask = p_plan.full_mask();
    r_samples = read;
    return true;
}

} // namespace netw::repl

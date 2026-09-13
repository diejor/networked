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

bool life_is_writable(int64_t p_life) {
    return p_life >= 0 && p_life < ROW_LIFE_WRAP;
}

bool life_admits(int64_t p_expected, uint64_t p_declared) {
    return p_expected < 0
        || (p_expected % ROW_LIFE_WRAP) == int64_t(p_declared);
}

bool write_stamp_flag(WriteStream &p_stream, int64_t p_tick) {
    bool stamped = p_tick >= 0;
    return p_stream.bool1(stamped);
}

bool write_stamp(WriteStream &p_stream, int64_t p_base_tick, int64_t p_tick) {
    if (p_tick < 0) {
        return true;
    }
    int64_t delta = p_tick - p_base_tick;
    return p_stream.svarint(delta, 3);
}

bool read_stamp_flag(ReadStream &p_stream, bool &r_stamped) {
    return p_stream.bool1(r_stamped);
}

bool read_stamp(
    ReadStream &p_stream,
    bool p_stamped,
    int64_t p_base_tick,
    int64_t &r_tick
) {
    r_tick = -1;
    if (!p_stamped) {
        return true;
    }
    int64_t delta = 0;
    if (!p_stream.svarint(delta, 3)) {
        return false;
    }
    r_tick = p_base_tick + delta;
    return r_tick >= 0;
}

bool ack_is_writable(int64_t p_base_tick, int64_t p_ack) {
    return p_ack >= 0 && p_base_tick >= 0;
}

bool write_ack_flag(WriteStream &p_stream, int64_t p_base_tick, int64_t p_ack) {
    bool carried = ack_is_writable(p_base_tick, p_ack);
    return p_stream.bool1(carried);
}

bool write_ack_step(WriteStream &p_stream, int64_t p_base_tick, int64_t p_ack) {
    if (!ack_is_writable(p_base_tick, p_ack)) {
        return true;
    }
    int64_t delta = p_ack - p_base_tick;
    return p_stream.svarint(delta, 5);
}

bool read_ack_flag(ReadStream &p_stream, bool &r_carried) {
    return p_stream.bool1(r_carried);
}

bool read_ack_step(
    ReadStream &p_stream,
    bool p_carried,
    int64_t p_base_tick,
    int64_t &r_ack
) {
    r_ack = -1;
    if (!p_carried) {
        return true;
    }
    int64_t delta = 0;
    if (!p_stream.svarint(delta, 5) || p_base_tick < 0) {
        return false;
    }
    const int64_t ack = p_base_tick + delta;
    if (ack < 0) {
        return false;
    }
    r_ack = ack;
    return true;
}

uint64_t zigzag(int64_t p_step) {
    return (uint64_t(p_step) << 1) ^ uint64_t(p_step >> 63);
}

int64_t unzigzag(uint64_t p_coded) {
    return int64_t(p_coded >> 1) ^ -int64_t(p_coded & 1);
}

int bucket_of(uint64_t p_coded, int p_width) {
    for (int which = 0; which < 3; ++which) {
        const int span = wire::LADDER_BUCKET_BITS[which];
        if (span >= p_width) {
            break;
        }
        if (p_coded < (uint64_t(1) << span)) {
            return which + 1;
        }
    }
    return 0;
}

bool write_element(
    WriteStream &p_stream,
    const wire::ColumnPlan &p_slot,
    uint64_t p_code,
    uint64_t p_base_code
) {
    const int64_t step = int64_t(p_code) - int64_t(p_base_code);
    const uint64_t coded = zigzag(step);
    uint64_t selector = uint64_t(bucket_of(coded, p_slot.width));
    if (!p_stream.bits(selector, wire::LADDER_SELECTOR_BITS)) {
        return false;
    }
    if (selector == 0) {
        uint64_t whole = p_code;
        return p_stream.bits(whole, p_slot.width);
    }
    uint64_t body = coded;
    return p_stream.bits(body, wire::LADDER_BUCKET_BITS[selector - 1]);
}

bool read_element(
    ReadStream &p_stream,
    const wire::ColumnPlan &p_slot,
    uint64_t p_base_code,
    uint64_t &r_code
) {
    uint64_t selector = 0;
    if (!p_stream.bits(selector, wire::LADDER_SELECTOR_BITS)) {
        return false;
    }
    if (selector == 0) {
        return p_stream.bits(r_code, p_slot.width);
    }
    const int span = wire::LADDER_BUCKET_BITS[selector - 1];
    if (span >= p_slot.width) {
        return false;
    }
    uint64_t body = 0;
    if (!p_stream.bits(body, span)) {
        return false;
    }
    if (int(selector) != bucket_of(body, p_slot.width)) {
        return false;
    }
    const int64_t code = int64_t(p_base_code) + unzigzag(body);
    const int64_t ceiling
        = p_slot.width >= 63 ? INT64_MAX : int64_t(uint64_t(1) << p_slot.width);
    if (code < 0 || code >= ceiling) {
        return false;
    }
    r_code = uint64_t(code);
    return true;
}

bool write_columns(
    WriteStream &p_stream,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row,
    uint64_t p_mask,
    const wire::CodeRow *p_baseline
) {
    for (uint32_t index = 0; index < p_plan.column_count(); ++index) {
        if ((p_mask & (uint64_t(1) << index)) == 0) {
            continue;
        }
        const wire::ColumnPlan &slot = p_plan.column(index);
        const bool stepping = p_baseline != nullptr && slot.laddered();
        for (int element = 0; element < slot.stride; ++element) {
            const int64_t at = slot.offset + int64_t(element) * slot.width;
            uint64_t code = p_row.read_bits(at, slot.width);
            const bool wrote = stepping
                ? write_element(
                      p_stream,
                      slot,
                      code,
                      p_baseline->read_bits(at, slot.width)
                  )
                : p_stream.bits(code, slot.width);
            if (!wrote) {
                return false;
            }
        }
    }
    return true;
}

bool read_columns(
    ReadStream &p_stream,
    const wire::WirePlan &p_plan,
    wire::CodeRow &r_row,
    uint64_t p_mask,
    const wire::CodeRow *p_baseline
) {
    for (uint32_t index = 0; index < p_plan.column_count(); ++index) {
        if ((p_mask & (uint64_t(1) << index)) == 0) {
            continue;
        }
        const wire::ColumnPlan &slot = p_plan.column(index);
        const bool stepping = p_baseline != nullptr && slot.laddered();
        for (int element = 0; element < slot.stride; ++element) {
            const int64_t at = slot.offset + int64_t(element) * slot.width;
            uint64_t code = 0;
            const bool read = stepping
                ? read_element(
                      p_stream,
                      slot,
                      p_baseline->read_bits(at, slot.width),
                      code
                  )
                : p_stream.bits(code, slot.width);
            if (!read || !r_row.write_bits(at, slot.width, code)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

bool mask_ladders(const wire::WirePlan &p_plan, uint64_t p_mask) {
    for (uint32_t index = 0; index < p_plan.column_count(); ++index) {
        if ((p_mask & (uint64_t(1) << index)) != 0
            && p_plan.column(index).laddered()) {
            return true;
        }
    }
    return false;
}

PackedByteArray write_row_frame(
    const RowFrameHeader &p_header,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row,
    const RowBaseline &p_baseline
) {
    NETW_ZONE_NC("Row frame write", colors::WIRE);
    PackedByteArray out;
    if (!p_plan.valid() || !p_row.valid_for(p_plan) || p_header.mask == 0
        || !life_is_writable(p_header.life)) {
        return out;
    }
    if ((p_header.mask & ~p_plan.full_mask()) != 0) {
        return out;
    }
    const bool ladders = mask_ladders(p_plan, p_header.mask);
    const bool stepping
        = ladders && p_baseline.named() && p_baseline.row->valid_for(p_plan);
    const bool names_a_seq
        = stepping && p_baseline.naming == BaselineNaming::BY_SEQ;

    WriteStream stream;
    uint64_t life = uint64_t(p_header.life);
    uint64_t mask = p_header.mask;
    bool has_base = stepping;
    uint64_t base_low = uint64_t(p_baseline.low);
    if (!stream.bits(life, ROW_LIFE_BITS)
        || !stream.bits(mask, int(p_plan.mask_width()))
        || !write_ack_flag(stream, p_base_tick, p_header.reconcile_ack)
        || !write_stamp_flag(stream, p_header.tick)
        || (ladders && !stream.bool1(has_base))
        || (names_a_seq && !stream.bits(base_low, 8))
        || !write_ack_step(stream, p_base_tick, p_header.reconcile_ack)
        || !write_stamp(stream, p_base_tick, p_header.tick)
        || !write_columns(
            stream,
            p_plan,
            p_row,
            p_header.mask,
            stepping ? p_baseline.row : nullptr
        )) {
        return PackedByteArray();
    }
    if (!stream.align_verify() || !stream.ok()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool read_row_frame(
    const PackedByteArray &p_bytes,
    int64_t p_base_tick,
    int64_t p_expected_life,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    wire::CodeRow &r_row,
    const RowBaselineSource &p_source,
    RowRefusal *r_refusal
) {
    NETW_ZONE_NC("Row frame read", colors::WIRE);
    if (r_refusal != nullptr) {
        *r_refusal = RowRefusal::MALFORMED;
    }
    if (!p_plan.valid() || !r_row.valid_for(p_plan)) {
        NETW_DEBUG(
            sys::WIRE,
            "A row frame arrived for a plan this peer cannot decode with."
        );
        return false;
    }
    ReadStream stream(p_bytes);
    uint64_t life = 0;
    uint64_t mask = 0;
    int64_t ack = -1;
    int64_t tick = -1;
    uint64_t base_low = 0;
    bool has_ack = false;
    bool stamped = false;
    bool has_base = false;
    if (!stream.bits(life, ROW_LIFE_BITS)
        || !stream.bits(mask, int(p_plan.mask_width()))
        || !read_ack_flag(stream, has_ack)
        || !read_stamp_flag(stream, stamped)) {
        NETW_DEBUG(sys::WIRE, "A row frame ended inside its header.");
        return false;
    }
    const bool reads_a_seq = p_source.naming == BaselineNaming::BY_SEQ;
    if (mask_ladders(p_plan, mask)
        && (!stream.bool1(has_base)
            || (has_base && reads_a_seq && !stream.bits(base_low, 8)))) {
        NETW_DEBUG(sys::WIRE, "A row frame ended inside its baseline name.");
        return false;
    }
    if (!read_ack_step(stream, has_ack, p_base_tick, ack)
        || !read_stamp(stream, stamped, p_base_tick, tick)) {
        NETW_DEBUG(sys::WIRE, "A row frame ended inside its header.");
        return false;
    }
    if (!life_admits(p_expected_life, life)) {
        NETW_DEBUG(
            sys::WIRE,
            "A row frame declares life %d against a route living at %d.",
            int(life),
            int(p_expected_life)
        );
        return false;
    }
    if (mask == 0 || (mask & ~p_plan.full_mask()) != 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A row frame carries mask %d, which its plan does not declare.",
            int(mask)
        );
        return false;
    }

    wire::CodeRow ordered;
    const wire::CodeRow *baseline = nullptr;
    if (has_base && !reads_a_seq) {
        ordered.copy_from(r_row);
        baseline = &ordered;
    } else if (has_base) {
        baseline = p_source.ring != nullptr && p_source.seq >= 0
            ? p_source.ring->resolve(uint16_t(p_source.seq), uint8_t(base_low))
            : nullptr;
    }
    if (has_base) {
        if (baseline == nullptr || !baseline->valid_for(p_plan)) {
            NETW_DEBUG(
                sys::WIRE,
                "A row frame steps from a baseline at seq low byte %d that "
                "this peer no longer holds.",
                int(base_low)
            );
            if (r_refusal != nullptr) {
                *r_refusal = RowRefusal::BASELINE_UNKNOWN;
            }
            return false;
        }
    }

    wire::CodeRow staged = r_row;
    if (!read_columns(stream, p_plan, staged, mask, baseline)) {
        NETW_DEBUG(sys::WIRE, "A row frame ran out inside its columns.");
        return false;
    }
    if (!stream.align_verify() || !stream.ok()
        || stream.bits_remaining() != 0) {
        NETW_DEBUG(
            sys::WIRE,
            "A row frame has %d bits left after its columns.",
            int(stream.bits_remaining())
        );
        return false;
    }

    r_header.life = int64_t(life);
    r_header.tick = tick;
    r_header.reconcile_ack = ack;
    r_header.mask = mask;
    r_row = staged;
    if (r_refusal != nullptr) {
        *r_refusal = RowRefusal::NONE;
    }
    return true;
}

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
            || !write_columns(
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
        if (!read_columns(
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

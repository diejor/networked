#include "netw/repl/row_codec.hpp"

namespace netw::repl {

using wire::ReadStream;
using wire::WriteStream;

namespace {

bool ack_is_writable(int64_t p_base_tick, int64_t p_ack) {
    return p_ack >= 0 && p_base_tick >= 0;
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

} // namespace

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

bool mask_ladders(const wire::WirePlan &p_plan, uint64_t p_mask) {
    for (uint32_t index = 0; index < p_plan.column_count(); ++index) {
        if ((p_mask & (uint64_t(1) << index)) != 0
            && p_plan.column(index).laddered()) {
            return true;
        }
    }
    return false;
}

bool write_row_columns(
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

bool read_row_columns(
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

} // namespace netw::repl

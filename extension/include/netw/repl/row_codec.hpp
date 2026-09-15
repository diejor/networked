#pragma once

#include <cstdint>

#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/stream.hpp"

namespace netw::repl {

bool mask_ladders(const wire::WirePlan &p_plan, uint64_t p_mask);

bool write_stamp_flag(wire::WriteStream &p_stream, int64_t p_tick);
bool write_stamp(
    wire::WriteStream &p_stream,
    int64_t p_base_tick,
    int64_t p_tick
);
bool read_stamp_flag(wire::ReadStream &p_stream, bool &r_stamped);
bool read_stamp(
    wire::ReadStream &p_stream,
    bool p_stamped,
    int64_t p_base_tick,
    int64_t &r_tick
);

bool write_ack_flag(
    wire::WriteStream &p_stream,
    int64_t p_base_tick,
    int64_t p_ack
);
bool write_ack_step(
    wire::WriteStream &p_stream,
    int64_t p_base_tick,
    int64_t p_ack
);
bool read_ack_flag(wire::ReadStream &p_stream, bool &r_carried);
bool read_ack_step(
    wire::ReadStream &p_stream,
    bool p_carried,
    int64_t p_base_tick,
    int64_t &r_ack
);

bool write_row_columns(
    wire::WriteStream &p_stream,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row,
    uint64_t p_mask,
    const wire::CodeRow *p_baseline
);

bool read_row_columns(
    wire::ReadStream &p_stream,
    const wire::WirePlan &p_plan,
    wire::CodeRow &r_row,
    uint64_t p_mask,
    const wire::CodeRow *p_baseline
);

} // namespace netw::repl

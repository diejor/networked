#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/repl/window_ring.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

struct RowFrameHeader {
    int64_t route = 0;
    uint8_t comp = 0;
    uint8_t channel = 0;
    int64_t tick = -1;
    int64_t reconcile_ack = -1;
    uint64_t mask = 0;
};

godot::PackedByteArray write_row_frame(
    const RowFrameHeader &p_header,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row
);

bool read_row_frame(
    const godot::PackedByteArray &p_bytes,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    wire::CodeRow &r_row
);

bool peek_row_frame(
    const godot::PackedByteArray &p_bytes,
    RowFrameHeader &r_header
);

godot::PackedByteArray write_plain_frame(
    const RowFrameHeader &p_header,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row
);

bool read_plain_frame(
    const godot::PackedByteArray &p_bytes,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    wire::CodeRow &r_row
);

godot::PackedByteArray write_window_frame(
    const RowFrameHeader &p_header,
    const wire::WirePlan &p_plan,
    const godot::LocalVector<WindowSample> &p_samples
);

bool read_window_frame(
    const godot::PackedByteArray &p_bytes,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    godot::LocalVector<WindowSample> &r_samples
);

} // namespace netw::repl

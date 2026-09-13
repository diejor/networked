#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/repl/baseline_ring.hpp"
#include "netw/repl/window_ring.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

constexpr int ROW_LIFE_BITS = 4;
constexpr int ROW_LIFE_WRAP = 1 << ROW_LIFE_BITS;

struct RowFrameHeader {
    int64_t life = 0;
    int64_t tick = -1;
    int64_t reconcile_ack = -1;
    uint64_t mask = 0;
};

struct RowArrival {
    int64_t ordinal = 0;
    int64_t base_tick = -1;
    int64_t life = -1;
    int64_t seq = -1;
};

enum class BaselineNaming : uint8_t {
    BY_SEQ,
    BY_ORDER,
};

struct RowBaseline {
    const wire::CodeRow *row = nullptr;
    uint8_t low = 0;
    BaselineNaming naming = BaselineNaming::BY_SEQ;

    bool named() const {
        return row != nullptr;
    }
};

struct RowBaselineSource {
    const BaselineRing *ring = nullptr;
    int64_t seq = -1;
    BaselineNaming naming = BaselineNaming::BY_SEQ;
};

enum class RowRefusal : uint8_t {
    NONE,
    MALFORMED,
    BASELINE_UNKNOWN,
};

bool mask_ladders(const wire::WirePlan &p_plan, uint64_t p_mask);

godot::PackedByteArray write_row_frame(
    const RowFrameHeader &p_header,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row,
    const RowBaseline &p_baseline = RowBaseline()
);

bool read_row_frame(
    const godot::PackedByteArray &p_bytes,
    int64_t p_base_tick,
    int64_t p_expected_life,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    wire::CodeRow &r_row,
    const RowBaselineSource &p_source = RowBaselineSource(),
    RowRefusal *r_refusal = nullptr
);

godot::PackedByteArray write_window_frame(
    const RowFrameHeader &p_header,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    const godot::LocalVector<WindowSample> &p_samples
);

bool read_window_frame(
    const godot::PackedByteArray &p_bytes,
    int64_t p_base_tick,
    int64_t p_expected_life,
    const wire::WirePlan &p_plan,
    RowFrameHeader &r_header,
    godot::LocalVector<WindowSample> &r_samples
);

} // namespace netw::repl

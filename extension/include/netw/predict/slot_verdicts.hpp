#pragma once

#include <cstdint>

namespace netw::predict {

constexpr int64_t UNROUTED_ORDER_KEY = int64_t(1) << 62;

struct SlotCursors {
    int role = 0;
    int schedule = 0;
    int64_t ack = -1;
    bool ack_advanced = false;
    int64_t last_replayed_label = -1;
    bool last_replayed_fresh = false;
    int64_t last_driven_input_tick = -1;
};

int64_t order_key_for_route(int64_t p_route);

int64_t history_record_tick(
    const SlotCursors &p_cursors,
    int64_t p_fallback_tick
);

bool consumed_unslotted_transition(const SlotCursors &p_cursors);

bool has_consumed_state_tick(
    const SlotCursors &p_cursors,
    int64_t p_state_tick
);

} // namespace netw::predict

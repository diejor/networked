#include "netw/predict/slot_verdicts.hpp"

#include "netw/api/predict.hpp"

namespace netw::predict {

int64_t order_key_for_route(int64_t p_route) {
    return p_route > 0 ? p_route : UNROUTED_ORDER_KEY;
}

int64_t history_record_tick(
    const SlotCursors &p_cursors,
    int64_t p_fallback_tick
) {
    const bool consuming = p_cursors.role == NetwPredict::ROLE_CONSUME;
    const bool framed = p_cursors.schedule == NetwPredict::SCHEDULE_FRAME;
    if (consuming && framed) {
        return p_cursors.ack_advanced && p_cursors.last_replayed_fresh
            ? p_cursors.last_replayed_label + 1
            : -1;
    }
    if (consuming && p_cursors.ack >= 0) {
        return p_cursors.ack_advanced ? p_cursors.ack + 1 : -1;
    }
    if (framed) {
        return p_cursors.ack_advanced ? p_cursors.last_driven_input_tick + 1
                                      : -1;
    }
    return p_fallback_tick;
}

bool consumed_unslotted_transition(const SlotCursors &p_cursors) {
    return p_cursors.role == NetwPredict::ROLE_CONSUME
        && p_cursors.schedule == NetwPredict::SCHEDULE_FRAME
        && p_cursors.ack_advanced && !p_cursors.last_replayed_fresh;
}

bool has_consumed_state_tick(
    const SlotCursors &p_cursors,
    int64_t p_state_tick
) {
    if (p_cursors.role != NetwPredict::ROLE_CONSUME) {
        return true;
    }
    if (p_cursors.schedule == NetwPredict::SCHEDULE_FRAME) {
        return p_cursors.last_replayed_label + 1 >= p_state_tick;
    }
    return p_cursors.ack >= 0 && p_cursors.ack + 1 >= p_state_tick;
}

} // namespace netw::predict

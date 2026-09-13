#include "support/netw_test.h"

#include "netw/api/predict.hpp"
#include "netw/predict/slot_verdicts.hpp"

namespace TestNetwPredictSlotVerdict {

using netw::NetwPredict;
using netw::predict::consumed_unslotted_transition;
using netw::predict::has_consumed_state_tick;
using netw::predict::history_record_tick;
using netw::predict::order_key_for_route;
using netw::predict::SlotCursors;
using netw::predict::UNROUTED_ORDER_KEY;

SlotCursors consuming_frame(bool p_advanced, bool p_fresh, int64_t p_label) {
    SlotCursors read;
    read.role = NetwPredict::ROLE_CONSUME;
    read.schedule = NetwPredict::SCHEDULE_FRAME;
    read.ack_advanced = p_advanced;
    read.last_replayed_fresh = p_fresh;
    read.last_replayed_label = p_label;
    return read;
}

SlotCursors consuming_tick(bool p_advanced, int64_t p_ack) {
    SlotCursors read;
    read.role = NetwPredict::ROLE_CONSUME;
    read.schedule = NetwPredict::SCHEDULE_TICK;
    read.ack_advanced = p_advanced;
    read.ack = p_ack;
    return read;
}

TEST_CASE(
    "[Networked][Predict][Hosted] A routed member sorts by its route and an "
    "unrouted one after every routed member"
) {
    NETW_CHECK_EQ(order_key_for_route(7), int64_t(7));
    NETW_CHECK_EQ(order_key_for_route(0), UNROUTED_ORDER_KEY);
    NETW_CHECK_EQ(order_key_for_route(-1), UNROUTED_ORDER_KEY);
    NETW_CHECK_LT(order_key_for_route(1 << 20), order_key_for_route(-1));
}

TEST_CASE(
    "[Networked][Predict][Hosted] A consuming FRAME pass slots the replayed "
    "label only when the ack moved on a fresh entry"
) {
    NETW_CHECK_EQ(
        history_record_tick(consuming_frame(true, true, 11), 99),
        int64_t(12)
    );
    NETW_CHECK_EQ(
        history_record_tick(consuming_frame(true, false, 11), 99),
        int64_t(-1)
    );
    NETW_CHECK_EQ(
        history_record_tick(consuming_frame(false, true, 11), 99),
        int64_t(-1)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted] A consuming TICK pass that advanced no ack "
    "declines its history slot rather than overwriting it"
) {
    NETW_CHECK_EQ(history_record_tick(consuming_tick(true, 4), 99), int64_t(5));
    NETW_CHECK_EQ(
        history_record_tick(consuming_tick(false, 4), 99),
        int64_t(-1)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted] An authoring pass slots its driven tick on "
    "FRAME and falls back to the pass tick on TICK"
) {
    SlotCursors framed;
    framed.role = NetwPredict::ROLE_PREDICT;
    framed.schedule = NetwPredict::SCHEDULE_FRAME;
    framed.ack_advanced = true;
    framed.last_driven_input_tick = 30;
    NETW_CHECK_EQ(history_record_tick(framed, 99), int64_t(31));
    framed.ack_advanced = false;
    NETW_CHECK_EQ(history_record_tick(framed, 99), int64_t(-1));

    SlotCursors ticked;
    ticked.role = NetwPredict::ROLE_PREDICT;
    ticked.schedule = NetwPredict::SCHEDULE_TICK;
    NETW_CHECK_EQ(history_record_tick(ticked, 99), int64_t(99));
}

TEST_CASE(
    "[Networked][Predict][Hosted] A repeat entry that advanced the ack still "
    "owes its journal row a close"
) {
    CHECK(consumed_unslotted_transition(consuming_frame(true, false, 11)));
    CHECK_FALSE(consumed_unslotted_transition(consuming_frame(true, true, 11)));
    CHECK_FALSE(
        consumed_unslotted_transition(consuming_frame(false, false, 11))
    );
    CHECK_FALSE(consumed_unslotted_transition(consuming_tick(true, 4)));
}

TEST_CASE(
    "[Networked][Predict][Hosted] Only a consuming role gates a view tick, "
    "and it gates on the tier's own cursor"
) {
    SlotCursors predicting;
    predicting.role = NetwPredict::ROLE_PREDICT;
    predicting.schedule = NetwPredict::SCHEDULE_TICK;
    CHECK(has_consumed_state_tick(predicting, 1000));

    CHECK(has_consumed_state_tick(consuming_frame(true, true, 11), 12));
    CHECK_FALSE(has_consumed_state_tick(consuming_frame(true, true, 11), 13));

    CHECK(has_consumed_state_tick(consuming_tick(true, 4), 5));
    CHECK_FALSE(has_consumed_state_tick(consuming_tick(true, 4), 6));
    CHECK_FALSE(has_consumed_state_tick(consuming_tick(false, -1), 0));
}

} // namespace TestNetwPredictSlotVerdict

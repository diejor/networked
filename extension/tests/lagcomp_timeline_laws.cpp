#include "support/netw_test.h"

#include "netw/lagcomp_core.hpp"
#include "support/carrier.h"

namespace TestNetwLagCompTimelineLaws {

#if defined(NETW_TIER_HOSTED)

using namespace godot;
using netw::NetwLagCompCore;
using netw_test::Carrier;

Dictionary at(double p_x) {
    Dictionary out;
    out[StringName("position")] = Vector2(real_t(p_x), 0.0);
    return out;
}

double x_of(const Dictionary &p_row) {
    return double(Vector2(p_row[StringName("position")]).x);
}

Ref<NetwLagCompCore> core() {
    Ref<NetwLagCompCore> out;
    out.instantiate();
    return out;
}

TEST_CASE(
    "[Networked][LagComp][Timeline] a view tick between recordings reads the "
    "state that was standing at it"
) {
    const Ref<NetwLagCompCore> history = core();
    const int64_t slot = history->timeline_open(8);
    CHECK(history->timeline_is_open(slot));

    history->timeline_record(slot, 10, at(1.0));
    history->timeline_record(slot, 14, at(2.0));

    NETW_CHECK_EQ(x_of(history->timeline_sample(slot, 14)), 2.0);
    NETW_CHECK_EQ(x_of(history->timeline_sample(slot, 13)), 1.0);
    NETW_CHECK_EQ(x_of(history->timeline_sample(slot, 10)), 1.0);
    NETW_CHECK_EQ(history->timeline_sample_tick(slot, 13), 10);

    // Older than anything recorded is an ABSENCE, not the oldest row. A
    // caller told "here is where it was" about a tick history never covered
    // would validate a hit against a guess.
    CHECK(history->timeline_sample(slot, 9).is_empty());
    NETW_CHECK_EQ(history->timeline_sample_tick(slot, 9), -1);

    // The store the slot samples is the store the prediction plane reads.
    REQUIRE(history->timeline_history(slot).is_valid());
    NETW_CHECK_EQ(
        x_of(history->timeline_history(slot)->latest_state_at_or_before(13)),
        1.0
    );
    history->timeline_history(slot)->trim_before(14);
    CHECK(history->timeline_sample(slot, 13).is_empty());

    // A tick past the newest reads the newest, because nothing has moved the
    // body since and the record is still the standing truth.
    NETW_CHECK_EQ(x_of(history->timeline_sample(slot, 900)), 2.0);

    history->timeline_close(slot);
    CHECK(!history->timeline_is_open(slot));
    CHECK(history->timeline_sample(slot, 14).is_empty());
}

TEST_CASE(
    "[Networked][LagComp][Timeline] a rewind stands the body where history "
    "says it stood and puts back exactly what it overwrote"
) {
    const Ref<NetwLagCompCore> history = core();
    Carrier *body = memnew(Carrier);
    body->set_position(Vector2(50.0, 7.0));

    const int64_t moved = history->timeline_open(8);
    history->timeline_bind_owner(moved, body);
    history->timeline_record(moved, 10, at(1.0));
    history->timeline_record(moved, 20, at(2.0));

    PackedInt64Array slots;
    slots.push_back(moved);
    NETW_CHECK_EQ(
        history->rewind(slots, 14, Callable(body, StringName("observe_self"))),
        1
    );

    // Inside the body the node stands at history; on return it stands where
    // the live world left it, in every field the row did not name.
    NETW_CHECK_EQ(double(body->observed_x()), 1.0);
    NETW_CHECK_EQ(double(body->get_position().x), 50.0);
    NETW_CHECK_EQ(double(body->get_position().y), 7.0);
    NETW_CHECK_EQ(body->rewind_visit_count(), 1);

    memdelete(body);
}

TEST_CASE(
    "[Networked][LagComp][Timeline] a rewind moves no slot it cannot answer "
    "for, and still runs its body once"
) {
    const Ref<NetwLagCompCore> history = core();
    Carrier *unrecorded = memnew(Carrier);
    unrecorded->set_position(Vector2(3.0, 0.0));

    const int64_t ownerless = history->timeline_open(8);
    const int64_t empty = history->timeline_open(8);
    history->timeline_bind_owner(empty, unrecorded);
    history->timeline_record(ownerless, 10, at(1.0));

    PackedInt64Array slots;
    slots.push_back(ownerless);
    slots.push_back(empty);
    slots.push_back(empty + 9000);
    NETW_CHECK_EQ(
        history->rewind(
            slots,
            14,
            Callable(unrecorded, StringName("observe_self"))
        ),
        0
    );
    NETW_CHECK_EQ(double(unrecorded->get_position().x), 3.0);
    NETW_CHECK_EQ(unrecorded->rewind_visit_count(), 1);

    memdelete(unrecorded);
}

TEST_CASE(
    "[Networked][LagComp][Timeline] an owner freed before the rewind unbinds "
    "its slot rather than reaching a dead object"
) {
    const Ref<NetwLagCompCore> history = core();
    Carrier *body = memnew(Carrier);
    const int64_t slot = history->timeline_open(8);
    CHECK(history->timeline_bind_owner(slot, body));
    CHECK(history->timeline_owner_bound(slot));
    history->timeline_record(slot, 10, at(1.0));
    memdelete(body);

    PackedInt64Array slots;
    slots.push_back(slot);
    NETW_CHECK_EQ(history->rewind(slots, 14, Callable()), 0);
    CHECK(!history->timeline_owner_bound(slot));

    // The history outlives the object: a despawned entity's retained window
    // is still a legitimate answer to where it was.
    NETW_CHECK_EQ(x_of(history->timeline_sample(slot, 14)), 1.0);
}

#endif

} // namespace TestNetwLagCompTimelineLaws

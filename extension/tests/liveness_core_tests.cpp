
#include "support/netw_test.h"

#include "netw/entity_ids.hpp"
#include "netw/api/liveness_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwLivenessCore {

using namespace godot;
using netw::NetwLivenessCore;
using netw_test::CallLog;

Ref<NetwLivenessCore> make_core() {
    Ref<NetwLivenessCore> core;
    core.instantiate();
    return core;
}

RID spawn(const Ref<NetwLivenessCore> &core, int *out_route) {
    const RID entity = core->entity_create();
    const int route = core->reserve_route();
    core->bind_route(entity, route);
    if (out_route) {
        *out_route = route;
    }
    return entity;
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L1 routes are monotonic and never repeat"
) {
    Ref<NetwLivenessCore> core = make_core();

    int previous = 0;
    for (int i = 0; i < 256; i++) {
        const int route = core->reserve_route();
        CHECK(route > previous);
        previous = route;
    }

    SUBCASE("clear restarts the session") {
        core->clear();
        CHECK(core->reserve_route() == 1);
    }
}

TEST_CASE("[Networked][Liveness][Hosted] L2 set_state moves forward only") {
    Ref<NetwLivenessCore> core = make_core();
    const RID entity = spawn(core, nullptr);

    CHECK(core->state_of(entity) == NetwLivenessCore::STATE_LIVE);

    SUBCASE("forward edges are taken") {
        CHECK(core->set_state(entity, NetwLivenessCore::STATE_LINGERING));
        CHECK(core->state_of(entity) == NetwLivenessCore::STATE_LINGERING);
        CHECK(core->set_state(entity, NetwLivenessCore::STATE_DEAD));
        CHECK(core->state_of(entity) == NetwLivenessCore::STATE_DEAD);
    }

    SUBCASE("live may reach dead without lingering") {
        CHECK(core->set_state(entity, NetwLivenessCore::STATE_DEAD));
        CHECK(core->state_of(entity) == NetwLivenessCore::STATE_DEAD);
    }

    SUBCASE("backward edges are refused and change nothing") {
        core->set_state(entity, NetwLivenessCore::STATE_DEAD);
        CHECK_FALSE(core->set_state(entity, NetwLivenessCore::STATE_LINGERING));
        CHECK_FALSE(core->set_state(entity, NetwLivenessCore::STATE_LIVE));
        CHECK_FALSE(core->set_state(entity, NetwLivenessCore::STATE_UNKNOWN));
        CHECK(core->state_of(entity) == NetwLivenessCore::STATE_DEAD);
    }

    SUBCASE("a state is never re-entered") {
        CHECK_FALSE(core->set_state(entity, NetwLivenessCore::STATE_LIVE));
    }

    SUBCASE("an unknown entity refuses every edge") {
        const RID stranger;
        CHECK_FALSE(core->set_state(stranger, NetwLivenessCore::STATE_LIVE));
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L3 a dead route stays dead for the session"
) {
    Ref<NetwLivenessCore> core = make_core();
    int route = 0;
    const RID entity = spawn(core, &route);

    core->set_state(entity, NetwLivenessCore::STATE_DEAD);
    CHECK(core->route_state(route) == NetwLivenessCore::STATE_DEAD);

    SUBCASE("the tombstone survives every later mint") {
        for (int i = 0; i < 64; i++) {
            spawn(core, nullptr);
        }
        CHECK(core->route_state(route) == NetwLivenessCore::STATE_DEAD);
    }

    SUBCASE("the record is still addressable by its own handle") {
        CHECK(core->entity_is_valid(entity));
        CHECK(core->route_of(entity) == route);
    }

    SUBCASE("an unbound route reads unknown, not dead") {
        CHECK(
            core->route_state(route + 1000) == NetwLivenessCore::STATE_UNKNOWN
        );
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L4 the route bridge agrees in both "
    "directions"
) {
    Ref<NetwLivenessCore> core = make_core();

    RID entities[16];
    int routes[16];
    for (int i = 0; i < 16; i++) {
        entities[i] = spawn(core, &routes[i]);
    }

    for (int i = 0; i < 16; i++) {
        CHECK(core->rid_from_route(core->route_of(entities[i])) == entities[i]);
        CHECK(core->route_of(core->rid_from_route(routes[i])) == routes[i]);
    }

    SUBCASE("an unbound entity has no route") {
        const RID unbound = core->entity_create();
        CHECK(core->route_of(unbound) == 0);
        CHECK(core->state_of(unbound) == NetwLivenessCore::STATE_UNKNOWN);
    }

    SUBCASE("an unknown route has no entity") {
        CHECK_FALSE(core->rid_from_route(9999).is_valid());
        CHECK(core->route_of(RID()) == 0);
    }

    SUBCASE("route zero is never bindable") {
        const RID entity = core->entity_create();
        CHECK_FALSE(core->bind_route(entity, 0));
        CHECK_FALSE(core->bind_route(entity, -1));
        CHECK(core->route_of(entity) == 0);
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L5 a revival is a new epoch on the same "
    "record"
) {
    Ref<NetwLivenessCore> core = make_core();
    int route = 0;
    const RID entity = spawn(core, &route);
    NETW_CHECK_EQ(core->epoch_of(entity), 0);
    core->set_state(entity, NetwLivenessCore::STATE_DEAD);

    CHECK(core->bind_route(entity, route));

    NETW_CHECK_EQ(core->epoch_of(entity), 1);
    CHECK(core->route_state(route) == NetwLivenessCore::STATE_LIVE);
    CHECK(core->rid_from_route(route) == entity);

    SUBCASE("the record is the one that carried the tombstone") {
        NETW_CHECK_EQ(core->route_of(entity), route);
        CHECK(core->entity_is_valid(entity));
    }

    SUBCASE("a second death and revival is a third life") {
        core->set_state(entity, NetwLivenessCore::STATE_DEAD);
        CHECK(core->bind_route(entity, route));
        NETW_CHECK_EQ(core->epoch_of(entity), 2);
    }

    SUBCASE("set_state is still no way back") {
        core->set_state(entity, NetwLivenessCore::STATE_DEAD);
        CHECK_FALSE(core->set_state(entity, NetwLivenessCore::STATE_LIVE));
        CHECK(core->state_of(entity) == NetwLivenessCore::STATE_DEAD);
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L11 a route never renames, alive or "
    "tombstoned"
) {
    Ref<NetwLivenessCore> core = make_core();
    int route = 0;
    const RID holder = spawn(core, &route);
    const RID stranger = core->entity_create();

    CHECK_FALSE(core->bind_route(stranger, route));
    CHECK(core->rid_from_route(route) == holder);
    NETW_CHECK_EQ(core->route_of(stranger), 0);
    CHECK(core->state_of(stranger) == NetwLivenessCore::STATE_UNKNOWN);

    SUBCASE("a tombstone holds its route against a newcomer") {
        core->set_state(holder, NetwLivenessCore::STATE_DEAD);
        CHECK_FALSE(core->bind_route(stranger, route));
        CHECK(core->rid_from_route(route) == holder);
        CHECK(core->route_state(route) == NetwLivenessCore::STATE_DEAD);
    }

    SUBCASE("the refusal spends no epoch") {
        core->set_state(holder, NetwLivenessCore::STATE_DEAD);
        core->bind_route(stranger, route);
        NETW_CHECK_EQ(core->epoch_of(holder), 0);
        NETW_CHECK_EQ(core->epoch_of(stranger), 0);
    }

    SUBCASE("the holder may still bind its own route") {
        CHECK(core->bind_route(holder, route));
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L12 the epoch moves on one edge and by one"
) {
    Ref<NetwLivenessCore> core = make_core();
    int route = 0;
    const RID entity = spawn(core, &route);

    SUBCASE("re-binding a live route is not a life") {
        for (int i = 0; i < 8; i++) {
            CHECK(core->bind_route(entity, route));
        }
        NETW_CHECK_EQ(core->epoch_of(entity), 0);
    }

    SUBCASE("lingering is the same life") {
        core->set_state(entity, NetwLivenessCore::STATE_LINGERING);
        CHECK(core->bind_route(entity, route));
        NETW_CHECK_EQ(core->epoch_of(entity), 0);
    }

    SUBCASE("an unbound record is on its first life") {
        NETW_CHECK_EQ(core->epoch_of(core->entity_create()), 0);
    }

    SUBCASE("a record this plane never knew has no life at all") {
        NETW_CHECK_EQ(core->epoch_of(RID()), -1);
        const RID loose = netw::entity_ids::mint();
        NETW_CHECK_EQ(core->epoch_of(loose), -1);
        netw::entity_ids::release(loose);
    }

    SUBCASE("the bulk door revives on the same edge") {
        PackedInt64Array one;
        one.push_back(route);
        core->tombstone_routes_data(one);
        core->bind_routes_data(one);
        NETW_CHECK_EQ(core->epoch_of(entity), 1);
        CHECK(core->rid_from_route(route) == entity);
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] live_routes is sorted and holds only live "
    "routes"
) {
    Ref<NetwLivenessCore> core = make_core();

    RID entities[8];
    int routes[8];
    for (int i = 0; i < 8; i++) {
        entities[i] = spawn(core, &routes[i]);
    }
    core->set_state(entities[2], NetwLivenessCore::STATE_LINGERING);
    core->set_state(entities[5], NetwLivenessCore::STATE_DEAD);

    const PackedInt32Array live = core->live_routes();
    CHECK(live.size() == 6);
    for (int i = 1; i < live.size(); i++) {
        CHECK(live[i - 1] < live[i]);
    }
    CHECK(live.find(routes[2]) == -1);
    CHECK(live.find(routes[5]) == -1);
    CHECK(live.find(routes[0]) != -1);
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L6 the bulk door mints an identity per "
    "route and lands on one already standing"
) {
    Ref<NetwLivenessCore> core = make_core();

    PackedInt64Array routes;
    for (int i = 0; i < 3; i++) {
        routes.push_back(core->reserve_route());
    }
    core->bind_routes_data(routes);

    CHECK(core->live_routes().size() == 3);
    for (int i = 0; i < routes.size(); i++) {
        CHECK(core->route_state(int(routes[i]))
              == NetwLivenessCore::STATE_LIVE);
    }

    SUBCASE("a route a wrapper already holds is reused, never minted past") {
        const RID held = core->rid_from_route(int(routes[1]));
        core->bind_routes_data(routes);
        CHECK(core->rid_from_route(int(routes[1])) == held);
    }

    SUBCASE("a route below one is not an identity") {
        PackedInt64Array bad;
        bad.push_back(0);
        bad.push_back(-4);
        core->bind_routes_data(bad);
        CHECK(core->live_routes().size() == 3);
    }

    SUBCASE("the death edge tombstones without touching its neighbours") {
        PackedInt64Array one;
        one.push_back(routes[1]);
        core->tombstone_routes_data(one);

        CHECK(core->route_state(int(routes[0]))
              == NetwLivenessCore::STATE_LIVE);
        CHECK(core->route_state(int(routes[1]))
              == NetwLivenessCore::STATE_DEAD);
        CHECK(core->route_state(int(routes[2]))
              == NetwLivenessCore::STATE_LIVE);
        CHECK(core->live_routes().size() == 2);
    }

    SUBCASE("tombstoning a route nobody bound changes nothing") {
        PackedInt64Array stranger;
        stranger.push_back(9999);
        core->tombstone_routes_data(stranger);
        CHECK(core->route_state(9999) == NetwLivenessCore::STATE_UNKNOWN);
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L7 a wait is answered once, by the binding "
    "that made its route live"
) {
    Ref<NetwLivenessCore> core = make_core();
    CallLog log;
    const int route = core->reserve_route() + 1;

    core->when_live(route, log.callable("live"), 4, false, Callable());
    NETW_CHECK_EQ(core->pending_live_count(), 1);
    NETW_CHECK_EQ(log.count("live"), 0);

    PackedInt64Array claimed;
    claimed.push_back(route);
    core->bind_routes_data(claimed);

    NETW_CHECK_EQ(log.count("live"), 1);
    NETW_CHECK_EQ(core->pending_live_count(), 0);

    SUBCASE("a second binding does not answer the same caller twice") {
        core->bind_routes_data(claimed);
        NETW_CHECK_EQ(log.count("live"), 1);
    }

    SUBCASE("a wait on a route that is already live runs immediately") {
        core->when_live(route, log.callable("late"), 4, false, Callable());
        NETW_CHECK_EQ(log.count("late"), 1);
        NETW_CHECK_EQ(core->pending_live_count(), 0);
    }

    SUBCASE("waits are answered in the order they were parked") {
        const int other = core->reserve_route() + 1;
        core->when_live(other, log.callable("first"), 4, false, Callable());
        core->when_live(other, log.callable("second"), 4, false, Callable());
        core->flush_live(other);

        CHECK(log.order()
              == Vector<StringName>({"live", "first", "second"}));
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L8 a wait expires against the counter it "
    "was armed on"
) {
    Ref<NetwLivenessCore> core = make_core();
    CallLog log;
    const int route = core->reserve_route() + 1;

    core->when_live(
        route,
        log.callable("live"),
        core->frame() + 3,
        false,
        log.callable("expired")
    );

    CHECK(core->poll(0).is_empty());
    CHECK(core->poll(0).is_empty());
    NETW_CHECK_EQ(log.count("expired"), 0);
    NETW_CHECK_EQ(core->pending_live_count(), 1);

    const PackedInt32Array expired = core->poll(0);
    NETW_CHECK_EQ(expired.size(), 1);
    if (expired.size() == 1) {
        NETW_CHECK_EQ(expired[0], route);
    }
    NETW_CHECK_EQ(log.count("expired"), 1);
    NETW_CHECK_EQ(log.count("live"), 0);
    NETW_CHECK_EQ(core->pending_live_count(), 0);

    SUBCASE("an expired wait is never answered again") {
        core->poll(0);
        core->flush_live(route);
        NETW_CHECK_EQ(log.count("expired"), 1);
        NETW_CHECK_EQ(log.count("live"), 0);
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L9 one expiry does not take its route's "
    "other waits with it"
) {
    Ref<NetwLivenessCore> core = make_core();
    CallLog log;
    const int route = core->reserve_route() + 1;

    core->when_live(
        route,
        log.callable("live_early"),
        2,
        false,
        log.callable("early")
    );
    core->when_live(
        route,
        log.callable("live_late"),
        4,
        false,
        log.callable("late")
    );

    core->poll(0);
    core->poll(0);
    NETW_CHECK_EQ(log.count("early"), 1);
    NETW_CHECK_EQ(log.count("late"), 0);
    NETW_CHECK_EQ(core->pending_live_count(), 1);

    SUBCASE("the survivor is still answerable by a binding") {
        core->flush_live(route);
        NETW_CHECK_EQ(log.count("live_late"), 1);
        NETW_CHECK_EQ(log.count("live_early"), 0);
    }

    SUBCASE("the survivor expires on its own deadline") {
        core->poll(0);
        NETW_CHECK_EQ(log.count("late"), 0);
        core->poll(0);
        NETW_CHECK_EQ(log.count("late"), 1);
        NETW_CHECK_EQ(core->pending_live_count(), 0);
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] L10 a wait armed on a clock ignores the "
    "frame counter"
) {
    Ref<NetwLivenessCore> core = make_core();
    CallLog log;
    const int route = core->reserve_route() + 1;

    core->when_live(route, log.callable("live"), 10, true, log.callable("out"));

    for (int i = 0; i < 6; i++) {
        CHECK(core->poll(1).is_empty());
    }
    NETW_CHECK_EQ(log.count("out"), 0);

    SUBCASE("the clock reaching the deadline is what expires it") {
        CHECK(core->poll(10).size() == 1);
        NETW_CHECK_EQ(log.count("out"), 1);
    }

    SUBCASE("a route that dies drops its waits unanswered") {
        core->abandon_live(route);
        NETW_CHECK_EQ(core->pending_live_count(), 0);
        core->poll(10);
        core->flush_live(route);
        NETW_CHECK_EQ(log.count("out"), 0);
        NETW_CHECK_EQ(log.count("live"), 0);
    }
}

TEST_CASE("[Networked][Liveness][Hosted] clear releases the session") {
    Ref<NetwLivenessCore> core = make_core();
    int route = 0;
    const RID entity = spawn(core, &route);

    core->clear();

    CHECK(core->route_count() == 0);
    CHECK(core->live_routes().is_empty());
    CHECK(core->route_state(route) == NetwLivenessCore::STATE_UNKNOWN);
    CHECK_FALSE(core->entity_is_valid(entity));

    SUBCASE("the waiting room and the frame counter go with it") {
        CallLog log;
        core->when_live(route, log.callable("live"), 4, false, Callable());
        core->poll(0);
        core->clear();

        NETW_CHECK_EQ(core->pending_live_count(), 0);
        NETW_CHECK_EQ(core->frame(), 0);
        core->poll(0);
        NETW_CHECK_EQ(log.count("live"), 0);
    }
}

TEST_CASE(
    "[Networked][Liveness][Hosted] M1 the mint and the record plane answer "
    "different questions"
) {
    const RID loose = netw::entity_ids::mint();
    Ref<NetwLivenessCore> core = make_core();

    CHECK(netw::entity_ids::minted(loose));
    CHECK_FALSE(core->entity_is_valid(loose));

    CHECK(core->adopt(loose));
    CHECK(core->entity_is_valid(loose));

    CHECK(core->set_state(loose, NetwLivenessCore::STATE_LIVE));
    CHECK(core->adopt(loose));
    NETW_CHECK_EQ(core->state_of(loose), NetwLivenessCore::STATE_LIVE);

    core->clear();
    CHECK_FALSE(core->entity_is_valid(loose));
    CHECK(netw::entity_ids::minted(loose));

    netw::entity_ids::release(loose);
    CHECK_FALSE(netw::entity_ids::minted(loose));
}

TEST_CASE(
    "[Networked][Liveness][Hosted] M2 a handle the mint never issued cannot "
    "open a record"
) {
    Ref<NetwLivenessCore> core = make_core();

    CHECK_FALSE(core->adopt(RID()));
    CHECK_FALSE(core->entity_is_valid(RID()));

    const RID retired = core->entity_create();
    core->clear();
    CHECK_FALSE(netw::entity_ids::minted(retired));
    CHECK_FALSE(core->adopt(retired));
    CHECK_FALSE(core->entity_is_valid(retired));
}

TEST_CASE(
    "[Networked][Liveness][Hosted] M3 two planes never see each other's "
    "entities, and a released handle is never issued again"
) {
    Ref<NetwLivenessCore> a = make_core();
    Ref<NetwLivenessCore> b = make_core();

    const RID from_a = a->entity_create();
    const RID from_b = b->entity_create();
    CHECK(from_a != from_b);
    CHECK_FALSE(a->entity_is_valid(from_b));
    CHECK_FALSE(b->entity_is_valid(from_a));

    a->clear();
    const RID reborn = a->entity_create();
    CHECK(reborn != from_a);

    CHECK(b->entity_is_valid(from_b));

    a->clear();
    b->clear();
}

TEST_CASE(
    "[Networked][Liveness][Hosted] M4 a plane releases every handle it "
    "adopted, routed or not"
) {
    const int before = netw::entity_ids::outstanding();
    Ref<NetwLivenessCore> core = make_core();

    const RID routed = core->entity_create();
    core->entity_create();
    core->bind_route(routed, core->reserve_route());
    NETW_CHECK_EQ(netw::entity_ids::outstanding(), before + 2);

    core->clear();
    NETW_CHECK_EQ(netw::entity_ids::outstanding(), before);
}

TEST_CASE(
    "[Networked][Liveness][Hosted] M5 a handle outlives every holder but the "
    "last"
) {
    const int before = netw::entity_ids::outstanding();

    const RID carried = netw::entity_ids::mint();
    NETW_CHECK_EQ(netw::entity_ids::holders(carried), 1);

    Ref<NetwLivenessCore> a = make_core();
    Ref<NetwLivenessCore> b = make_core();
    CHECK(a->adopt(carried));
    CHECK(b->adopt(carried));
    NETW_CHECK_EQ(netw::entity_ids::holders(carried), 3);
    NETW_CHECK_EQ(netw::entity_ids::outstanding(), before + 1);

    a->clear();
    CHECK(netw::entity_ids::minted(carried));
    CHECK_FALSE(a->entity_is_valid(carried));
    CHECK(b->entity_is_valid(carried));

    b->clear();
    CHECK(netw::entity_ids::minted(carried));
    NETW_CHECK_EQ(netw::entity_ids::holders(carried), 1);

    netw::entity_ids::release(carried);
    CHECK_FALSE(netw::entity_ids::minted(carried));
    NETW_CHECK_EQ(netw::entity_ids::outstanding(), before);

    SUBCASE("a second adoption by one plane claims no second hold") {
        const RID once = netw::entity_ids::mint();
        Ref<NetwLivenessCore> plane = make_core();
        CHECK(plane->adopt(once));
        CHECK(plane->adopt(once));
        NETW_CHECK_EQ(netw::entity_ids::holders(once), 2);
        plane->clear();
        NETW_CHECK_EQ(netw::entity_ids::holders(once), 1);
        netw::entity_ids::release(once);
    }

    SUBCASE("a handle the mint never issued is retained by nobody") {
        CHECK_FALSE(netw::entity_ids::retain(RID()));
        NETW_CHECK_EQ(netw::entity_ids::holders(RID()), 0);
        Ref<NetwLivenessCore> plane = make_core();
        CHECK_FALSE(plane->adopt(RID()));
    }

    SUBCASE("releasing past the last holder is not a way back") {
        const RID gone = netw::entity_ids::mint();
        netw::entity_ids::release(gone);
        netw::entity_ids::release(gone);
        CHECK_FALSE(netw::entity_ids::minted(gone));
        NETW_CHECK_EQ(netw::entity_ids::holders(gone), 0);
        CHECK_FALSE(netw::entity_ids::retain(gone));
    }
}

} // namespace TestNetwLivenessCore

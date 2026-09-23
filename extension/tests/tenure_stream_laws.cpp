#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/declared_nodes.h"
#include "support/loopback_rig.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/replication_send.hpp"
#include "netw/wire/control_record.hpp"
#include "netw/wire/stream_book.hpp"

namespace TestTenureStream {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPropertySet;

using netw_test::gdsrc::BROADCAST_MASKED_AIM;

constexpr int TICKRATE = 30;
constexpr double TICK_MS = 1000.0 / TICKRATE;

StringName AIM() {
    return StringName("aim_dir");
}

Ref<netw::LocalLinkConditions> latency_of(int p_ticks, int64_t p_seed) {
    Ref<netw::LocalLinkConditions> made
        = netw::LocalLinkConditions::create(p_seed);
    made->set_latency_ms(double(p_ticks) * TICK_MS);
    return made;
}

double aim_x(Node *p_node) {
    return Vector2(p_node->get(AIM())).x;
}

netw::ReplicationSend *row_send_of(NetwMultiplayer *p_core) {
    netw::ReplicationCore *plane = p_core->get_replication_plane();
    REQUIRE(bool(plane != nullptr));
    netw::ReplicationSend *send
        = plane->get_sync_pipeline()->row_send_under_test();
    REQUIRE(bool(send != nullptr));
    return send;
}

netw::wire::StreamReaderBook &readers_of(NetwMultiplayer *p_core) {
    return row_send_of(p_core)->reader_book();
}

netw::wire::StreamLane broadcast_lane(NetwMultiplayer *p_core, Node *p_node) {
    const Ref<netw::NetwPropertySetBinding> binding
        = NetwEntity::of(p_node)->get_broadcast_binding();
    REQUIRE(binding.is_valid());
    const netw::repl::SetRow *row
        = p_core->get_replication_plane()->get_sync_model()->row_for(
            binding->get_route(),
            int64_t(netw::NetwSyncModel::KIND_DERIVED),
            binding->get_order_key(),
            binding->get_set()->record
        );
    REQUIRE(bool(row != nullptr));
    netw::wire::StreamLane lane;
    lane.route = binding->get_route();
    lane.ordinal = uint8_t(row->ordinal);
    lane.family = netw::wire::StreamFamily::VOLATILE;
    return lane;
}

bool author_if_steering(
    LoopbackRig &p_rig,
    const FlowPair &p_pair,
    int p_client,
    double p_base
) {
    Node2D *mirror = p_pair.mirror(p_client);
    if (!NetwEntity::of(mirror)->get_is_controlled_locally()) {
        return false;
    }
    const int64_t tick = clock_tick(p_rig, p_client) + 1;
    author_at(
        mirror,
        AIM(),
        Vector2(p_base + double(tick), 0.0),
        NetwPropertySet::RECORD_BROADCAST,
        tick
    );
    return true;
}

TEST_CASE(
    "[Networked][Control][SceneTree] TS1 after A to B to A no row A wrote in "
    "its first tenure applies at the server, with or without a sample from "
    "B in between, and A's second tenure still reaches it"
) {
    bool b_authors = false;
    SUBCASE("B writes a sample during its tenure") {
        b_authors = true;
    }
    SUBCASE("B writes nothing during its tenure") {
        b_authors = false;
    }
    LoopbackRig rig(2);
    rig.mount();
    flow_clocks(rig, TICKRATE);
    const int a = rig.peer_id(0);
    const int b = rig.peer_id(1);
    rig.conditions(-1, latency_of(8, 21), a);

    const FlowPair pair = stand_flow_pair(rig, BROADCAST_MASKED_AIM, "Crate");
    steer(pair, a);
    const Ref<NetwEntity> host = NetwEntity::of(pair.authored);
    const Ref<NetwEntity> at_a = NetwEntity::of(pair.mirror(0));
    const uint64_t first_tenure = at_a->get_control_tenure();

    int64_t first_tenure_last = -1;
    auto author_a = [&]() {
        const bool first = at_a->get_control_tenure() == first_tenure;
        if (author_if_steering(rig, pair, 0, 0.0) && first) {
            first_tenure_last = clock_tick(rig, 0) + 1;
        }
    };

    for (int step = 0; step < 24; ++step) {
        author_a();
        rig.step_ticks(1);
    }
    const bool warmed = aim_x(pair.authored) > 0.0;
    REQUIRE(warmed);

    double last = aim_x(pair.authored);
    int stale = 0;
    bool second_applied = false;
    bool b_applied = false;
    host->grant_control(b);
    for (int step = 0; step < 40; ++step) {
        if (step == 4) {
            host->grant_control(a);
        }
        author_a();
        if (b_authors) {
            author_if_steering(rig, pair, 1, 1000.0);
        }
        rig.step_ticks(1);
        const double seen = aim_x(pair.authored);
        if (seen == last) {
            continue;
        }
        last = seen;
        if (seen >= 1000.0) {
            b_applied = true;
        } else if (seen <= double(first_tenure_last)) {
            stale += 1;
        } else {
            second_applied = true;
        }
    }
    NETW_CHECK_EQ(stale, 0);
    CHECK(second_applied);
    NETW_CHECK_EQ(int(b_applied), int(b_authors));
    NETW_CHECK_EQ(host->get_controller(), int64_t(a));
}

TEST_CASE(
    "[Networked][Control][SceneTree] TS2 the new owner's first row applies "
    "even when it carries the same tick as the old owner's last row"
) {
    LoopbackRig rig(2);
    rig.mount();
    flow_clocks(rig, TICKRATE);
    const int a = rig.peer_id(0);
    const int b = rig.peer_id(1);
    const FlowPair pair = stand_flow_pair(rig, BROADCAST_MASKED_AIM, "Crate");
    steer(pair, a);
    const Ref<NetwEntity> host = NetwEntity::of(pair.authored);

    const int64_t shared_tick = 20;
    author_at(
        pair.mirror(0),
        AIM(),
        Vector2(20.0, 0.0),
        NetwPropertySet::RECORD_BROADCAST,
        shared_tick
    );
    rig.step_ticks(6);
    const bool old_owner_applied = aim_x(pair.authored) == 20.0;
    REQUIRE(old_owner_applied);

    host->grant_control(b);
    for (int step = 0; step < 6; ++step) {
        rig.step_ticks(1);
    }
    REQUIRE(NetwEntity::of(pair.mirror(1))->get_is_controlled_locally());
    author_at(
        pair.mirror(1),
        AIM(),
        Vector2(5000.0, 0.0),
        NetwPropertySet::RECORD_BROADCAST,
        shared_tick
    );
    for (int step = 0; step < 12 && aim_x(pair.authored) != 5000.0; ++step) {
        rig.step_ticks(1);
    }
    const bool new_owner_applied = aim_x(pair.authored) == 5000.0;
    CHECK(new_owner_applied);
}

TEST_CASE(
    "[Networked][Control][SceneTree] TS5 a writer that loses authorship "
    "closes its lanes for the entity and tells every receiver"
) {
    LoopbackRig rig(2);
    rig.mount();
    flow_clocks(rig, TICKRATE);
    const int a = rig.peer_id(0);
    const int b = rig.peer_id(1);
    const FlowPair pair = stand_flow_pair(rig, BROADCAST_MASKED_AIM, "Crate");
    steer(pair, a);
    for (int step = 0; step < 10; ++step) {
        author_if_steering(rig, pair, 0, 0.0);
        rig.step_ticks(1);
    }
    const netw::wire::StreamLane lane
        = broadcast_lane(rig.client(0), pair.mirror(0));
    netw::wire::StreamWriterBook &writers
        = row_send_of(rig.client(0))->writer_book();
    const bool seated = writers.token_of(1, lane) != 0
        && writers.token_of(b, lane) != 0;
    REQUIRE(seated);
    const uint64_t unknown_before
        = readers_of(rig.server()).unknown_token_count();

    NetwEntity::of(pair.authored)->grant_control(b);
    for (int step = 0; step < 4; ++step) {
        rig.step_ticks(1);
    }
    const bool closed
        = writers.request_of(1, lane) == 0 && writers.request_of(b, lane) == 0;
    CHECK(closed);
    const bool told
        = readers_of(rig.server()).unknown_token_count() > unknown_before;
    CHECK(told);
}

Node *build_aimed(const Variant &p_name) {
    return flow_body(BROADCAST_MASKED_AIM, String(p_name));
}

Array one_string() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

TEST_CASE(
    "[Networked][Control][SceneTree] TS6 a peer hidden while the tenure "
    "moves and then admitted again seats the new author's lane, so no park "
    "is left waiting for a decision it will never hear"
) {
    LoopbackRig rig(2);
    rig.mount();
    flow_clocks(rig, TICKRATE);
    rig.join(0, StringName("author"));
    rig.join(1, StringName("watcher"));
    const int a = rig.peer_id(0);
    const int c = rig.peer_id(1);
    Array args;
    args.push_back(String("Aimed"));
    const int route = rig.spawn_registered(
        StringName("tenure_aimed"),
        callable_mp_static(&build_aimed),
        args,
        one_string()
    );
    REQUIRE(bool(route > 0));
    Node *authored = rig.route_node(route, -1);
    REQUIRE(bool(authored != nullptr));
    const Ref<NetwEntity> host = NetwEntity::of(authored);
    NetwMultiplayer *server = flow_core(rig.server());
    const Ref<netw::NetwInterestLayer> layer
        = server->interest_layer(StringName("tenure_flap"));
    REQUIRE(layer.is_valid());
    layer->add_entity(host);
    layer->add_viewer(a);
    layer->add_viewer(c);
    server->interest_flush_now();
    host->grant_control(a);
    rig.step_ticks(4);

    auto author_at_a = [&](int p_ticks) {
        for (int step = 0; step < p_ticks; ++step) {
            Node2D *mine = Object::cast_to<Node2D>(rig.route_node(route, 0));
            if (mine != nullptr
                && NetwEntity::of(mine)->get_is_controlled_locally()) {
                const int64_t tick = clock_tick(rig, 0) + 1;
                author_at(
                    mine,
                    AIM(),
                    Vector2(double(tick), 0.0),
                    NetwPropertySet::RECORD_BROADCAST,
                    tick
                );
            }
            rig.step_ticks(1);
        }
    };
    author_at_a(8);
    Node *watched = rig.route_node(route, 1);
    REQUIRE(bool(watched != nullptr));
    const bool fed = aim_x(watched) > 0.0;
    REQUIRE(fed);

    layer->remove_viewer(c);
    server->interest_flush_now();
    rig.step_ticks(4);
    NetwMultiplayer *watcher = flow_core(rig.client(1));
    NETW_CHECK_EQ(
        int64_t(watcher->entity_get_state(watcher->entity_from_route(route))),
        int64_t(NetwMultiplayer::ENTITY_STATE_ABSENT)
    );
    host->revoke_control();
    rig.step_ticks(2);
    host->grant_control(a);
    author_at_a(4);

    layer->add_viewer(c);
    server->interest_flush_now();
    author_at_a(20);

    Node *revived = rig.route_node(route, 1);
    REQUIRE(bool(revived != nullptr));
    const Ref<NetwEntity> at_c = NetwEntity::of(revived);
    NETW_CHECK_EQ(at_c->get_control_tenure(), host->get_control_tenure());
    netw::wire::StreamReaderBook &readers = readers_of(rig.client(1));
    NETW_CHECK_EQ(readers.parked_count(), 0);
    const double newest = aim_x(rig.route_node(route, 0));
    const bool caught_up = aim_x(revived) > newest - 6.0;
    CHECK(caught_up);
}

struct Regrant {
    LoopbackRig rig;
    FlowPair pair;
    int a = 0;
    int b = 0;
    Ref<NetwEntity> host;
    netw::wire::StreamLane lane;
    uint64_t request = 0;

    explicit Regrant(int p_delay_to_b) : rig(2) {
        rig.mount();
        flow_clocks(rig, TICKRATE);
        a = rig.peer_id(0);
        b = rig.peer_id(1);
        pair = stand_flow_pair(rig, BROADCAST_MASKED_AIM, "Crate");
        steer(pair, a);
        host = NetwEntity::of(pair.authored);
        for (int step = 0; step < 10; ++step) {
            author_if_steering(rig, pair, 0, 0.0);
            rig.step_ticks(1);
        }
        lane = broadcast_lane(rig.client(0), pair.mirror(0));
        rig.conditions(1, latency_of(p_delay_to_b, 31), 1);
        rig.conditions(-1, latency_of(2, 32), a);
        host->grant_control(b);
        host->grant_control(a);
        for (int step = 0; step < 3 && request == 0; ++step) {
            author_if_steering(rig, pair, 0, 0.0);
            rig.step_ticks(1);
            request = row_send_of(rig.client(0))
                          ->writer_book()
                          .request_of(b, lane);
        }
        REQUIRE(bool(request != 0));
    }

    void forge_open_at_b() {
        netw::wire::ControlRecord open;
        open.tag = netw::wire::ControlTag::OPEN;
        open.request = request;
        open.route = lane.route;
        open.ordinal = lane.ordinal;
        open.family = lane.family;
        open.epoch = uint64_t(rig.client(1)->liveness_route_epoch(lane.route));
        open.tenure = host->get_control_tenure();
        rig.client(1)->row_control_receive(
            netw::wire::write_control_record(open),
            a
        );
    }

    Ref<NetwEntity> at_b() const {
        return NetwEntity::of(pair.mirror(1));
    }
};

TEST_CASE(
    "[Networked][Control][SceneTree] TS3 an OPEN for a tenure the receiver "
    "has not heard of parks, and is seated the moment the decisions naming "
    "its author land"
) {
    Regrant stand(5);
    const uint64_t tenure = stand.host->get_control_tenure();
    REQUIRE(bool(stand.at_b()->get_control_tenure() < tenure));
    netw::wire::StreamReaderBook &readers = readers_of(stand.rig.client(1));
    const uint64_t first_tenure = readers.token_at(stand.a, stand.lane);
    stand.forge_open_at_b();
    NETW_CHECK_EQ(readers.parked_count(), 1);
    NETW_CHECK_EQ(readers.token_at(stand.a, stand.lane), first_tenure);

    bool seated_on_arrival = false;
    for (int step = 0; step < 10; ++step) {
        author_if_steering(stand.rig, stand.pair, 0, 0.0);
        stand.rig.step_ticks(1);
        if (stand.at_b()->get_control_tenure() == tenure) {
            const uint64_t seated = readers.token_at(stand.a, stand.lane);
            seated_on_arrival = seated != 0 && seated != first_tenure;
            break;
        }
    }
    CHECK(seated_on_arrival);
    const bool drained
        = readers.parked_count() == 0 && readers.expired_park_count() == 0;
    CHECK(drained);

    const double before = aim_x(stand.pair.mirror(1));
    for (int step = 0; step < 12; ++step) {
        author_if_steering(stand.rig, stand.pair, 0, 0.0);
        stand.rig.step_ticks(1);
    }
    const bool flowing = aim_x(stand.pair.mirror(1)) > before;
    CHECK(flowing);
}

TEST_CASE(
    "[Networked][Control][SceneTree] TS4 a park that outlives the repair "
    "interval answers RESET, the writer opens again and the receiver seats "
    "the fresh request once the decisions land"
) {
    Regrant stand(15);
    const uint64_t tenure = stand.host->get_control_tenure();
    stand.forge_open_at_b();
    netw::wire::StreamReaderBook &readers = readers_of(stand.rig.client(1));
    NETW_CHECK_EQ(readers.parked_count(), 1);

    for (int step = 0; step < 40; ++step) {
        author_if_steering(stand.rig, stand.pair, 0, 0.0);
        stand.rig.step_ticks(1);
    }
    const bool expired = readers.expired_park_count() > 0;
    CHECK(expired);
    const bool decided = stand.at_b()->get_control_tenure() == tenure;
    CHECK(decided);
    const uint64_t reopened
        = row_send_of(stand.rig.client(0))
              ->writer_book()
              .request_of(stand.b, stand.lane);
    const bool opened_again = reopened > stand.request;
    CHECK(opened_again);
    const uint64_t seated = row_send_of(stand.rig.client(0))
                                ->writer_book()
                                .token_of(stand.b, stand.lane);
    const bool reseated
        = seated != 0 && readers.token_at(stand.a, stand.lane) == seated;
    CHECK(reseated);

    const double before = aim_x(stand.pair.mirror(1));
    for (int step = 0; step < 6; ++step) {
        author_if_steering(stand.rig, stand.pair, 0, 0.0);
        stand.rig.step_ticks(1);
    }
    const bool flowing = aim_x(stand.pair.mirror(1)) > before;
    CHECK(flowing);
}

} // namespace TestTenureStream

#endif

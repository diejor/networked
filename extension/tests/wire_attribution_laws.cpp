#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/schema_model.hpp"
#include "netw/wire/attribution.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestWireAttributionLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwSchema;
using netw_test::EntityDecl;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;
namespace schema_model = netw::schema_model;

struct Declared {
    Declared() {
        schema_model::clear();
    }
    ~Declared() {
        schema_model::clear();
    }

    static void mobs() {
        const Ref<netw::NetwQuantize> none;
        const Ref<NetwSchema> schema = NetwSchema::declare("AttributedMob");
        schema->vector3("pos", none, 1);
        schema->i32("hp", 1);
    }
};

int64_t reconcile_gap(NetwMultiplayer *p_core) {
    const netw::wire::AttributionBook &book = p_core->attribution_book();
    return p_core->get_sent_bytes() - book.get_attributed_out()
        - book.get_framing_out();
}

int64_t peer_channel_sum(const Dictionary &p_rows) {
    int64_t total = 0;
    const Array peers = p_rows.keys();
    for (int at = 0; at < peers.size(); ++at) {
        const Dictionary channels = p_rows[peers[at]];
        const Array ids = channels.keys();
        for (int row = 0; row < ids.size(); ++row) {
            total += int64_t(channels[ids[row]]);
        }
    }
    return total;
}

void drive(LoopbackRig &p_rig) {
    NetwMultiplayer *host = p_rig.server();
    const RID table = host->table_find_or_adopt("AttributedMob");
    p_rig.step_ticks(4);
    if (table.is_valid()) {
        const PackedInt64Array routes = host->liveness_claim_routes(3);
        PackedVector3Array at;
        PackedInt32Array hits;
        for (int row = 0; row < 3; ++row) {
            at.push_back(Vector3(float(row), 0.0f, 0.0f));
            hits.push_back(100 - row);
        }
        host->table_write_routes(table, routes);
        host->table_write_column(table, 0, at);
        host->table_write_column(table, 1, hits);
        host->table_commit(table);
    }
    p_rig.step_ticks(20);
}

TEST_CASE(
    "[Networked][Wire][SceneTree] A1 every byte a session counts as sent is "
    "either attributed to a peer and a channel or reported as framing"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    drive(rig);

    NetwMultiplayer *host = rig.server();
    NetwMultiplayer *guest = rig.client(0);

    const bool host_sent = host->get_sent_bytes() > 0;
    CHECK(host_sent);
    const bool guest_sent = guest->get_sent_bytes() > 0;
    CHECK(guest_sent);

    const int64_t host_gap = reconcile_gap(host);
    NETW_CHECK_EQ(host_gap, int64_t(0));
    const int64_t guest_gap = reconcile_gap(guest);
    NETW_CHECK_EQ(guest_gap, int64_t(0));
}

TEST_CASE(
    "[Networked][Wire][SceneTree] A2 the per-peer-per-channel table holds "
    "every attributed byte, so a grouping loses none of them"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    drive(rig);

    NetwMultiplayer *host = rig.server();
    const Dictionary snapshot = host->attribution_snapshot();
    const int64_t attributed = int64_t(snapshot[StringName("attributed_out")]);
    const bool attributed_any = attributed > 0;
    CHECK(attributed_any);

    const int64_t by_peer_channel
        = peer_channel_sum(snapshot[StringName("bytes_out")]);
    NETW_CHECK_EQ(by_peer_channel, attributed);

    int64_t by_route = 0;
    const Dictionary routes = snapshot[StringName("route_bytes_out")];
    const Array route_ids = routes.keys();
    for (int at = 0; at < route_ids.size(); ++at) {
        by_route += int64_t(routes[route_ids[at]]);
    }
    NETW_CHECK_EQ(by_route, attributed);
}

TEST_CASE(
    "[Networked][Wire][SceneTree] A3 an inbound byte is attributed to the "
    "peer that sent it and never exceeds what that peer was counted receiving"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    drive(rig);

    NetwMultiplayer *guest = rig.client(0);
    const netw::wire::AttributionBook &book = guest->attribution_book();
    const bool heard = book.get_attributed_in() > 0;
    CHECK(heard);
    const bool within = book.get_attributed_in() <= guest->get_received_bytes();
    CHECK(within);

    const Dictionary snapshot = guest->attribution_snapshot();
    const int64_t by_peer_channel
        = peer_channel_sum(snapshot[StringName("bytes_in")]);
    NETW_CHECK_EQ(by_peer_channel, book.get_attributed_in());
}

Node *build_attributed_body(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

Array one_string_name(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array one_string_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

TEST_CASE(
    "[Networked][Wire][SceneTree] A4 a spawn frame is attributed to the "
    "entity it spawns, though it addresses no route on the wire"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const StringName id("attributed_body");
    rig.register_constructor(
        rig.server(),
        id,
        callable_mp_static(&build_attributed_body),
        one_string_type()
    );
    rig.register_constructor(
        rig.client(0),
        id,
        callable_mp_static(&build_attributed_body),
        one_string_type()
    );
    const int route = rig.spawn_registered(
        id,
        callable_mp_static(&build_attributed_body),
        one_string_name("Attributed"),
        one_string_type(),
        arena,
        Variant(),
        false
    );
    rig.pump(10);
    REQUIRE(rig.route_node(route, 0) != nullptr);

    const Dictionary snapshot = rig.server()->attribution_snapshot();
    const Dictionary routes = snapshot[StringName("route_bytes_out")];
    const bool named = routes.has(int64_t(route));
    CHECK(named);
    if (named) {
        const bool spent = int64_t(routes[int64_t(route)]) > 0;
        CHECK(spent);
    }
}

RID a_synced_body(LoopbackRig &p_rig, const char *p_name, float p_at) {
    return p_rig.declare_entity(
        EntityDecl()
            .named(p_name)
            .on_schema("AttributedPose")
            .synced(StringName("position"))
            .mounted()
            .placed_at(Vector2(p_at, 0.0))
    );
}

int64_t column_rows(NetwMultiplayer *p_core) {
    const Dictionary snapshot = p_core->attribution_snapshot();
    const Dictionary columns = snapshot[StringName("columns")];
    return int64_t(columns.size());
}

TEST_CASE(
    "[Networked][Wire][SceneTree] A5 column grain is recorded only while it "
    "is armed, because it costs an add for every column a frame carries"
) {
    LoopbackRig rig(1);
    rig.mount();
    WorldDecl world;
    world.clocked(30, 0);
    rig.declare_world(world);
    a_synced_body(rig, "Quiet", 1.0);
    rig.step_ticks(8);

    NetwMultiplayer *host = rig.server();
    const bool unarmed_empty = column_rows(host) == 0;
    CHECK(unarmed_empty);
    CHECK_FALSE(host->attribution_is_armed());

    host->attribution_set_armed(true);
    CHECK(host->attribution_is_armed());
    a_synced_body(rig, "Watched", 2.0);
    rig.step_ticks(12);

    const bool armed_recorded = column_rows(host) > 0;
    CHECK(armed_recorded);

    const Dictionary snapshot = host->attribution_snapshot();
    const Dictionary columns = snapshot[StringName("columns")];
    const Array keys = columns.keys();
    bool every_cell_spent = keys.size() > 0;
    for (int at = 0; at < keys.size(); ++at) {
        const Dictionary cell = columns[keys[at]];
        if (int64_t(cell[StringName("bits")]) <= 0) {
            every_cell_spent = false;
        }
    }
    CHECK(every_cell_spent);
}

TEST_CASE(
    "[Networked][Wire][SceneTree] A6 the profiler feed reads the book and "
    "never spends it, so arming an instrument cannot change what it measures"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    drive(rig);

    NetwMultiplayer *host = rig.server();
    const bool silent_unarmed = host->attribution_feed_payload().is_empty();
    CHECK(silent_unarmed);

    host->attribution_set_armed(true);
    const int64_t before = host->attribution_book().get_attributed_out();
    const bool spent = before > 0;
    CHECK(spent);

    const Array first = host->attribution_feed_payload();
    const Array second = host->attribution_feed_payload();
    const bool carried = first.size() == 1 && second.size() == 1;
    CHECK(carried);

    NETW_CHECK_EQ(host->attribution_book().get_attributed_out(), before);
    const int64_t gap = reconcile_gap(host);
    NETW_CHECK_EQ(gap, int64_t(0));
}

} // namespace TestWireAttributionLaws

#endif

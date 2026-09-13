#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_recorder.h"

#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/schema_core.hpp"
#include "netw/schema_model.hpp"

namespace TestTableSessionFlow {

using namespace godot;
using namespace netw_test;
using netw::LocalLinkConditions;
using netw::NetwMultiplayer;
using netw::NetwSchema;
namespace schema_model = netw::schema_model;
using netw::SchemaCore;

struct Declared {
    Declared() {
        schema_model::clear();
    }
    ~Declared() {
        schema_model::clear();
    }

    static void mobs(bool reliable = false) {
        const Ref<netw::NetwQuantize> none;
        const Ref<NetwSchema> schema = NetwSchema::declare("FlowMob");
        schema->vector3("pos", none, 1);
        schema->i32("hp", 1);
        schema->reliable(reliable);
    }
};

PackedInt64Array int64s(const std::initializer_list<int64_t> &values) {
    PackedInt64Array out;
    for (const int64_t value : values) {
        out.push_back(value);
    }
    return out;
}

PackedInt32Array int32s(const std::initializer_list<int32_t> &values) {
    PackedInt32Array out;
    for (const int32_t value : values) {
        out.push_back(value);
    }
    return out;
}

PackedVector3Array points(int count) {
    PackedVector3Array out;
    for (int at = 0; at < count; ++at) {
        out.push_back(Vector3(float(at), 0.0f, 0.0f));
    }
    return out;
}

Error publish_at(
    NetwMultiplayer *core,
    const RID &table,
    const PackedInt64Array &routes,
    const PackedVector3Array &at,
    const PackedInt32Array &hits
) {
    core->table_write_routes(table, routes);
    core->table_write_column(table, 0, at);
    core->table_write_column(table, 1, hits);
    return core->table_commit(table);
}

PackedVector3Array along_x(const std::initializer_list<float> &xs) {
    PackedVector3Array out;
    for (const float x : xs) {
        out.push_back(Vector3(x, 0.0f, 0.0f));
    }
    return out;
}

Error publish(
    NetwMultiplayer *core,
    const RID &table,
    const PackedInt64Array &routes,
    const PackedInt32Array &hits
) {
    core->table_write_routes(table, routes);
    core->table_write_column(table, 0, points(routes.size()));
    core->table_write_column(table, 1, hits);
    return core->table_commit(table);
}

TEST_CASE(
    "[Networked][Table][SceneTree] TS1 a committed wave becomes rows on a "
    "client that owns no node for any of them, live on both peers"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    NetwMultiplayer *host = rig.server();
    NetwMultiplayer *guest = rig.client(0);
    const RID table = host->table_find_or_adopt("FlowMob");
    NETW_CHECK_EQ(int(table.is_valid()), 1);
    if (!table.is_valid()) {
        return;
    }

    const PackedInt64Array routes = host->liveness_claim_routes(3);
    NETW_CHECK_EQ(publish(host, table, routes, int32s({100, 90, 80})), OK);
    rig.step_ticks(3);

    const RID mirror = guest->table_find("FlowMob");
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);
    if (!mirror.is_valid() || routes.size() != 3) {
        return;
    }
    NETW_CHECK_EQ(guest->table_read_routes(mirror).size(), 3);
    NETW_CHECK_EQ(guest->table_get_row(mirror, routes[1]), 1);
    const PackedInt32Array seen = guest->table_read_column(mirror, 1);
    NETW_CHECK_EQ(seen.size(), 3);
    if (seen.size() == 3) {
        NETW_CHECK_EQ(seen[2], 80);
    }
    NETW_CHECK_EQ(int(guest->entity_from_route(routes[0]).is_valid()), 1);
    CHECK(
        guest->entity_get_node(guest->entity_from_route(routes[0])) == nullptr
    );
}

TEST_CASE(
    "[Networked][Table][SceneTree] TS2 a client hears one emission per wave "
    "and reads that wave's cohorts from inside the handler's tick"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    NetwMultiplayer *host = rig.server();
    NetwMultiplayer *guest = rig.client(0);
    const RID table = host->table_find_or_adopt("FlowMob");
    Vector<StringName> watched;
    watched.push_back(StringName("table_received"));
    const Recorder heard(guest, watched);

    const PackedInt64Array first = host->liveness_claim_routes(2);
    publish(host, table, first, int32s({1, 2}));
    rig.step_ticks(3);
    NETW_CHECK_EQ(heard.count("table_received"), 1);

    const RID mirror = guest->table_find("FlowMob");
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);
    if (!mirror.is_valid() || first.size() != 2) {
        return;
    }
    NETW_CHECK_EQ(guest->table_read_births(mirror).size(), 2);
    NETW_CHECK_EQ(guest->table_read_deaths(mirror).size(), 0);

    const PackedInt64Array second = host->liveness_claim_routes(1);
    publish(host, table, int64s({first[0], second[0]}), int32s({3, 4}));
    rig.step_ticks(3);

    NETW_CHECK_EQ(heard.count("table_received"), 2);
    NETW_CHECK_EQ(guest->table_read_births(mirror).size(), 1);
    NETW_CHECK_EQ(guest->table_read_deaths(mirror).size(), 1);
}

TEST_CASE(
    "[Networked][Table][SceneTree] TS3 releasing a route retires it on the "
    "client too, so a row the host dropped cannot come back"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    NetwMultiplayer *host = rig.server();
    NetwMultiplayer *guest = rig.client(0);
    const RID table = host->table_find_or_adopt("FlowMob");

    const PackedInt64Array routes = host->liveness_claim_routes(2);
    publish(host, table, routes, int32s({1, 2}));
    rig.step_ticks(3);

    host->liveness_release_routes(routes);
    rig.step_ticks(3);

    if (routes.size() != 2) {
        return;
    }
    NETW_CHECK_EQ(
        guest->entity_get_state(guest->entity_from_route(routes[0])),
        int(NetwMultiplayer::ENTITY_STATE_DEAD)
    );
    const RID mirror = guest->table_find("FlowMob");
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);
    if (!mirror.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(guest->table_get_row(mirror, routes[0]), -1);
}

TEST_CASE(
    "[Networked][Table][SceneTree] TS4 a peer that arrives after a table "
    "published is healed by a snapshot rather than by the next change"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    NetwMultiplayer *host = rig.server();
    const RID table = host->table_find_or_adopt("FlowMob");

    const PackedInt64Array routes = host->liveness_claim_routes(2);
    publish(host, table, routes, int32s({11, 12}));
    rig.step_ticks(3);

    const int late = rig.add_client();
    rig.step_ticks(6);
    NetwMultiplayer *joined = rig.client(late);
    const RID mirror = joined->table_find("FlowMob");
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);
    if (!mirror.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(joined->table_read_routes(mirror).size(), 2);
    const PackedInt32Array seen = joined->table_read_column(mirror, 1);
    NETW_CHECK_EQ(seen.size(), 2);
    if (seen.size() == 2) {
        NETW_CHECK_EQ(seen[0], 11);
    }
}

TEST_CASE(
    "[Networked][Table][SceneTree] TS5 a reliable table survives a lossy "
    "link, which is the reason a rare-change table asks to be one"
) {
    Declared declared;
    Declared::mobs(true);
    LoopbackRig rig(1);
    NetwMultiplayer *host = rig.server();
    NetwMultiplayer *guest = rig.client(0);
    const RID table = host->table_find_or_adopt("FlowMob");
    NETW_CHECK_EQ(int(host->get_table_core()->is_reliable(table)), 1);

    Ref<LocalLinkConditions> lossy = LocalLinkConditions::create(7);
    lossy->set_packet_loss(0.6);
    rig.conditions(0, lossy);

    const PackedInt64Array routes = host->liveness_claim_routes(2);
    publish(host, table, routes, int32s({31, 32}));
    rig.step_ticks(20);

    const RID mirror = guest->table_find("FlowMob");
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);
    if (!mirror.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(guest->table_read_routes(mirror).size(), 2);
}

TEST_CASE(
    "[Networked][Table][SceneTree] TS6 the host reads its own committed "
    "wave and hears its own emission, so host code is client code"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    NetwMultiplayer *host = rig.server();
    const RID table = host->table_find_or_adopt("FlowMob");
    Vector<StringName> watched;
    watched.push_back(StringName("table_received"));
    const Recorder heard(host, watched);

    const PackedInt64Array routes = host->liveness_claim_routes(2);
    publish(host, table, routes, int32s({41, 42}));
    rig.step_ticks(2);

    NETW_CHECK_EQ(heard.count("table_received"), 1);
    NETW_CHECK_EQ(host->table_read_routes(table).size(), 2);
    const PackedInt32Array mine = host->table_read_column(table, 1);
    NETW_CHECK_EQ(mine.size(), 2);
    if (mine.size() == 2) {
        NETW_CHECK_EQ(mine[1], 42);
    }
}

TEST_CASE(
    "[Networked][Table][SceneTree] TS8 a second wave over the routes a client "
    "already holds moves those rows, because a table is a live stream rather "
    "than one delivered snapshot"
) {
    Declared declared;
    Declared::mobs();
    LoopbackRig rig(1);
    NetwMultiplayer *host = rig.server();
    NetwMultiplayer *guest = rig.client(0);
    const RID table = host->table_find_or_adopt("FlowMob");
    NETW_CHECK_EQ(int(table.is_valid()), 1);
    if (!table.is_valid()) {
        return;
    }

    const PackedInt64Array routes = host->liveness_claim_routes(2);
    NETW_CHECK_EQ(
        publish_at(
            host,
            table,
            routes,
            along_x({1.0f, 2.0f}),
            int32s({100, 100})
        ),
        OK
    );
    rig.step_ticks(3);

    const RID mirror = guest->table_find("FlowMob");
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);
    if (!mirror.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(guest->table_read_routes(mirror).size(), 2);

    NETW_CHECK_EQ(
        publish_at(
            host,
            table,
            routes,
            along_x({9.0f, 8.0f}),
            int32s({90, 80})
        ),
        OK
    );
    rig.step_ticks(3);

    const PackedVector3Array moved = guest->table_read_column(mirror, 0);
    const PackedInt32Array hp = guest->table_read_column(mirror, 1);
    NETW_CHECK_EQ(moved.size(), 2);
    NETW_CHECK_EQ(hp.size(), 2);
    if (moved.size() != 2 || hp.size() != 2) {
        return;
    }
    CHECK(moved[0] == Vector3(9.0f, 0.0f, 0.0f));
    CHECK(moved[1] == Vector3(8.0f, 0.0f, 0.0f));
    NETW_CHECK_EQ(hp[0], 90);
    NETW_CHECK_EQ(hp[1], 80);
}

} // namespace TestTableSessionFlow

#endif

#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/entity.hpp"
#include "netw/spawn/pipeline.hpp"

#include "support/stock_probes.h"
#include "support/stock_stand.h"
#include "support/value_flow_stand.h"

#include <godot_cpp/classes/node2d.hpp>

namespace TestSpawnAdoptParkLaws {

using namespace godot;
using netw_test::LoopbackRig;
using netw_test::SyncProp;

constexpr int TICKRATE = 30;

Node *shared_probe(const String &p_name) {
    Node *root
        = netw_test::scripted_root(netw_test::gdsrc::STOCK_SYNC_PROBE, p_name);
    Vector<SyncProp> props;
    SyncProp row;
    row.path = NodePath(".:synced_value");
    row.mode = SceneReplicationConfig::REPLICATION_MODE_ALWAYS;
    props.push_back(row);
    netw_test::add_stock_sync(root, "Sync", props);
    return root;
}

int64_t counter_of(LoopbackRig &p_rig, int p_client, const char *p_name) {
    const Dictionary counters
        = Dictionary(p_rig.spawn_plane(p_client)->counters());
    return int64_t(counters[StringName(p_name)]);
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] an ADOPT naming a node the receiver "
    "has not built yet PARKS, because the peer's own node is still on "
    "its way rather than absent"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);

    Node *server_side = shared_probe("Shared");
    rig.branch(-1)->add_child(server_side);
    rig.step_ticks(8);

    NETW_CHECK_GE(counter_of(rig, -1, "spawn_book_spawned"), int64_t(1));
    NETW_CHECK_EQ(counter_of(rig, 0, "drops_spawn_unresolved"), int64_t(0));
    NETW_CHECK_GE(counter_of(rig, 0, "spawn_deferrals"), int64_t(1));
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] a parked ADOPT lands when the "
    "receiver's node finally arrives, so the route survives a race it "
    "used to lose for the whole session"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);

    Node *server_side = shared_probe("Shared");
    rig.branch(-1)->add_child(server_side);
    rig.step_ticks(8);

    Node *client_side = shared_probe("Shared");
    rig.branch(0)->add_child(client_side);
    rig.step_ticks(8);

    CHECK(netw::NetwEntity::of(client_side).is_valid());
    NETW_CHECK_EQ(counter_of(rig, 0, "drops_spawn_unresolved"), int64_t(0));

    server_side->set(StringName("synced_value"), 61);
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                client_side,
                StringName("synced_value"),
                61
            )
        ),
        61
    );
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] a parked ADOPT whose node never "
    "arrives expires once, so a structure that truly does not match is "
    "reported rather than held forever"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    rig.spawn_plane(0)->set_park_timeout_seconds(0.0);

    Node *server_side = shared_probe("Shared");
    rig.branch(-1)->add_child(server_side);
    rig.step_ticks(8);

    NETW_CHECK_EQ(counter_of(rig, 0, "spawn_park_expired"), int64_t(1));
    NETW_CHECK_EQ(counter_of(rig, 0, "drops_spawn_unresolved"), int64_t(1));
    NETW_CHECK_EQ(int64_t(rig.spawn_plane(0)->get_park().size()), int64_t(0));
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] a synchronizer whose sibling root "
    "left the tree adopts when the root returns, rather than latching "
    "for the rest of the session"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);

    Node *holder = memnew(Node);
    holder->set_name("Holder");
    Node *target = netw_test::scripted_root(
        netw_test::gdsrc::STOCK_SYNC_PROBE,
        "Target"
    );
    holder->add_child(target);

    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    sync->set_name("Sync");
    sync->set_root_path(NodePath("../Target"));
    Ref<SceneReplicationConfig> config;
    config.instantiate();
    config->add_property(NodePath(".:synced_value"));
    config->property_set_replication_mode(
        NodePath(".:synced_value"),
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS
    );
    sync->set_replication_config(config);
    holder->add_child(sync);

    rig.branch(-1)->add_child(holder);
    holder->remove_child(target);
    rig.step_ticks(4);

    holder->add_child(target);
    rig.step_ticks(8);

    CHECK(netw::NetwEntity::of(target).is_valid());
}

} // namespace TestSpawnAdoptParkLaws

#endif

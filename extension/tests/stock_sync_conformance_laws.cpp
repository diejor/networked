#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/entity.hpp"

#include "support/netw_cells.h"
#include "support/stock_probes.h"
#include "support/stock_stand.h"
#include "support/value_flow_stand.h"

#include <godot_cpp/classes/node2d.hpp>

namespace TestStockSyncConformanceLaws {

using namespace godot;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;
using netw_test::StockWorld;
using netw_test::SyncProp;

constexpr int TICKRATE = 30;
const char *PROBE_SCRIPT = netw_test::gdsrc::STOCK_SYNC_PROBE;

int SYNCHRONIZED_ROWS = 0;
int SPAWNED_ROWS = 0;

void note_synchronized() {
    SYNCHRONIZED_ROWS += 1;
}

void note_spawned(Node *) {
    SPAWNED_ROWS += 1;
}

SyncProp prop_row(const char *p_path, int p_mode, bool p_on_spawn) {
    SyncProp row;
    row.path = NodePath(p_path);
    row.mode = p_mode;
    row.on_spawn = p_on_spawn;
    return row;
}

Ref<PackedScene> probe_scene() {
    Node *root = netw_test::scripted_root(PROBE_SCRIPT, "SyncStockProbe");
    Vector<SyncProp> props;
    props.push_back(prop_row(
        ".:spawn_value",
        SceneReplicationConfig::REPLICATION_MODE_NEVER,
        true
    ));
    props.push_back(prop_row(
        ".:synced_value",
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS,
        false
    ));
    props.push_back(prop_row(
        ".:watched_value",
        SceneReplicationConfig::REPLICATION_MODE_ON_CHANGE,
        false
    ));
    netw_test::add_stock_sync(root, "Sync", props);
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("SyncStockProbe")
    );
    memdelete(root);
    return packed;
}

Ref<PackedScene> multi_sync_scene() {
    Node *root = netw_test::scripted_root(PROBE_SCRIPT, "MultiSyncProbe");
    Vector<SyncProp> first;
    first.push_back(prop_row(
        ".:synced_value",
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS,
        false
    ));
    netw_test::add_stock_sync(root, "Sync", first);
    Vector<SyncProp> second;
    second.push_back(prop_row(
        ".:watched_value",
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS,
        false
    ));
    netw_test::add_stock_sync(root, "Sync2", second);
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("MultiSyncProbe")
    );
    memdelete(root);
    return packed;
}

Ref<PackedScene> sub_node_scene() {
    Node *root = netw_test::scripted_root(PROBE_SCRIPT, "SubNodeProbe");
    Node2D *visual = memnew(Node2D);
    visual->set_name("Visual");
    root->add_child(visual);
    visual->set_owner(root);
    Vector<SyncProp> props;
    props.push_back(prop_row(
        "Visual:modulate",
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS,
        false
    ));
    netw_test::add_stock_sync(root, "Sync", props);
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("SubNodeProbe")
    );
    memdelete(root);
    return packed;
}

Node *manual_probe() {
    Node *root = netw_test::scripted_root(PROBE_SCRIPT, "AdoptProbe");
    Vector<SyncProp> props;
    props.push_back(prop_row(
        ".:synced_value",
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS,
        false
    ));
    netw_test::add_stock_sync(root, "Sync", props);
    return root;
}

MultiplayerSynchronizer *sync_of(Node *p_probe, const char *p_name) {
    return Object::cast_to<MultiplayerSynchronizer>(
        p_probe->get_node_or_null(NodePath(p_name))
    );
}

enum Shape {
    SHAPE_ONE_SYNC,
    SHAPE_TWO_SYNCS,
    SHAPE_SUB_NODE,
};

enum Plant {
    PLANT_NONE,
    PLANT_THE_AUTHORITY_NEVER_AUTHORS,
    PLANT_THE_SPAWN_FIELD_IS_AUTHORED_AFTER_THE_SPAWN,
};

struct CarryScenario {
    String label;
    Shape shape = SHAPE_ONE_SYNC;
    bool carries_spawn = true;
    bool carries_synced = true;
    bool carries_watch = true;
    bool carries_sub_node = false;
};

CarryScenario one_synchronizer() {
    CarryScenario scenario;
    scenario.label = "one-synchronizer";
    return scenario;
}

CarryScenario two_synchronizers() {
    CarryScenario scenario;
    scenario.label = "two-synchronizers";
    scenario.shape = SHAPE_TWO_SYNCS;
    scenario.carries_spawn = false;
    return scenario;
}

CarryScenario a_sub_node_field() {
    CarryScenario scenario;
    scenario.label = "sub-node-field";
    scenario.shape = SHAPE_SUB_NODE;
    scenario.carries_spawn = false;
    scenario.carries_synced = false;
    scenario.carries_watch = false;
    scenario.carries_sub_node = true;
    return scenario;
}

struct CarryEvidence {
    bool materialized = false;
    String spawn_value;
    String enter_tree_value;
    int64_t synced = -1;
    int64_t watched = -1;
    Color modulate;
};

class CarryRun {
    CarryScenario declared;
    Plant planted = PLANT_NONE;
    CarryEvidence seen;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

public:
    explicit CarryRun(
        const CarryScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        LoopbackRig rig(1);
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        Ref<PackedScene> scene;
        if (p_scenario.shape == SHAPE_TWO_SYNCS) {
            scene = multi_sync_scene();
        } else if (p_scenario.shape == SHAPE_SUB_NODE) {
            scene = sub_node_scene();
        } else {
            scene = probe_scene();
        }
        PackedStringArray scenes;
        scenes.push_back(scene->get_path());
        StockWorld world = netw_test::mount_stock_world(rig, scenes);

        Node *node = scene->instantiate();
        node->set_name("CarryProbe");
        if (p_scenario.carries_spawn
            && !plant_is(PLANT_THE_SPAWN_FIELD_IS_AUTHORED_AFTER_THE_SPAWN)) {
            node->set(StringName("spawn_value"), String("spawned"));
        }
        world.arena(-1)->add_child(node, true);

        Node *mirror
            = netw_test::pump_until_child(rig, world.arena(0), "CarryProbe");
        if (plant_is(PLANT_THE_SPAWN_FIELD_IS_AUTHORED_AFTER_THE_SPAWN)) {
            node->set(StringName("spawn_value"), String("spawned"));
            rig.step_ticks(8);
        }
        seen.materialized = mirror != nullptr;
        if (mirror == nullptr) {
            return;
        }
        seen.spawn_value = String(mirror->get(StringName("spawn_value")));
        seen.enter_tree_value
            = String(mirror->get(StringName("enter_tree_spawn_value")));

        if (!plant_is(PLANT_THE_AUTHORITY_NEVER_AUTHORS)) {
            node->set(StringName("synced_value"), 77);
            node->set(StringName("watched_value"), 42);
            if (p_scenario.carries_sub_node) {
                Node2D *visual = Object::cast_to<Node2D>(
                    node->get_node_or_null(NodePath("Visual"))
                );
                REQUIRE_MESSAGE(visual != nullptr, "the probe has no visual");
                visual->set_modulate(Color(0.25, 0.5, 0.75, 1.0));
            }
        }
        if (p_scenario.carries_synced) {
            seen.synced = int64_t(
                netw_test::pump_until_value(
                    rig,
                    mirror,
                    StringName("synced_value"),
                    77
                )
            );
        }
        if (p_scenario.carries_watch) {
            seen.watched = int64_t(
                netw_test::pump_until_value(
                    rig,
                    mirror,
                    StringName("watched_value"),
                    42
                )
            );
        }
        if (p_scenario.carries_sub_node) {
            Node2D *visual = Object::cast_to<Node2D>(
                mirror->get_node_or_null(NodePath("Visual"))
            );
            REQUIRE_MESSAGE(visual != nullptr, "the mirror has no visual");
            netw_test::pump_until_value(
                rig,
                visual,
                StringName("modulate"),
                Color(0.25, 0.5, 0.75, 1.0)
            );
            seen.modulate = visual->get_modulate();
        }
        if (p_scenario.shape == SHAPE_TWO_SYNCS) {
            seen.watched = int64_t(
                netw_test::pump_until_value(
                    rig,
                    mirror,
                    StringName("watched_value"),
                    42
                )
            );
        }
    }

    const CarryScenario &scenario() const {
        return declared;
    }

    const CarryEvidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<CarryRun> CarryLaw;

LawVerdict law_materializes(const CarryRun &p_run) {
    if (!p_run.evidence().materialized) {
        return law_broken("the peer never materialized the spawned node");
    }
    return law_held();
}

const CarryLaw L_MATERIALIZES = {
    "materializes",
    "a node added under the spawner's spawn path reaches every visible peer "
    "at the same tree location",
    &law_materializes,
};

LawVerdict law_pre_tree(const CarryRun &p_run) {
    const CarryEvidence &seen = p_run.evidence();
    if (!p_run.scenario().carries_spawn) {
        return law_held();
    }
    if (seen.spawn_value != String("spawned")) {
        return law_broken(
            "the receiver holds '%s' as the spawn field",
            seen.spawn_value.utf8().get_data()
        );
    }
    if (seen.enter_tree_value != String("spawned")) {
        return law_broken(
            "the receiver read '%s' at enter_tree",
            seen.enter_tree_value.utf8().get_data()
        );
    }
    return law_held();
}

const CarryLaw L_PRE_TREE = {
    "pre-tree",
    "a spawn property is already applied when the receiver's node enters the "
    "tree, so its own enter_tree observes the authored value",
    &law_pre_tree,
};

LawVerdict law_carries(const CarryRun &p_run) {
    const CarryScenario &scenario = p_run.scenario();
    const CarryEvidence &seen = p_run.evidence();
    if (scenario.carries_synced && seen.synced != 77) {
        return law_broken(
            "the always-replicated field reads %d",
            int(seen.synced)
        );
    }
    if ((scenario.carries_watch || scenario.shape == SHAPE_TWO_SYNCS)
        && seen.watched != 42) {
        return law_broken("the second field reads %d", int(seen.watched));
    }
    if (scenario.carries_sub_node
        && !seen.modulate.is_equal_approx(Color(0.25, 0.5, 0.75, 1.0))) {
        return law_broken(
            "the sub-node field reads %d hundredths of red",
            int(seen.modulate.r * 100.0)
        );
    }
    return law_held();
}

const CarryLaw L_CARRIES = {
    "carries",
    "every declared field reaches the receiver, whichever synchronizer and "
    "whichever node under the root declares it",
    &law_carries,
};

const CarryLaw CARRY_LAWS[] = {L_MATERIALIZES, L_PRE_TREE, L_CARRIES};

TEST_CASE(
    "[Networked][Sync][SceneTree] the stock synchronizer carry laws "
    "hold"
) {
    const CarryScenario CORPUS[] = {
        one_synchronizer(),
        two_synchronizers(),
        a_sub_node_field(),
    };
    for (const CarryScenario &scenario : CORPUS) {
        const CarryRun run(scenario);
        for (const CarryLaw &law : CARRY_LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Sync][SceneTree] an authority that authors nothing reds "
    "carries"
) {
    const CarryScenario scenario = one_synchronizer();
    const CarryRun run(scenario, PLANT_THE_AUTHORITY_NEVER_AUTHORS);
    NETW_CELL(L_CARRIES, scenario);
    NETW_LAW_BREAKS(L_CARRIES, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a spawn field authored after the spawn "
    "reds pre-tree"
) {
    const CarryScenario scenario = one_synchronizer();
    const CarryRun run(
        scenario,
        PLANT_THE_SPAWN_FIELD_IS_AUTHORED_AFTER_THE_SPAWN
    );
    NETW_CELL(L_PRE_TREE, scenario);
    NETW_LAW_BREAKS(L_PRE_TREE, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a spawn field authored inside _ready "
    "still rides the spawn frame, because the snapshot is taken at the "
    "settle after tree entry rather than on the way in"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> scene = probe_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *node = scene->instantiate();
    node->set_name("ReadyMutated");
    node->set(StringName("spawn_value"), String("pre-ready"));
    node->set(StringName("mutate_in_ready"), true);
    world.arena(-1)->add_child(node, true);
    NETW_CHECK_EQ(
        int(String(node->get(StringName("spawn_value"))) == "ready-mutated"),
        1
    );

    Node *mirror
        = netw_test::pump_until_child(rig, world.arena(0), "ReadyMutated");
    REQUIRE(mirror != nullptr);
    NETW_CHECK_EQ(
        int(String(mirror->get(StringName("spawn_value"))) == "ready-mutated"),
        1
    );
}

TEST_CASE(
    "[Networked][Sync][SceneTree] the synchronizer emits synchronized on "
    "the receiver when it applies an incoming sync"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> scene = probe_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *node = scene->instantiate();
    node->set_name("SignalProbe");
    world.arena(-1)->add_child(node, true);
    Node *mirror
        = netw_test::pump_until_child(rig, world.arena(0), "SignalProbe");
    REQUIRE(mirror != nullptr);

    MultiplayerSynchronizer *sync = sync_of(mirror, "Sync");
    REQUIRE(sync != nullptr);
    SYNCHRONIZED_ROWS = 0;
    sync->connect(
        StringName("synchronized"),
        callable_mp_static(&note_synchronized)
    );

    node->set(StringName("synced_value"), 5);
    netw_test::pump_until_value(rig, mirror, StringName("synced_value"), 5);

    sync->disconnect(
        StringName("synchronized"),
        callable_mp_static(&note_synchronized)
    );
    NETW_CHECK_GT(SYNCHRONIZED_ROWS, 0);
    SYNCHRONIZED_ROWS = 0;
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a peer hidden by the synchronizer's "
    "visibility receives nothing, and is healed to the current value "
    "when it is shown"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> scene = probe_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *node = scene->instantiate();
    node->set_name("HiddenProbe");
    MultiplayerSynchronizer *sync = sync_of(node, "Sync");
    REQUIRE(sync != nullptr);
    sync->set_visibility_public(false);
    world.arena(-1)->add_child(node, true);
    node->set(StringName("synced_value"), 9);

    CHECK(
        netw_test::pump_until_child(rig, world.arena(0), "HiddenProbe", 20)
        == nullptr
    );

    sync->set_visibility_for(rig.peer_id(0), true);
    sync->update_visibility();

    Node *mirror
        = netw_test::pump_until_child(rig, world.arena(0), "HiddenProbe");
    REQUIRE(mirror != nullptr);
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                mirror,
                StringName("synced_value"),
                9
            )
        ),
        9
    );
}

TEST_CASE(
    "[Networked][Sync][SceneTree] moving the synchronizer's authority to "
    "a client moves the stream with it"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> scene = probe_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *node = scene->instantiate();
    node->set_name("AuthProbe");
    world.arena(-1)->add_child(node, true);
    node->set(StringName("synced_value"), 3);
    Node *mirror
        = netw_test::pump_until_child(rig, world.arena(0), "AuthProbe");
    REQUIRE(mirror != nullptr);
    netw_test::pump_until_value(rig, mirror, StringName("synced_value"), 3);

    sync_of(node, "Sync")->set_multiplayer_authority(rig.peer_id(0));
    sync_of(mirror, "Sync")->set_multiplayer_authority(rig.peer_id(0));

    mirror->set(StringName("synced_value"), 44);
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                node,
                StringName("synced_value"),
                44
            )
        ),
        44
    );
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a peer admitted after the value "
    "changed heals to the current one rather than the spawn default"
) {
    LoopbackRig rig(2);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> scene = probe_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *node = scene->instantiate();
    node->set_name("LateProbe");
    MultiplayerSynchronizer *sync = sync_of(node, "Sync");
    REQUIRE(sync != nullptr);
    sync->set_visibility_public(false);
    sync->set_visibility_for(rig.peer_id(0), true);
    world.arena(-1)->add_child(node, true);

    Node *first = netw_test::pump_until_child(rig, world.arena(0), "LateProbe");
    REQUIRE(first != nullptr);
    node->set(StringName("synced_value"), 123);
    netw_test::pump_until_value(rig, first, StringName("synced_value"), 123);
    CHECK(
        netw_test::pump_until_child(rig, world.arena(1), "LateProbe", 10)
        == nullptr
    );

    sync->set_visibility_for(rig.peer_id(1), true);
    sync->update_visibility();

    Node *late = netw_test::pump_until_child(rig, world.arena(1), "LateProbe");
    REQUIRE(late != nullptr);
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                late,
                StringName("synced_value"),
                123
            )
        ),
        123
    );
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a spawner-less synchronizer on a "
    "matched pair is adopted in place, so the same route lands on the "
    "instance every peer already holds"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);

    Node *client_manual = manual_probe();
    rig.branch(0)->add_child(client_manual);
    Node *server_manual = manual_probe();
    rig.branch(-1)->add_child(server_manual);
    rig.pump(6);

    server_manual->set(StringName("synced_value"), 71);
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                client_manual,
                StringName("synced_value"),
                71
            )
        ),
        71
    );
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a session torn down and re-hosted "
    "synchronizes again rather than staying wedged on the old routes"
) {
    const Ref<PackedScene> scene = probe_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    for (int host = 0; host < 2; ++host) {
        LoopbackRig rig(1);
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        StockWorld world = netw_test::mount_stock_world(rig, scenes);

        Node *node = scene->instantiate();
        node->set_name(vformat("HostProbe%d", host));
        world.arena(-1)->add_child(node, true);
        Node *mirror = netw_test::pump_until_child(
            rig,
            world.arena(0),
            vformat("HostProbe%d", host)
        );
        REQUIRE(mirror != nullptr);
        node->set(StringName("synced_value"), 5 + host);
        NETW_CHECK_EQ(
            int64_t(
                netw_test::pump_until_value(
                    rig,
                    mirror,
                    StringName("synced_value"),
                    5 + host
                )
            ),
            5 + host
        );
    }
}

TEST_CASE(
    "[Networked][Sync][SceneTree] across a whole session of spawns, "
    "syncs, deltas and a later admission the stock replicator receives "
    "nothing, so no peer ever holds a duplicate"
) {
    LoopbackRig rig(2);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> scene = probe_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    SPAWNED_ROWS = 0;
    world.spawner(0)->connect(
        StringName("spawned"),
        callable_mp_static(&note_spawned)
    );

    const char *NAMES[] = {"Cap0", "Cap1", "Cap2"};
    Vector<MultiplayerSynchronizer *> syncs;
    for (const char *probe : NAMES) {
        Node *node = scene->instantiate();
        node->set_name(probe);
        MultiplayerSynchronizer *sync = sync_of(node, "Sync");
        REQUIRE(sync != nullptr);
        sync->set_visibility_public(false);
        sync->set_visibility_for(rig.peer_id(0), true);
        syncs.push_back(sync);
        world.arena(-1)->add_child(node, true);
    }
    for (const char *probe : NAMES) {
        CHECK(
            netw_test::pump_until_child(rig, world.arena(0), probe) != nullptr
        );
    }
    world.spawner(0)->disconnect(
        StringName("spawned"),
        callable_mp_static(&note_spawned)
    );
    NETW_CHECK_EQ(SPAWNED_ROWS, 3);
    SPAWNED_ROWS = 0;
    NETW_CHECK_EQ(world.arena(0)->get_child_count(), 3);

    Node *authority = world.arena(-1)->get_node_or_null(NodePath("Cap0"));
    Node *mirror = world.arena(0)->get_node_or_null(NodePath("Cap0"));
    REQUIRE(authority != nullptr);
    REQUIRE(mirror != nullptr);
    authority->set(StringName("synced_value"), 100);
    authority->set(StringName("watched_value"), 200);
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                mirror,
                StringName("synced_value"),
                100
            )
        ),
        100
    );
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                mirror,
                StringName("watched_value"),
                200
            )
        ),
        200
    );

    for (MultiplayerSynchronizer *sync : syncs) {
        sync->set_visibility_for(rig.peer_id(1), true);
        sync->update_visibility();
    }
    for (const char *probe : NAMES) {
        CHECK(
            netw_test::pump_until_child(rig, world.arena(1), probe) != nullptr
        );
    }
    NETW_CHECK_EQ(world.arena(1)->get_child_count(), 3);
    Node *late = world.arena(1)->get_node_or_null(NodePath("Cap0"));
    REQUIRE(late != nullptr);
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                late,
                StringName("synced_value"),
                100
            )
        ),
        100
    );
}

Node *authority_probe(const String &p_name) {
    Node *root = netw_test::scripted_root(
        netw_test::gdsrc::STOCK_AUTHORITY_PROBE,
        p_name
    );
    Vector<SyncProp> props;
    props.push_back(prop_row(
        ".:synced_value",
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS,
        false
    ));
    netw_test::add_stock_sync(root, "Sync", props);
    return root;
}

Ref<PackedScene> authority_scene() {
    Node *root = authority_probe("AuthorityProbe");
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("AuthorityProbe")
    );
    memdelete(root);
    return packed;
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a scene the stock spawner replicated "
    "keeps the authority it claimed in _enter_tree"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> scene = authority_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    const int client = rig.peer_id(0);
    const String named = itos(client);

    Node *node = scene->instantiate();
    node->set_name(named);
    world.arena(-1)->add_child(node, true);
    Node *mirror = netw_test::pump_until_child(rig, world.arena(0), named);
    REQUIRE(mirror != nullptr);

    Vector<Node *> held;
    held.push_back(node);
    held.push_back(mirror);
    netw_test::peers_agree_on_authority(held);

    NETW_CHECK_EQ(node->get_multiplayer_authority(), client);
    NETW_CHECK_EQ(mirror->get_multiplayer_authority(), client);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a transferred controller outlasts the "
    "name the game derives its authority from, on a peer that joins "
    "after the transfer"
) {
    LoopbackRig rig(2);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> scene = authority_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    const int derived = rig.peer_id(0);
    const int granted = rig.peer_id(1);
    const String named = itos(derived);

    Node *node = scene->instantiate();
    node->set_name(named);
    world.arena(-1)->add_child(node, true);
    REQUIRE(netw_test::pump_until_child(rig, world.arena(0), named) != nullptr);

    const Ref<netw::NetwEntity> entity = netw::NetwEntity::of(node);
    REQUIRE(entity.is_valid());
    entity->grant_control(granted);
    rig.pump(8);

    NETW_CHECK_EQ(node->get_multiplayer_authority(), granted);
    Vector<Node *> held;
    held.push_back(node);
    for (int client = 0; client < 2; ++client) {
        Node *seen = world.arena(client)->get_node_or_null(NodePath(named));
        REQUIRE(seen != nullptr);
        held.push_back(seen);
        NETW_CHECK_EQ(seen->get_multiplayer_authority(), granted);
    }
    netw_test::peers_agree_on_authority(held);

    const int late = rig.add_client();
    rig.hold(late);
    rig.mount_late(late);
    netw_test::seat_stock_branch(rig.branch(late), world);
    rig.release(late);
    rig.pump(8);

    Node *joined = netw_test::pump_until_child(rig, world.arena(late), named);
    REQUIRE(joined != nullptr);
    NETW_CHECK_EQ(joined->get_multiplayer_authority(), granted);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a spawner-less pair that claimed its "
    "own authority in _enter_tree still holds it after adoption"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);

    const int client = rig.peer_id(0);
    const String named = itos(client);

    Node *server_side = authority_probe(named);
    rig.branch(-1)->add_child(server_side);
    Node *client_side = authority_probe(named);
    rig.branch(0)->add_child(client_side);
    rig.pump(6);

    client_side->set(StringName("synced_value"), 44);
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                server_side,
                StringName("synced_value"),
                44
            )
        ),
        44
    );

    CHECK(netw::NetwEntity::of(server_side).is_valid());

    Vector<Node *> held;
    held.push_back(server_side);
    held.push_back(client_side);
    netw_test::peers_agree_on_authority(held);

    NETW_CHECK_EQ(server_side->get_multiplayer_authority(), client);
    NETW_CHECK_EQ(client_side->get_multiplayer_authority(), client);
}

} // namespace TestStockSyncConformanceLaws

#endif

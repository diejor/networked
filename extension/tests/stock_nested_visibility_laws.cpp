#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/stock_probes.h"
#include "support/stock_stand.h"
#include "support/value_flow_stand.h"

namespace TestStockNestedVisibilityLaws {

using namespace godot;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;
using netw_test::StockWorld;
using netw_test::SyncProp;

constexpr int TICKRATE = 30;
const char *PROBE_SOURCE = netw_test::gdsrc::STOCK_NESTED_VISIBILITY_PROBE;

Ref<Script> probe_script() {
    static Ref<Script> *held
        = new Ref<Script>(netw_test::minted_script(PROBE_SOURCE));
    REQUIRE_MESSAGE(held->is_valid(), "the nested probe script did not load");
    return *held;
}

Vector<SyncProp> synced_value_row() {
    Vector<SyncProp> props;
    SyncProp row;
    row.path = NodePath(".:synced_value");
    row.mode = SceneReplicationConfig::REPLICATION_MODE_ALWAYS;
    props.push_back(row);
    return props;
}

Node *nested_probe(const char *p_name, const char *p_kind) {
    Node *node = netw_test::scripted_root(probe_script(), p_name);
    node->set(StringName("event_kind"), StringName(p_kind));
    return node;
}

Ref<PackedScene> child_scene() {
    Node *root = nested_probe("NestedVisibilityChild", "child");
    netw_test::add_stock_sync(root, "Sync", synced_value_row(), root, true);
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("NestedVisibilityChild")
    );
    memdelete(root);
    return packed;
}

Ref<PackedScene> nested_sync_scene() {
    Node *root = nested_probe("NestedSyncShape", "parent");
    netw_test::add_stock_sync(
        root,
        "RootSync",
        synced_value_row(),
        root,
        false
    );
    Node *nested = nested_probe("Nested", "");
    root->add_child(nested);
    nested->set_owner(root);
    netw_test::add_stock_sync(nested, "Sync", synced_value_row(), root, true);
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("NestedSyncShape")
    );
    memdelete(root);
    return packed;
}

Ref<PackedScene> nested_spawner_scene() {
    Node *root = nested_probe("NestedSpawnerShape", "parent");
    netw_test::add_stock_sync(
        root,
        "RootSync",
        synced_value_row(),
        root,
        false
    );
    Node *gun = memnew(Node);
    gun->set_name("Gun");
    root->add_child(gun);
    gun->set_owner(root);
    Node *children = memnew(Node);
    children->set_name("Children");
    gun->add_child(children);
    children->set_owner(root);
    MultiplayerSpawner *spawner = memnew(MultiplayerSpawner);
    spawner->set_name("ChildSpawner");
    spawner->set_spawn_path(NodePath("../Children"));
    gun->add_child(spawner);
    spawner->set_owner(root);
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("NestedSpawnerShape")
    );
    memdelete(root);
    return packed;
}

Ref<PackedScene> dummy_free_scene() {
    Node *root = nested_probe("DummyFreeShape", "parent");
    Node *public_node = nested_probe("Public", "");
    root->add_child(public_node);
    public_node->set_owner(root);
    netw_test::add_stock_sync(
        public_node,
        "Sync",
        synced_value_row(),
        root,
        true
    );
    Node *private_node = nested_probe("Private", "");
    root->add_child(private_node);
    private_node->set_owner(root);
    netw_test::add_stock_sync(
        private_node,
        "Sync",
        synced_value_row(),
        root,
        false
    );
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("DummyFreeShape")
    );
    memdelete(root);
    return packed;
}

MultiplayerSynchronizer *sync_of(Node *p_node, const char *p_name) {
    return Object::cast_to<MultiplayerSynchronizer>(
        p_node->get_node_or_null(NodePath(p_name))
    );
}

void clear_lifecycle() {
    probe_script()->call(StringName("clear_lifecycle_events"));
}

Array lifecycle_kinds(int64_t p_peer, const char *p_phase) {
    return probe_script()
        ->call(StringName("lifecycle_kinds"), p_peer, StringName(p_phase));
}

bool ordered(const Array &p_kinds, const char *p_first, const char *p_second) {
    if (p_kinds.size() != 2) {
        return false;
    }
    return StringName(p_kinds[0]) == StringName(p_first)
        && StringName(p_kinds[1]) == StringName(p_second);
}

bool pump_until_absent(
    LoopbackRig &p_rig,
    Node *p_parent,
    const char *p_path,
    int p_budget = 60
) {
    for (int round = 0; round < p_budget; ++round) {
        if (p_parent->get_node_or_null(NodePath(p_path)) == nullptr) {
            return true;
        }
        p_rig.pump();
    }
    return p_parent->get_node_or_null(NodePath(p_path)) == nullptr;
}

enum Act {
    ACT_ADMIT_AFTER_SPAWN,
    ACT_HIDE_THEN_SHOW,
    ACT_ADMIT_BESIDE_A_HIDDEN_ROOT,
};

enum Plant {
    PLANT_NONE,
    PLANT_THE_ROOT_IS_PUBLIC,
    PLANT_THE_HIDDEN_ROOT_IS_ALSO_ADMITTED,
};

struct NestedScenario {
    String label;
    Act act = ACT_ADMIT_AFTER_SPAWN;
};

NestedScenario admit_after_spawn() {
    NestedScenario scenario;
    scenario.label = "admit-after-spawn";
    return scenario;
}

NestedScenario hide_then_show() {
    NestedScenario scenario;
    scenario.label = "hide-then-show";
    scenario.act = ACT_HIDE_THEN_SHOW;
    return scenario;
}

NestedScenario admit_beside_a_hidden_root() {
    NestedScenario scenario;
    scenario.label = "admit-beside-a-hidden-root";
    scenario.act = ACT_ADMIT_BESIDE_A_HIDDEN_ROOT;
    return scenario;
}

struct NestedEvidence {
    bool clamped_before = false;
    bool clamped_after = false;
    bool admitted_root = false;
    bool admitted_child = false;
    Array enter_kinds;
    Array exit_kinds;
    bool exits_observed = false;
    int64_t child_value = -1;
};

class NestedRun {
    NestedScenario declared;
    Plant planted = PLANT_NONE;
    NestedEvidence seen;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

public:
    explicit NestedRun(
        const NestedScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        LoopbackRig rig(2);
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        const Ref<PackedScene> child = child_scene();
        const Ref<PackedScene> shape = nested_spawner_scene();
        probe_script()->set(StringName("child_scene_path"), child->get_path());
        PackedStringArray scenes;
        scenes.push_back(shape->get_path());
        StockWorld world = netw_test::mount_stock_world(rig, scenes);

        Node *root = shape->instantiate();
        root->set_name("NestedRoot");
        MultiplayerSynchronizer *root_sync = sync_of(root, "RootSync");
        REQUIRE_MESSAGE(root_sync != nullptr, "the shape has no root sync");
        if (plant_is(PLANT_THE_ROOT_IS_PUBLIC)) {
            root_sync->set_visibility_public(true);
        } else {
            root_sync->set_visibility_for(rig.peer_id(0), true);
        }
        if (p_scenario.act == ACT_HIDE_THEN_SHOW) {
            root_sync->set_visibility_for(rig.peer_id(1), true);
        }
        world.arena(-1)->add_child(root, true);

        Node *hidden = nullptr;
        if (p_scenario.act == ACT_ADMIT_BESIDE_A_HIDDEN_ROOT) {
            hidden = shape->instantiate();
            hidden->set_name("HiddenRoot");
            if (plant_is(PLANT_THE_HIDDEN_ROOT_IS_ALSO_ADMITTED)) {
                sync_of(hidden, "RootSync")
                    ->set_visibility_for(rig.peer_id(1), true);
            }
            world.arena(-1)->add_child(hidden, true);
        }

        REQUIRE(
            netw_test::pump_until_child(rig, world.arena(0), "NestedRoot")
            != nullptr
        );
        seen.clamped_before
            = world.arena(1)->get_node_or_null(NodePath("NestedRoot"))
            == nullptr;

        Node *authored = child->instantiate();
        authored->set_name("NestedChild");
        authored->set(StringName("synced_value"), 73);
        root->get_node_or_null(NodePath("Gun/Children"))
            ->add_child(authored, true);
        REQUIRE(
            netw_test::pump_until_child(
                rig,
                world.arena(0),
                "NestedRoot/Gun/Children/NestedChild"
            )
            != nullptr
        );

        if (p_scenario.act == ACT_HIDE_THEN_SHOW) {
            REQUIRE(
                netw_test::pump_until_child(
                    rig,
                    world.arena(1),
                    "NestedRoot/Gun/Children/NestedChild"
                )
                != nullptr
            );
            clear_lifecycle();
            root_sync->set_visibility_for(rig.peer_id(1), false);
            root_sync->update_visibility();
            CHECK(pump_until_absent(rig, world.arena(1), "NestedRoot"));
            seen.exit_kinds = lifecycle_kinds(rig.peer_id(1), "exit");
            seen.exits_observed = true;
        }

        clear_lifecycle();
        root_sync->set_visibility_for(rig.peer_id(1), true);
        root_sync->update_visibility();

        Node *late_root
            = netw_test::pump_until_child(rig, world.arena(1), "NestedRoot");
        seen.admitted_root = late_root != nullptr;
        Node *late_child = netw_test::pump_until_child(
            rig,
            world.arena(1),
            "NestedRoot/Gun/Children/NestedChild"
        );
        seen.admitted_child = late_child != nullptr;
        seen.enter_kinds = lifecycle_kinds(rig.peer_id(1), "enter");
        if (late_child != nullptr) {
            seen.child_value = int64_t(
                netw_test::pump_until_value(
                    rig,
                    late_child,
                    StringName("synced_value"),
                    73
                )
            );
        }
        seen.clamped_after = hidden == nullptr
            || world.arena(1)->get_node_or_null(NodePath("HiddenRoot"))
                == nullptr;
        probe_script()->set(StringName("child_scene_path"), String());
        clear_lifecycle();
    }

    const NestedScenario &scenario() const {
        return declared;
    }

    const NestedEvidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<NestedRun> NestedLaw;

LawVerdict law_clamped(const NestedRun &p_run) {
    const NestedEvidence &seen = p_run.evidence();
    if (p_run.scenario().act != ACT_HIDE_THEN_SHOW && !seen.clamped_before) {
        return law_broken("a peer denied the root still received it");
    }
    if (!seen.clamped_after) {
        return law_broken("a peer denied a second root still received it");
    }
    return law_held();
}

const NestedLaw L_CLAMPED = {
    "clamped",
    "a nested stream never targets a peer that is missing the gated root, and "
    "a child spawned under a hidden parent waits for it rather than "
    "materializing alone",
    &law_clamped,
};

LawVerdict law_parent_first(const NestedRun &p_run) {
    const NestedEvidence &seen = p_run.evidence();
    if (!seen.admitted_root || !seen.admitted_child) {
        return law_broken(
            "the admitted peer holds root=%d child=%d",
            int(seen.admitted_root),
            int(seen.admitted_child)
        );
    }
    if (!ordered(seen.enter_kinds, "parent", "child")) {
        return law_broken(
            "the admitted peer observed %d enter edges out of order",
            int(seen.enter_kinds.size())
        );
    }
    return law_held();
}

const NestedLaw L_PARENT_FIRST = {
    "parent-first",
    "gaining visibility materializes the admitted subtree in topological "
    "order, the parent before every child",
    &law_parent_first,
};

LawVerdict law_child_first(const NestedRun &p_run) {
    const NestedEvidence &seen = p_run.evidence();
    if (!seen.exits_observed) {
        return law_held();
    }
    if (!ordered(seen.exit_kinds, "child", "parent")) {
        return law_broken(
            "the hidden peer observed %d exit edges out of order",
            int(seen.exit_kinds.size())
        );
    }
    return law_held();
}

const NestedLaw L_CHILD_FIRST = {
    "child-first",
    "losing visibility removes the subtree in the reverse order, every child "
    "before its parent",
    &law_child_first,
};

LawVerdict law_heals(const NestedRun &p_run) {
    const NestedEvidence &seen = p_run.evidence();
    if (seen.child_value != 73) {
        return law_broken(
            "the admitted child reads %d rather than the authored value",
            int(seen.child_value)
        );
    }
    return law_held();
}

const NestedLaw L_HEALS = {
    "heals",
    "the replayed subtree carries the current authored value rather than the "
    "value it was spawned with",
    &law_heals,
};

const NestedLaw NESTED_LAWS[]
    = {L_CLAMPED, L_PARENT_FIRST, L_CHILD_FIRST, L_HEALS};

TEST_CASE("[Networked][Sync][SceneTree] the nested visibility laws hold") {
    const NestedScenario CORPUS[] = {
        admit_after_spawn(),
        hide_then_show(),
        admit_beside_a_hidden_root(),
    };
    for (const NestedScenario &scenario : CORPUS) {
        const NestedRun run(scenario);
        for (const NestedLaw &law : NESTED_LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a root every peer may see reds "
    "clamped"
) {
    const NestedScenario scenario = admit_after_spawn();
    const NestedRun run(scenario, PLANT_THE_ROOT_IS_PUBLIC);
    NETW_CELL(L_CLAMPED, scenario);
    NETW_LAW_BREAKS(L_CLAMPED, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a second root admitted alongside the "
    "first reds clamped"
) {
    const NestedScenario scenario = admit_beside_a_hidden_root();
    const NestedRun run(scenario, PLANT_THE_HIDDEN_ROOT_IS_ALSO_ADMITTED);
    NETW_CELL(L_CLAMPED, scenario);
    NETW_LAW_BREAKS(L_CLAMPED, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a public nested stream reaches only "
    "the peers that hold its gated root, and reaches every node of the "
    "admitted subtree"
) {
    LoopbackRig rig(2);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> shape = nested_sync_scene();
    PackedStringArray scenes;
    scenes.push_back(shape->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *root = shape->instantiate();
    root->set_name("NestedSyncRoot");
    sync_of(root, "RootSync")->set_visibility_for(rig.peer_id(0), true);
    world.arena(-1)->add_child(root, true);

    Node *admitted
        = netw_test::pump_until_child(rig, world.arena(0), "NestedSyncRoot");
    REQUIRE(admitted != nullptr);
    CHECK(
        netw_test::pump_until_child(rig, world.arena(1), "NestedSyncRoot", 10)
        == nullptr
    );

    root->set(StringName("synced_value"), 31);
    Node *nested = root->get_node_or_null(NodePath("Nested"));
    REQUIRE(nested != nullptr);
    nested->set(StringName("synced_value"), 41);

    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                admitted,
                StringName("synced_value"),
                31
            )
        ),
        31
    );
    Node *admitted_nested = admitted->get_node_or_null(NodePath("Nested"));
    REQUIRE(admitted_nested != nullptr);
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                admitted_nested,
                StringName("synced_value"),
                41
            )
        ),
        41
    );
    CHECK(
        world.arena(1)->get_node_or_null(NodePath("NestedSyncRoot")) == nullptr
    );
}

TEST_CASE(
    "[Networked][Sync][SceneTree] nested synchronizers need no dummy "
    "root synchronizer, and each honors its own visibility"
) {
    LoopbackRig rig(2);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const Ref<PackedScene> shape = dummy_free_scene();
    PackedStringArray scenes;
    scenes.push_back(shape->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *root = shape->instantiate();
    root->set_name("DummyFreeRoot");
    Node *private_node = root->get_node_or_null(NodePath("Private"));
    REQUIRE(private_node != nullptr);
    MultiplayerSynchronizer *private_sync = sync_of(private_node, "Sync");
    REQUIRE(private_sync != nullptr);
    private_sync->set_visibility_for(rig.peer_id(0), true);
    world.arena(-1)->add_child(root, true);

    Node *first
        = netw_test::pump_until_child(rig, world.arena(0), "DummyFreeRoot");
    Node *second
        = netw_test::pump_until_child(rig, world.arena(1), "DummyFreeRoot");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    root->get_node_or_null(NodePath("Public"))
        ->set(StringName("synced_value"), 11);
    private_node->set(StringName("synced_value"), 22);

    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                first->get_node_or_null(NodePath("Public")),
                StringName("synced_value"),
                11
            )
        ),
        11
    );
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                first->get_node_or_null(NodePath("Private")),
                StringName("synced_value"),
                22
            )
        ),
        22
    );
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                second->get_node_or_null(NodePath("Public")),
                StringName("synced_value"),
                11
            )
        ),
        11
    );
    rig.step_ticks(20);
    NETW_CHECK_EQ(
        int64_t(second->get_node_or_null(NodePath("Private"))
                    ->get(StringName("synced_value"))),
        0
    );

    private_sync->set_visibility_for(rig.peer_id(1), true);
    private_sync->update_visibility();
    NETW_CHECK_EQ(
        int64_t(
            netw_test::pump_until_value(
                rig,
                second->get_node_or_null(NodePath("Private")),
                StringName("synced_value"),
                22
            )
        ),
        22
    );
}

} // namespace TestStockNestedVisibilityLaws

#endif

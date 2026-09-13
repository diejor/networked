#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/stock_probes.h"
#include "support/stock_stand.h"
#include "support/value_flow_stand.h"

#include "netw/api/entity.hpp"

namespace TestSpawnReparentStreamLaws {

using namespace godot;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;
using netw_test::StockWorld;
using netw_test::SyncProp;

constexpr int TICKRATE = 30;
constexpr int64_t BEFORE_THE_MOVE = 11;
constexpr int64_t AFTER_THE_MOVE = 22;
const char *PROBE_SCRIPT = netw_test::gdsrc::STOCK_SYNC_PROBE;

SyncProp always_replicated(const char *p_path) {
    SyncProp row;
    row.path = NodePath(p_path);
    row.mode = SceneReplicationConfig::REPLICATION_MODE_ALWAYS;
    row.on_spawn = false;
    return row;
}

Ref<PackedScene> probe_scene() {
    Node *root = netw_test::scripted_root(PROBE_SCRIPT, "StreamProbe");
    Vector<SyncProp> props;
    props.push_back(always_replicated(".:synced_value"));
    netw_test::add_stock_sync(root, "Sync", props);
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("StreamProbe")
    );
    memdelete(root);
    return packed;
}

Node *plain_child(Node *p_parent, const char *p_name) {
    Node *made = memnew(Node);
    made->set_name(p_name);
    p_parent->add_child(made);
    return made;
}

int64_t route_of(Node *p_node) {
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::of(p_node);
    return entity.is_valid() ? entity->get_route() : 0;
}

int64_t counter_of(LoopbackRig &p_rig, int p_client, const char *p_name) {
    netw::spawn::Pipeline *pipeline = p_rig.spawn_plane(p_client);
    if (pipeline == nullptr) {
        return -1;
    }
    return int64_t(pipeline->counters().get(StringName(p_name), -1));
}

enum Destination {
    DESTINATION_IS_AN_ENTITY,
    DESTINATION_IS_A_PLAIN_NODE,
};

struct MoveScenario {
    String label;
    Destination destination = DESTINATION_IS_AN_ENTITY;
    String parent_after = "Home/Arena/Vehicle";
    bool holds_the_client = false;
    bool renames_the_peer_branch = false;
    bool crosses_to_another_spawner = false;
    bool moves_through_the_node_verb = false;
};

MoveScenario under_an_entity() {
    MoveScenario scenario;
    scenario.label = "under-an-entity";
    return scenario;
}

MoveScenario under_a_plain_node() {
    MoveScenario scenario;
    scenario.label = "under-a-plain-node";
    scenario.destination = DESTINATION_IS_A_PLAIN_NODE;
    scenario.parent_after = "Home/Arena/Garage";
    return scenario;
}

MoveScenario batched_behind_a_held_link() {
    MoveScenario scenario;
    scenario.label = "batched-behind-a-held-link";
    scenario.holds_the_client = true;
    return scenario;
}

MoveScenario onto_a_peer_shaped_differently() {
    MoveScenario scenario;
    scenario.label = "onto-a-peer-shaped-differently";
    scenario.renames_the_peer_branch = true;
    scenario.parent_after = "Elsewhere/Arena/Vehicle";
    return scenario;
}

MoveScenario through_the_node_verb() {
    MoveScenario scenario;
    scenario.label = "through-the-node-verb";
    scenario.moves_through_the_node_verb = true;
    return scenario;
}

MoveScenario into_another_spawners_arena() {
    MoveScenario scenario;
    scenario.label = "into-another-spawners-arena";
    scenario.destination = DESTINATION_IS_A_PLAIN_NODE;
    scenario.crosses_to_another_spawner = true;
    scenario.parent_after = "Away/Arena";
    return scenario;
}

struct MoveEvidence {
    bool materialized = false;
    bool same_instance = false;
    String parent_after;
    int64_t server_route_before = 0;
    int64_t server_route_after = 0;
    int64_t client_route_after = 0;
    int64_t value_before = -1;
    int64_t value_after = -1;
    int64_t drops_unresolved = -1;
    int64_t drops_bad_sender = -1;
};

class MoveRun {
    MoveScenario declared;
    MoveEvidence seen;

public:
    explicit MoveRun(const MoveScenario &p_scenario) : declared(p_scenario) {
        LoopbackRig rig(1);
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);

        const Ref<PackedScene> scene = probe_scene();
        PackedStringArray scenes;
        scenes.push_back(scene->get_path());
        StockWorld world = netw_test::mount_stock_world(rig, scenes);
        netw_test::relabel_stock_world(world, "Home");

        Node *mover = scene->instantiate();
        mover->set_name("Mover");
        world.arena(-1)->add_child(mover, true);

        Node *destination = nullptr;
        if (p_scenario.crosses_to_another_spawner) {
            StockWorld away = netw_test::mount_stock_world(rig, scenes);
            netw_test::relabel_stock_world(away, "Away");
            destination = away.arena(-1);
        } else if (p_scenario.destination == DESTINATION_IS_AN_ENTITY) {
            destination = scene->instantiate();
            destination->set_name("Vehicle");
            world.arena(-1)->add_child(destination, true);
            netw_test::pump_until_child(rig, world.arena(0), "Vehicle");
        } else {
            destination = plain_child(world.arena(-1), "Garage");
            plain_child(world.arena(0), "Garage");
        }

        Node *mirror
            = netw_test::pump_until_child(rig, world.arena(0), "Mover");
        seen.materialized = mirror != nullptr;
        if (mirror == nullptr) {
            return;
        }

        mover->set(StringName("synced_value"), BEFORE_THE_MOVE);
        seen.value_before = int64_t(
            netw_test::pump_until_value(
                rig,
                mirror,
                StringName("synced_value"),
                BEFORE_THE_MOVE
            )
        );
        seen.server_route_before = route_of(mover);

        if (p_scenario.renames_the_peer_branch) {
            Node *held = world.arena(0)->get_parent();
            REQUIRE_MESSAGE(held != nullptr, "the peer holds no world");
            held->set_name("Elsewhere");
        }

        const Ref<netw::NetwEntity> entity = netw::NetwEntity::of(mover);
        REQUIRE_MESSAGE(entity.is_valid(), "the spawned mover has no record");
        if (p_scenario.holds_the_client) {
            rig.hold(0);
        }
        if (p_scenario.moves_through_the_node_verb) {
            mover->reparent(destination);
        } else {
            entity->reparent_to(destination, Ref<netw::NetwReparentOpts>());
        }
        rig.pump(10);
        if (p_scenario.holds_the_client) {
            rig.release(0);
            rig.pump(10);
        }

        seen.server_route_after = route_of(mover);
        Node *moved = rig.route_node(int(seen.server_route_before), 0);
        seen.same_instance = moved == mirror;
        if (moved != nullptr && moved->get_parent() != nullptr) {
            seen.parent_after
                = String(rig.branch(0)->get_path_to(moved->get_parent()));
        }
        seen.client_route_after = route_of(moved);

        mover->set(StringName("synced_value"), AFTER_THE_MOVE);
        seen.value_after = int64_t(
            netw_test::pump_until_value(
                rig,
                moved != nullptr ? moved : mirror,
                StringName("synced_value"),
                AFTER_THE_MOVE
            )
        );

        seen.drops_unresolved = counter_of(rig, 0, "drops_spawn_unresolved");
        seen.drops_bad_sender = counter_of(rig, 0, "drops_spawn_bad_sender");
    }

    const MoveScenario &scenario() const {
        return declared;
    }

    const MoveEvidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<MoveRun> MoveLaw;

LawVerdict law_moves(const MoveRun &p_run) {
    const MoveEvidence &seen = p_run.evidence();
    if (!seen.materialized) {
        return law_broken("the peer never materialized the mover");
    }
    if (!seen.same_instance) {
        return law_broken("the peer holds a different instance after the move");
    }
    if (seen.parent_after != p_run.scenario().parent_after) {
        return law_broken(
            "the peer's mover sits under '%s'",
            seen.parent_after.utf8().get_data()
        );
    }
    return law_held();
}

const MoveLaw L_MOVES = {
    "moves",
    "the peer moves the instance it already holds under its own copy of the "
    "destination, rather than despawning and respawning it",
    &law_moves,
};

LawVerdict law_keeps_the_route(const MoveRun &p_run) {
    const MoveEvidence &seen = p_run.evidence();
    if (seen.server_route_before <= 0) {
        return law_broken("the spawn minted no route");
    }
    if (seen.server_route_after != seen.server_route_before) {
        return law_broken(
            "the authority renumbered %d to %d",
            int(seen.server_route_before),
            int(seen.server_route_after)
        );
    }
    if (seen.client_route_after != seen.server_route_before) {
        return law_broken(
            "the peer answers route %d for route %d",
            int(seen.client_route_after),
            int(seen.server_route_before)
        );
    }
    return law_held();
}

const MoveLaw L_KEEPS_THE_ROUTE = {
    "keeps-the-route",
    "the entity is addressed by the route it was spawned with, before and "
    "after the move, on both peers",
    &law_keeps_the_route,
};

LawVerdict law_keeps_the_stream(const MoveRun &p_run) {
    const MoveEvidence &seen = p_run.evidence();
    if (seen.value_before != BEFORE_THE_MOVE) {
        return law_broken(
            "the stream read %d before the move",
            int(seen.value_before)
        );
    }
    if (seen.value_after != AFTER_THE_MOVE) {
        return law_broken(
            "the stream read %d after the move",
            int(seen.value_after)
        );
    }
    return law_held();
}

const MoveLaw L_KEEPS_THE_STREAM = {
    "keeps-the-stream",
    "a value the authority writes after the move reaches the peer, because "
    "the sync address names the entity rather than where it sits",
    &law_keeps_the_stream,
};

LawVerdict law_resolves_quietly(const MoveRun &p_run) {
    const MoveEvidence &seen = p_run.evidence();
    if (seen.drops_unresolved != 0) {
        return law_broken(
            "the peer dropped %d frames as unresolved",
            int(seen.drops_unresolved)
        );
    }
    if (seen.drops_bad_sender != 0) {
        return law_broken(
            "the peer refused %d frames by sender",
            int(seen.drops_bad_sender)
        );
    }
    return law_held();
}

const MoveLaw L_RESOLVES_QUIETLY = {
    "resolves-quietly",
    "the move takes the resolved arm, so neither an unresolvable anchor nor "
    "a refused sender explains the peer's tree",
    &law_resolves_quietly,
};

const MoveLaw MOVE_LAWS[] = {
    L_MOVES,
    L_KEEPS_THE_ROUTE,
    L_KEEPS_THE_STREAM,
    L_RESOLVES_QUIETLY,
};

TEST_CASE(
    "[Networked][Spawn][Sync][SceneTree] a moved entity keeps the stream it "
    "had, whichever destination the move names"
) {
    const MoveScenario CORPUS[] = {
        under_an_entity(),
        under_a_plain_node(),
        batched_behind_a_held_link(),
        onto_a_peer_shaped_differently(),
        into_another_spawners_arena(),
        through_the_node_verb(),
    };
    for (const MoveScenario &scenario : CORPUS) {
        const MoveRun run(scenario);
        for (const MoveLaw &law : MOVE_LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Spawn][Scene][SceneTree] the verb that seats a player in a "
    "scene admits its peer there, and a scene that has released the peer "
    "reports the entity standing in it"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID home = rig.declare_scene(StringName("Home"));
    REQUIRE(home.is_valid());

    const RID body = rig.declare_entity(
        netw_test::EntityDecl().named(StringName("Alice"))
    );
    Node *owner = rig.node_of(body);
    REQUIRE_MESSAGE(owner != nullptr, "the declared body has no node");
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::of(owner);
    REQUIRE(entity.is_valid());
    entity->set_peer_id(rig.peer_id(0));

    rig.seat(body, home);
    const int64_t route = entity->get_route();
    REQUIRE(route > 0);

    CHECK_FALSE(rig.server()->scene_leaves_route_unadmitted(route));

    rig.server()->scene_release(home, rig.peer_id(0));
    CHECK(rig.server()->scene_leaves_route_unadmitted(route));

    entity->set_peer_id(0);
    CHECK_FALSE(rig.server()->scene_leaves_route_unadmitted(route));
}

} // namespace TestSpawnReparentStreamLaws

#endif

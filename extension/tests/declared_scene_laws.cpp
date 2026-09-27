#include "support/netw_test.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwDeclaredSceneLaws {

using namespace netw_test;

EntityDecl crate() {
    return EntityDecl().named("Crate").on_route(91).placed_at(godot::Vector2());
}

Scenario placed_entity() {
    Scenario scenario;
    scenario.label = "placed-entity";
    scenario.world.scene("Arena").scene("Annex").entity(crate(), "Arena");
    return scenario.until(2);
}

Scenario moved_entity() {
    Scenario scenario = placed_entity();
    scenario.label = "moved-entity";
    scenario.place(1, "Crate", "Annex");
    return scenario;
}

Scenario relocated_entity() {
    Scenario scenario = placed_entity();
    scenario.label = "relocated-entity";
    scenario.move(1, "Crate", "Annex");
    return scenario;
}

Scenario nested_scene() {
    Scenario scenario;
    scenario.label = "nested-scene";
    scenario.world.scene("Arena").scene("Vault").entity(crate(), "Vault");
    scenario.place(1, "Vault", "Arena");
    return scenario.until(2);
}

Scenario watched_scene() {
    Scenario scenario = placed_entity();
    scenario.label = "watched-scene";
    scenario.clients = 2;
    scenario.watch(1, "Arena", 0);
    return scenario;
}

Scenario unwatched_scene() {
    Scenario scenario = watched_scene();
    scenario.label = "unwatched-scene";
    scenario.unwatch(2, "Arena", 0);
    return scenario.until(3);
}

Scenario sibling_scenes() {
    Scenario scenario;
    scenario.label = "sibling-scenes";
    scenario.world.scene("First", "Arena").scene("Second", "Arena");
    scenario.watch(1, "First", 0);
    return scenario.until(2);
}

LawVerdict law_membership_follows_the_tree(const ScenarioRun &p_run) {
    const Membership arena = p_run.membership("Arena");
    const Membership annex = p_run.membership("Annex");
    if (!arena.taken() || !annex.taken()) {
        return law_broken("a declared scene answered nothing");
    }
    const bool relocated = p_run.scenario().declares("place")
        || p_run.scenario().declares("move");
    const Membership &home = relocated ? annex : arena;
    const Membership &vacated = relocated ? arena : annex;
    if (!home.encloses("Crate")) {
        return law_broken(
            "the scene the crate resides in encloses %d entities and not it",
            home.members()
        );
    }
    if (vacated.encloses("Crate")) {
        return law_broken("the scene the crate left still encloses it");
    }
    if (vacated.members() != 0) {
        return law_broken(
            "an empty scene encloses %d entities",
            vacated.members()
        );
    }
    return law_held();
}

LawVerdict law_a_nested_scene_owns_its_own(const ScenarioRun &p_run) {
    const Membership arena = p_run.membership("Arena");
    const Membership vault = p_run.membership("Vault");
    if (!arena.taken() || !vault.taken()) {
        return law_broken("a declared scene answered nothing");
    }
    if (!vault.encloses("Crate")) {
        return law_broken("the inner scene does not enclose its own member");
    }
    if (!arena.encloses("Vault")) {
        return law_broken("the outer scene does not enclose the inner one");
    }
    if (arena.encloses("Crate")) {
        return law_broken(
            "the outer scene reaches past the inner one to its %d member(s)",
            vault.members()
        );
    }
    if (arena.members() != 1) {
        return law_broken(
            "the outer scene encloses %d entities where the inner one is all "
            "it owns",
            arena.members()
        );
    }
    return law_held();
}

bool declared_watch(
    const Scenario &p_scenario,
    const godot::StringName &p_scene,
    int p_client
) {
    bool watching = false;
    for (const Scenario::Step &step : p_scenario.steps) {
        if (step.subject != p_scene || int(step.value) != p_client) {
            continue;
        }
        if (step.verb == godot::StringName("watch")) {
            watching = true;
        } else if (step.verb == godot::StringName("unwatch")) {
            watching = false;
        }
    }
    return watching;
}

LawVerdict law_a_boundary_is_answerable(const ScenarioRun &p_run) {
    const Scenario &scenario = p_run.scenario();
    for (int at = 0; at < scenario.world.scene_count(); ++at) {
        const godot::StringName name = scenario.world.scene_at(at).name;
        const Membership scene = p_run.membership(name);
        if (!scene.taken()) {
            return law_broken("a declared scene answered nothing");
        }
        bool named = false;
        for (int client = 0; client < scenario.clients; ++client) {
            named = named || declared_watch(scenario, name, client);
        }
        if (scene.has_boundary() != named) {
            return law_broken(
                named ? "a scene holding %d viewer(s) answers with no layer"
                      : "a scene nobody watches answers with a layer, "
                        "holding %d viewer(s)",
                scene.viewers()
            );
        }
    }
    return law_held();
}

LawVerdict law_a_watch_is_per_scene(const ScenarioRun &p_run) {
    const Scenario &scenario = p_run.scenario();
    for (int at = 0; at < scenario.world.scene_count(); ++at) {
        const godot::StringName name = scenario.world.scene_at(at).name;
        const Membership scene = p_run.membership(name);
        if (!scene.taken()) {
            return law_broken("a declared scene answered nothing");
        }
        for (int client = 0; client < scenario.clients; ++client) {
            const bool owed = declared_watch(scenario, name, client);
            if (scene.subscribes(client) != owed) {
                return law_broken(
                    owed ? "a scene client %d watches holds %d viewer(s) "
                           "without it"
                         : "a scene client %d watches nothing of holds %d "
                           "viewer(s) with it",
                    client,
                    scene.viewers()
                );
            }
        }
    }
    return law_held();
}

const LawRow L_BOUNDARY = {
    "L-BOUNDARY",
    "a scene a peer watches answers with the layer that watch opened, "
    "and a scene nobody named answers with none",
    law_a_boundary_is_answerable,
};

const LawRow L_MEMBER = {
    "L-MEMBER",
    "an entity belongs to the scene above it, and a placement that moves it "
    "moves the membership with nothing re-enrolling it",
    law_membership_follows_the_tree,
};

const LawRow L_NEST = {
    "L-NEST",
    "a nested scene owns its own descendants, so the scene above it encloses "
    "the nested scene and nothing under it",
    law_a_nested_scene_owns_its_own,
};

const LawRow L_WATCH = {
    "L-WATCH",
    "a scene reaches exactly the peers watching it and not unwatched since, "
    "so a scene nobody named denies, and an instance sharing another's stem "
    "carries its own boundary",
    law_a_watch_is_per_scene,
};

TEST_CASE(
    "[Networked][Scene][Declared] the scene facet is consumed at arm, so a "
    "live entity refuses both writes"
) {
    LoopbackRig rig(0);
    const godot::RID scene = rig.declare_scene("Arena");

    NETW_CHECK_EQ(
        int(rig.server()->scene_declare(scene)),
        int(godot::ERR_UNCONFIGURED)
    );
    NETW_CHECK_EQ(
        int(rig.server()->scene_undeclare(scene)),
        int(godot::ERR_UNCONFIGURED)
    );
    CHECK(rig.server()->scene_is_declared(scene));
}

TEST_CASE(
    "[Networked][Scene][Declared] a scene that declared itself carries the "
    "facet and answers under the name it declared"
) {
    LoopbackRig rig(0);
    const godot::RID scene = rig.declare_scene("DeclaredArena");

    CHECK(rig.server()->scene_is_declared(scene));
    CHECK(
        godot::StringName(rig.server()->scene_get_param(
            scene,
            netw::NetwMultiplayer::SCENE_PARAM_LABEL
        ))
        == godot::StringName("DeclaredArena")
    );

    netw::NetwMultiplayer *api = rig.server();
    const godot::RID plain = api->entity_create();
    godot::Node *body = memnew(godot::Node);
    body->set_name("Crate");
    NETW_CHECK_GT(int(api->entity_admit(plain)), 0);
    NETW_CHECK_EQ(int(api->entity_bind_node(plain, body)), int(godot::OK));

    CHECK_FALSE(api->scene_is_declared(plain));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a placed entity belongs to its scene"
) {
    const Scenario scenario = placed_entity();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_MEMBER, scenario);
    NETW_LAW_HOLDS(L_MEMBER, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a placement moves the membership with it"
) {
    const Scenario scenario = moved_entity();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_MEMBER, scenario);
    NETW_LAW_HOLDS(L_MEMBER, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] an enrolled membership misses the move"
) {
    const Scenario scenario = moved_entity();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::scenes(rig, scenario, PLANT_ENROLLED_MEMBERSHIP);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_MEMBER, scenario);
    NETW_LAW_BREAKS(L_MEMBER, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a published move carries the "
    "membership with it"
) {
    const Scenario scenario = relocated_entity();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_MEMBER, scenario);
    NETW_LAW_HOLDS(L_MEMBER, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] an enrolled membership misses the "
    "published move"
) {
    const Scenario scenario = relocated_entity();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::scenes(rig, scenario, PLANT_ENROLLED_MEMBERSHIP);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_MEMBER, scenario);
    NETW_LAW_BREAKS(L_MEMBER, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a nested scene owns its own members"
) {
    const Scenario scenario = nested_scene();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_NEST, scenario);
    NETW_LAW_HOLDS(L_NEST, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a walk that ignores nesting claims the "
    "inner members"
) {
    const Scenario scenario = nested_scene();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::scenes(rig, scenario, PLANT_TRANSPARENT_NESTING);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_NEST, scenario);
    NETW_LAW_BREAKS(L_NEST, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a scene nobody named denies every peer"
) {
    const Scenario scenario = placed_entity();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    {
        NETW_CELL(L_WATCH, scenario);
        NETW_LAW_HOLDS(L_WATCH, run);
    }
    {
        NETW_CELL(L_BOUNDARY, scenario);
        NETW_LAW_HOLDS(L_BOUNDARY, run);
    }
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a watched scene reaches only its peer"
) {
    const Scenario scenario = watched_scene();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_WATCH, scenario);
    NETW_LAW_HOLDS(L_WATCH, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] an unwatched peer reaches nowhere"
) {
    const Scenario scenario = unwatched_scene();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_WATCH, scenario);
    NETW_LAW_HOLDS(L_WATCH, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] two instances of one level are watched "
    "apart"
) {
    const Scenario scenario = sibling_scenes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    {
        NETW_CELL(L_WATCH, scenario);
        NETW_LAW_HOLDS(L_WATCH, run);
    }
    {
        NETW_CELL(L_BOUNDARY, scenario);
        NETW_LAW_HOLDS(L_BOUNDARY, run);
    }
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a boundary keyed by stem answers for "
    "the instance nobody watches"
) {
    const Scenario scenario = sibling_scenes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::scenes(rig, scenario, PLANT_SHARED_ADMISSION);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_BOUNDARY, scenario);
    NETW_LAW_BREAKS(L_BOUNDARY, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a watch keyed by stem reaches the "
    "sibling instance"
) {
    const Scenario scenario = sibling_scenes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::scenes(rig, scenario, PLANT_SHARED_ADMISSION);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_WATCH, scenario);
    NETW_LAW_BREAKS(L_WATCH, run);
}

} // namespace TestNetwDeclaredSceneLaws

#endif

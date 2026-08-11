#include "support/netw_test.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwDeclaredSceneLaws {

using namespace netw_test;

EntityDecl crate() {
    return EntityDecl().named("Crate").on_route(91).placed_at(godot::Vector2());
}

// One entity seated in one of two scenes, which is the smallest world that can
// tell membership from the absence of it.
Scenario seated_entity() {
    Scenario scenario;
    scenario.label = "seated-entity";
    scenario.world.scene("Arena").scene("Annex").entity(crate(), "Arena");
    return scenario.until(2);
}

Scenario moved_entity() {
    Scenario scenario = seated_entity();
    scenario.label = "moved-entity";
    scenario.seat(1, "Crate", "Annex");
    return scenario;
}

// An inner scene seated inside an outer one. Both enclose the crate by
// ancestry, and only the inner one owns it.
Scenario nested_scene() {
    Scenario scenario;
    scenario.label = "nested-scene";
    scenario.world.scene("Arena").scene("Vault").entity(crate(), "Vault");
    scenario.seat(1, "Vault", "Arena");
    return scenario.until(2);
}

Scenario admitted_scene() {
    Scenario scenario = seated_entity();
    scenario.label = "admitted-scene";
    scenario.clients = 2;
    scenario.admit(1, "Arena", 0);
    return scenario;
}

Scenario released_scene() {
    Scenario scenario = admitted_scene();
    scenario.label = "released-scene";
    scenario.release(2, "Arena", 0);
    return scenario.until(3);
}

// Two instances of one level. Stems are not unique, so this is the world that
// says whether a boundary belongs to the instance or to the archetype.
Scenario sibling_scenes() {
    Scenario scenario;
    scenario.label = "sibling-scenes";
    scenario.world.scene("First", "Arena").scene("Second", "Arena");
    scenario.admit(1, "First", 0);
    return scenario.until(2);
}

LawVerdict law_membership_follows_the_tree(const ScenarioRun &p_run) {
    const Membership arena = p_run.membership("Arena");
    const Membership annex = p_run.membership("Annex");
    if (!arena.taken() || !annex.taken()) {
        return law_broken("a declared scene answered nothing");
    }
    const bool moved = p_run.scenario().declares("seat");
    const Membership &home = moved ? annex : arena;
    const Membership &vacated = moved ? arena : annex;
    if (!home.encloses("Crate")) {
        return law_broken(
            "the scene the crate is seated in encloses %d entities and not it",
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

// Whether the scenario left [param p_client] admitted to [param p_scene],
// read as the last admission edge it declared. A scene that was never named is
// denying, which is what a boundary defaults to.
bool declared_admission(
    const Scenario &p_scenario,
    const godot::StringName &p_scene,
    int p_client
) {
    bool admitted = false;
    for (const Scenario::Step &step : p_scenario.steps) {
        if (step.subject != p_scene || int(step.value) != p_client) {
            continue;
        }
        if (step.verb == godot::StringName("admit")) {
            admitted = true;
        } else if (step.verb == godot::StringName("release")) {
            admitted = false;
        }
    }
    return admitted;
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
            named = named || declared_admission(scenario, name, client);
        }
        if (scene.has_boundary() != named) {
            return law_broken(
                named
                    ? "a scene holding %d viewer(s) answers with no layer"
                    : "a scene nobody admitted anyone to answers with a layer, "
                      "holding %d viewer(s)",
                scene.viewers()
            );
        }
    }
    return law_held();
}

LawVerdict law_admission_is_per_scene(const ScenarioRun &p_run) {
    const Scenario &scenario = p_run.scenario();
    for (int at = 0; at < scenario.world.scene_count(); ++at) {
        const godot::StringName name = scenario.world.scene_at(at).name;
        const Membership scene = p_run.membership(name);
        if (!scene.taken()) {
            return law_broken("a declared scene answered nothing");
        }
        for (int client = 0; client < scenario.clients; ++client) {
            const bool owed = declared_admission(scenario, name, client);
            if (scene.admits(client) != owed) {
                return law_broken(
                    owed
                        ? "a scene that admitted client %d holds %d viewer(s) "
                          "without it"
                        : "a scene that admitted client %d nothing holds %d "
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
    "a scene that admitted a peer answers with the layer its admission opened, "
    "and a scene nobody named answers with none",
    law_a_boundary_is_answerable,
};

const LawRow L_MEMBER = {
    "L-MEMBER",
    "an entity belongs to the scene above it, and a seat that moves it moves "
    "the membership with nothing re-enrolling it",
    law_membership_follows_the_tree,
};

const LawRow L_NEST = {
    "L-NEST",
    "a nested scene owns its own descendants, so the scene above it encloses "
    "the nested scene and nothing under it",
    law_a_nested_scene_owns_its_own,
};

const LawRow L_ADMIT = {
    "L-ADMIT",
    "a scene admits exactly the peers admitted to it and not released since, "
    "so a scene nobody named denies, and an instance sharing another's stem "
    "carries its own boundary",
    law_admission_is_per_scene,
};

TEST_CASE(
    "[Networked][Scene][Declared][Law] a seated entity belongs to its scene"
) {
    const Scenario scenario = seated_entity();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_MEMBER, scenario);
    NETW_LAW_HOLDS(L_MEMBER, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a seat moves the membership with it"
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
    const Scenario scenario = seated_entity();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    {
        NETW_CELL(L_ADMIT, scenario);
        NETW_LAW_HOLDS(L_ADMIT, run);
    }
    {
        NETW_CELL(L_BOUNDARY, scenario);
        NETW_LAW_HOLDS(L_BOUNDARY, run);
    }
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] an admitted scene admits only its peer"
) {
    const Scenario scenario = admitted_scene();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_ADMIT, scenario);
    NETW_LAW_HOLDS(L_ADMIT, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a released peer is admitted nowhere"
) {
    const Scenario scenario = released_scene();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_ADMIT, scenario);
    NETW_LAW_HOLDS(L_ADMIT, run);
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] two instances of one level admit apart"
) {
    const Scenario scenario = sibling_scenes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());
    {
        NETW_CELL(L_ADMIT, scenario);
        NETW_LAW_HOLDS(L_ADMIT, run);
    }
    {
        NETW_CELL(L_BOUNDARY, scenario);
        NETW_LAW_HOLDS(L_BOUNDARY, run);
    }
}

TEST_CASE(
    "[Networked][Scene][Declared][Law] a boundary keyed by stem answers for "
    "the instance nobody admitted"
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
    "[Networked][Scene][Declared][Law] an admission keyed by stem reaches the "
    "sibling instance"
) {
    const Scenario scenario = sibling_scenes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::scenes(rig, scenario, PLANT_SHARED_ADMISSION);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_ADMIT, scenario);
    NETW_LAW_BREAKS(L_ADMIT, run);
}

} // namespace TestNetwDeclaredSceneLaws

#endif

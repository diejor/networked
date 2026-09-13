#include "support/netw_test.h"

#include "support/event_ring.h"
#include "support/netw_cells.h"

#include <memory>

#include "godot/callable.hpp"
#include "godot/variant.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwEventShellLaws {

using namespace godot;
using netw::EventPlane;
using netw::NetwMultiplayer;
using netw_test::EventRing;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

constexpr int64_t ROUTE = 12;
constexpr int64_t SESSION_RING = 0;
constexpr int64_t ACTOR_PEER = 3;
constexpr int64_t OBSERVER_PEER = 4;
constexpr int64_t OUTSIDE_THE_TAXONOMY = 9;
const char *SUBJECT = "Racer";
const char *THE_LAYER_WATCHED = "combat";
const char *THE_OTHER_LAYER = "ambient";

enum Plant {
    PLANT_NONE,
    PLANT_AN_UNARMED_SESSION,
    PLANT_A_DEATH_THAT_SNAPSHOTS_NOTHING,
    PLANT_A_ROW_KEYED_BY_ROUTE_ALONE,
    PLANT_A_STAGE_COUNTED_AS_REMOTE_INPUT,
};

class RowSink final : public CallableCustom {
    std::shared_ptr<Dictionary> stopped;
    ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    RowSink(
        const std::shared_ptr<Dictionary> &p_stopped,
        const Object *p_anchor
    )
        : stopped(p_stopped), anchor(netw::gd::instance_id(p_anchor)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("RowSink");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &RowSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &RowSink::before;
    }

    ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **p_arguments,
        int p_count,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        if (p_count > 0) {
            *stopped = Dictionary(*p_arguments[0]);
        }
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

struct ShellScenario {
    String label;
    int spawns = 0;
    int scene_lives = 0;
    int interest_commits = 0;
    int peer_edges = 0;
    int stages = 0;
    bool dies = false;
    bool observes_a_layer = false;
    bool reports_outside_the_taxonomy = false;
};

ShellScenario a_peer_edge() {
    ShellScenario scenario;
    scenario.label = "a-peer-edge";
    scenario.peer_edges = 1;
    return scenario;
}

ShellScenario a_lifecycle() {
    ShellScenario scenario;
    scenario.label = "a-lifecycle";
    scenario.spawns = 2;
    scenario.scene_lives = 1;
    scenario.interest_commits = 1;
    return scenario;
}

ShellScenario a_death() {
    ShellScenario scenario;
    scenario.label = "a-death";
    scenario.spawns = 1;
    scenario.dies = true;
    return scenario;
}

ShellScenario a_reported_stage() {
    ShellScenario scenario;
    scenario.label = "a-reported-stage";
    scenario.stages = 1;
    return scenario;
}

ShellScenario a_layered_observer() {
    ShellScenario scenario;
    scenario.label = "a-layered-observer";
    scenario.observes_a_layer = true;
    return scenario;
}

ShellScenario a_value_nobody_named() {
    ShellScenario scenario;
    scenario.label = "a-value-nobody-named";
    scenario.reports_outside_the_taxonomy = true;
    return scenario;
}

class ShellRun {
    ShellScenario declared;
    Plant planted = PLANT_NONE;
    Array route_rows;
    Array session_rows;
    Dictionary stopped_on;
    Dictionary layer_row;
    int64_t watches_standing = -1;
    bool unwatched = false;
    int64_t illegal_watch = 0;
    int64_t remote_input = -1;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

public:
    ShellRun(const ShellScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        Ref<NetwMultiplayer> session;
        session.instantiate();
        if (!plant_is(PLANT_AN_UNARMED_SESSION)) {
            session->event_arm(true);
        }

        const std::shared_ptr<Dictionary> death
            = std::make_shared<Dictionary>();
        const std::shared_ptr<Dictionary> layered
            = std::make_shared<Dictionary>();

        if (declared.dies) {
            PackedInt64Array terminal;
            terminal.push_back(EventPlane::DESPAWNED);
            Dictionary target;
            if (plant_is(PLANT_A_ROW_KEYED_BY_ROUTE_ALONE)) {
                target[StringName("route")] = ROUTE + 1;
            } else {
                target[StringName("entity_id")] = StringName(SUBJECT);
            }
            session->event_watch(
                terminal,
                target,
                Dictionary(),
                Callable(memnew(RowSink(death, session.ptr()))),
                Dictionary()
            );
        }

        int64_t layer_watch = 0;
        if (declared.observes_a_layer) {
            PackedInt64Array entered;
            entered.push_back(EventPlane::OBSERVER_ENTERED);
            Dictionary predicate;
            Array layers;
            layers.push_back(String(THE_LAYER_WATCHED));
            predicate[StringName("layer_in")] = layers;
            layer_watch = session->event_watch(
                entered,
                Dictionary(),
                predicate,
                Callable(memnew(RowSink(layered, session.ptr()))),
                Dictionary()
            );
            Dictionary unknown;
            unknown[StringName("layer_is")] = String(THE_LAYER_WATCHED);
            illegal_watch = session->event_watch(
                entered,
                Dictionary(),
                unknown,
                Callable(memnew(RowSink(layered, session.ptr()))),
                Dictionary()
            );
        }

        for (int index = 0; index < declared.spawns; index++) {
            Dictionary detail;
            detail[StringName("peer_id")] = ACTOR_PEER;
            session->report_event(
                EventPlane::SPAWNED,
                ROUTE,
                detail,
                ACTOR_PEER,
                StringName(SUBJECT),
                Dictionary(),
                OK
            );
        }
        for (int index = 0; index < declared.scene_lives; index++) {
            Dictionary detail;
            detail[StringName("scene")] = String("Track");
            session->report_event(
                EventPlane::SCENE_LIVE,
                ROUTE,
                detail,
                0,
                StringName(),
                Dictionary(),
                OK
            );
        }
        for (int index = 0; index < declared.interest_commits; index++) {
            session->report_event(
                EventPlane::INTEREST_COMMIT,
                SESSION_RING,
                Dictionary(),
                0,
                StringName(),
                Dictionary(),
                OK
            );
        }
        for (int index = 0; index < declared.peer_edges; index++) {
            Dictionary detail;
            detail[StringName("peer")] = ACTOR_PEER;
            session->report_event(
                EventPlane::PEER_JOINED,
                SESSION_RING,
                detail,
                ACTOR_PEER,
                StringName(),
                Dictionary(),
                OK
            );
        }
        for (int index = 0; index < declared.stages; index++) {
            Dictionary detail;
            detail[StringName("comp")] = 2;
            if (plant_is(PLANT_A_STAGE_COUNTED_AS_REMOTE_INPUT)) {
                session->count_verdict(ERR_INVALID_DATA, ROUTE);
            }
            session->report_event(
                EventPlane::SYNC_DECODE,
                ROUTE,
                detail,
                0,
                StringName(),
                Dictionary(),
                ERR_INVALID_DATA
            );
        }
        if (declared.observes_a_layer) {
            Dictionary quiet;
            quiet[StringName("layer")] = StringName(THE_OTHER_LAYER);
            session->report_event(
                EventPlane::OBSERVER_ENTERED,
                ROUTE,
                quiet,
                OBSERVER_PEER,
                StringName(),
                Dictionary(),
                OK
            );
            layer_row = *layered;
            Dictionary loud;
            loud[StringName("layer")] = StringName(THE_LAYER_WATCHED);
            session->report_event(
                EventPlane::OBSERVER_ENTERED,
                ROUTE,
                loud,
                OBSERVER_PEER,
                StringName(),
                Dictionary(),
                OK
            );
        }
        if (declared.reports_outside_the_taxonomy) {
            session->report_event(
                OUTSIDE_THE_TAXONOMY,
                ROUTE,
                Dictionary(),
                0,
                StringName(),
                Dictionary(),
                OK
            );
        }
        if (declared.dies) {
            Dictionary model;
            model[StringName("entity_id")] = StringName(SUBJECT);
            model[StringName("peer_id")] = ACTOR_PEER;
            Dictionary detail;
            detail[StringName("peer_id")] = ACTOR_PEER;
            session->report_event(
                EventPlane::DESPAWNING,
                ROUTE,
                detail,
                ACTOR_PEER,
                StringName(SUBJECT),
                plant_is(PLANT_A_DEATH_THAT_SNAPSHOTS_NOTHING) ? Dictionary()
                                                               : model,
                OK
            );
            session->report_event(
                EventPlane::DESPAWNED,
                ROUTE,
                Dictionary(),
                0,
                StringName(),
                Dictionary(),
                OK
            );
        }

        route_rows = session->event_ring(ROUTE);
        session_rows = session->event_ring(SESSION_RING);
        stopped_on = *death;
        if (declared.observes_a_layer) {
            layer_row = *layered;
            watches_standing = session->event_watches().size();
            unwatched = session->event_unwatch(layer_watch);
        }
        remote_input = session->stats_get_verdict_count(ERR_INVALID_DATA);
    }

    const ShellScenario &scenario() const {
        return declared;
    }

    EventRing on_route() const {
        return EventRing(route_rows);
    }

    EventRing on_session() const {
        return EventRing(session_rows);
    }

    Dictionary death_row() const {
        return stopped_on;
    }

    Dictionary layered_row() const {
        return layer_row;
    }

    int64_t watches() const {
        return watches_standing;
    }

    bool released() const {
        return unwatched;
    }

    int64_t refused_watch() const {
        return illegal_watch;
    }

    int64_t counted_as_remote() const {
        return remote_input;
    }
};

typedef LawRowFor<ShellRun> ShellLaw;

int rows_of(const EventRing &p_ring, int64_t p_event) {
    int total = 0;
    for (int index = 0; index < p_ring.size(); index++) {
        total += p_ring.event_at(index) == p_event ? 1 : 0;
    }
    return total;
}

LawVerdict law_complete(const ShellRun &p_run) {
    const ShellScenario &declared = p_run.scenario();
    const EventRing route = p_run.on_route();
    const EventRing shared = p_run.on_session();
    if (declared.dies) {
        if (route.size() != 0) {
            return law_broken(
                "a death left %d rows of history behind it",
                route.size()
            );
        }
    } else {
        if (rows_of(route, EventPlane::SPAWNED) != declared.spawns) {
            return law_broken(
                "%d spawn rows for %d spawns",
                rows_of(route, EventPlane::SPAWNED),
                declared.spawns
            );
        }
        if (rows_of(route, EventPlane::SCENE_LIVE) != declared.scene_lives) {
            return law_broken(
                "%d scene rows for %d scenes",
                rows_of(route, EventPlane::SCENE_LIVE),
                declared.scene_lives
            );
        }
    }
    if (rows_of(shared, EventPlane::INTEREST_COMMIT)
        != declared.interest_commits) {
        return law_broken(
            "%d commit rows for %d commits",
            rows_of(shared, EventPlane::INTEREST_COMMIT),
            declared.interest_commits
        );
    }
    if (rows_of(shared, EventPlane::PEER_JOINED) != declared.peer_edges) {
        return law_broken(
            "%d peer rows for %d edges",
            rows_of(shared, EventPlane::PEER_JOINED),
            declared.peer_edges
        );
    }
    if (rows_of(shared, EventPlane::SPAWNED) != 0) {
        return law_broken("a route's spawn landed on the session's own ring");
    }
    return law_held();
}

LawVerdict law_closed(const ShellRun &p_run) {
    if (p_run.scenario().reports_outside_the_taxonomy
        && p_run.on_route().size() != 0) {
        return law_broken(
            "a value outside the taxonomy opened %d rows",
            p_run.on_route().size()
        );
    }
    if (p_run.scenario().observes_a_layer && p_run.refused_watch() != -1) {
        return law_broken(
            "a row naming an unknown predicate key installed as %d",
            int(p_run.refused_watch())
        );
    }
    return law_held();
}

LawVerdict law_terminal(const ShellRun &p_run) {
    if (!p_run.scenario().dies) {
        return law_held();
    }
    const Dictionary row = p_run.death_row();
    if (row.is_empty()) {
        return law_broken("nothing stopped on the death");
    }
    const int64_t event = int64_t(row[netw::event_key::event()]);
    if (event != EventPlane::DESPAWNED) {
        return law_broken(
            "the row stopped on is %s, not the death",
            EventPlane::name_of(event)
        );
    }
    const StringName entity_id = row[netw::event_key::entity_id()];
    if (String(entity_id) != String(SUBJECT)) {
        return law_broken("the death names no subject");
    }
    const Dictionary model = row[netw::event_key::model()];
    if (String(model.get(StringName("entity_id"), String()))
        != String(SUBJECT)) {
        return law_broken("the death carries no model of what died");
    }
    if (int64_t(model.get(StringName("peer_id"), -1)) != ACTOR_PEER) {
        return law_broken("the model the death carries names no owner");
    }
    return law_held();
}

LawVerdict law_targeted(const ShellRun &p_run) {
    if (!p_run.scenario().observes_a_layer) {
        return law_held();
    }
    const Dictionary row = p_run.layered_row();
    if (row.is_empty()) {
        return law_broken("the layer nobody watched was reported anyway");
    }
    const int64_t peer = int64_t(row[netw::event_key::peer()]);
    if (peer != OBSERVER_PEER) {
        return law_broken("the row stopped on names peer %d", int(peer));
    }
    if (p_run.watches() != 1) {
        return law_broken(
            "%d rows stand where one was installed",
            int(p_run.watches())
        );
    }
    if (!p_run.released()) {
        return law_broken("the row that was installed could not be released");
    }
    return law_held();
}

LawVerdict law_local(const ShellRun &p_run) {
    if (p_run.scenario().stages == 0) {
        return law_held();
    }
    if (p_run.counted_as_remote() != 0) {
        return law_broken(
            "a stage the session reported was counted as %d remote refusals",
            int(p_run.counted_as_remote())
        );
    }
    const EventRing route = p_run.on_route();
    for (int index = 0; index < route.size(); index++) {
        const Dictionary row = route.at(index);
        if (row.is_empty()) {
            continue;
        }
        const int64_t event = int64_t(row[netw::event_key::event()]);
        const int64_t verdict = int64_t(row[netw::event_key::verdict()]);
        if (event == EventPlane::SYNC_DECODE
            && verdict != int64_t(ERR_INVALID_DATA)) {
            return law_broken("the stage row carries verdict %d", int(verdict));
        }
    }
    return law_held();
}

const ShellLaw L_COMPLETE = {
    "complete",
    "an act the session reported leaves one row of its own kind, on the ring "
    "its route names",
    &law_complete,
};

const ShellLaw L_CLOSED = {
    "closed",
    "the taxonomy and the predicate vocabulary are closed at the session face",
    &law_closed,
};

const ShellLaw L_TERMINAL = {
    "terminal",
    "a death carries the model its subject last had and takes the route's "
    "history with it",
    &law_terminal,
};

const ShellLaw L_TARGETED = {
    "targeted",
    "a row sees the edges it named and no others",
    &law_targeted,
};

const ShellLaw L_LOCAL = {
    "local",
    "a stage the session judged reports its verdict without counting it as "
    "remote input",
    &law_local,
};

const ShellLaw LAWS[] = {L_COMPLETE, L_CLOSED, L_TERMINAL, L_TARGETED, L_LOCAL};

TEST_CASE("[Networked][Event][Hosted] the shell emitter laws hold") {
    const ShellScenario CORPUS[] = {
        a_peer_edge(),
        a_lifecycle(),
        a_death(),
        a_reported_stage(),
        a_layered_observer(),
        a_value_nobody_named(),
    };
    for (const ShellScenario &scenario : CORPUS) {
        const ShellRun run(scenario);
        for (const ShellLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE("[Networked][Event][Hosted] an unwatched session records nothing") {
    const ShellScenario scenario = a_lifecycle();
    const ShellRun run(scenario, PLANT_AN_UNARMED_SESSION);
    NETW_CHECK_EQ(run.on_route().size(), 0);
    NETW_CHECK_EQ(run.on_session().size(), 0);
}

TEST_CASE(
    "[Networked][Event][Hosted] a death that snapshots nothing reds "
    "terminal"
) {
    const ShellScenario scenario = a_death();
    const ShellRun run(scenario, PLANT_A_DEATH_THAT_SNAPSHOTS_NOTHING);
    NETW_CELL(L_TERMINAL, scenario);
    NETW_LAW_BREAKS(L_TERMINAL, run);
}

TEST_CASE(
    "[Networked][Event][Hosted] a row keyed by another route misses the "
    "death and reds terminal"
) {
    const ShellScenario scenario = a_death();
    const ShellRun run(scenario, PLANT_A_ROW_KEYED_BY_ROUTE_ALONE);
    NETW_CELL(L_TERMINAL, scenario);
    NETW_LAW_BREAKS(L_TERMINAL, run);
}

TEST_CASE(
    "[Networked][Event][Hosted] a local stage counted as remote input "
    "reds local"
) {
    const ShellScenario scenario = a_reported_stage();
    const ShellRun run(scenario, PLANT_A_STAGE_COUNTED_AS_REMOTE_INPUT);
    NETW_CELL(L_LOCAL, scenario);
    NETW_LAW_BREAKS(L_LOCAL, run);
}

} // namespace TestNetwEventShellLaws

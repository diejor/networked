#include "support/netw_test.h"

#include <thread>

#include "support/event_ring.h"
#include "support/netw_call_log.h"
#include "support/netw_cells.h"

#include "godot/variant.hpp"
#include "netw/event_plane.hpp"

namespace TestNetwEventPlaneLaws {

using namespace godot;
using netw::EventPlane;
using netw::NetwEvent;
using netw_test::CallLog;
using netw_test::EventRing;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::law_broken;
using netw_test::law_held;

constexpr int64_t WATCHED_ROUTE = 7;
constexpr int64_t OTHER_ROUTE = 8;
constexpr int64_t FLOOD_LENGTH = 200;
constexpr int64_t REJECTED_VERDICT = 12;

enum Plant {
    PLANT_NONE,
    PLANT_UNBOUNDED_RING,
    PLANT_SYNCHRONOUS_LANE,
    PLANT_NO_DEDUPE,
    PLANT_KNOWN_KEYS_ONLY,
    PLANT_NO_SNAPSHOT,
    PLANT_TARGET_ELSEWHERE,
    PLANT_TARGET_EVERYTHING,
};

struct EventScenario {
    String label;
    int64_t event = EventPlane::GATE_SYNC;
    int64_t watched = 0;
    int64_t unwatched = 0;
    int64_t lane = 0;
    bool armed = true;
    bool verdicts = false;
    bool terminal = false;
    bool illegal_installs = false;
};

EventScenario watched_gate() {
    EventScenario scenario;
    scenario.label = "watched-gate";
    scenario.watched = 3;
    return scenario;
}

EventScenario gate_flood() {
    EventScenario scenario;
    scenario.label = "gate-flood";
    scenario.watched = FLOOD_LENGTH;
    return scenario;
}

EventScenario verdict_flood() {
    EventScenario scenario;
    scenario.label = "verdict-flood";
    scenario.event = EventPlane::VERDICT;
    scenario.watched = 40;
    scenario.verdicts = true;
    return scenario;
}

EventScenario lane_raised() {
    EventScenario scenario;
    scenario.label = "lane-raised";
    scenario.watched = 2;
    scenario.lane = 3;
    return scenario;
}

EventScenario entity_death() {
    EventScenario scenario;
    scenario.label = "entity-death";
    scenario.watched = 0;
    scenario.terminal = true;
    return scenario;
}

EventScenario unwatched_route() {
    EventScenario scenario;
    scenario.label = "unwatched-route";
    scenario.watched = 1;
    scenario.unwatched = 4;
    return scenario;
}

EventScenario refused_rows() {
    EventScenario scenario;
    scenario.label = "refused-rows";
    scenario.watched = 1;
    scenario.illegal_installs = true;
    return scenario;
}

EventScenario unarmed_gate() {
    EventScenario scenario;
    scenario.label = "unarmed-gate";
    scenario.watched = 2;
    scenario.unwatched = 2;
    scenario.armed = false;
    return scenario;
}

struct Raised {
    EventPlane::Emission fact;
    bool lane = false;
    bool wanted = false;
};

class EventRun {
    EventPlane plane;
    CallLog sink_log;
    EventScenario declared;
    Plant planted = PLANT_NONE;
    Vector<Raised> raised;
    Vector<int64_t> installs;
    Vector<int64_t> refusals;
    Array shadow_ring;
    Array drained;
    int64_t delivered_before_drain = 0;

    void raise(const EventPlane::Emission &p_fact, bool p_lane, bool p_wanted) {
        Raised row;
        row.fact = p_fact;
        row.lane = p_lane;
        row.wanted = p_wanted;
        raised.push_back(row);

        Ref<NetwEvent> shadow;
        shadow.instantiate();
        shadow->event = p_fact.event;
        shadow->phase = p_fact.phase;
        shadow->tick = p_fact.tick;
        shadow->route = p_fact.route;
        shadow->verdict = p_fact.verdict;
        shadow->detail = p_fact.detail;
        shadow->model = p_fact.model;
        shadow_ring.push_back(shadow);
    }

    EventPlane::Emission gate(int64_t p_route, int64_t p_tick) const {
        EventPlane::Emission fact(declared.event, EventPlane::AFTER, p_tick);
        fact.route = p_route;
        fact.peer = 3;
        if (declared.verdicts) {
            fact.verdict = REJECTED_VERDICT;
        }
        Dictionary detail;
        detail["sender"] = 3;
        detail["channel"] = 1;
        detail["comp"] = 4;
        detail["stage"] = "sync";
        fact.detail = detail;
        return fact;
    }

    void install_rows() {
        if (declared.illegal_installs) {
            PackedInt64Array unknown_event;
            unknown_event.push_back(9);
            refusals.push_back(plane.watch(
                plant_is(PLANT_KNOWN_KEYS_ONLY) ? legal_events()
                                                : unknown_event,
                Dictionary(),
                Dictionary(),
                sink_log.callable("illegal"),
                Dictionary()
            ));
            Dictionary bad_predicate;
            bad_predicate["verdict_is"] = 1;
            refusals.push_back(plane.watch(
                legal_events(),
                Dictionary(),
                plant_is(PLANT_KNOWN_KEYS_ONLY) ? Dictionary() : bad_predicate,
                sink_log.callable("illegal"),
                Dictionary()
            ));
            Dictionary bad_target;
            bad_target["entity"] = "P";
            refusals.push_back(plane.watch(
                legal_events(),
                plant_is(PLANT_KNOWN_KEYS_ONLY) ? Dictionary() : bad_target,
                Dictionary(),
                sink_log.callable("illegal"),
                Dictionary()
            ));
        }

        Dictionary target;
        if (plant_is(PLANT_TARGET_ELSEWHERE)) {
            target["route"] = WATCHED_ROUTE + 1000;
        } else if (!plant_is(PLANT_TARGET_EVERYTHING)) {
            target["route"] = WATCHED_ROUTE;
        }
        Dictionary opts;
        if (plant_is(PLANT_NO_DEDUPE)) {
            opts["dedupe"] = false;
        }
        installs.push_back(plane.watch(
            legal_events(),
            target,
            Dictionary(),
            sink_log.callable("row"),
            opts
        ));
    }

    PackedInt64Array legal_events() const {
        PackedInt64Array events;
        events.push_back(declared.event);
        events.push_back(EventPlane::DESPAWNING);
        events.push_back(EventPlane::DESPAWNED);
        return events;
    }

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    void drive() {
        plane.set_armed(declared.armed);
        install_rows();

        for (int64_t index = 0; index < declared.watched; index++) {
            const EventPlane::Emission fact = gate(WATCHED_ROUTE, index);
            raise(fact, false, true);
            plane.emit(fact);
        }
        for (int64_t index = 0; index < declared.unwatched; index++) {
            const EventPlane::Emission fact = gate(OTHER_ROUTE, index);
            raise(fact, false, false);
            plane.emit(fact);
        }
        if (declared.terminal) {
            drive_terminal();
        }
        if (declared.lane > 0) {
            drive_lane();
        }
        drained = plane.ring(WATCHED_ROUTE);
    }

    void drive_terminal() {
        EventPlane::Emission opening(
            EventPlane::DESPAWNING,
            EventPlane::AFTER,
            50
        );
        opening.route = WATCHED_ROUTE;
        if (!plant_is(PLANT_NO_SNAPSHOT)) {
            opening.entity_id = StringName("Platform");
            Dictionary model;
            model["stage"] = "live";
            model["control"] = 3;
            opening.model = model;
        }
        raise(opening, false, true);
        plane.emit(opening);

        EventPlane::Emission closing(
            EventPlane::DESPAWNED,
            EventPlane::AFTER,
            51
        );
        closing.route = WATCHED_ROUTE;
        raise(closing, false, true);
        plane.emit(closing);
    }

    void drive_lane() {
        Vector<EventPlane::Emission> facts;
        for (int64_t index = 0; index < declared.lane; index++) {
            EventPlane::Emission fact = gate(WATCHED_ROUTE, 100 + index);
            fact.phase = EventPlane::BEFORE;
            facts.push_back(fact);
            raise(fact, true, true);
        }
        if (plant_is(PLANT_SYNCHRONOUS_LANE)) {
            for (const EventPlane::Emission &fact : facts) {
                plane.emit(fact);
            }
            delivered_before_drain = sink_log.count("row");
            plane.drain_staged();
            return;
        }
        EventPlane *target = &plane;
        std::thread lane([target, facts]() {
            for (const EventPlane::Emission &fact : facts) {
                target->stage(fact);
            }
        });
        lane.join();
        delivered_before_drain = sink_log.count("row");
        plane.drain_staged();
    }

public:
    explicit EventRun(
        const EventScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        drive();
    }

    const EventScenario &scenario() const {
        return declared;
    }

    const CallLog &log() const {
        return sink_log;
    }

    const Vector<Raised> &facts() const {
        return raised;
    }

    const Vector<int64_t> &installed() const {
        return installs;
    }

    const Vector<int64_t> &refused() const {
        return refusals;
    }

    int64_t deliveries() const {
        return sink_log.count("row");
    }

    int64_t deliveries_before_drain() const {
        return delivered_before_drain;
    }

    Ref<NetwEvent> delivery(int p_index) const {
        const Array carried = sink_log.args("row", p_index);
        if (carried.is_empty()) {
            return Ref<NetwEvent>();
        }
        return Ref<NetwEvent>(carried[0]);
    }

    EventRing ring() const {
        return EventRing(
            planted == PLANT_UNBOUNDED_RING ? shadow_ring : drained
        );
    }

    int64_t recorded() const {
        int64_t total = 0;
        for (const Raised &row : raised) {
            if (row.fact.route != WATCHED_ROUTE) {
                continue;
            }
            total += 1;
            if (EventPlane::is_terminal(row.fact.event)) {
                total = 0;
            }
        }
        return total;
    }
};

typedef LawRowFor<EventRun> EventLaw;

LawVerdict law_delivered(const EventRun &p_run) {
    int64_t expected = 0;
    for (const Raised &row : p_run.facts()) {
        expected += row.wanted ? 1 : 0;
    }
    if (p_run.scenario().verdicts) {
        expected = expected > 0 ? 1 : 0;
    }
    if (p_run.deliveries() != expected) {
        return law_broken(
            "%d of %d wanted facts reached the row",
            int(p_run.deliveries()),
            int(expected)
        );
    }
    for (int index = 0; index < int(p_run.deliveries()); index++) {
        const Ref<NetwEvent> record = p_run.delivery(index);
        if (record.is_null()) {
            return law_broken("delivery %d carried no record", index);
        }
        if (!EventPlane::is_event(record->event)) {
            return law_broken(
                "delivery %d carried event %d, outside the taxonomy",
                index,
                int(record->event)
            );
        }
    }
    return law_held();
}

LawVerdict law_untouched(const EventRun &p_run) {
    if (p_run.log().count("illegal") != 0) {
        return law_broken("a refused row's sink ran");
    }
    for (const Raised &row : p_run.facts()) {
        if (row.wanted) {
            continue;
        }
        for (int index = 0; index < int(p_run.deliveries()); index++) {
            const Ref<NetwEvent> record = p_run.delivery(index);
            if (record.is_null()) {
                continue;
            }
            if (record->route == row.fact.route
                && record->tick == row.fact.tick) {
                return law_broken(
                    "route %d tick %d was delivered and nobody asked",
                    int(row.fact.route),
                    int(row.fact.tick)
                );
            }
        }
    }
    return law_held();
}

LawVerdict law_bounded(const EventRun &p_run) {
    const int64_t recorded = p_run.recorded();
    const int64_t capacity = int64_t(EventPlane::RING_CAPACITY);
    const int64_t expected = recorded < capacity ? recorded : capacity;
    const EventRing ring = p_run.ring();
    if (int64_t(ring.size()) != expected) {
        return law_broken(
            "the ring holds %d rows of %d recorded, bounded at %d",
            ring.size(),
            int(recorded),
            int(capacity)
        );
    }
    for (int index = 1; index < ring.size(); index++) {
        if (ring.tick_at(index - 1) > ring.tick_at(index)) {
            return law_broken(
                "row %d ticks %d, after %d",
                index,
                int(ring.tick_at(index)),
                int(ring.tick_at(index - 1))
            );
        }
    }
    return law_held();
}

LawVerdict law_dedupe(const EventRun &p_run) {
    if (!p_run.scenario().verdicts) {
        return law_held();
    }
    int64_t raised_verdicts = 0;
    for (const Raised &row : p_run.facts()) {
        raised_verdicts += row.fact.verdict == REJECTED_VERDICT ? 1 : 0;
    }
    if (raised_verdicts < 2) {
        return law_broken(
            "the scenario raised %d verdicts, too few to dedupe",
            int(raised_verdicts)
        );
    }
    if (p_run.deliveries() != 1) {
        return law_broken(
            "%d of %d verdicts reached the row",
            int(p_run.deliveries()),
            int(raised_verdicts)
        );
    }
    return law_held();
}

LawVerdict law_closed(const EventRun &p_run) {
    if (!p_run.scenario().illegal_installs) {
        return law_held();
    }
    if (p_run.refused().is_empty()) {
        return law_broken("the scenario declared no illegal install");
    }
    for (int index = 0; index < p_run.refused().size(); index++) {
        if (p_run.refused()[index] != -1) {
            return law_broken(
                "illegal install %d opened row %d",
                index,
                int(p_run.refused()[index])
            );
        }
    }
    for (int index = 0; index < p_run.installed().size(); index++) {
        if (p_run.installed()[index] <= 0) {
            return law_broken("a legal install was refused");
        }
    }
    return law_held();
}

LawVerdict law_drained(const EventRun &p_run) {
    if (p_run.scenario().lane == 0) {
        return law_held();
    }
    if (p_run.deliveries_before_drain() != p_run.scenario().watched) {
        return law_broken(
            "%d deliveries before the drain, %d raised on the main thread",
            int(p_run.deliveries_before_drain()),
            int(p_run.scenario().watched)
        );
    }
    for (int64_t index = 0; index < p_run.scenario().lane; index++) {
        const Ref<NetwEvent> record
            = p_run.delivery(int(p_run.scenario().watched + index));
        if (record.is_null()) {
            return law_broken("lane fact %d was never delivered", int(index));
        }
        if (record->phase != EventPlane::AFTER) {
            return law_broken(
                "lane fact %d arrived in phase %d",
                int(index),
                int(record->phase)
            );
        }
        if (record->tick != 100 + index) {
            return law_broken(
                "lane fact %d arrived out of order, tick %d",
                int(index),
                int(record->tick)
            );
        }
    }
    return law_held();
}

LawVerdict law_terminal(const EventRun &p_run) {
    if (!p_run.scenario().terminal) {
        return law_held();
    }
    Ref<NetwEvent> closing;
    for (int index = 0; index < int(p_run.deliveries()); index++) {
        const Ref<NetwEvent> record = p_run.delivery(index);
        if (record.is_valid() && record->event == EventPlane::DESPAWNED) {
            closing = record;
        }
    }
    if (closing.is_null()) {
        return law_broken("the terminal event was never delivered");
    }
    if (closing->model.is_empty()) {
        return law_broken("the terminal event carried no model");
    }
    if (String(closing->model.get("stage", String())) != String("live")) {
        return law_broken("the terminal model is not the one snapshotted");
    }
    if (closing->entity_id != StringName("Platform")) {
        return law_broken(
            "the terminal event names %s, not the subject that died",
            String(closing->entity_id).utf8().get_data()
        );
    }
    return law_held();
}

const EventLaw L_DELIVERED = {
    "delivered",
    "a fact a row names reaches that row's sink, once, as a record",
    &law_delivered,
};

const EventLaw L_UNTOUCHED = {
    "untouched",
    "a fact no row names touches nothing",
    &law_untouched,
};

const EventLaw L_BOUNDED = {
    "bounded",
    "a ring holds its newest rows to a bound, oldest first",
    &law_bounded,
};

const EventLaw L_DEDUPE = {
    "dedupe",
    "a verdict flood from one route delivers once",
    &law_dedupe,
};

const EventLaw L_CLOSED = {
    "closed",
    "an install outside the closed vocabulary is refused",
    &law_closed,
};

const EventLaw L_DRAINED = {
    "drained",
    "a fact raised off the main thread arrives at the drain, after, in order",
    &law_drained,
};

const EventLaw L_TERMINAL = {
    "terminal",
    "a terminal event carries the model its subject last had",
    &law_terminal,
};

const EventLaw LAWS[] = {
    L_DELIVERED, L_UNTOUCHED, L_BOUNDED, L_DEDUPE,
    L_CLOSED,    L_DRAINED,   L_TERMINAL,
};

constexpr int LAWS_SIZE = int(sizeof(LAWS) / sizeof(LAWS[0]));

TEST_CASE("[Networked][Event][Hosted] the event plane's laws hold") {
    const EventScenario CORPUS[] = {
        watched_gate(),  gate_flood(),      verdict_flood(), lane_raised(),
        entity_death(),  unwatched_route(), refused_rows(),  unarmed_gate(),
    };
    for (const EventScenario &scenario : CORPUS) {
        const EventRun run(scenario);
        REQUIRE(run.facts().size() > 0);
        for (int law = 0; law < LAWS_SIZE; law++) {
            NETW_CELL(LAWS[law], scenario);
            NETW_LAW_HOLDS(LAWS[law], run);
        }
    }
}

TEST_CASE("[Networked][Event][Hosted] a ring that never evicts reds bounded") {
    const EventScenario scenario = gate_flood();
    const EventRun run(scenario, PLANT_UNBOUNDED_RING);
    NETW_CELL(L_BOUNDED, scenario);
    NETW_LAW_BREAKS(L_BOUNDED, run);
}

TEST_CASE("[Networked][Event][Hosted] a lane fact delivered where it was "
          "raised reds drained") {
    const EventScenario scenario = lane_raised();
    const EventRun run(scenario, PLANT_SYNCHRONOUS_LANE);
    NETW_CELL(L_DRAINED, scenario);
    NETW_LAW_BREAKS(L_DRAINED, run);
}

TEST_CASE("[Networked][Event][Hosted] a verdict row without dedupe reds "
          "dedupe") {
    const EventScenario scenario = verdict_flood();
    const EventRun run(scenario, PLANT_NO_DEDUPE);
    NETW_CELL(L_DEDUPE, scenario);
    NETW_LAW_BREAKS(L_DEDUPE, run);
}

TEST_CASE("[Networked][Event][Hosted] an install inside the vocabulary reds "
          "closed") {
    const EventScenario scenario = refused_rows();
    const EventRun run(scenario, PLANT_KNOWN_KEYS_ONLY);
    NETW_CELL(L_CLOSED, scenario);
    NETW_LAW_BREAKS(L_CLOSED, run);
}

TEST_CASE("[Networked][Event][Hosted] a death that snapshots nothing reds "
          "terminal") {
    const EventScenario scenario = entity_death();
    const EventRun run(scenario, PLANT_NO_SNAPSHOT);
    NETW_CELL(L_TERMINAL, scenario);
    NETW_LAW_BREAKS(L_TERMINAL, run);
}

TEST_CASE("[Networked][Event][Hosted] a row targeting another route reds "
          "delivered") {
    const EventScenario scenario = watched_gate();
    const EventRun run(scenario, PLANT_TARGET_ELSEWHERE);
    NETW_CELL(L_DELIVERED, scenario);
    NETW_LAW_BREAKS(L_DELIVERED, run);
}

TEST_CASE("[Networked][Event][Hosted] a row targeting every route reds "
          "untouched") {
    const EventScenario scenario = unwatched_route();
    const EventRun run(scenario, PLANT_TARGET_EVERYTHING);
    NETW_CELL(L_UNTOUCHED, scenario);
    NETW_LAW_BREAKS(L_UNTOUCHED, run);
}

} // namespace TestNetwEventPlaneLaws

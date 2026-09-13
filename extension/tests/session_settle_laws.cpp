#include "support/netw_test.h"

#include "support/netw_call_log.h"
#include "support/netw_cells.h"

#include <memory>

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/settle_queue.hpp"

namespace TestNetwSessionSettleLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::SettleQueue;
using netw_test::CallLog;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

enum Plant {
    PLANT_NONE,
    PLANT_A_KEY_THAT_COALESCES_NOTHING,
    PLANT_A_CANCEL_THAT_MISSES_ITS_KEY,
    PLANT_A_SECOND_ASK_BEFORE_THE_SECOND_DRAIN,
    PLANT_A_CYCLE_THAT_STOPS_ITSELF,
};

class SessionRescheduler final : public CallableCustom {
    NetwMultiplayer *session;
    Callable mark;
    std::shared_ptr<Callable> again;
    StringName key;
    std::shared_ptr<int> passes;
    int stops_after;
    ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    SessionRescheduler(
        NetwMultiplayer *p_session,
        const Callable &p_mark,
        const std::shared_ptr<Callable> &p_again,
        const StringName &p_key,
        const std::shared_ptr<int> &p_passes,
        int p_stops_after
    )
        : session(p_session), mark(p_mark), again(p_again), key(p_key),
          passes(p_passes), stops_after(p_stops_after),
          anchor(netw::gd::instance_id(p_session)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("SessionRescheduler");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &SessionRescheduler::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &SessionRescheduler::before;
    }

    ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **,
        int,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        (*passes)++;
        if (mark.is_valid()) {
            mark.call();
        }
        const bool keeps_going = stops_after <= 0 || *passes < stops_after;
        if (keeps_going && again && !again->is_null()) {
            session->session_defer(*again, key);
        }
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

struct SettleScenario {
    String label;
    bool coalesces = false;
    bool cancels = false;
    bool cycles = false;
};

SettleScenario plain_effects() {
    SettleScenario scenario;
    scenario.label = "plain-effects";
    return scenario;
}

SettleScenario a_coalescing_key() {
    SettleScenario scenario;
    scenario.label = "a-coalescing-key";
    scenario.coalesces = true;
    return scenario;
}

SettleScenario a_withdrawn_key() {
    SettleScenario scenario;
    scenario.label = "a-withdrawn-key";
    scenario.cancels = true;
    return scenario;
}

SettleScenario a_key_asked_twice_then_withdrawn() {
    SettleScenario scenario;
    scenario.label = "asked-twice-then-withdrawn";
    scenario.coalesces = true;
    scenario.cancels = true;
    return scenario;
}

SettleScenario an_effect_that_reschedules_itself() {
    SettleScenario scenario;
    scenario.label = "an-effect-that-reschedules-itself";
    scenario.cycles = true;
    return scenario;
}

class SettleRun {
    SettleScenario declared;
    Plant planted = PLANT_NONE;
    CallLog log;
    Vector<StringName> before_the_drain;
    Vector<StringName> after_the_drain;
    Vector<StringName> after_a_second_drain;
    int pending_after = -1;
    int cycle_passes = 0;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

public:
    SettleRun(const SettleScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        Ref<NetwMultiplayer> session;
        session.instantiate();

        std::shared_ptr<int> passes = std::make_shared<int>(0);
        if (declared.cycles) {
            std::shared_ptr<Callable> again = std::make_shared<Callable>();
            const int stops = plant_is(PLANT_A_CYCLE_THAT_STOPS_ITSELF) ? 2 : 0;
            const Callable cycle = Callable(memnew(SessionRescheduler(
                session.ptr(),
                log.callable("spun"),
                again,
                StringName("cycle"),
                passes,
                stops
            )));
            *again = cycle;
            session->session_defer(cycle, StringName("cycle"));
        } else {
            session->session_defer(log.callable("first"), StringName());
            if (declared.coalesces) {
                session->session_defer(log.callable("keyed"), "k");
                session->session_defer(log.callable("second"), StringName());
                session->session_defer(
                    log.callable("keyed-again"),
                    plant_is(PLANT_A_KEY_THAT_COALESCES_NOTHING) ? "other" : "k"
                );
            } else {
                session->session_defer(log.callable("second"), StringName());
            }
            if (declared.cancels) {
                session->session_defer(log.callable("withdrawn"), "gone");
                session->session_cancel_deferred(
                    plant_is(PLANT_A_CANCEL_THAT_MISSES_ITS_KEY) ? "not-a-key"
                                                                 : "gone"
                );
            }
        }

        before_the_drain = log.order();
        session->session_flush_deferred();
        after_the_drain = log.order();
        pending_after = session->settle_pending();
        cycle_passes = *passes;

        if (plant_is(PLANT_A_SECOND_ASK_BEFORE_THE_SECOND_DRAIN)) {
            session->session_defer(log.callable("first"), StringName());
        }
        session->session_flush_deferred();
        after_a_second_drain = log.order();
    }

    const SettleScenario &scenario() const {
        return declared;
    }

    const Vector<StringName> &asked() const {
        return before_the_drain;
    }

    const Vector<StringName> &ran() const {
        return after_the_drain;
    }

    const Vector<StringName> &ran_again() const {
        return after_a_second_drain;
    }

    int pending() const {
        return pending_after;
    }

    int passes() const {
        return cycle_passes;
    }
};

typedef LawRowFor<SettleRun> SettleLaw;

int index_of(const Vector<StringName> &p_order, const StringName &p_tag) {
    for (int index = 0; index < p_order.size(); index++) {
        if (p_order[index] == p_tag) {
            return index;
        }
    }
    return -1;
}

int count_of(const Vector<StringName> &p_order, const StringName &p_tag) {
    int total = 0;
    for (int index = 0; index < p_order.size(); index++) {
        total += p_order[index] == p_tag ? 1 : 0;
    }
    return total;
}

LawVerdict law_deferred(const SettleRun &p_run) {
    if (!p_run.asked().is_empty()) {
        return law_broken(
            "%d effects ran before anything drained",
            p_run.asked().size()
        );
    }
    return law_held();
}

LawVerdict law_ordered(const SettleRun &p_run) {
    if (p_run.scenario().cycles) {
        return law_held();
    }
    const Vector<StringName> &ran = p_run.ran();
    if (index_of(ran, "first") != 0) {
        return law_broken(
            "the first effect asked for ran at %d",
            index_of(ran, "first")
        );
    }
    if (!p_run.scenario().coalesces) {
        return law_held();
    }
    if (count_of(ran, "keyed") != 0) {
        return law_broken("a coalesced key ran its withdrawn body too");
    }
    if (index_of(ran, "keyed-again") < index_of(ran, "second")) {
        return law_broken(
            "the key ran at %d, ahead of the unkeyed effect at %d",
            index_of(ran, "keyed-again"),
            index_of(ran, "second")
        );
    }
    return law_held();
}

LawVerdict law_withdrawn(const SettleRun &p_run) {
    if (!p_run.scenario().cancels) {
        return law_held();
    }
    if (count_of(p_run.ran(), "withdrawn") != 0) {
        return law_broken("a cancelled key ran anyway");
    }
    return law_held();
}

LawVerdict law_once(const SettleRun &p_run) {
    if (p_run.ran().size() != p_run.ran_again().size()) {
        return law_broken(
            "a second drain over no new work ran %d more effects",
            p_run.ran_again().size() - p_run.ran().size()
        );
    }
    if (p_run.pending() != 0) {
        return law_broken("the drain left %d effects pending", p_run.pending());
    }
    return law_held();
}

LawVerdict law_bounded(const SettleRun &p_run) {
    if (!p_run.scenario().cycles) {
        return law_held();
    }
    if (p_run.passes() != NetwMultiplayer::settle_max_passes()) {
        return law_broken(
            "a cycle ran %d passes against the published bound of %d",
            p_run.passes(),
            NetwMultiplayer::settle_max_passes()
        );
    }
    if (NetwMultiplayer::settle_max_passes() != int(SettleQueue::MAX_PASSES)) {
        return law_broken(
            "the session publishes a bound of %d and drains at %d",
            NetwMultiplayer::settle_max_passes(),
            int(SettleQueue::MAX_PASSES)
        );
    }
    return law_held();
}

const SettleLaw L_DEFERRED = {
    "deferred",
    "a scheduled effect waits for a drain and runs at no other moment",
    &law_deferred,
};

const SettleLaw L_ORDERED = {
    "ordered",
    "effects run in the order asked, and a key runs at its latest position",
    &law_ordered,
};

const SettleLaw L_WITHDRAWN = {
    "withdrawn",
    "a cancelled key leaves nothing for the drain to run",
    &law_withdrawn,
};

const SettleLaw L_ONCE = {
    "once",
    "a drained session repeats nothing and holds nothing back",
    &law_once,
};

const SettleLaw L_BOUNDED = {
    "bounded",
    "a self-rescheduling effect stops at the bound the session publishes",
    &law_bounded,
};

const SettleLaw LAWS[]
    = {L_DEFERRED, L_ORDERED, L_WITHDRAWN, L_ONCE, L_BOUNDED};

TEST_CASE("[Networked][Settle][Hosted] the session settle laws hold") {
    const SettleScenario CORPUS[] = {
        plain_effects(),
        a_coalescing_key(),
        a_withdrawn_key(),
        a_key_asked_twice_then_withdrawn(),
        an_effect_that_reschedules_itself(),
    };
    for (const SettleScenario &scenario : CORPUS) {
        const SettleRun run(scenario);
        for (const SettleLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Settle][Hosted] a second key that coalesces with "
    "nothing reds ordered"
) {
    const SettleScenario scenario = a_coalescing_key();
    const SettleRun run(scenario, PLANT_A_KEY_THAT_COALESCES_NOTHING);
    NETW_CELL(L_ORDERED, scenario);
    NETW_LAW_BREAKS(L_ORDERED, run);
}

TEST_CASE(
    "[Networked][Settle][Hosted] a cancel that names another key reds "
    "withdrawn"
) {
    const SettleScenario scenario = a_withdrawn_key();
    const SettleRun run(scenario, PLANT_A_CANCEL_THAT_MISSES_ITS_KEY);
    NETW_CELL(L_WITHDRAWN, scenario);
    NETW_LAW_BREAKS(L_WITHDRAWN, run);
}

TEST_CASE(
    "[Networked][Settle][Hosted] work asked for between two drains reds "
    "once"
) {
    const SettleScenario scenario = plain_effects();
    const SettleRun run(scenario, PLANT_A_SECOND_ASK_BEFORE_THE_SECOND_DRAIN);
    NETW_CELL(L_ONCE, scenario);
    NETW_LAW_BREAKS(L_ONCE, run);
}

TEST_CASE(
    "[Networked][Settle][Hosted] a cycle that gives up early reds "
    "bounded"
) {
    const SettleScenario scenario = an_effect_that_reschedules_itself();
    const SettleRun run(scenario, PLANT_A_CYCLE_THAT_STOPS_ITSELF);
    NETW_CELL(L_BOUNDED, scenario);
    NETW_LAW_BREAKS(L_BOUNDED, run);
}

} // namespace TestNetwSessionSettleLaws

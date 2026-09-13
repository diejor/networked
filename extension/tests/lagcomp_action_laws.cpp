#include "support/netw_test.h"

#include "support/loopback_rig.h"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/action.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/timeline.hpp"

namespace TestNetwLagCompActionLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwTimeline;

constexpr int TICK_ALIGNED = 0;
constexpr int STATE_READY = 1;
constexpr int IMMEDIATE = 2;

EntityDecl action_player(netw::MissingInput p_policy) {
    return EntityDecl()
        .named("P")
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK)
        .missing_input(p_policy)
        .mounted();
}

struct ActionRig {
    LoopbackRig rig;
    Carrier *owner = nullptr;
    Carrier *authority = nullptr;

    explicit ActionRig(netw::MissingInput p_policy = netw::MissingInput::STALL)
        : rig(1) {
        rig.mount();
        WorldDecl world;
        world.clocked(30, 3).lag_compensated().player(
            action_player(p_policy),
            0
        );
        rig.declare_world(world);
        owner = Object::cast_to<Carrier>(
            rig.node_of(rig.entity_of(StringName("P"), 0), 0)
        );
        authority = Object::cast_to<Carrier>(rig.node_of(rig.entity_of("P")));
        REQUIRE(owner != nullptr);
        REQUIRE(authority != nullptr);
        owner->arm_action(rig.shell_at(0));
        authority->arm_action(rig.shell());
    }

    int64_t owner_tick() const {
        return rig.clock_of(rig.client(0)).get_tick();
    }

    int64_t authority_tick() const {
        return rig.clock_of(rig.server()).get_tick();
    }

    void uplink_delay(int p_polls) {
        rig.conditions(
            -1,
            netw::LocalLinkConditions::polls(p_polls),
            rig.peer_id(0)
        );
    }

    void gate_deadline(int p_ticks) {
        rig.server()->set_input_gate_deadline_ticks(p_ticks);
    }

    int64_t future_limit() const {
        return int64_t(rig.server()->get_max_future_action_ticks());
    }

    int64_t gate_fallbacks() const {
        const Dictionary metrics = rig.server()->lagcomp_metrics();
        return int64_t(metrics.get(StringName("gate_fallbacks"), -1));
    }

    Ref<NetwTimeline> authority_history() const {
        netw::NetwMultiplayer *api = rig.server();
        return api->lagcomp_timeline_of(rig.entity_of("P"));
    }

    netw::NetwPredictionHandle *authority_handle() const {
        return rig.prediction_handle(StringName("P"));
    }

    void step_until_requested(int p_budget) {
        for (int at = 0; at < p_budget && authority->requests() == 0; ++at) {
            rig.step_ticks(1);
        }
    }

    void step_until_denied(int p_budget) {
        for (int at = 0; at < p_budget && owner->denials() == 0; ++at) {
            rig.step_ticks(1);
        }
    }
};

Dictionary rightward() {
    Dictionary out;
    out[StringName("motion")] = Vector2(1.0, 0.0);
    return out;
}

TEST_CASE(
    "[Networked][LagComp][Action] AC1 a request takes its optimistic "
    "effect on the spot and gives it back when authority denies it, so the "
    "owner never keeps a ghost the server refused"
) {
    ActionRig fixture;

    fixture.owner->fire(fixture.owner_tick(), IMMEDIATE);
    NETW_CHECK_EQ(fixture.owner->ghosts(), 1);
    REQUIRE(fixture.owner->ghost() != nullptr);

    fixture.rig.step_ticks(30);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.owner->denials(), 1);
    CHECK(fixture.owner->ghost()->is_queued_for_deletion());
}

TEST_CASE(
    "[Networked][LagComp][Action] AC2 a tick-aligned request waits "
    "for its view tick to arrive and then runs AT it, so authority evaluates "
    "the world the requester meant rather than the one it found"
) {
    ActionRig fixture;
    const int64_t fire_tick = fixture.authority_tick() + 3;

    fixture.owner->fire(fire_tick, TICK_ALIGNED);
    NETW_CHECK_EQ(fixture.owner->ghosts(), 1);
    fixture.rig.step_ticks(3);

    NETW_CHECK_EQ(fixture.authority->requests(), 0);
    fixture.rig.step_ticks(1);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.authority->viewed_tick(), fire_tick);
    NETW_CHECK_EQ(fixture.authority->requested_tick(), fire_tick);
    NETW_CHECK_EQ(fixture.authority->executed_tick(), fire_tick);
}

TEST_CASE(
    "[Networked][LagComp][Action] AC3 a view tick further ahead than "
    "the session admits is denied without ever reaching the authority "
    "method, because a request the server would have to wait arbitrarily "
    "long for is not a request it can hold"
) {
    ActionRig fixture;

    fixture.owner->fire(
        fixture.authority_tick() + fixture.future_limit() + 4,
        TICK_ALIGNED
    );
    fixture.step_until_denied(30);

    NETW_CHECK_EQ(fixture.authority->requests(), 0);
    NETW_CHECK_EQ(fixture.owner->denials(), 1);
}

TEST_CASE(
    "[Networked][LagComp][Action] AC4 an immediate request runs on "
    "arrival, so it keeps the tick the requester asked about while executing "
    "at whatever tick the server had reached"
) {
    ActionRig fixture;
    const int64_t requested = fixture.authority_tick() + 3;

    fixture.owner->fire(requested, IMMEDIATE);
    fixture.step_until_requested(30);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.authority->requested_tick(), requested);
    NETW_CHECK_LE(
        fixture.authority->viewed_tick(),
        fixture.authority->executed_tick()
    );

    SUBCASE("and a tick further out still runs before it, not at it") {
        ActionRig later;
        const int64_t ahead = later.authority_tick() + 6;
        later.owner->fire(ahead, IMMEDIATE);
        later.step_until_requested(30);

        NETW_CHECK_EQ(later.authority->requests(), 1);
        NETW_CHECK_EQ(later.authority->requested_tick(), ahead);
        NETW_CHECK_LT(later.authority->executed_tick(), ahead);
    }
}

TEST_CASE(
    "[Networked][LagComp][Action] AC5 a state-ready request waits "
    "until authority has RECORDED state at the view tick, so a starved "
    "uplink holds the gate rather than resolving against history that has "
    "not caught up"
) {
    ActionRig fixture;
    fixture.gate_deadline(24);
    fixture.uplink_delay(8);
    const int64_t fire_tick = fixture.authority_tick() + 3;
    const Ref<NetwTimeline> history = fixture.authority_history();
    REQUIRE(history.is_valid());

    fixture.owner->fire(fire_tick, STATE_READY);
    fixture.rig.step_ticks(5);

    NETW_CHECK_EQ(fixture.authority->requests(), 0);
    fixture.step_until_requested(20);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.authority->viewed_tick(), fire_tick);
    CHECK_FALSE(history->state_at(fire_tick).is_empty());
    NETW_CHECK_EQ(fixture.gate_fallbacks(), int64_t(0));
}

TEST_CASE(
    "[Networked][LagComp][Action] AC6 a gate that waited out its "
    "deadline resolves best-effort and says so, because a state slot the "
    "input never arrived for would otherwise hold the request forever"
) {
    ActionRig fixture;
    fixture.gate_deadline(2);
    const int64_t fire_tick = fixture.authority_tick() + 3;
    const CallLog log;
    fixture.rig.server()->connect(
        "lagcomp_action_gate_fallback",
        log.callable(StringName("fell_back"))
    );

    fixture.owner->fire(fire_tick, STATE_READY);
    fixture.rig.step_ticks(6);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.authority->viewed_tick(), fire_tick);
    NETW_CHECK_EQ(log.count(StringName("fell_back")), 1);
    const Array said = log.args(StringName("fell_back"));
    REQUIRE(said.size() == 2);
    NETW_CHECK_EQ(int64_t(said[1]), fire_tick);
    NETW_CHECK_EQ(fixture.gate_fallbacks(), int64_t(1));
}

TEST_CASE(
    "[Networked][LagComp][Action] AC7 a consume that STEPPED OVER a "
    "lost input still records state at the view tick, so the gate opens on "
    "the recording rather than on the input, and no fallback is charged"
) {
    ActionRig fixture(netw::MissingInput::STALL);
    fixture.gate_deadline(12);
    fixture.uplink_delay(60);

    const Ref<NetwTimeline> history = fixture.authority_history();
    REQUIRE(history.is_valid());
    netw::NetwPredictionHandle *handle = fixture.authority_handle();
    REQUIRE(handle != nullptr);

    fixture.rig.step_ticks(2);
    const int64_t first_input_tick = fixture.authority_tick();
    const int64_t view_tick = first_input_tick + 2;
    const StringName key = fixture.rig.server()->lagcomp_effect_key(
        fixture.rig.entity_of("P"),
        view_tick,
        0
    );

    handle->record_server_input(first_input_tick, rightward());
    history->record_input(view_tick, rightward());
    fixture.rig.server()->action_send_request(
        fixture.rig.branch(-1)->get_path_to(fixture.authority),
        StringName("_server_action"),
        view_tick,
        Variant(),
        key,
        STATE_READY
    );
    fixture.step_until_requested(12);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.authority->viewed_tick(), view_tick);
    CHECK_FALSE(history->state_at(view_tick).is_empty());
    Object *stats = handle->get(StringName("stats"));
    REQUIRE(stats != nullptr);
    NETW_CHECK_GT(int64_t(stats->get(StringName("missing"))), int64_t(0));
    NETW_CHECK_EQ(fixture.gate_fallbacks(), int64_t(0));
}

TEST_CASE(
    "[Networked][LagComp][Action] AC8 both peers spell the same act "
    "the same way, which is what lets the requester adopt its own optimistic "
    "effect off the authoritative result rather than being told separately"
) {
    ActionRig fixture;
    const StringName owner_key = fixture.rig.client(0)->lagcomp_effect_key(
        fixture.rig.entity_of(StringName("P"), 0),
        44,
        0
    );
    const StringName authority_key = fixture.rig.server()->lagcomp_effect_key(
        fixture.rig.entity_of("P"),
        44,
        0
    );
    CHECK(owner_key == authority_key);

    const int64_t tick = fixture.owner_tick();
    const StringName key = fixture.rig.client(0)->lagcomp_effect_key(
        fixture.rig.entity_of(StringName("P"), 0),
        tick,
        0
    );

    fixture.owner->fire(tick, IMMEDIATE);
    REQUIRE(fixture.owner->ghost() != nullptr);
    fixture.rig.client(0)->lagcomp_effect_adopt(key);

    NETW_CHECK_EQ(fixture.owner->confirmations(), 1);
    CHECK(fixture.owner->ghost()->is_queued_for_deletion());
}

TEST_CASE(
    "[Networked][LagComp][Action] AC9 a declared confirm replaces "
    "the default hand-back, so an owner that means to KEEP what it built "
    "keeps it and is still told the act was confirmed"
) {
    ActionRig fixture;
    const CallLog log;
    const Ref<netw::NetwAction> action = fixture.owner->armed_action();
    REQUIRE(action.is_valid());
    action->set("confirm", log.callable(StringName("confirmed")));

    const int64_t tick = fixture.owner_tick();
    const StringName key = fixture.rig.client(0)->lagcomp_effect_key(
        fixture.rig.entity_of(StringName("P"), 0),
        tick,
        0
    );

    fixture.owner->fire(tick, IMMEDIATE);
    REQUIRE(fixture.owner->ghost() != nullptr);
    fixture.rig.client(0)->lagcomp_effect_adopt(key);

    NETW_CHECK_EQ(fixture.owner->confirmations(), 1);
    NETW_CHECK_EQ(log.count(StringName("confirmed")), 1);
    REQUIRE(fixture.owner->ghost() != nullptr);
    CHECK_FALSE(fixture.owner->ghost()->is_queued_for_deletion());
}

TEST_CASE(
    "[Networked][LagComp] every authority on an entity holds its own action "
    "slot, so two actions armed at one view tick never share an effect key"
) {
    ActionRig fixture;
    netw::NetwMultiplayer *api = fixture.rig.shell();
    Node *node = fixture.authority;

    const Ref<netw::NetwAction> armed
        = api->lagcomp_action(Callable(node, StringName("_server_action")));
    const Ref<netw::NetwAction> rearmed
        = api->lagcomp_action(Callable(node, StringName("_server_action")));
    const Ref<netw::NetwAction> other
        = api->lagcomp_action(Callable(node, StringName("_predict_ghost")));
    REQUIRE(armed.is_valid());
    REQUIRE(rearmed.is_valid());
    REQUIRE(other.is_valid());

    const int64_t slot = armed->action_slot();
    NETW_CHECK_EQ(rearmed->action_slot(), slot);
    NETW_CHECK_EQ(int(bool(other->action_slot() == slot)), 0);

    const Ref<netw::NetwAction> detached
        = api->lagcomp_action(Callable(api, StringName("poll")));
    REQUIRE(detached.is_valid());
    NETW_CHECK_EQ(detached->action_slot(), int64_t(0));
}

} // namespace TestNetwLagCompActionLaws

#endif

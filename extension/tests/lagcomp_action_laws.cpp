#include "support/link.h"
#include "support/netw_test.h"

#include "support/fault_log.h"
#include "support/loopback_rig.h"
#include "support/netw_call_log.h"
#include "support/predict_rows.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/utility.hpp"
#include "godot/variant.hpp"
#include "netw/api/action.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/context.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/prediction.hpp"
#include "netw/lagcomp_core.hpp"
#include "netw/predict/engine.hpp"
#include "netw/session/frames.hpp"
#include "netw/session_decl.hpp"

namespace TestNetwLagCompActionLaws {

using namespace godot;
using namespace netw_test;

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

    explicit ActionRig(
        netw::MissingInput p_policy = netw::MissingInput::STALL,
        const StringName &p_authority = StringName("_server_action")
    )
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
        owner->arm_action(rig.shell_at(0), p_authority);
        authority->arm_action(rig.shell(), p_authority);
    }

    int64_t owner_tick() const {
        return rig.clock_of(rig.client(0)).get_tick();
    }

    int64_t authority_tick() const {
        return rig.clock_of(rig.server()).get_tick();
    }

    void uplink_delay(int p_polls) {
        rig.conditions(-1, netw_test::polls(p_polls), rig.peer_id(0));
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

    int64_t authority_rewind_slot() const {
        netw::NetwMultiplayer *api = rig.server();
        return api->get_lagcomp_core()->timeline_slot_of(
            api->entity_get_view(rig.entity_of("P"))
        );
    }

    bool authority_recorded(int64_t p_tick) const {
        return rig.server()->get_lagcomp_core()->timeline_recorded_at(
            authority_rewind_slot(),
            p_tick
        );
    }

    netw::NetwPrediction *authority_handle() const {
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
    "[Networked][LagComp][Action] AC1b every argument a request carries "
    "reaches the authority method in the order it was written, after the "
    "context, so a game passes a call's arguments rather than packing them"
) {
    ActionRig fixture(
        netw::MissingInput::STALL,
        StringName("_server_action_carrying")
    );

    fixture.owner->fire(
        fixture.owner_tick(),
        IMMEDIATE,
        netw::gd::array_of(Vector2(3.0, -4.0), 7)
    );
    fixture.rig.step_ticks(30);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    const Array carried = fixture.authority->carried_arguments();
    REQUIRE(carried.size() == 2);
    const bool first_arrived = Vector2(carried[0]) == Vector2(3.0, -4.0);
    CHECK(first_arrived);
    NETW_CHECK_EQ(int(carried[1]), 7);
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
    REQUIRE(fixture.authority_rewind_slot() >= 0);

    fixture.owner->fire(fire_tick, STATE_READY);
    fixture.rig.step_ticks(5);

    NETW_CHECK_EQ(fixture.authority->requests(), 0);
    fixture.step_until_requested(20);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.authority->viewed_tick(), fire_tick);
    CHECK(fixture.authority_recorded(fire_tick));
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
    fixture.rig.server()->observe(
        netw::NetwMultiplayer::LAGCOMP_ACTION_GATE_FALLBACK,
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

    netw::NetwPrediction *handle = fixture.authority_handle();
    REQUIRE(handle != nullptr);
    netw::NetwPredictionEngine *const pool = fixture.rig.prediction_pool();
    const int64_t slot = fixture.rig.prediction_slot(StringName("P"));
    REQUIRE(pool->input_lane_open(slot));

    fixture.rig.step_ticks(2);
    const int64_t first_input_tick = fixture.authority_tick();
    const int64_t view_tick = first_input_tick + 2;
    const StringName key = fixture.rig.server()->lagcomp_effect_key(
        fixture.rig.entity_of("P"),
        view_tick,
        0
    );

    const netw::predict::StateRow right
        = netw_test::input_row(pool, slot, rightward());
    pool->record_authored_command(
        slot,
        first_input_tick,
        first_input_tick,
        true,
        right
    );
    if (pool->next_input_tick_of(slot) < 0) {
        pool->set_next_input_tick(slot, first_input_tick);
    }
    pool->record_authored_command(slot, view_tick, view_tick, true, right);
    fixture.rig.server()->action_send_request(
        fixture.rig.branch(-1)->get_path_to(fixture.authority),
        StringName("_server_action"),
        view_tick,
        Array(),
        key,
        STATE_READY
    );
    fixture.step_until_requested(12);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.authority->viewed_tick(), view_tick);
    CHECK(fixture.authority_recorded(view_tick));
    NETW_CHECK_GT(
        handle->get_stats()->get_int_fact(netw::NetwPredictStats::FACT_MISSING),
        int64_t(0)
    );
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
    "[Networked][LagComp][Action] AC10 an action minted through Netw.action "
    "on a session that declared neither lag compensation nor prediction "
    "predicts on its controller, runs on the server and answers, because "
    "the handle asks the next configuration pass for the defaults and the "
    "request applies them"
) {
    LoopbackRig rig(1);
    rig.mount();
    WorldDecl world;
    world.clocked(30, 3).player(
        EntityDecl()
            .named("P")
            .synced("position")
            .placed_at(Vector2())
            .mounted(),
        0
    );
    rig.declare_world(world);
    Carrier *owner = Object::cast_to<Carrier>(
        rig.node_of(rig.entity_of(StringName("P"), 0), 0)
    );
    Carrier *authority
        = Object::cast_to<Carrier>(rig.node_of(rig.entity_of("P")));
    REQUIRE(owner != nullptr);
    REQUIRE(authority != nullptr);
    REQUIRE_FALSE(rig.server()->lagcomp_is_configured());

    const Ref<netw::NetwAction> asked
        = netw::Netw::action(Callable(owner, StringName("_server_action")));
    const Ref<netw::NetwAction> judged
        = netw::Netw::action(Callable(authority, StringName("_server_action")));
    REQUIRE(asked.is_valid());
    REQUIRE(judged.is_valid());
    CHECK_FALSE(rig.server()->lagcomp_is_configured());
    CHECK_FALSE(rig.client(0)->lagcomp_is_configured());
    rig.server()->config_settle();
    CHECK(rig.server()->lagcomp_is_configured());
    const CallLog log;
    asked->set_predict(Callable(owner, StringName("_predict_ghost")));
    asked->connect("denied", log.callable(StringName("denied")));

    asked->request(rig.clock_of(rig.client(0)).get_tick(), Array());
    CHECK(rig.client(0)->lagcomp_is_configured());
    NETW_CHECK_EQ(owner->ghosts(), 1);
    rig.step_ticks(30);

    NETW_CHECK_EQ(authority->requests(), 1);
    NETW_CHECK_EQ(log.count(StringName("denied")), 1);
}

struct ClientAction {
    LoopbackRig rig;
    Carrier *owner = nullptr;
    Carrier *authority = nullptr;
    Node *orphan = nullptr;

    ClientAction() : rig(1) {
        rig.mount();
        WorldDecl world;
        world.player(
            EntityDecl()
                .named("P")
                .synced("position")
                .placed_at(Vector2())
                .mounted(),
            0
        );
        rig.declare_world(world);
        owner = Object::cast_to<Carrier>(
            rig.node_of(rig.entity_of(StringName("P"), 0), 0)
        );
        authority = Object::cast_to<Carrier>(rig.node_of(rig.entity_of("P")));
        REQUIRE(owner != nullptr);
        REQUIRE(authority != nullptr);
        Ref<netw::NetwClockConfig> clock;
        clock.instantiate();
        clock->set("tickrate", 24);
        REQUIRE(int(rig.server()->clock_initialize(clock)) == int(OK));
        rig.server()->clock_engine().set_manual_tick(true);
        authority->arm_action(rig.server());
        rig.server()->config_settle();
        REQUIRE(rig.server()->lagcomp_is_configured());
        REQUIRE_FALSE(session()->lagcomp_is_configured());
    }

    ~ClientAction() {
        if (orphan != nullptr) {
            memdelete(orphan);
        }
    }

    netw::NetwMultiplayer *session() const {
        return rig.client(0);
    }

    Node *child(const char *p_name) const {
        Node *made = memnew(Node);
        made->set_name(StringName(p_name));
        rig.branch(0)->add_child(made);
        return made;
    }

    void fire(int p_view_tick) const {
        owner->fire(p_view_tick, IMMEDIATE);
        if (session()->clock_is_configured()) {
            session()->clock_engine().set_manual_tick(true);
        }
    }
};

struct Configuring {
    netw::NetwMultiplayer *session = nullptr;
    Carrier *carrier = nullptr;
    int calls = 0;
    int64_t future_seen = -1;
    int ghosts_seen = -1;
    int64_t effects_seen = -1;
};

Configuring &configuring() {
    static Configuring held;
    return held;
}

void request_while_configuring() {
    Configuring &seen = configuring();
    seen.calls += 1;
    seen.future_seen = seen.session->get_max_future_action_ticks();
    seen.carrier->fire(5, IMMEDIATE);
    seen.ghosts_seen = seen.carrier->ghosts();
    seen.effects_seen = seen.session->lagcomp_effect_count();
}

TEST_CASE(
    "[Networked][LagComp][Action] AC13 a completed lag compensation draft "
    "runs an action in the declaring frame without premature defaults"
) {
    ClientAction fixture;
    bool early_handle = false;
    SUBCASE("the handle exists before configuration") {
        early_handle = true;
    }
    SUBCASE("the handle is created after the fluent tail") {
    }
    if (early_handle) {
        fixture.owner->arm_action(fixture.session());
        CHECK_FALSE(fixture.session()->lagcomp_is_configured());
    }
    const Ref<netw::NetwLagCompensationConfig> draft
        = netw::Netw::configure_lagcomp(fixture.rig.branch(0));
    netw::Netw::configure_clock(fixture.rig.branch(0))->ticks_per_second(24);
    draft->max_future_action(17)->input_gate_deadline(19);
    if (!early_handle) {
        fixture.owner->arm_action(fixture.session());
    }
    const FaultWatch watch;

    fixture.fire(0);

    NETW_CHECK_EQ(fixture.owner->ghosts(), 1);
    NETW_CHECK_EQ(fixture.session()->get_max_future_action_ticks(), 17);
    NETW_CHECK_EQ(fixture.session()->get_input_gate_deadline_ticks(), 19);
    NETW_CHECK_EQ(
        int(fixture.session()->clock_get_param(
            netw::NetwMultiplayer::CLOCK_PARAM_TICKRATE
        )),
        24
    );
    fixture.rig.step_ticks(30);
    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.owner->denials(), 1);

    fixture.session()->config_settle();
    draft->max_future_action(3);
    fixture.fire(1);
    fixture.rig.step_ticks(30);

    NETW_CHECK_EQ(fixture.owner->ghosts(), 2);
    NETW_CHECK_EQ(fixture.authority->requests(), 2);
    NETW_CHECK_EQ(fixture.session()->get_max_future_action_ticks(), 17);
    NETW_CHECK_EQ(draft->get_max_future_action_ticks(), 17);
    NETW_CHECK_EQ(
        watch.log->warning_count_containing("after the session started"),
        1
    );
    NETW_CHECK_EQ(watch.log->errors_logged(), 0);
}

TEST_CASE(
    "[Networked][LagComp][Action] AC14 settings the session cannot apply "
    "refuse the request before it predicts, arms an effect or sends, and "
    "say why once"
) {
    ClientAction fixture;
    fixture.owner->arm_action(fixture.session());
    Node *first = fixture.child("First");
    const FaultWatch watch;
    String said;
    bool as_error = true;

    SUBCASE("two nodes declare lag compensation") {
        netw::Netw::configure_lagcomp(first)->max_future_action(4);
        netw::Netw::configure_lagcomp(fixture.child("Second"))
            ->max_future_action(5);
        said = "more than one node";
    }
    SUBCASE("the declaring node left before the request") {
        netw::Netw::configure_lagcomp(first)->max_future_action(4);
        first->get_parent()->remove_child(first);
        fixture.orphan = first;
        said = "left the tree";
        as_error = false;
    }
    SUBCASE("the declaration holds something other than settings") {
        netw::session_decl::declare(
            first,
            netw::session_decl::KIND_LAGCOMP_CONFIG,
            Callable(),
            Variant(int64_t(5)),
            "configure_lagcomp"
        );
        said = "cannot be read";
    }

    fixture.fire(0);
    fixture.fire(1);
    fixture.rig.step_ticks(5);

    NETW_CHECK_EQ(fixture.authority->requests(), 0);
    NETW_CHECK_EQ(fixture.owner->ghosts(), 0);
    NETW_CHECK_EQ(fixture.owner->denials(), 0);
    NETW_CHECK_EQ(fixture.session()->lagcomp_effect_count(), 0);
    CHECK_FALSE(fixture.session()->lagcomp_is_configured());
    if (as_error) {
        CHECK(watch.log->error_count_containing(said) >= 1);
    } else {
        NETW_CHECK_EQ(watch.log->warning_count_containing(said), 1);
    }
}

TEST_CASE(
    "[Networked][LagComp][Action] AC15 a request made from a callback that "
    "reacts to the settings being applied is refused as busy, while that "
    "callback already reads every applied setting"
) {
    ClientAction fixture;
    fixture.owner->arm_action(fixture.session());
    netw::Netw::configure_clock(fixture.rig.branch(0))->ticks_per_second(24);
    netw::Netw::configure_lagcomp(fixture.rig.branch(0))->max_future_action(17);
    configuring() = Configuring();
    configuring().session = fixture.session();
    configuring().carrier = fixture.owner;
    fixture.session()->observe(
        netw::NetwMultiplayer::CLOCK_CONFIGURED,
        callable_mp_static(&request_while_configuring)
    );
    const FaultWatch watch;

    fixture.fire(0);
    fixture.rig.step_ticks(30);

    NETW_CHECK_EQ(configuring().calls, 1);
    NETW_CHECK_EQ(configuring().future_seen, 17);
    NETW_CHECK_EQ(configuring().ghosts_seen, 0);
    NETW_CHECK_EQ(configuring().effects_seen, 0);
    NETW_CHECK_EQ(fixture.owner->ghosts(), 1);
    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(
        watch.log->error_count_containing("while the session applied"),
        1
    );
    configuring() = Configuring();
}

PackedByteArray forged_request(int64_t p_view_tick) {
    netw::session::ActionRequest body;
    body.method = StringName("_server_action");
    body.view_tick = p_view_tick;
    body.data = netw::gd::var_to_bytes(Array());
    body.key = StringName("forged");
    body.timing = IMMEDIATE;
    return netw::session::frame_write(body);
}

TEST_CASE(
    "[Networked][LagComp][Action] AC11 the server runs an action request "
    "only when the peer that sent it controls the entity, so a client "
    "cannot act through a player it does not drive"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][prediction] Peer * requested an action on '*', "
        "which it does not control, so the server ignored it. Only "
        "the peer in NetwEntity.controller can request it.",
        1
    );

    ActionRig fixture;
    const Ref<netw::NetwEntity> judged
        = fixture.rig.server()->entity_get_view(fixture.rig.entity_of("P"));
    REQUIRE(judged.is_valid());
    const int64_t controller = fixture.rig.peer_id(0);
    REQUIRE(judged->get_controller() == controller);

    fixture.rig.server()->action_receive_carrier(
        judged,
        forged_request(fixture.authority_tick()),
        controller + 1
    );
    NETW_CHECK_EQ(fixture.authority->requests(), 0);

    fixture.rig.server()->action_receive_carrier(
        judged,
        forged_request(fixture.authority_tick()),
        controller
    );
    NETW_CHECK_EQ(fixture.authority->requests(), 1);
}

TEST_CASE(
    "[Networked][LagComp][Action] AC12 an action the server runs without "
    "denying it or binding a node confirms as soon as the server answers, "
    "so the requester never waits out the timeout to hear it was denied"
) {
    ActionRig fixture;
    fixture.authority->set_denies(false);

    fixture.owner->fire(fixture.owner_tick(), IMMEDIATE);
    REQUIRE(fixture.owner->ghost() != nullptr);
    fixture.step_until_requested(30);
    fixture.rig.step_ticks(4);

    NETW_CHECK_EQ(fixture.authority->requests(), 1);
    NETW_CHECK_EQ(fixture.owner->confirmations(), 1);
    NETW_CHECK_EQ(fixture.owner->denials(), 0);
    CHECK(fixture.owner->ghost()->is_queued_for_deletion());
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

#include "support/netw_test.h"

#include "support/netw_cells.h"

#include <memory>

#include "godot/callable.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwSessionIntakeLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::ReplicationCore;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

constexpr int64_t ROUTE = 7;
constexpr int64_t CONTROLLER = 2;
constexpr int64_t CUSTOM_CHANNEL = 120;

enum Plant {
    PLANT_NONE,
    PLANT_A_SENDER_THE_DIRECTION_TRUSTS,
    PLANT_A_CHANNEL_THAT_NEVER_ASKED_TO_DEFER,
    PLANT_A_BIND_THAT_NEVER_ARRIVES,
    PLANT_A_CHANNEL_THAT_ASKED_TO_DEFER,
};

class SenderSink final : public CallableCustom {
    std::shared_ptr<PackedInt64Array> seen;
    ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    SenderSink(
        const std::shared_ptr<PackedInt64Array> &p_seen,
        const Object *p_anchor
    )
        : seen(p_seen), anchor(netw::gd::instance_id(p_anchor)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("SenderSink");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &SenderSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &SenderSink::before;
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
        seen->push_back(p_count >= 3 ? int64_t(*p_arguments[2]) : int64_t(-1));
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

struct IntakeScenario {
    String label;
    int64_t channel = CUSTOM_CHANNEL;
    int64_t sender = 3;
    bool defers = false;
    bool binds_the_route_first = false;
    bool binds_the_route_after = false;
    bool reaches_the_handler = false;
    bool counts_an_unknown_route = false;
    bool counts_a_missing_route_verdict = false;
};

IntakeScenario a_command_from_a_stranger() {
    IntakeScenario scenario;
    scenario.label = "a-command-from-a-stranger";
    scenario.channel = netw::wire::builtin_channel("PREDICT_COMMAND");
    scenario.sender = 3;
    scenario.binds_the_route_first = true;
    return scenario;
}

IntakeScenario an_ack_from_a_client() {
    IntakeScenario scenario;
    scenario.label = "an-ack-from-a-client";
    scenario.channel = netw::wire::builtin_channel("PREDICT_ACK");
    scenario.sender = 2;
    scenario.binds_the_route_first = true;
    return scenario;
}

IntakeScenario a_deferring_channel_ahead_of_its_route() {
    IntakeScenario scenario;
    scenario.label = "a-deferring-channel-ahead-of-its-route";
    scenario.defers = true;
    scenario.binds_the_route_after = true;
    scenario.reaches_the_handler = true;
    return scenario;
}

IntakeScenario a_plain_channel_ahead_of_its_route() {
    IntakeScenario scenario;
    scenario.label = "a-plain-channel-ahead-of-its-route";
    scenario.counts_an_unknown_route = true;
    return scenario;
}

IntakeScenario a_sync_frame_for_a_route_nobody_bound() {
    IntakeScenario scenario;
    scenario.label = "a-sync-frame-for-a-route-nobody-bound";
    scenario.channel = netw::wire::builtin_channel("SYNC");
    scenario.sender = 2;
    scenario.counts_an_unknown_route = true;
    scenario.counts_a_missing_route_verdict = true;
    return scenario;
}

IntakeScenario a_plain_channel_on_a_bound_route() {
    IntakeScenario scenario;
    scenario.label = "a-plain-channel-on-a-bound-route";
    scenario.binds_the_route_first = true;
    scenario.reaches_the_handler = true;
    return scenario;
}

class IntakeRun {
    IntakeScenario declared;
    Plant planted = PLANT_NONE;
    PackedInt64Array handled;
    int64_t unknown_route_drops = 0;
    int64_t missing_route_verdicts = 0;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

public:
    IntakeRun(const IntakeScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        Ref<NetwMultiplayer> session;
        session.instantiate();
        ReplicationCore *plane = session->get_replication_plane();

        Node3D *body = memnew(Node3D);
        netw::gd::scene_root()->add_child(body);
        const Ref<NetwEntity> wrapper = NetwEntity::ensure(body);
        wrapper->set_controller(CONTROLLER);

        const std::shared_ptr<PackedInt64Array> seen
            = std::make_shared<PackedInt64Array>();
        bool defers = declared.defers;
        if (plant_is(PLANT_A_CHANNEL_THAT_NEVER_ASKED_TO_DEFER)) {
            defers = false;
        }
        if (plant_is(PLANT_A_CHANNEL_THAT_ASKED_TO_DEFER)) {
            defers = true;
        }
        plane->register_channel(
            declared.channel,
            Callable(memnew(SenderSink(seen, session.ptr()))),
            defers
        );

        if (declared.binds_the_route_first) {
            session->liveness_bind_route(ROUTE, wrapper.ptr());
        }

        PackedByteArray payload;
        payload.push_back(1);
        const int64_t sender = plant_is(PLANT_A_SENDER_THE_DIRECTION_TRUSTS)
            ? (declared.channel == netw::wire::builtin_channel("PREDICT_ACK")
                   ? 1
                   : CONTROLLER)
            : declared.sender;
        session->receive_carrier(
            netw::NetwMultiplayer::frame_pack(
                ROUTE,
                0,
                declared.channel,
                payload,
                String()
            ),
            sender,
            true,
            -1,
            -1
        );

        if (declared.binds_the_route_after
            && !plant_is(PLANT_A_BIND_THAT_NEVER_ARRIVES)) {
            session->liveness_bind_route(ROUTE, wrapper.ptr());
            session->session_flush_deferred();
        }

        handled = *seen;
        const Dictionary snapshot = session->stats_snapshot();
        unknown_route_drops
            = int64_t(snapshot.get(StringName("drops_unknown_route"), -1));
        missing_route_verdicts
            = int64_t(snapshot.get(StringName("verdict_does_not_exist"), -1));

        session->clear_session_state();
        body->queue_free();
    }

    const IntakeScenario &scenario() const {
        return declared;
    }

    const PackedInt64Array &reached() const {
        return handled;
    }

    int64_t unknown_routes() const {
        return unknown_route_drops;
    }

    int64_t missing_route_verdict() const {
        return missing_route_verdicts;
    }

    int64_t sender_sent() const {
        return declared.sender;
    }
};

typedef LawRowFor<IntakeRun> IntakeLaw;

LawVerdict law_admits(const IntakeRun &p_run) {
    const int reached = int(p_run.reached().size());
    const int expected = p_run.scenario().reaches_the_handler ? 1 : 0;
    if (reached != expected) {
        return law_broken(
            "the handler ran %d times against %d",
            reached,
            expected
        );
    }
    return law_held();
}

LawVerdict law_sender(const IntakeRun &p_run) {
    for (int index = 0; index < p_run.reached().size(); index++) {
        if (p_run.reached()[index] != p_run.sender_sent()) {
            return law_broken(
                "the handler was told peer %d sent a frame peer %d sent",
                int(p_run.reached()[index]),
                int(p_run.sender_sent())
            );
        }
    }
    return law_held();
}

LawVerdict law_counted(const IntakeRun &p_run) {
    const int64_t expected = p_run.scenario().counts_an_unknown_route ? 1 : 0;
    if (p_run.unknown_routes() != expected) {
        return law_broken(
            "%d frames counted at an unknown route against %d",
            int(p_run.unknown_routes()),
            int(expected)
        );
    }
    return law_held();
}

LawVerdict law_both_books(const IntakeRun &p_run) {
    const int64_t expected
        = p_run.scenario().counts_a_missing_route_verdict ? 1 : 0;
    if (p_run.missing_route_verdict() != expected) {
        return law_broken(
            "%d frames counted as a missing route against %d",
            int(p_run.missing_route_verdict()),
            int(expected)
        );
    }
    return law_held();
}

const IntakeLaw L_ADMITS = {
    "admits",
    "a frame reaches a handler only where its route and its author both hold",
    &law_admits,
};

const IntakeLaw L_SENDER = {
    "sender",
    "a handler is told the peer that sent the frame it is running for",
    &law_sender,
};

const IntakeLaw L_COUNTED = {
    "counted",
    "a frame nobody could place is counted once, and a parked one never is",
    &law_counted,
};

const IntakeLaw L_BOTH_BOOKS = {
    "both-books",
    "a refused frame reaches the verdict book as well as the drop counter",
    &law_both_books,
};

const IntakeLaw LAWS[] = {L_ADMITS, L_SENDER, L_COUNTED, L_BOTH_BOOKS};

TEST_CASE("[Networked][Session][SceneTree] the carrier intake laws hold") {
    const IntakeScenario CORPUS[] = {
        a_command_from_a_stranger(),
        an_ack_from_a_client(),
        a_deferring_channel_ahead_of_its_route(),
        a_plain_channel_ahead_of_its_route(),
        a_sync_frame_for_a_route_nobody_bound(),
        a_plain_channel_on_a_bound_route(),
    };
    for (const IntakeScenario &scenario : CORPUS) {
        const IntakeRun run(scenario);
        for (const IntakeLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a command the controller did send "
    "reds admits"
) {
    const IntakeScenario scenario = a_command_from_a_stranger();
    const IntakeRun run(scenario, PLANT_A_SENDER_THE_DIRECTION_TRUSTS);
    NETW_CELL(L_ADMITS, scenario);
    NETW_LAW_BREAKS(L_ADMITS, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] an ack the server did send reds "
    "admits"
) {
    const IntakeScenario scenario = an_ack_from_a_client();
    const IntakeRun run(scenario, PLANT_A_SENDER_THE_DIRECTION_TRUSTS);
    NETW_CELL(L_ADMITS, scenario);
    NETW_LAW_BREAKS(L_ADMITS, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a channel that never asked to defer "
    "reds admits"
) {
    const IntakeScenario scenario = a_deferring_channel_ahead_of_its_route();
    const IntakeRun run(scenario, PLANT_A_CHANNEL_THAT_NEVER_ASKED_TO_DEFER);
    NETW_CELL(L_ADMITS, scenario);
    NETW_LAW_BREAKS(L_ADMITS, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a route that is never bound reds "
    "admits"
) {
    const IntakeScenario scenario = a_deferring_channel_ahead_of_its_route();
    const IntakeRun run(scenario, PLANT_A_BIND_THAT_NEVER_ARRIVES);
    NETW_CELL(L_ADMITS, scenario);
    NETW_LAW_BREAKS(L_ADMITS, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a channel that asked to defer reds "
    "counted"
) {
    const IntakeScenario scenario = a_plain_channel_ahead_of_its_route();
    const IntakeRun run(scenario, PLANT_A_CHANNEL_THAT_ASKED_TO_DEFER);
    NETW_CELL(L_COUNTED, scenario);
    NETW_LAW_BREAKS(L_COUNTED, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a sync frame for a route the "
    "session bound reds both-books"
) {
    IntakeScenario scenario = a_sync_frame_for_a_route_nobody_bound();
    scenario.binds_the_route_first = true;
    const IntakeRun run(scenario);
    NETW_CELL(L_BOTH_BOOKS, scenario);
    NETW_LAW_BREAKS(L_BOTH_BOOKS, run);
}

} // namespace TestNetwSessionIntakeLaws

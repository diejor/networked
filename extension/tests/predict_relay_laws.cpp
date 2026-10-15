#include "support/netw_test.h"

#include "support/loopback_rig.h"

#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/simulation.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/frame_records.hpp"
#include "netw/predict/joint.hpp"
#include "netw/wire/registry.hpp"

#include <initializer_list>

#if defined(NETW_TIER_HOSTED)

namespace TestNetwPredictRelayLaws {

using namespace netw_test;
using godot::Dictionary;
using godot::PackedByteArray;
using godot::PackedInt64Array;
using godot::Ref;
using godot::StringName;
using godot::Vector;
using godot::Vector2;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPrediction;
using netw::NetwPredictionEngine;
using netw::NetwPredictStats;

constexpr int SETTLE_TICKS = 60;

EntityDecl driver(const char *p_name, netw::Schedule p_schedule) {
    return EntityDecl()
        .named(p_name)
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(p_schedule);
}

WorldDecl two_drivers(WorldDecl::Reconcile p_group, bool p_member_joint) {
    Vector<StringName> simulated;
    simulated.push_back(StringName("Q"));
    WorldDecl world;
    world.clocked(30, 3)
        .lag_compensated()
        .player(driver("P", netw::Schedule::TICK), 0)
        .player(driver("Q", netw::Schedule::TICK), 1)
        .island(StringName("P"), simulated, p_group)
        .simulating(StringName("Q"));
    if (p_member_joint) {
        Vector<StringName> others;
        others.push_back(StringName("P"));
        world.island(StringName("Q"), others);
    }
    return world;
}

struct Drivers {
    LoopbackRig rig;

    Drivers(
        const WorldDecl &p_world,
        std::initializer_list<const char *> p_names,
        int p_clients = 2
    )
        : rig(p_clients) {
        rig.declare_world(p_world);
        for (const char *name : p_names) {
            const int route = int(served(name)->get_route());
            for (int client = 0; client < rig.count(); ++client) {
                if (!rig.holds_entity(StringName(name), client)) {
                    rig.declare_mirror(
                        client,
                        driver(name, netw::Schedule::TICK)
                            .on_route(route)
                            .controlled_by(0)
                    );
                }
            }
            admit_everyone(name);
        }
    }

    Ref<NetwEntity> served(const char *p_name) const {
        return rig.server()->entity_get_view(rig.entity_of(StringName(p_name)));
    }

    void admit(const char *p_name, const PackedInt64Array &p_peers) {
        rig.server()->interest_set_entity_intent(served(p_name), p_peers);
        rig.server()->interest_flush_now();
    }

    void admit_everyone(const char *p_name) {
        PackedInt64Array peers;
        peers.push_back(rig.peer_id(-1));
        for (int client = 0; client < rig.count(); ++client) {
            peers.push_back(rig.peer_id(client));
        }
        admit(p_name, peers);
    }

    void drive(const char *p_name, int p_client, const Vector2 &p_motion) {
        rig.node_of(rig.entity_of(StringName(p_name), p_client), p_client)
            ->set("motion", p_motion);
    }

    bool relays_to(const char *p_name, int p_client) const {
        return rig.server()
            ->relay_peers(int64_t(served(p_name)->get_rid_handle().get_id()))
            .has(rig.peer_id(p_client));
    }

    int64_t relayed_recorded(const char *p_name, int p_client) const {
        return rig.prediction_handle(StringName(p_name), p_client)
            ->get_stats()
            ->get_int_fact(NetwPredictStats::FACT_RELAYED_RECORDED);
    }
};

void check_subscribed_by_selection(const WorldDecl &p_world) {
    Drivers drivers(p_world, {"P", "Q"});
    drivers.drive("P", 0, Vector2(1.0, 0.0));
    drivers.drive("Q", 1, Vector2(0.0, 1.0));
    drivers.rig.step_ticks(SETTLE_TICKS);
    CHECK(drivers.relays_to("Q", 0));

    NetwMultiplayer *client = drivers.rig.client(0);
    client->simulation_handle(drivers.rig.entity_of(StringName("P"), 0))
        ->forget(
            client->entity_get_view(drivers.rig.entity_of(StringName("Q"), 0))
        );
    drivers.rig.step_ticks(5);
    CHECK_FALSE(drivers.relays_to("Q", 0));
}

TEST_CASE(
    "[Networked][Predict][Relay] R1 a member a predicted subject selects is "
    "subscribed to its relayed input on the server whatever reconcile the "
    "group or the member declares, and forgetting it unsubscribes"
) {
    SUBCASE("an INDEPENDENT group") {
        check_subscribed_by_selection(
            two_drivers(WorldDecl::INDEPENDENT, false)
        );
    }
    SUBCASE("a member that declares JOINT itself") {
        check_subscribed_by_selection(two_drivers(WorldDecl::JOINT, true));
    }
}

TEST_CASE(
    "[Networked][Predict][Relay] R2 a client that simulates a host-controlled "
    "entity receives the input the host ran for it"
) {
    Vector<StringName> simulated;
    simulated.push_back(StringName("H"));
    WorldDecl world;
    world.clocked(30, 3)
        .lag_compensated()
        .player(driver("P", netw::Schedule::TICK), 0)
        .hosted(driver("H", netw::Schedule::TICK))
        .island(StringName("P"), simulated)
        .simulating(StringName("H"));
    Drivers drivers(world, {"P", "H"}, 1);
    drivers.drive("P", 0, Vector2(1.0, 0.0));
    drivers.drive("H", -1, Vector2(0.0, 1.0));
    drivers.rig.step_ticks(SETTLE_TICKS);
    REQUIRE(drivers.relays_to("H", 0));
    NETW_CHECK_GT(drivers.relayed_recorded("H", 0), int64_t(0));
}

constexpr int RELAY_LATENCY_POLLS = 12;
constexpr int GUESS_OBSERVED_TICKS = 90;
constexpr int GUESS_FLIP_TICKS = 6;

struct Guessed {
    int64_t guessed = 0;
    int64_t repeated = 0;
    int64_t samples = 0;
};

int64_t newest_relayed_before(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    int64_t p_from
) {
    const int relayed = int(netw::predict::CellProvenance::RELAYED);
    for (int64_t key = p_from; key >= 0; --key) {
        if (p_pool->joint_provenance_at(p_slot, key) == relayed) {
            return key;
        }
    }
    return -1;
}

int64_t hash_at(NetwPredictionEngine *p_pool, int64_t p_slot, int64_t p_key) {
    netw::predict::StateRow cell;
    if (!p_pool->read_command_at(p_slot, p_key, cell)) {
        return -1;
    }
    return p_pool->command_hash_row(p_slot, cell);
}

void judge_guesses(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    Guessed &r_out
) {
    const int64_t present = p_pool->newest_matrix_transition_of(p_slot);
    const int64_t newest = newest_relayed_before(p_pool, p_slot, present);
    const int64_t previous = newest_relayed_before(p_pool, p_slot, newest - 1);
    if (newest < 0 || previous < 0) {
        return;
    }
    const int64_t held = hash_at(p_pool, p_slot, newest);
    if (held != hash_at(p_pool, p_slot, previous)) {
        return;
    }
    r_out.samples += 1;
    const int substituted = int(netw::predict::CellProvenance::SUBSTITUTED);
    for (int64_t key = newest + 1; key <= present; ++key) {
        if (p_pool->joint_provenance_at(p_slot, key) != substituted) {
            continue;
        }
        r_out.guessed += 1;
        r_out.repeated += hash_at(p_pool, p_slot, key) == held ? 1 : 0;
    }
}

Guessed guessed_past_relay() {
    Drivers drivers(two_drivers(WorldDecl::JOINT, true), {"P", "Q"});
    LoopbackRig &rig = drivers.rig;
    rig.conditions(
        0,
        netw::LocalLinkConditions::polls(RELAY_LATENCY_POLLS),
        rig.peer_id(-1)
    );
    drivers.drive("P", 0, Vector2(1.0, 0.0));
    drivers.drive("Q", 1, Vector2(0.0, 1.0));
    rig.step_ticks(SETTLE_TICKS);
    NetwPredictionEngine *const viewed = rig.prediction_pool(0);
    const int64_t slot = rig.prediction_slot(StringName("Q"), 0);
    Guessed out;
    for (int tick = 0; tick < GUESS_OBSERVED_TICKS; ++tick) {
        const bool up = (tick / GUESS_FLIP_TICKS) % 2 == 0;
        drivers.drive("Q", 1, Vector2(0.0, up ? 1.0 : -1.0));
        rig.step_ticks(1);
        judge_guesses(viewed, slot, out);
    }
    return out;
}

TEST_CASE(
    "[Networked][Predict][Relay] R4 a simulated member with no predict_input "
    "repeats the newest relayed input past it, forward and in a joint replay"
) {
    const Guessed run = guessed_past_relay();
    REQUIRE(run.samples >= GUESS_OBSERVED_TICKS / 2);
    REQUIRE(run.guessed >= run.samples);
    NETW_CHECK_EQ(run.repeated, run.guessed);
}

struct Confirmed {
    int64_t relayed = 0;
    int64_t passes = 0;
    int64_t relay_floor_moves = 0;
};

Confirmed confirmed_by_relay() {
    Drivers drivers(two_drivers(WorldDecl::JOINT, true), {"P", "Q"});
    LoopbackRig &rig = drivers.rig;
    rig.conditions(
        0,
        netw::LocalLinkConditions::polls(RELAY_LATENCY_POLLS),
        rig.peer_id(-1)
    );
    drivers.drive("P", 0, Vector2(1.0, 0.0));
    drivers.drive("Q", 1, Vector2(0.0, 1.0));
    rig.step_ticks(SETTLE_TICKS);
    NetwPredictionEngine *const viewed = rig.prediction_pool(0);
    const int64_t owner = rig.prediction_slot(StringName("P"), 0);
    const int64_t member = rig.prediction_slot(StringName("Q"), 0);
    const auto stat = [&](int64_t p_slot, int p_column) {
        return viewed->joint_stats(p_slot)[p_column];
    };
    const int64_t relayed = drivers.relayed_recorded("Q", 0);
    const int64_t passes = stat(owner, NetwPredictionEngine::STAT_JOINT_PASSES);
    const int64_t moves
        = stat(member, NetwPredictionEngine::STAT_JOINT_FLOOR_RELAY_MOVES);
    rig.step_ticks(SETTLE_TICKS);
    Confirmed out;
    out.relayed = drivers.relayed_recorded("Q", 0) - relayed;
    out.passes = stat(owner, NetwPredictionEngine::STAT_JOINT_PASSES) - passes;
    out.relay_floor_moves
        = stat(member, NetwPredictionEngine::STAT_JOINT_FLOOR_RELAY_MOVES)
        - moves;
    return out;
}

TEST_CASE(
    "[Networked][Predict][Relay] R5 a relayed input equal to the guess it "
    "replaces moves no relay floor and opens no joint replay"
) {
    const Confirmed run = confirmed_by_relay();
    REQUIRE(run.relayed >= SETTLE_TICKS / 2);
    NETW_CHECK_EQ(run.relay_floor_moves, int64_t(0));
    NETW_CHECK_EQ(run.passes, int64_t(0));
}

class DriftStepper : public netw::NetwPhysicsStepper {
public:
    godot::LocalVector<godot::Node2D *> bodies;

    bool can_step() override {
        return true;
    }

    void step(const godot::RID &, double) override {
        for (godot::Node2D *body : bodies) {
            body->set_position(body->get_position() + Vector2(0.0, 0.5));
        }
    }
};

Ref<DriftStepper> stepped_on(
    const LoopbackRig &p_rig,
    int p_client,
    std::initializer_list<const char *> p_names
) {
    NetwMultiplayer *api
        = p_client < 0 ? p_rig.server() : p_rig.client(p_client);
    Ref<DriftStepper> stepper;
    stepper.instantiate();
    godot::RID space;
    for (const char *name : p_names) {
        const Ref<NetwEntity> entity
            = p_rig.prediction_handle(StringName(name), p_client)->get_entity();
        godot::Node2D *body
            = godot::Object::cast_to<godot::Node2D>(entity->get_owner());
        REQUIRE(body != nullptr);
        netw::gd::scene_root()->add_child(body);
        stepper->bodies.push_back(body);
        space = api->entity_space_of(entity).space;
    }
    REQUIRE(space.is_valid());
    api->predict_stepper_install(space, stepper);
    for (const char *name : p_names) {
        p_rig.prediction_handle(StringName(name), p_client)
            ->get_entity()
            ->get_simulation()
            ->set_schedule(netw::NetwSimulation::SCHEDULE_STEPPED);
    }
    return stepper;
}

void unmount(const Ref<DriftStepper> &p_stepper) {
    for (godot::Node2D *body : p_stepper->bodies) {
        netw::gd::scene_root()->remove_child(body);
    }
}

constexpr int FILED_FRAMES = 90;
constexpr int FILED_JUDGED_FROM = 40;
constexpr int VIEWER_LINK_POLLS = 3;
constexpr int AUTHOR_LINK_POLLS = 7;

struct Filing {
    int64_t judged = 0;
    int64_t matched = 0;
    int64_t offset = 0;
    int64_t consume_gap = 0;
};

WorldDecl stepped_pair() {
    Vector<StringName> simulated;
    simulated.push_back(StringName("Q"));
    WorldDecl world;
    world.clocked(60, 3)
        .lag_compensated()
        .player(driver("P", netw::Schedule::FRAME), 0)
        .player(driver("Q", netw::Schedule::FRAME), 1)
        .island(StringName("P"), simulated)
        .simulating(StringName("Q"));
    return world;
}

struct SteppedPair {
    Drivers drivers;
    Ref<DriftStepper> server;
    Ref<DriftStepper> viewer;
    Ref<DriftStepper> author;

    SteppedPair() : drivers(stepped_pair(), {"P", "Q"}) {
        LoopbackRig &rig = drivers.rig;
        server = stepped_on(rig, -1, {"P", "Q"});
        viewer = stepped_on(rig, 0, {"P", "Q"});
        author = stepped_on(rig, 1, {"Q"});
        rig.conditions(
            -1,
            netw::LocalLinkConditions::polls(VIEWER_LINK_POLLS),
            rig.peer_id(0)
        );
        rig.conditions(
            -1,
            netw::LocalLinkConditions::polls(AUTHOR_LINK_POLLS),
            rig.peer_id(1)
        );
        drivers.drive("P", 0, Vector2(1.0, 0.0));
    }

    ~SteppedPair() {
        unmount(server);
        unmount(viewer);
        unmount(author);
    }
};

int64_t newest_ran(NetwPredictionEngine *p_served, int64_t p_slot) {
    netw::predict::CommandFrameRecord window;
    REQUIRE(
        netw::predict::CommandFrameRecord::decode(
            *p_served->input_schema(p_slot),
            p_served->build_relay_frame(p_slot),
            window
        )
    );
    REQUIRE(window.transition_count() > 0);
    return window.transition_at(window.transition_count() - 1).index;
}

int64_t ran_hash(NetwPredictionEngine *p_served, int64_t p_slot) {
    return p_served->command_hash_row(p_slot, *p_served->command_of(p_slot));
}

Filing filed_at_world_tick() {
    SteppedPair pair;
    LoopbackRig &rig = pair.drivers.rig;
    NetwPredictionEngine *const served = rig.prediction_pool(-1);
    NetwPredictionEngine *const viewed = rig.prediction_pool(0);
    const int64_t served_q = rig.prediction_slot(StringName("Q"));
    const int64_t served_p = rig.prediction_slot(StringName("P"));
    const int64_t viewed_q = rig.prediction_slot(StringName("Q"), 0);
    godot::HashMap<int64_t, int64_t> ran;
    for (int frame = 0; frame < FILED_FRAMES; ++frame) {
        pair.drivers.drive("Q", 1, Vector2(real_t(frame % 5), 0.0));
        rig.step_frame(1);
        if (frame >= FILED_JUDGED_FROM) {
            ran[newest_ran(served, served_q)] = ran_hash(served, served_q);
        }
    }
    Filing out;
    out.offset = viewed->joint_world_offset_of(viewed_q);
    out.consume_gap = served->ack_of(served_p) - served->ack_of(served_q);
    netw::predict::StateRow cell;
    for (const godot::KeyValue<int64_t, int64_t> &row : ran) {
        const int64_t transition = row.key - out.offset - 1;
        out.judged += 1;
        if (viewed->joint_provenance_at(viewed_q, transition)
                == int(netw::predict::CellProvenance::RELAYED)
            && viewed->read_command_at(viewed_q, transition, cell)
            && viewed->command_hash_row(viewed_q, cell) == row.value) {
            out.matched += 1;
        }
    }
    return out;
}

TEST_CASE(
    "[Networked][Predict][Relay] R3 a relayed input the server ran at world "
    "tick t is filed at the receiver's transition t minus its world offset "
    "minus one, the transition that lands on world tick t, whatever consume "
    "delay the author and the receiver have"
) {
    const Filing filing = filed_at_world_tick();
    REQUIRE(filing.offset >= 1);
    REQUIRE(filing.consume_gap >= 2);
    REQUIRE(filing.judged >= 30);
    NETW_CHECK_EQ(filing.matched, filing.judged);
}

constexpr int TURN_FRAMES = 30;
constexpr int TURNS = 24;
constexpr int TURNS_SETTLING = 2;
constexpr double VIEWER_JITTER_MS = 30.0;
constexpr int64_t VIEWER_JITTER_SEED = 7;
constexpr real_t POSE_TOLERANCE = 0.001;

struct Turned {
    int64_t judged = 0;
    int64_t matched = 0;
};

struct ViewedFrame {
    int64_t paired = 0;
    int64_t ran = 0;
    int64_t relay_floor = -1;
    int64_t pass_floor = -1;
    bool replayed = false;
    bool healed = false;
    Vector2 pose;
};

bool input_changed_in(
    const godot::HashMap<int64_t, int64_t> &p_ran,
    int64_t p_after,
    int64_t p_through
) {
    const int64_t *held = p_ran.getptr(p_after);
    for (int64_t tick = p_after + 1; tick <= p_through; ++tick) {
        const int64_t *next = p_ran.getptr(tick);
        if (held == nullptr || next == nullptr || *next != *held) {
            return true;
        }
    }
    return false;
}

Turned replayed_through_turns() {
    SteppedPair pair;
    LoopbackRig &rig = pair.drivers.rig;
    const Ref<netw::LocalLinkConditions> jittered
        = netw::LocalLinkConditions::create(VIEWER_JITTER_SEED);
    jittered->set_jitter_ms(VIEWER_JITTER_MS);
    rig.conditions(0, jittered, rig.peer_id(-1));
    NetwPredictionEngine *const served = rig.prediction_pool(-1);
    NetwPredictionEngine *const viewed = rig.prediction_pool(0);
    const int64_t served_q = rig.prediction_slot(StringName("Q"));
    const int64_t viewed_q = rig.prediction_slot(StringName("Q"), 0);
    const int64_t viewed_p = rig.prediction_slot(StringName("P"), 0);
    const godot::Node2D *const served_body = pair.server->bodies[1];
    const godot::Node2D *const viewed_body = pair.viewer->bodies[1];
    const auto owner_stat
        = [&](int p_stat) { return viewed->joint_stats(viewed_p)[p_stat]; };
    const auto turn = [&](int p_frame) {
        const real_t heading = (p_frame / TURN_FRAMES) % 2 == 0 ? 1.0 : -1.0;
        pair.drivers.drive("Q", 1, Vector2(heading, 0.0));
        rig.step_frame(1);
    };
    int frame = 0;
    for (; frame < TURNS_SETTLING * TURN_FRAMES; ++frame) {
        turn(frame);
    }
    godot::HashMap<int64_t, int64_t> ran;
    godot::HashMap<int64_t, Vector2> served_pose;
    godot::LocalVector<ViewedFrame> frames;
    int64_t passes_seen = owner_stat(NetwPredictionEngine::STAT_JOINT_PASSES);
    int64_t heals_seen
        = owner_stat(NetwPredictionEngine::STAT_JOINT_HEAL_SNAPS);
    for (; frame < TURNS * TURN_FRAMES; ++frame) {
        turn(frame);
        const int64_t newest = newest_ran(served, served_q);
        ran[newest] = ran_hash(served, served_q);
        served_pose[newest] = served_body->get_position();
        const int64_t passed
            = owner_stat(NetwPredictionEngine::STAT_JOINT_PASSES);
        const int64_t healed
            = owner_stat(NetwPredictionEngine::STAT_JOINT_HEAL_SNAPS);
        ViewedFrame seen;
        seen.paired = rig.clock_of(rig.client(0)).get_tick()
            + viewed->joint_world_offset_of(viewed_q);
        seen.ran = newest;
        seen.relay_floor = viewed->joint_relay_floor_of(viewed_q);
        seen.pass_floor = owner_stat(NetwPredictionEngine::STAT_JOINT_FLOOR);
        seen.replayed = passed > passes_seen;
        seen.healed = healed > heals_seen;
        seen.pose = viewed_body->get_position();
        passes_seen = passed;
        heals_seen = healed;
        frames.push_back(seen);
    }
    Turned out;
    for (uint32_t at = 1; at < frames.size(); ++at) {
        const ViewedFrame &relayed = frames[at - 1];
        const ViewedFrame &seen = frames[at];
        const Vector2 *truth = served_pose.getptr(seen.paired);
        if (!seen.replayed || relayed.healed || relayed.relay_floor < 0
            || seen.pass_floor != relayed.relay_floor || truth == nullptr
            || input_changed_in(ran, relayed.ran, seen.paired)) {
            continue;
        }
        out.judged += 1;
        out.matched += (*truth - seen.pose).length() < POSE_TOLERANCE;
    }
    return out;
}

TEST_CASE(
    "[Networked][Predict][Relay] O1 a joint replay from the floor a relayed "
    "input change set lands the author on the server's pose at the paired "
    "world tick, whichever of the relay and the rows reaches a jittered "
    "viewer first"
) {
    const Turned turned = replayed_through_turns();
    NETW_CHECK_GE(turned.judged, int64_t(TURNS / 2));
    NETW_CHECK_EQ(turned.matched, turned.judged);
}

TEST_CASE(
    "[Networked][Predict][Relay] R6 a subscriber that leaves an entity's "
    "interest keeps its subscription, and receives the entity's relayed "
    "input again when it returns"
) {
    Drivers drivers(two_drivers(WorldDecl::JOINT, true), {"P", "Q"});
    drivers.drive("P", 0, Vector2(1.0, 0.0));
    drivers.drive("Q", 1, Vector2(0.0, 1.0));
    drivers.rig.step_ticks(SETTLE_TICKS);
    REQUIRE(drivers.relays_to("Q", 0));

    PackedInt64Array without;
    without.push_back(drivers.rig.peer_id(-1));
    without.push_back(drivers.rig.peer_id(1));
    drivers.admit("Q", without);
    drivers.rig.step_ticks(10);
    CHECK(drivers.relays_to("Q", 0));

    drivers.admit_everyone("Q");
    const int64_t before = drivers.relayed_recorded("Q", 0);
    drivers.rig.step_ticks(10);
    NETW_CHECK_GT(drivers.relayed_recorded("Q", 0), before);
}

TEST_CASE(
    "[Networked][Predict][Relay] R8 a member selected while outside the "
    "subscriber's interest is subscribed anyway, receives nothing, and "
    "starts receiving its relayed input once interest admits it"
) {
    Drivers drivers(two_drivers(WorldDecl::JOINT, true), {"P"});
    PackedInt64Array without;
    without.push_back(drivers.rig.peer_id(-1));
    without.push_back(drivers.rig.peer_id(1));
    drivers.admit("Q", without);
    drivers.drive("P", 0, Vector2(1.0, 0.0));
    drivers.drive("Q", 1, Vector2(0.0, 1.0));
    drivers.rig.step_ticks(SETTLE_TICKS);
    CHECK(drivers.relays_to("Q", 0));
    const int64_t outside = drivers.relayed_recorded("Q", 0);
    drivers.rig.step_ticks(10);
    NETW_CHECK_EQ(drivers.relayed_recorded("Q", 0), outside);

    drivers.admit_everyone("Q");
    drivers.rig.step_ticks(10);
    NETW_CHECK_GT(drivers.relayed_recorded("Q", 0), outside);
}

int64_t channel_count(
    const Dictionary &p_snapshot,
    const char *p_table,
    int64_t p_peer,
    int64_t p_channel
) {
    const Dictionary peers = p_snapshot.get(StringName(p_table), Dictionary());
    const Dictionary channels = peers.get(p_peer, Dictionary());
    return int64_t(channels.get(p_channel, 0));
}

TEST_CASE(
    "[Networked][Predict][Relay] R7 a relayed frame carries the newest four "
    "inputs the server ran and no evidence, and that is all a subscriber "
    "receives"
) {
    Drivers drivers(two_drivers(WorldDecl::JOINT, true), {"P", "Q"});
    drivers.drive("P", 0, Vector2(1.0, 0.0));
    drivers.drive("Q", 1, Vector2(0.0, 1.0));
    drivers.rig.step_ticks(SETTLE_TICKS);
    REQUIRE(drivers.relays_to("Q", 0));

    NetwPredictionEngine *const served = drivers.rig.prediction_pool(-1);
    const int64_t slot = drivers.rig.prediction_slot(StringName("Q"));
    const PackedByteArray bytes = served->build_relay_frame(slot);
    netw::predict::CommandFrameRecord frame;
    REQUIRE(
        netw::predict::CommandFrameRecord::decode(
            *served->input_schema(slot),
            bytes,
            frame
        )
    );
    NETW_CHECK_EQ(frame.transition_count(), 4);
    NETW_CHECK_EQ(frame.evidence_count(), 0);

    NetwMultiplayer *viewer = drivers.rig.client(0);
    viewer->attribution_set_armed(true);
    drivers.rig.step_ticks(SETTLE_TICKS);
    const Dictionary seen = viewer->attribution_snapshot();
    const int64_t server = drivers.rig.peer_id(-1);
    const int64_t channel = netw::wire::builtin_channel("PREDICT_RELAY");
    const int64_t frames
        = channel_count(seen, "frames_in_by_channel", server, channel);
    const int64_t received = channel_count(seen, "bytes_in", server, channel);
    REQUIRE(frames > 0);
    NETW_CHECK_LE(received, frames * int64_t(bytes.size()));
}

TEST_CASE(
    "[Networked][Predict][Relay] O4 a copy first selected mid-stream is "
    "reconstructed by the rows it took as a remote copy, so it installs"
) {
    Vector<StringName> unselected;
    unselected.push_back(StringName("Q"));
    WorldDecl world;
    world.clocked(30, 3)
        .lag_compensated()
        .player(driver("P", netw::Schedule::TICK), 0)
        .player(driver("Q", netw::Schedule::TICK), 1)
        .island(StringName("P"), unselected, WorldDecl::JOINT);
    Drivers drivers(world, {"P", "Q"});
    drivers.drive("Q", 1, Vector2(0.0, 1.0));
    drivers.rig.step_ticks(SETTLE_TICKS);

    NetwMultiplayer *viewer = drivers.rig.client(0);
    viewer->simulation_handle(drivers.rig.entity_of(StringName("P"), 0))
        ->simulate(
            viewer->entity_get_view(drivers.rig.entity_of(StringName("Q"), 0))
        );
    drivers.drive("Q", 1, Vector2(1.0, 0.0));
    drivers.rig.step_ticks(20);

    NetwPredictionEngine *const pool = drivers.rig.prediction_pool(0);
    const int64_t slot = drivers.rig.prediction_slot(StringName("Q"), 0);
    CHECK(pool->stream_reconstructed_of(slot));
    NETW_CHECK_GT(
        drivers.rig.prediction_handle(StringName("Q"), 0)
            ->get_stats()
            ->get_int_fact(NetwPredictStats::FACT_INSTALLS),
        int64_t(0)
    );
}

} // namespace TestNetwPredictRelayLaws

#endif

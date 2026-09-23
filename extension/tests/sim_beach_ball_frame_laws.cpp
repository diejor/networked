#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/display/timing.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace TestNetwSimBeachBallFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::sim::Mode;

constexpr const char *BALL_ID = "sim_beach_ball";

constexpr const char *BALL_SOURCE = R"(extends RigidBody3D

func _init() -> void:
	gravity_scale = 0.0
	collision_layer = 0
	collision_mask = 0
	linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	linear_damp = 0.0
	angular_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	angular_damp = 0.0
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	add_child(shape)
	var visual := Node3D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.simulation.replicas = NetwSimulationHandle.REPLICAS_ACTIVE
	e.interpolation.visual_root = ^"Visual"
	Netw.configure_property(self, &"position").state().heartbeat(30) \
			.interpolate(NetwInterpolate.new().lerp())
	Netw.configure_property(self, &"quaternion").state() \
			.interpolate(NetwInterpolate.new().slerp())
	Netw.configure_property(self, &"linear_velocity").state()
	Netw.configure_property(self, &"angular_velocity").state()
	Netw.configure_property(self, &"sleeping").state()
)";

constexpr int CLIENTS = 2;
constexpr int COPIES = CLIENTS + 1;
constexpr int TICKRATE = 60;
constexpr int DISPLAY_OFFSET = 2;
constexpr int HEARTBEAT_TICKS = 30;
const Vector3 THROW(3.0, 0.0, 0.0);
const Vector3 SPIN(0.0, 1.0, 0.0);
const Vector3 DRIFT(0.0, 0.0, 0.5);

constexpr int FLIGHT_OPEN = 20;
constexpr int FLIGHT_CLOSE = 50;
constexpr int STOP_FRAME = 60;
constexpr int REST_FRAME = 180;
constexpr int BEAT_WAIT_FRAMES = HEARTBEAT_TICKS + 10;
constexpr int HEAL_BUDGET = HEARTBEAT_TICKS + DISPLAY_OFFSET + 3;
constexpr int WATCH_FRAMES = HEAL_BUDGET + 10;
constexpr int DRIFTED = 0;
constexpr int AGREEING = 1;

Script *ball_script = nullptr;

Node *build_ball(const Variant &p_name) {
    if (ball_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(ball_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct CopySeen {
    Mode mode = Mode::NONE;
    bool frozen = true;
    int flight_frames = 0;
    int one_install_frames = 0;
    int one_row_frames = 0;
    int integrated_frames = 0;
    double worst_body_step_error = 0.0;
    double worst_visual_step_error = 0.0;
    int64_t newest_age = -1;
    bool rested = false;
    int heal_ticks = -1;
    int64_t heal_installs = 0;
    double heal_body_jump = 0.0;
    double worst_heal_visual_step = 0.0;
    int awake_frames = 0;
    int64_t rest_installs = 0;
    int64_t rest_skips = 0;
};

struct BallEvidence {
    bool driven = false;
    bool seated = false;
    bool author_rested = false;
    bool beat_before_drift = false;
    CopySeen copies[COPIES];
};

BallEvidence &ball_evidence() {
    static BallEvidence evidence;
    return evidence;
}

double step_length() {
    return THROW.length() / double(TICKRATE);
}

class BeachBallScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int route = 0;
    int drift_frame = -1;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    Vector3 body_was[COPIES];
    Vector3 visual_was[COPIES];
    int64_t installed_was[COPIES] = {};
    int64_t held_was[COPIES] = {};
    int64_t skipped_was[COPIES] = {};

    RigidBody3D *ball_at(int p_client) const {
        return Object::cast_to<RigidBody3D>(stand->node_at(p_client, route));
    }

    Node3D *visual_at(int p_client) const {
        RigidBody3D *ball = ball_at(p_client);
        return ball == nullptr
            ? nullptr
            : Object::cast_to<Node3D>(ball->get_node_or_null("Visual"));
    }

    const netw::sim::Row *row_at(int p_client) const {
        const Ref<NetwEntity> entity = NetwEntity::of(ball_at(p_client));
        return entity.is_null()
            ? nullptr
            : stand->session(p_client)->sim_row_of(entity->get_rid_handle());
    }

    CopySeen &seen(int p_client) const {
        return ball_evidence().copies[p_client + 1];
    }

    bool open() {
        script = netw_test::minted_script(BALL_SOURCE);
        if (script.is_null()) {
            return false;
        }
        ball_script = script.ptr();
        stand = new netw_test::SimStand(CLIENTS);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(BALL_ID), callable_mp_static(&build_ball));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE, DISPLAY_OFFSET);
        stand->session(0)->session_submit_join(StringName("first"), Array());
        stand->pump(4);
        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(String(BALL_ID));
        const RID made
            = server->spawn_registered(StringName(BALL_ID), args, nullptr);
        Node *built = server->entity_get_node(made);
        if (built == nullptr) {
            return false;
        }
        arena->add_child(built);
        stand->pump(8);
        route = int(server->entity_get_route(made));
        for (int client = -1; client < CLIENTS; ++client) {
            if (visual_at(client) == nullptr) {
                return false;
            }
        }
        ball_at(-1)->set_linear_velocity(THROW);
        ball_at(-1)->set_angular_velocity(SPIN);
        ball_evidence().seated = true;
        return true;
    }

    void close() {
        ball_evidence().driven = true;
        delete stand;
        stand = nullptr;
        ball_script = nullptr;
        script = Ref<Script>();
    }

    void pump_display() {
        for (int client = 0; client < CLIENTS; ++client) {
            NetwMultiplayer *session = stand->session(client);
            session->display_pump_entity(
                NetwEntity::of(ball_at(client))->get_rid_handle(),
                netw::display::capture_timing(
                    &session->clock_engine(),
                    1.0 / double(TICKRATE)
                )
            );
        }
    }

    void remember() {
        for (int client = -1; client < CLIENTS; ++client) {
            body_was[client + 1] = ball_at(client)->get_global_position();
            visual_was[client + 1] = visual_at(client)->get_global_position();
            const netw::sim::Row *row = row_at(client);
            if (row != nullptr) {
                installed_was[client + 1] = row->installs.stats.installed;
                held_was[client + 1] = row->installs.stats.held;
                skipped_was[client + 1] = row->installs.stats.skipped;
            }
        }
    }

    void judge_flight() {
        const double expected = step_length();
        for (int client = 0; client < CLIENTS; ++client) {
            CopySeen &copy = seen(client);
            const netw::sim::Row *row = row_at(client);
            if (row == nullptr) {
                continue;
            }
            const double body_step = ball_at(client)->get_global_position()
                                         .distance_to(body_was[client + 1]);
            const double visual_step = visual_at(client)
                                           ->get_global_position()
                                           .distance_to(visual_was[client + 1]);
            copy.flight_frames += 1;
            copy.one_install_frames
                += row->installs.stats.installed - installed_was[client + 1]
                    == 1
                ? 1
                : 0;
            copy.one_row_frames
                += row->installs.stats.held - held_was[client + 1] == 1 ? 1 : 0;
            copy.integrated_frames += body_step > expected * 0.5 ? 1 : 0;
            copy.worst_body_step_error = MAX(
                copy.worst_body_step_error,
                Math::abs(body_step - expected)
            );
            copy.worst_visual_step_error = MAX(
                copy.worst_visual_step_error,
                Math::abs(visual_step - expected)
            );
            copy.newest_age = row->installs.stats.newest_age;
        }
    }

    void read_modes() {
        for (int client = -1; client < CLIENTS; ++client) {
            CopySeen &copy = seen(client);
            const netw::sim::Row *row = row_at(client);
            copy.mode = row != nullptr ? row->bodies.applied : Mode::NONE;
            copy.frozen = ball_at(client)->is_freeze_enabled();
        }
    }

    void stop() {
        ball_at(-1)->set_linear_velocity(Vector3());
        ball_at(-1)->set_angular_velocity(Vector3());
    }

    void read_rest() {
        ball_evidence().author_rested = ball_at(-1)->is_sleeping();
        for (int client = 0; client < CLIENTS; ++client) {
            seen(client).rested = ball_at(client)->is_sleeping();
        }
    }

    void drift() {
        RigidBody3D *ball = ball_at(DRIFTED);
        ball->set_global_position(ball->get_global_position() + DRIFT);
        ball->set_sleeping(true);
    }

    bool beat_reached_agreeing() const {
        const netw::sim::Row *row = row_at(AGREEING);
        return row != nullptr
            && row->installs.stats.skipped > skipped_was[AGREEING + 1];
    }

    void judge_heal() {
        CopySeen &drifted = seen(DRIFTED);
        const netw::sim::Row *row = row_at(DRIFTED);
        const Vector3 body = ball_at(DRIFTED)->get_global_position();
        const Vector3 author = ball_at(-1)->get_global_position();
        if (drifted.heal_ticks < 0 && body.distance_to(author) < 0.001) {
            drifted.heal_ticks = frame - drift_frame;
            drifted.heal_body_jump = body.distance_to(body_was[DRIFTED + 1]);
        }
        const double visual_step = visual_at(DRIFTED)
                                       ->get_global_position()
                                       .distance_to(visual_was[DRIFTED + 1]);
        drifted.worst_heal_visual_step
            = MAX(drifted.worst_heal_visual_step, visual_step);
        if (row != nullptr) {
            drifted.heal_installs
                += row->installs.stats.installed - installed_was[DRIFTED + 1];
        }

        CopySeen &agreeing = seen(AGREEING);
        const netw::sim::Row *quiet = row_at(AGREEING);
        agreeing.awake_frames += ball_at(AGREEING)->is_sleeping() ? 0 : 1;
        if (quiet != nullptr) {
            agreeing.rest_installs
                += quiet->installs.stats.installed - installed_was[AGREEING + 1];
            agreeing.rest_skips
                += quiet->installs.stats.skipped - skipped_was[AGREEING + 1];
        }
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || frame < 0) {
            return false;
        }
        if (frame == 0) {
            if (!open()) {
                close();
                frame = -1;
                return false;
            }
            ++frame;
            return true;
        }
        const bool unheard
            = drift_frame < 0 && frame > REST_FRAME + BEAT_WAIT_FRAMES;
        if (unheard || (drift_frame >= 0 && frame > drift_frame + WATCH_FRAMES)) {
            close();
            frame = -1;
            return false;
        }
        if (frame == STOP_FRAME) {
            stop();
        }
        if (frame == drift_frame) {
            drift();
        }
        stand->step_ticks(1);
        pump_display();
        if (frame > FLIGHT_OPEN && frame <= FLIGHT_CLOSE) {
            judge_flight();
        }
        if (frame == FLIGHT_CLOSE) {
            read_modes();
        }
        if (frame == REST_FRAME) {
            read_rest();
        }
        if (frame > REST_FRAME && drift_frame < 0 && beat_reached_agreeing()) {
            drift_frame = frame + 1;
            ball_evidence().beat_before_drift = true;
        }
        if (drift_frame >= 0 && frame > drift_frame) {
            judge_heal();
        }
        remember();
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(BeachBallScenario, beach_ball_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] a server-authored beach ball with replicas "
    "active runs on both clients of a three-peer session, installs each "
    "accepted row once at the display offset, and its visual follows the "
    "body without a step"
) {
    const BallEvidence &evidence = ball_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_EQ(int(evidence.copies[0].mode), int(Mode::AUTHORITY));
    for (int client = 0; client < CLIENTS; ++client) {
        const CopySeen &copy = evidence.copies[client + 1];
        NETW_CHECK_EQ(int(copy.mode), int(Mode::ACTIVE));
        CHECK_FALSE(copy.frozen);
        NETW_CHECK_EQ(copy.flight_frames, FLIGHT_CLOSE - FLIGHT_OPEN);
        NETW_CHECK_EQ(copy.one_row_frames, copy.flight_frames);
        NETW_CHECK_EQ(copy.one_install_frames, copy.flight_frames);
        NETW_CHECK_EQ(copy.newest_age, int64_t(DISPLAY_OFFSET));
        NETW_CHECK_EQ(copy.integrated_frames, copy.flight_frames);
        NETW_CHECK_LT(copy.worst_body_step_error, 0.01);
        NETW_CHECK_LT(copy.worst_visual_step_error, 0.01);
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a drift on a resting beach ball copy heals "
    "through the next heartbeat with one install, the visual absorbing the "
    "jump, while the copy that agrees skips the heartbeat and stays asleep"
) {
    const BallEvidence &evidence = ball_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    REQUIRE(evidence.author_rested);
    REQUIRE(evidence.copies[DRIFTED + 1].rested);
    REQUIRE(evidence.copies[AGREEING + 1].rested);
    REQUIRE(evidence.beat_before_drift);
    const CopySeen &drifted = evidence.copies[DRIFTED + 1];
    NETW_CHECK_GE(drifted.heal_ticks, HEARTBEAT_TICKS - DISPLAY_OFFSET - 3);
    NETW_CHECK_LE(drifted.heal_ticks, HEAL_BUDGET);
    NETW_CHECK_EQ(drifted.heal_installs, int64_t(1));
    NETW_CHECK_GT(drifted.heal_body_jump, 0.4);
    NETW_CHECK_LT(drifted.worst_heal_visual_step, 0.25);
    const CopySeen &agreeing = evidence.copies[AGREEING + 1];
    NETW_CHECK_EQ(agreeing.awake_frames, 0);
    NETW_CHECK_EQ(agreeing.rest_installs, int64_t(0));
    NETW_CHECK_GE(agreeing.rest_skips, int64_t(1));
}

} // namespace TestNetwSimBeachBallFrameLaws

#endif

#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/display/book.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/history.hpp"
#include "netw/display/runtime.hpp"

using namespace godot;

namespace TestNetwDisplayPumpRestFrameLaws {

using netw::NetwMultiplayer;

constexpr const char *MARKER_ID = "display_pump_rest_marker";
constexpr const char *CUBE_ID = "display_pump_rest_cube";

constexpr const char *MARKER_SOURCE = R"(extends Node3D

func _init() -> void:
	var visual := Node3D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	Netw.configure_property(self, &"position").broadcast() \
			.interpolate(NetwInterpolate.new().lerp().smooth(0.0))
)";

constexpr const char *CUBE_SOURCE = R"(extends RigidBody3D

func _init() -> void:
	gravity_scale = 0.0
	collision_layer = 0
	collision_mask = 0
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	add_child(shape)
	var visual := Node3D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	Netw.configure_property(self, &"position").broadcast() \
			.interpolate(NetwInterpolate.new().lerp().smooth(0.0))
)";

constexpr int CLIENT = 0;
constexpr int MARKERS = 60;
constexpr int TICKRATE = 30;
constexpr int SETTLE_FRAMES = 90;
constexpr int MOVE_FRAMES = 10;
constexpr int TAIL_FRAMES = 60;
const Vector3 STEP(0.02, 0.0, 0.0);
const Vector3 PUSH(1.0, 0.0, 0.0);

Script *marker_script = nullptr;
Script *cube_script = nullptr;

Node *build_with(Script *p_script, const Variant &p_name) {
    if (p_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(p_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

Node *build_marker(const Variant &p_name) {
    return build_with(marker_script, p_name);
}

Node *build_cube(const Variant &p_name) {
    return build_with(cube_script, p_name);
}

struct PumpRestEvidence {
    bool driven = false;
    bool seated = false;
    bool body_settled = false;
    bool marker_settled = false;
    int arrived_frame = -1;
    int64_t pumped_before_arrival = -1;
    int64_t pumped_at_arrival = -1;
    int left_frame = -1;
    double left_error = -1.0;
    bool asleep_when_left = false;
};

PumpRestEvidence &pump_rest_evidence() {
    static PumpRestEvidence evidence;
    return evidence;
}

class PumpRestScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> markers;
    Ref<Script> cubes;
    int routes[MARKERS] = {};
    int body_route = 0;
    int64_t newest_seen = -1;
    int64_t pumped_seen = -1;
    int slept_at = -1;

    netw::display::Runtime *runtime_at(int p_client, int p_route) const {
        NetwMultiplayer *session = stand->session(p_client);
        const RID entity = session->entity_from_route(p_route);
        return session->get_display_book()->runtime_of(entity);
    }

    RigidBody3D *host_body() const {
        return Object::cast_to<RigidBody3D>(stand->node_at(-1, body_route));
    }

    Node3D *host_marker() const {
        return Object::cast_to<Node3D>(stand->node_at(-1, routes[0]));
    }

    static int64_t newest_of(const netw::display::Runtime *p_runtime) {
        if (p_runtime == nullptr || p_runtime->channels().is_empty()) {
            return -1;
        }
        return p_runtime->channels()[0]->display_history().newest_tick();
    }

    int spawn(const char *p_id, int p_index, const Vector3 &p_at) {
        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(String(p_id) + String::num_int64(p_index));
        const RID made
            = server->spawn_registered(StringName(p_id), args, nullptr);
        Node3D *built = Object::cast_to<Node3D>(server->entity_get_node(made));
        if (built == nullptr) {
            return 0;
        }
        built->set_position(p_at);
        stand->arena_of(-1)->add_child(built);
        return int(server->entity_get_route(made));
    }

    bool open() {
        markers = netw_test::minted_script(MARKER_SOURCE);
        cubes = netw_test::minted_script(CUBE_SOURCE);
        if (markers.is_null() || cubes.is_null()) {
            return false;
        }
        marker_script = markers.ptr();
        cube_script = cubes.ptr();
        stand = new netw_test::SimStand(1);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(MARKER_ID), callable_mp_static(&build_marker));
        stand->teach(StringName(CUBE_ID), callable_mp_static(&build_cube));
        stand->mount();
        stand->arena();
        stand->arm(TICKRATE);
        for (int index = 0; index < MARKERS; ++index) {
            routes[index]
                = spawn(MARKER_ID, index, Vector3(0.0, 0.0, 2.0 * index));
        }
        body_route = spawn(CUBE_ID, 0, Vector3(0.0, 2.0, 0.0));
        stand->pump(8);
        for (int index = 0; index < MARKERS; ++index) {
            if (routes[index] == 0
                || stand->node_at(CLIENT, routes[index]) == nullptr) {
                return false;
            }
        }
        if (body_route == 0 || host_body() == nullptr) {
            return false;
        }
        pump_rest_evidence().seated = true;
        return true;
    }

    void start() {
        PumpRestEvidence &seen = pump_rest_evidence();
        const netw::display::Runtime *body = runtime_at(-1, body_route);
        const netw::display::Runtime *marker = runtime_at(CLIENT, routes[0]);
        seen.body_settled = body != nullptr && !body->get_pump_listed();
        seen.marker_settled = marker != nullptr && !marker->get_pump_listed();
        newest_seen = newest_of(marker);
        pumped_seen = marker != nullptr ? marker->get_pumped() : -1;
        host_body()->set_linear_velocity(PUSH);
    }

    void watch_arrival() {
        PumpRestEvidence &seen = pump_rest_evidence();
        const netw::display::Runtime *marker = runtime_at(CLIENT, routes[0]);
        if (seen.arrived_frame >= 0 || marker == nullptr) {
            return;
        }
        const int64_t newest = newest_of(marker);
        if (newest > newest_seen) {
            seen.arrived_frame = frame;
            seen.pumped_before_arrival = pumped_seen;
            seen.pumped_at_arrival = marker->get_pumped();
        }
        newest_seen = newest;
        pumped_seen = marker->get_pumped();
    }

    void put_to_sleep() {
        slept_at = frame;
        RigidBody3D *body = host_body();
        body->set_linear_velocity(Vector3());
        body->set_sleeping(true);
    }

    void watch_leaving() {
        PumpRestEvidence &seen = pump_rest_evidence();
        const netw::display::Runtime *runtime = runtime_at(-1, body_route);
        if (seen.left_frame >= 0 || runtime == nullptr
            || runtime->get_pump_listed() || runtime->channels().is_empty()) {
            return;
        }
        RigidBody3D *body = host_body();
        const Vector3 shown = runtime->channels()[0]->get_last_written();
        seen.left_frame = frame - slept_at;
        seen.left_error = double(shown.distance_to(body->get_position()));
        seen.asleep_when_left = body->is_sleeping();
    }

    void close() {
        pump_rest_evidence().driven = true;
        delete stand;
        stand = nullptr;
        marker_script = nullptr;
        cube_script = nullptr;
        markers = Ref<Script>();
        cubes = Ref<Script>();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            close();
            done = true;
            return false;
        }
        if (frame == SETTLE_FRAMES) {
            start();
        }
        if (frame >= SETTLE_FRAMES && frame < SETTLE_FRAMES + MOVE_FRAMES) {
            host_marker()->set_position(host_marker()->get_position() + STEP);
        }
        if (frame > SETTLE_FRAMES) {
            watch_arrival();
        }
        if (frame == SETTLE_FRAMES + MOVE_FRAMES) {
            put_to_sleep();
        }
        if (slept_at >= 0 && frame > slept_at) {
            watch_leaving();
        }
        if (frame == SETTLE_FRAMES + MOVE_FRAMES + TAIL_FRAMES) {
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(PumpRestScenario, display_pump_rest_scenario);

TEST_CASE(
    "[Networked][Display][Frame] a settled remote runtime resumes pumping on "
    "the frame a new sample lands in its history"
) {
    const PumpRestEvidence &evidence = pump_rest_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    REQUIRE(evidence.marker_settled);
    NETW_CHECK_GT(evidence.arrived_frame, SETTLE_FRAMES);
    NETW_CHECK_GT(evidence.pumped_at_arrival, evidence.pumped_before_arrival);
}

TEST_CASE(
    "[Networked][Display][Frame] an authored body put to sleep mid-motion "
    "keeps its runtime pumping until the display reaches the body's final "
    "pose, and only then leaves the pump"
) {
    const PumpRestEvidence &evidence = pump_rest_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.body_settled);
    REQUIRE(evidence.asleep_when_left);
    NETW_CHECK_GE(evidence.left_frame, 1);
    NETW_CHECK_LE(evidence.left_error, 0.001);
}

} // namespace TestNetwDisplayPumpRestFrameLaws

#endif

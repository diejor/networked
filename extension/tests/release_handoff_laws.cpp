#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/minted_script.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/session/frames.hpp"

namespace TestReleaseHandoff {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwPromise;

constexpr int TICKRATE = 30;
constexpr int OVERSIZED_NOTES = 1100;
constexpr const char *BOARD_ID = "release_handoff_board";
constexpr const char *CUBE_ID = "release_handoff_cube";
constexpr const char *CAR_ID = "release_handoff_car";

constexpr const char *BOARD_SOURCE = R"(extends Node2D

var strokes := 0

func _init() -> void:
	var e := Netw.configure_entity(self)
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	Netw.configure_property(self, &"strokes").broadcast().on_spawn()
)";

constexpr const char *NOTED_BOARD_SOURCE = R"(extends Node2D

var strokes := 0
var notes := PackedByteArray()

func _init() -> void:
	var e := Netw.configure_entity(self)
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	Netw.configure_property(self, &"strokes").broadcast().on_spawn()
	Netw.configure_property(self, &"notes").broadcast()
)";

constexpr const char *CUBE_SOURCE = R"(extends RigidBody3D

const LIMIT := 64.0

var held_by := 0

func _init() -> void:
	freeze = true
	var e := Netw.configure_entity(self)
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	Netw.configure_property(self, &"held_by").broadcast()
	Netw.configure_property(self, &"position").broadcast().heartbeat(60) \
			.quantize(NetwQuantizeScalar.new().bits(20).limits(-LIMIT, LIMIT))
	Netw.configure_property(self, &"quaternion").broadcast() \
			.quantize(NetwQuantizeQuaternion.new().bits(16))
	Netw.configure_property(self, &"linear_velocity").broadcast() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-32.0, 32.0))
	Netw.configure_property(self, &"angular_velocity").broadcast() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-32.0, 32.0))
	Netw.configure_property(self, &"sleeping").broadcast()
)";

constexpr const char *CAR_SOURCE = R"(extends Node3D

var car_position := Vector3()
var car_rotation := Quaternion()
var car_linear_velocity := Vector3()
var car_angular_velocity := Vector3()
var car_sleeping := false
var spring_fl := 0.0
var spring_fr := 0.0
var spring_rl := 0.0
var spring_rr := 0.0
var steer_angle := 0.0

func _init() -> void:
	var e := Netw.configure_entity(self)
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	Netw.configure_property(self, &"car_position").broadcast() \
			.quantize(NetwQuantizeScalar.new().bits(19).limits(-128.0, 128.0))
	Netw.configure_property(self, &"car_rotation").broadcast() \
			.quantize(NetwQuantizeQuaternion.new().bits(16))
	Netw.configure_property(self, &"car_linear_velocity").broadcast() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-128.0, 128.0))
	Netw.configure_property(self, &"car_angular_velocity").broadcast() \
			.quantize(NetwQuantizeScalar.new().bits(16).limits(-64.0, 64.0))
	Netw.configure_property(self, &"car_sleeping").broadcast()
	for spring: StringName in [&"spring_fl", &"spring_fr", &"spring_rl", &"spring_rr"]:
		Netw.configure_property(self, spring).broadcast() \
				.quantize(NetwQuantizeScalar.new().bits(12).limits(0.0, 1.0))
	Netw.configure_property(self, &"steer_angle").broadcast() \
			.quantize(NetwQuantizeAngle.new().bits(12).centered())
)";

Script *building = nullptr;
int64_t first_strokes = -1;

Node *build(const Variant &p_name) {
    if (building == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(building->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
        if (first_strokes >= 0) {
            made->set("strokes", first_strokes);
        }
    }
    return made;
}

struct Stand {
    LoopbackRig rig;
    Ref<Script> script;
    int route = 0;

    Stand(const char *p_source, const char *p_id, int64_t p_strokes = -1)
        : rig(2) {
        script = minted_script(p_source);
        building = script.ptr();
        first_strokes = p_strokes;
        rig.mount();
        flow_clocks(rig, TICKRATE);
        Node *arena = rig.mirror_child("Arena");
        Array types;
        types.push_back(int(Variant::STRING));
        Array args;
        args.push_back(String(p_id));
        route = rig.spawn_registered(
            StringName(p_id),
            callable_mp_static(&build),
            args,
            types,
            arena
        );
        rig.pump(4);
    }

    ~Stand() {
        building = nullptr;
        first_strokes = -1;
    }

    Node *node(int p_client) const {
        return rig.route_node(route, p_client);
    }

    Ref<NetwEntity> entity(int p_client) const {
        Node *held = node(p_client);
        return held == nullptr ? Ref<NetwEntity>() : NetwEntity::of(held);
    }

    int64_t strokes(int p_client) const {
        Node *held = node(p_client);
        return held == nullptr ? -1 : int64_t(held->get("strokes"));
    }

    void draw(int p_client, int64_t p_strokes) const {
        Node *held = node(p_client);
        if (held != nullptr) {
            held->set("strokes", p_strokes);
        }
    }

    void author_with(int p_client, int64_t p_strokes) {
        entity(-1)->set_controller(rig.peer_id(p_client));
        rig.step_ticks(4);
        draw(p_client, p_strokes);
        rig.step_ticks(6);
    }
};

void quiet(int64_t, const String &) {
}

Ref<NetwPromise> quietly(const Ref<NetwPromise> &p_promise) {
    p_promise->catch_error(callable_mp_static(&quiet));
    return p_promise;
}

TEST_CASE(
    "[Networked][Control][SceneTree] RH1 a release handed to a third peer "
    "carries the releaser's newest board, and the successor authors from it"
) {
    Stand stand(BOARD_SOURCE, BOARD_ID);
    const int c = stand.rig.peer_id(1);
    stand.author_with(0, 3);
    NETW_REQUIRE_EQ(stand.strokes(1), 3);

    stand.draw(0, 8);
    const Ref<NetwPromise> release = stand.entity(0)->release_authority(c);
    CHECK_FALSE(release->get_is_settled());
    stand.rig.step_ticks(8);

    CHECK(release->get_is_completed());
    for (int client = -1; client < 2; ++client) {
        NETW_FORMAT_INT(peer_text, client);
        CAPTURE(peer_text);
        NETW_CHECK_EQ(stand.entity(client)->get_controller(), int64_t(c));
        NETW_CHECK_EQ(stand.strokes(client), 8);
    }

    stand.draw(1, 9);
    stand.rig.step_ticks(8);
    for (int client = -1; client < 2; ++client) {
        NETW_FORMAT_INT(peer_text, client);
        CAPTURE(peer_text);
        NETW_CHECK_EQ(stand.strokes(client), 9);
    }
}

TEST_CASE(
    "[Networked][Control][SceneTree] RH2 a release naming a successor that "
    "holds no copy is refused before anything is sent"
) {
    Stand stand(BOARD_SOURCE, BOARD_ID);
    const int a = stand.rig.peer_id(0);
    stand.author_with(0, 3);

    const Ref<NetwPromise> release
        = quietly(stand.entity(0)->release_authority(999));
    CHECK(release->get_is_failed());
    NETW_CHECK_EQ(int(release->get_code()), int(ERR_UNAVAILABLE));
    stand.rig.step_ticks(6);
    NETW_CHECK_EQ(stand.entity(-1)->get_controller(), int64_t(a));
    NETW_CHECK_EQ(stand.entity(0)->get_controller(), int64_t(a));
}

TEST_CASE(
    "[Networked][Control][SceneTree] RH3 a final state past the control "
    "budget is not sent, and the release still hands the entity on"
) {
    Stand stand(NOTED_BOARD_SOURCE, BOARD_ID, 3);
    stand.entity(-1)->set_controller(stand.rig.peer_id(0));
    stand.rig.step_ticks(4);
    NETW_REQUIRE_EQ(stand.strokes(-1), 3);

    PackedByteArray notes;
    notes.resize(OVERSIZED_NOTES);
    stand.node(0)->set("notes", notes);
    stand.draw(0, 8);
    const PackedByteArray image
        = stand.entity(0)->final_image_under_test(0);
    CHECK(image.is_empty());
    const Ref<NetwPromise> release = stand.entity(0)->release_authority();
    stand.rig.step_ticks(8);

    CHECK(release->get_is_completed());
    for (int client = -1; client < 2; ++client) {
        NETW_FORMAT_INT(peer_text, client);
        CAPTURE(peer_text);
        NETW_CHECK_EQ(stand.entity(client)->get_controller(), int64_t(0));
    }
    NETW_CHECK_EQ(stand.strokes(-1), 3);
    NETW_CHECK_EQ(stand.strokes(1), 3);
}

TEST_CASE(
    "[Networked][Control][SceneTree] RH4 the final state of the "
    "playground's cube row and of rocket league's car row sits well inside "
    "the control budget"
) {
    int64_t cube = 0;
    int64_t car = 0;
    {
        Stand stand(CUBE_SOURCE, CUBE_ID);
        cube = stand.entity(-1)->final_image_under_test(12345).size();
    }
    {
        Stand stand(CAR_SOURCE, CAR_ID);
        car = stand.entity(-1)->final_image_under_test(12345).size();
    }
    NETW_CHECK_EQ(cube, int64_t(43));
    NETW_CHECK_EQ(car, int64_t(42));
    NETW_CHECK_LT(car * 16, int64_t(netw::session::CONTROL_FINAL_STATE_CAP));
}

} // namespace TestReleaseHandoff

#endif

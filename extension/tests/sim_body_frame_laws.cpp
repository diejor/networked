#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/collision_shape.hpp"
#include "godot/engine.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/sim/body.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace TestNetwSimBodyFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::sim::Bodies;
using netw::sim::Mode;

constexpr double FLOOR_Y = 200.0;
constexpr double HALF_EXTENT = 8.0;
constexpr int SETTLE_FRAMES = 90;

struct RestEvidence {
    bool driven = false;
    Vector2 spawn;
    Vector2 rest;
    Vector2 one_step;
    Vector2 three_steps;
};

RestEvidence &rest_evidence() {
    static RestEvidence evidence;
    return evidence;
}

class RestScenario final : public netw_test::FrameScenario {
    int frame = 0;
    Node2D *arena = nullptr;
    RigidBody2D *ball = nullptr;

    void open() {
        arena = memnew(Node2D);
        StaticBody2D *floor_body = memnew(StaticBody2D);
        floor_body->set_position(Vector2(0.0, real_t(FLOOR_Y)));
        CollisionShape2D *floor_shape = memnew(CollisionShape2D);
        Ref<WorldBoundaryShape2D> boundary;
        boundary.instantiate();
        boundary->set_normal(Vector2(0.0, -1.0));
        floor_shape->set_shape(boundary);
        floor_body->add_child(floor_shape);
        arena->add_child(floor_body);

        ball = memnew(RigidBody2D);
        ball->set_contact_monitor(true);
        ball->set_max_contacts_reported(4);
        CollisionShape2D *ball_shape = memnew(CollisionShape2D);
        Ref<RectangleShape2D> box;
        box.instantiate();
        box->set_size(
            Vector2(real_t(HALF_EXTENT * 2.0), real_t(HALF_EXTENT * 2.0))
        );
        ball_shape->set_shape(box);
        ball->add_child(ball_shape);
        arena->add_child(ball);
        netw::gd::scene_root()->add_child(arena);
        rest_evidence().spawn = ball->get_global_position();
    }

    void close() {
        rest_evidence().driven = true;
        netw::gd::scene_root()->remove_child(arena);
        memdelete(arena);
        arena = nullptr;
        ball = nullptr;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || frame < 0) {
            return false;
        }
        if (frame == 0) {
            open();
        } else if (frame == SETTLE_FRAMES) {
            rest_evidence().rest = ball->get_global_position();
            Bodies bodies;
            netw::sim::record_authored(
                bodies,
                ball,
                Vector<NodePath>(),
                NodePath()
            );
            netw::sim::transition(bodies, Mode::PROXY);
        } else if (frame == SETTLE_FRAMES + 1) {
            rest_evidence().one_step = ball->get_global_position();
        } else if (frame == SETTLE_FRAMES + 3) {
            rest_evidence().three_steps = ball->get_global_position();
            close();
            frame = -1;
            return false;
        }
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(RestScenario, rest_scenario);

constexpr const char *CAR_BODY = R"(extends Node3D

var sphere: RigidBody3D
var display_position: Vector3

var sphere_position: Vector3:
	get:
		return sphere.position
	set(value):
		sphere.position = value

var sphere_linear_velocity: Vector3:
	get:
		return sphere.linear_velocity
	set(value):
		sphere.linear_velocity = value

func _init() -> void:
	sphere = RigidBody3D.new()
	sphere.name = &"Sphere"
	sphere.gravity_scale = 0.0
	sphere.collision_layer = 0
	sphere.collision_mask = 0
	sphere.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	sphere.linear_damp = 0.0
	sphere.can_sleep = false
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	sphere.add_child(shape)
	add_child(sphere)
)";

constexpr const char *INTERPOLATED_POSE
    = "\tNetw.configure_property(self, &\"sphere_position\").broadcast()"
      ".interpolate(NetwInterpolate.new().lerp().to(&\"display_position\"))\n";

constexpr const char *RAW_POSE
    = "\tNetw.configure_property(self, &\"sphere_position\").broadcast()\n";

constexpr const char *CAR_VELOCITY
    = "\tNetw.configure_property(self, &\"sphere_linear_velocity\")"
      ".broadcast()\n";

enum Car {
    RACING,
    UNDRAWN,
    CARS,
};

const char *CAR_IDS[CARS] = {"sim_racing_car", "sim_undrawn_car"};
const Vector3 DRIVE(3.0, 0.0, 0.0);
constexpr int CLIENTS = 2;
constexpr int TICKRATE = 30;

Script *car_scripts[CARS] = {nullptr, nullptr};

Node *build(Car p_car, const Variant &p_name) {
    if (car_scripts[p_car] == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(car_scripts[p_car]->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

Node *build_racing(const Variant &p_name) {
    return build(RACING, p_name);
}

Node *build_undrawn(const Variant &p_name) {
    return build(UNDRAWN, p_name);
}

Ref<Script> mint_car(Car p_car) {
    const String pose = p_car == RACING ? INTERPOLATED_POSE : RAW_POSE;
    const String source = String(CAR_BODY) + pose + String(CAR_VELOCITY);
    return netw_test::minted_script(source.utf8().get_data());
}

struct Copy {
    bool frozen = false;
    int freeze_mode = -1;
    Vector3 position;
    Vector3 velocity;
    Mode mode = Mode::NONE;
};

struct TransferEvidence {
    bool seated = false;
    int held_bodies = 0;
    int64_t controller_before = 0;
    int64_t controller_after = 0;
    Copy before_first;
    Copy before_server;
    Copy before_second;
    Copy pass_first;
    Copy pass_second;
    Copy step_first;
    Copy step_second;
    Copy active_server;
};

struct RaceEvidence {
    bool driven = false;
    int64_t first_controller = 0;
    int64_t second_controller = 0;
    TransferEvidence cars[CARS];
};

RaceEvidence &race_evidence() {
    static RaceEvidence evidence;
    return evidence;
}

class TransferScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int routes[CARS] = {0, 0};
    netw_test::SimStand *stand = nullptr;
    Ref<Script> scripts[CARS];

    Node *car_at(int p_car, int p_client) const {
        return stand->node_at(p_client, routes[p_car]);
    }

    RigidBody3D *sphere_at(int p_car, int p_client) const {
        Node *car = car_at(p_car, p_client);
        return car == nullptr
            ? nullptr
            : Object::cast_to<RigidBody3D>(car->get_node_or_null("Sphere"));
    }

    Ref<NetwEntity> entity_at(int p_car, int p_client) const {
        return NetwEntity::of(car_at(p_car, p_client));
    }

    Copy read(int p_car, int p_client) const {
        Copy copy;
        RigidBody3D *sphere = sphere_at(p_car, p_client);
        if (sphere == nullptr) {
            return copy;
        }
        copy.frozen = sphere->is_freeze_enabled();
        copy.freeze_mode = int(sphere->get_freeze_mode());
        copy.position = sphere->get_global_position();
        copy.velocity = sphere->get_linear_velocity();
        const netw::sim::Row *row = stand->session(p_client)->sim_row_of(
            entity_at(p_car, p_client)->get_rid_handle()
        );
        if (row != nullptr) {
            copy.mode = row->bodies.applied;
        }
        return copy;
    }

    void declare_sphere(int p_car, int p_client) {
        NetwMultiplayer *session = stand->session(p_client);
        const Ref<NetwEntity> entity = entity_at(p_car, p_client);
        const RID rid = entity->get_rid_handle();
        session->sim_row(rid).declaration.bodies.push_back(NodePath("Sphere"));
        session->sim_settle_body(entity);
        const netw::sim::Row *row = session->sim_row_of(rid);
        if (row != nullptr) {
            race_evidence().cars[p_car].held_bodies
                += int(row->bodies.held.size());
        }
    }

    bool seat(int p_car, Node *p_arena) {
        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(String(CAR_IDS[p_car]));
        const RID made = server->spawn_registered(
            StringName(CAR_IDS[p_car]),
            args,
            server->peer_get_player(stand->peer_id(0)).ptr()
        );
        Node *car = server->entity_get_node(made);
        if (car == nullptr) {
            return false;
        }
        p_arena->add_child(car);
        stand->pump(8);
        routes[p_car] = int(server->entity_get_route(made));
        return car_at(p_car, 0) != nullptr && car_at(p_car, 1) != nullptr
            && sphere_at(p_car, -1) != nullptr;
    }

    bool open() {
        for (int car = 0; car < CARS; ++car) {
            scripts[car] = mint_car(Car(car));
            if (scripts[car].is_null()) {
                return false;
            }
            car_scripts[car] = scripts[car].ptr();
        }
        stand = new netw_test::SimStand(CLIENTS);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(
            StringName(CAR_IDS[RACING]),
            callable_mp_static(&build_racing)
        );
        stand->teach(
            StringName(CAR_IDS[UNDRAWN]),
            callable_mp_static(&build_undrawn)
        );
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        stand->session(0)->session_submit_join(StringName("first"), Array());
        stand->pump(4);
        RaceEvidence &evidence = race_evidence();
        evidence.first_controller = stand->peer_id(0);
        evidence.second_controller = stand->peer_id(1);
        for (int car = 0; car < CARS; ++car) {
            evidence.cars[car].seated = seat(car, arena);
            if (!evidence.cars[car].seated) {
                return false;
            }
        }
        for (int car = 0; car < CARS; ++car) {
            declare_sphere(car, -1);
            declare_sphere(car, 0);
            declare_sphere(car, 1);
            sphere_at(car, 0)->set_linear_velocity(DRIVE);
        }
        return true;
    }

    void close() {
        race_evidence().driven = true;
        delete stand;
        stand = nullptr;
        for (int car = 0; car < CARS; ++car) {
            car_scripts[car] = nullptr;
            scripts[car] = Ref<Script>();
        }
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || frame < 0) {
            return false;
        }
        RaceEvidence &evidence = race_evidence();
        if (frame == 0) {
            if (!open()) {
                close();
                frame = -1;
                return false;
            }
        } else if (frame <= 6) {
            stand->step_ticks(1);
        } else if (frame == 7) {
            stand->pump(6);
            for (int car = 0; car < CARS; ++car) {
                TransferEvidence &seen = evidence.cars[car];
                seen.before_first = read(car, 0);
                seen.before_server = read(car, -1);
                seen.before_second = read(car, 1);
                seen.controller_before = entity_at(car, 1)->get_controller();
            }
        } else if (frame == 8) {
            for (int car = 0; car < CARS; ++car) {
                entity_at(car, -1)->grant_control(stand->peer_id(1));
            }
            stand->pump(6);
            for (int car = 0; car < CARS; ++car) {
                TransferEvidence &seen = evidence.cars[car];
                seen.controller_after = entity_at(car, 0)->get_controller();
                seen.pass_first = read(car, 0);
                seen.pass_second = read(car, 1);
            }
        } else if (frame == 9) {
            NetwMultiplayer *server = stand->session(-1);
            for (int car = 0; car < CARS; ++car) {
                TransferEvidence &seen = evidence.cars[car];
                seen.step_first = read(car, 0);
                seen.step_second = read(car, 1);
                const Ref<NetwEntity> entity = entity_at(car, -1);
                server->sim_row(entity->get_rid_handle()).declaration.replicas
                    = netw::sim::Replicas::ACTIVE;
                server->sim_settle_body(entity);
            }
        } else if (frame == 10) {
            for (int car = 0; car < CARS; ++car) {
                evidence.cars[car].active_server = read(car, -1);
            }
            close();
            frame = -1;
            return false;
        }
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(TransferScenario, transfer_scenario);

double step_length() {
    return DRIVE.length()
        / double(Engine::get_singleton()->get_physics_ticks_per_second());
}

bool frozen_kinematic(const Copy &p_copy) {
    return p_copy.frozen
        && p_copy.freeze_mode == int(RigidBody3D::FREEZE_MODE_KINEMATIC);
}

TEST_CASE(
    "[Networked][Sim][Frame] a contact-monitored body frozen as a proxy "
    "holds where it rests through the next steps"
) {
    const RestEvidence &evidence = rest_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_GT(evidence.rest.y, evidence.spawn.y + 100.0);
    NETW_CHECK_CLOSE(evidence.one_step.distance_to(evidence.rest), 0.0, 1.0);
    NETW_CHECK_CLOSE(
        evidence.three_steps.distance_to(evidence.rest),
        0.0,
        1.0
    );
}

TEST_CASE(
    "[Networked][Sim][Frame] a racing-shaped car freezes its child sphere "
    "on every copy its controller does not run"
) {
    const RaceEvidence &race = race_evidence();
    REQUIRE(race.driven);
    for (int car = 0; car < CARS; ++car) {
        NETW_FORMAT_TEXT(car_id, CAR_IDS[car]);
        CAPTURE(car_id);
        const TransferEvidence &evidence = race.cars[car];
        REQUIRE(evidence.seated);
        NETW_CHECK_EQ(evidence.held_bodies, 3);
        NETW_CHECK_EQ(evidence.controller_before, race.first_controller);
        NETW_CHECK_EQ(int(evidence.before_first.mode), int(Mode::AUTHORITY));
        CHECK_FALSE(evidence.before_first.frozen);
        NETW_CHECK_EQ(int(evidence.before_server.mode), int(Mode::PROXY));
        CHECK(frozen_kinematic(evidence.before_server));
        NETW_CHECK_EQ(int(evidence.before_second.mode), int(Mode::PROXY));
        CHECK(frozen_kinematic(evidence.before_second));
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a control transfer thaws the new controller's "
    "sphere and freezes the old one's in the pass that moved control, "
    "whether or not a display draws the car"
) {
    const RaceEvidence &race = race_evidence();
    REQUIRE(race.driven);
    for (int car = 0; car < CARS; ++car) {
        NETW_FORMAT_TEXT(car_id, CAR_IDS[car]);
        CAPTURE(car_id);
        const TransferEvidence &evidence = race.cars[car];
        REQUIRE(evidence.seated);
        NETW_CHECK_EQ(evidence.controller_after, race.second_controller);
        NETW_CHECK_EQ(int(evidence.pass_second.mode), int(Mode::AUTHORITY));
        CHECK_FALSE(evidence.pass_second.frozen);
        NETW_CHECK_EQ(int(evidence.pass_first.mode), int(Mode::PROXY));
        CHECK(frozen_kinematic(evidence.pass_first));
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] both spheres hold their mode through the next "
    "step, the new controller's running on at the newest sample's velocity "
    "and the old one's standing still"
) {
    const RaceEvidence &race = race_evidence();
    REQUIRE(race.driven);
    for (int car = 0; car < CARS; ++car) {
        NETW_FORMAT_TEXT(car_id, CAR_IDS[car]);
        CAPTURE(car_id);
        const TransferEvidence &evidence = race.cars[car];
        REQUIRE(evidence.seated);
        CHECK_FALSE(evidence.step_second.frozen);
        CHECK(evidence.step_second.velocity.is_equal_approx(DRIVE));
        NETW_CHECK_CLOSE(
            evidence.step_second.position.distance_to(
                evidence.pass_second.position
            ),
            step_length(),
            0.01
        );
        CHECK(frozen_kinematic(evidence.step_first));
        CHECK(evidence.step_first.position.is_equal_approx(
            evidence.pass_first.position
        ));
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a velocity installed after a proxy thaws into "
    "an active copy survives the next step"
) {
    const RaceEvidence &race = race_evidence();
    REQUIRE(race.driven);
    for (int car = 0; car < CARS; ++car) {
        NETW_FORMAT_TEXT(car_id, CAR_IDS[car]);
        CAPTURE(car_id);
        const TransferEvidence &evidence = race.cars[car];
        REQUIRE(evidence.seated);
        NETW_CHECK_EQ(int(evidence.active_server.mode), int(Mode::ACTIVE));
        CHECK_FALSE(evidence.active_server.frozen);
        CHECK(evidence.active_server.velocity.is_equal_approx(DRIVE));
    }
}

} // namespace TestNetwSimBodyFrameLaws

#endif

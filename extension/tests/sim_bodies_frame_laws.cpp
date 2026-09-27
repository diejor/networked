#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/engine.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/simulation_handle.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace TestNetwSimBodiesFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwSimulationHandle;
using netw::sim::Mode;

constexpr const char *ROOT_SOURCE = R"(extends RigidBody3D

func _init() -> void:
	gravity_scale = 0.0
	collision_layer = 0
	collision_mask = 0
	linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	linear_damp = 0.0
	can_sleep = false
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	add_child(shape)
	var e := Netw.configure_entity(self)
	e.simulation.bodies = [^"."]
	Netw.configure_property(self, &"position").broadcast()
	Netw.configure_property(self, &"linear_velocity").broadcast()
)";

constexpr const char *CHILD_SOURCE = R"(extends Node3D

var sphere: RigidBody3D

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

func loose(named: StringName) -> RigidBody3D:
	var body := RigidBody3D.new()
	body.name = named
	body.gravity_scale = 0.0
	body.collision_layer = 0
	body.collision_mask = 0
	body.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	body.linear_damp = 0.0
	body.can_sleep = false
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	body.add_child(shape)
	add_child(body)
	return body

func _init() -> void:
	sphere = loose(&"Sphere")
	var e := Netw.configure_entity(self)
	e.simulation.bodies = [^"Sphere"]
	Netw.configure_property(self, &"sphere_position").broadcast()
	Netw.configure_property(self, &"sphere_linear_velocity").broadcast()
)";

constexpr const char *TRUCK_SOURCE = R"(extends Node3D

var cab: RigidBody3D
var trailer: RigidBody3D

var cab_position: Vector3:
	get:
		return cab.position
	set(value):
		cab.position = value

var cab_velocity: Vector3:
	get:
		return cab.linear_velocity
	set(value):
		cab.linear_velocity = value

var trailer_position: Vector3:
	get:
		return trailer.position
	set(value):
		trailer.position = value

var trailer_velocity: Vector3:
	get:
		return trailer.linear_velocity
	set(value):
		trailer.linear_velocity = value

func loose(named: StringName) -> RigidBody3D:
	var body := RigidBody3D.new()
	body.name = named
	body.gravity_scale = 0.0
	body.collision_layer = 0
	body.collision_mask = 0
	body.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	body.linear_damp = 0.0
	body.can_sleep = false
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	body.add_child(shape)
	add_child(body)
	return body

func _init() -> void:
	cab = loose(&"Cab")
	trailer = loose(&"Trailer")
	trailer.position = Vector3(0.0, 0.0, -2.0)
	var hitch := PinJoint3D.new()
	hitch.name = &"Hitch"
	hitch.position = Vector3(0.0, 0.0, -1.0)
	add_child(hitch)
	hitch.node_a = ^"../Cab"
	hitch.node_b = ^"../Trailer"
	var e := Netw.configure_entity(self)
	e.simulation.bodies = [^"Cab", ^"Trailer"]
	Netw.configure_property(self, &"cab_position").broadcast()
	Netw.configure_property(self, &"cab_velocity").broadcast()
	Netw.configure_property(self, &"trailer_position").broadcast()
	Netw.configure_property(self, &"trailer_velocity").broadcast()
)";

constexpr const char *EXACT_LINE
    = "\te.simulation.restore = NetwSimulationHandle.RESTORE_EXACT\n";

enum Shape {
    ROOT,
    CHILD,
    TRUCK,
    SHAPES,
};

constexpr int PHASES = 2;
constexpr int KINDS = SHAPES * PHASES;
constexpr int MOST_BODIES = 2;
constexpr int AUTHOR = 0;
constexpr int CLIENTS = 2;
constexpr int OBSERVERS = 2;
const int OBSERVER_AT[OBSERVERS] = {-1, 1};
constexpr int TICKRATE = 60;
constexpr int DISPLAY_OFFSET = 2;
const Vector3 DRIVE(3.0, 0.0, 0.0);
const Vector3 DRIFT(0.0, 0.0, 0.5);

constexpr int PROXY_FRAME = 20;
constexpr int FLIP_FRAME = PROXY_FRAME + 1;
constexpr int WINDOW_OPEN = FLIP_FRAME + DISPLAY_OFFSET + 6;
constexpr int WINDOW_CLOSE = WINDOW_OPEN + 15;
constexpr int DRIFT_FRAME = WINDOW_CLOSE + 2;
constexpr int HEAL_FRAME = DRIFT_FRAME + DISPLAY_OFFSET + 4;
constexpr int LAST_FRAME = HEAL_FRAME + 6;

const char *SHAPE_NAMES[SHAPES] = {"a rigid root", "a child body", "a truck"};
const char *PHASE_NAMES[PHASES] = {"buffered", "exact"};
const int BODY_COUNT[SHAPES] = {1, 1, 2};
const char *BODY_PATHS[SHAPES][MOST_BODIES]
    = {{".", ""}, {"Sphere", ""}, {"Cab", "Trailer"}};
const char *SOURCES[SHAPES] = {ROOT_SOURCE, CHILD_SOURCE, TRUCK_SOURCE};

Script *kind_scripts[KINDS] = {};

String kind_id(int p_kind) {
    return vformat("sim_bodies_%d", p_kind);
}

Node *build_kind(int p_kind, const Variant &p_name) {
    if (kind_scripts[p_kind] == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(kind_scripts[p_kind]->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

Node *build_0(const Variant &p_name) {
    return build_kind(0, p_name);
}
Node *build_1(const Variant &p_name) {
    return build_kind(1, p_name);
}
Node *build_2(const Variant &p_name) {
    return build_kind(2, p_name);
}
Node *build_3(const Variant &p_name) {
    return build_kind(3, p_name);
}
Node *build_4(const Variant &p_name) {
    return build_kind(4, p_name);
}
Node *build_5(const Variant &p_name) {
    return build_kind(5, p_name);
}

Node *(*const BUILDERS[KINDS])(const Variant &)
    = {build_0, build_1, build_2, build_3, build_4, build_5};

struct BodySeen {
    bool proxy_frozen = false;
    bool active_frozen = true;
    int stepped = 0;
    int integrated = 0;
    double worst_step_error = 0.0;
    double drift_left = 1.0;
    double healed_advance = 0.0;
};

struct CopySeen {
    Mode proxy_mode = Mode::NONE;
    Mode active_mode = Mode::NONE;
    int64_t installed = 0;
    int64_t installed_after_drift = 0;
    int64_t newest_age = -1;
    int64_t youngest_age = -1;
    int64_t oldest_age = -1;
    BodySeen bodies[MOST_BODIES];
};

struct BodiesEvidence {
    bool driven = false;
    bool seated = false;
    CopySeen copies[KINDS][OBSERVERS];
};

BodiesEvidence &bodies_evidence() {
    static BodiesEvidence evidence;
    return evidence;
}

Shape shape_of(int p_kind) {
    return Shape(p_kind % SHAPES);
}

double step_length() {
    return DRIVE.length()
        / double(Engine::get_singleton()->get_physics_ticks_per_second());
}

class BodiesScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int routes[KINDS] = {};
    netw_test::SimStand *stand = nullptr;
    Ref<Script> scripts[KINDS];
    Vector3 ended[KINDS][OBSERVERS][MOST_BODIES];
    int64_t installed_at_drift[KINDS][OBSERVERS] = {};

    Node *node_at(int p_kind, int p_client) const {
        return stand->node_at(p_client, routes[p_kind]);
    }

    RigidBody3D *body_at(int p_kind, int p_client, int p_body) const {
        Node *owner = node_at(p_kind, p_client);
        if (owner == nullptr) {
            return nullptr;
        }
        return Object::cast_to<RigidBody3D>(owner->get_node_or_null(
            NodePath(BODY_PATHS[shape_of(p_kind)][p_body])
        ));
    }

    Ref<NetwEntity> entity_at(int p_kind, int p_client) const {
        return NetwEntity::of(node_at(p_kind, p_client));
    }

    const netw::sim::Row *row_at(int p_kind, int p_client) const {
        const Ref<NetwEntity> entity = entity_at(p_kind, p_client);
        return entity.is_null()
            ? nullptr
            : stand->session(p_client)->sim_row_of(entity->get_rid_handle());
    }

    CopySeen &seen(int p_kind, int p_observer) const {
        return bodies_evidence().copies[p_kind][p_observer];
    }

    bool seat(int p_kind, Node *p_arena) {
        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(kind_id(p_kind));
        const Ref<netw::NetwPlayer> owner
            = server->peer_get_player(stand->peer_id(AUTHOR));
        const RID made = server->spawn_registered(
            StringName(kind_id(p_kind)),
            args,
            owner.ptr()
        );
        Node *built = server->entity_get_node(made);
        if (built == nullptr) {
            return false;
        }
        p_arena->add_child(built);
        stand->pump(8);
        routes[p_kind] = int(server->entity_get_route(made));
        for (int client = -1; client < CLIENTS; ++client) {
            for (int body = 0; body < BODY_COUNT[shape_of(p_kind)]; ++body) {
                if (body_at(p_kind, client, body) == nullptr) {
                    return false;
                }
            }
        }
        return true;
    }

    bool open() {
        for (int kind = 0; kind < KINDS; ++kind) {
            String source = SOURCES[shape_of(kind)];
            if (kind >= SHAPES) {
                source += EXACT_LINE;
            }
            scripts[kind] = netw_test::minted_script(source.utf8().get_data());
            if (scripts[kind].is_null()) {
                return false;
            }
            kind_scripts[kind] = scripts[kind].ptr();
        }
        stand = new netw_test::SimStand(CLIENTS);
        if (!stand->ready()) {
            return false;
        }
        for (int kind = 0; kind < KINDS; ++kind) {
            stand->teach(
                StringName(kind_id(kind)),
                callable_mp_static(BUILDERS[kind])
            );
        }
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE, DISPLAY_OFFSET);
        stand->session(0)->session_submit_join(StringName("first"), Array());
        stand->pump(4);
        for (int kind = 0; kind < KINDS; ++kind) {
            if (!seat(kind, arena)) {
                return false;
            }
        }
        for (int kind = 0; kind < KINDS; ++kind) {
            for (int body = 0; body < BODY_COUNT[shape_of(kind)]; ++body) {
                body_at(kind, AUTHOR, body)->set_linear_velocity(DRIVE);
            }
        }
        bodies_evidence().seated = true;
        return true;
    }

    void close() {
        bodies_evidence().driven = true;
        delete stand;
        stand = nullptr;
        for (int kind = 0; kind < KINDS; ++kind) {
            kind_scripts[kind] = nullptr;
            scripts[kind] = Ref<Script>();
        }
    }

    template <typename F> void each_body(F p_visit) {
        for (int kind = 0; kind < KINDS; ++kind) {
            for (int at = 0; at < OBSERVERS; ++at) {
                for (int body = 0; body < BODY_COUNT[shape_of(kind)]; ++body) {
                    p_visit(
                        kind,
                        at,
                        body,
                        body_at(kind, OBSERVER_AT[at], body)
                    );
                }
            }
        }
    }

    void read_proxy() {
        each_body(
            [this](int p_kind, int p_at, int p_body, RigidBody3D *p_node) {
                CopySeen &copy = seen(p_kind, p_at);
                const netw::sim::Row *row = row_at(p_kind, OBSERVER_AT[p_at]);
                copy.proxy_mode
                    = row != nullptr ? row->bodies.applied : Mode::NONE;
                copy.bodies[p_body].proxy_frozen = p_node->is_freeze_enabled();
            }
        );
    }

    void flip() {
        for (int kind = 0; kind < KINDS; ++kind) {
            for (int at = 0; at < OBSERVERS; ++at) {
                entity_at(kind, OBSERVER_AT[at])
                    ->get_simulation()
                    ->set_replicas(NetwSimulationHandle::REPLICAS_ACTIVE);
            }
        }
    }

    void sample_start() {
        const double expected = step_length();
        each_body(
            [this,
             expected](int p_kind, int p_at, int p_body, RigidBody3D *p_node) {
                BodySeen &body = seen(p_kind, p_at).bodies[p_body];
                const double moved = p_node->get_global_position().distance_to(
                    ended[p_kind][p_at][p_body]
                );
                body.stepped += 1;
                body.integrated += moved > expected * 0.5 ? 1 : 0;
                body.worst_step_error
                    = MAX(body.worst_step_error, Math::abs(moved - expected));
            }
        );
    }

    void sample_end() {
        each_body(
            [this](int p_kind, int p_at, int p_body, RigidBody3D *p_node) {
                ended[p_kind][p_at][p_body] = p_node->get_global_position();
            }
        );
    }

    void read_active() {
        each_body(
            [this](int p_kind, int p_at, int p_body, RigidBody3D *p_node) {
                CopySeen &copy = seen(p_kind, p_at);
                const netw::sim::Row *row = row_at(p_kind, OBSERVER_AT[p_at]);
                copy.active_mode
                    = row != nullptr ? row->bodies.applied : Mode::NONE;
                if (row != nullptr) {
                    copy.installed = row->installs.stats.installed;
                    copy.newest_age = row->installs.stats.newest_age;
                }
                copy.bodies[p_body].active_frozen = p_node->is_freeze_enabled();
            }
        );
    }

    void drift() {
        for (int kind = 0; kind < KINDS; ++kind) {
            for (int at = 0; at < OBSERVERS; ++at) {
                const netw::sim::Row *row = row_at(kind, OBSERVER_AT[at]);
                installed_at_drift[kind][at]
                    = row != nullptr ? row->installs.stats.installed : 0;
            }
        }
        each_body([](int, int, int, RigidBody3D *p_node) {
            p_node->set_global_position(p_node->get_global_position() + DRIFT);
        });
    }

    void read_healed() {
        each_body(
            [this](int p_kind, int p_at, int p_body, RigidBody3D *p_node) {
                CopySeen &copy = seen(p_kind, p_at);
                const netw::sim::Row *row = row_at(p_kind, OBSERVER_AT[p_at]);
                if (row != nullptr) {
                    copy.installed_after_drift = row->installs.stats.installed
                        - installed_at_drift[p_kind][p_at];
                    copy.youngest_age = row->installs.stats.youngest_age;
                    copy.oldest_age = row->installs.stats.oldest_age;
                }
                const Vector3 author
                    = body_at(p_kind, AUTHOR, p_body)->get_global_position();
                copy.bodies[p_body].drift_left
                    = Math::abs(p_node->get_global_position().z - author.z);
            }
        );
    }

    void read_advance() {
        each_body(
            [this](int p_kind, int p_at, int p_body, RigidBody3D *p_node) {
                seen(p_kind, p_at).bodies[p_body].healed_advance
                    = p_node->get_global_position().x
                    - ended[p_kind][p_at][p_body].x;
            }
        );
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
        if (frame > LAST_FRAME) {
            close();
            frame = -1;
            return false;
        }
        const bool sampling = frame > WINDOW_OPEN && frame <= WINDOW_CLOSE;
        if (sampling) {
            sample_start();
        }
        if (frame == PROXY_FRAME) {
            read_proxy();
        }
        if (frame == FLIP_FRAME) {
            flip();
        }
        if (frame == DRIFT_FRAME) {
            drift();
        }
        if (frame == LAST_FRAME) {
            read_advance();
        }
        stand->step_ticks(1);
        if (sampling || frame == WINDOW_OPEN || frame == LAST_FRAME - 1) {
            sample_end();
        }
        if (frame == WINDOW_CLOSE) {
            read_active();
        }
        if (frame == HEAL_FRAME) {
            read_healed();
        }
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(BodiesScenario, bodies_scenario);

void copy_label(char (&r_text)[96], int p_kind, int p_at) {
    std::snprintf(
        r_text,
        sizeof(r_text),
        "%s, %s, on peer %d",
        SHAPE_NAMES[shape_of(p_kind)],
        PHASE_NAMES[p_kind / SHAPES],
        OBSERVER_AT[p_at]
    );
}

TEST_CASE(
    "[Networked][Sim][Frame] a rigid root, a child body and a two-body truck "
    "freeze together on a proxy copy and thaw together when their replicas "
    "turn active, and every body integrates one step between the rows it "
    "installs, whether the row lands on arrival or at the display tick"
) {
    const BodiesEvidence &evidence = bodies_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    for (int kind = 0; kind < KINDS; ++kind) {
        const Shape shape = shape_of(kind);
        for (int at = 0; at < OBSERVERS; ++at) {
            const CopySeen &copy = evidence.copies[kind][at];
            char copy_text[96];
            copy_label(copy_text, kind, at);
            CAPTURE(copy_text);
            NETW_CHECK_EQ(int(copy.proxy_mode), int(Mode::PROXY));
            NETW_CHECK_EQ(int(copy.active_mode), int(Mode::ACTIVE));
            NETW_CHECK_GT(copy.installed, int64_t(0));
            for (int body = 0; body < BODY_COUNT[shape]; ++body) {
                const BodySeen &seen = copy.bodies[body];
                NETW_FORMAT_TEXT(body_text, BODY_PATHS[shape][body]);
                CAPTURE(body_text);
                CHECK(seen.proxy_frozen);
                CHECK_FALSE(seen.active_frozen);
                NETW_CHECK_GT(seen.stepped, 0);
                NETW_CHECK_EQ(seen.integrated, seen.stepped);
                NETW_CHECK_LT(seen.worst_step_error, 0.01);
            }
        }
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a buffered install lands at the display offset "
    "and an exact one on arrival, and either heals a drifted body of every "
    "shape so that the healed pose carries into the next step"
) {
    const BodiesEvidence &evidence = bodies_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    const double expected = step_length();
    for (int kind = 0; kind < KINDS; ++kind) {
        const Shape shape = shape_of(kind);
        const bool buffered = kind < SHAPES;
        for (int at = 0; at < OBSERVERS; ++at) {
            const CopySeen &copy = evidence.copies[kind][at];
            char copy_text[96];
            copy_label(copy_text, kind, at);
            CAPTURE(copy_text);
            if (buffered) {
                NETW_CHECK_EQ(copy.newest_age, int64_t(DISPLAY_OFFSET));
            } else {
                NETW_CHECK_LT(copy.newest_age, int64_t(DISPLAY_OFFSET));
            }
            NETW_CHECK_GT(copy.installed_after_drift, int64_t(0));
            for (int body = 0; body < BODY_COUNT[shape]; ++body) {
                const BodySeen &seen = copy.bodies[body];
                NETW_FORMAT_TEXT(body_text, BODY_PATHS[shape][body]);
                CAPTURE(body_text);
                NETW_CHECK_LT(seen.drift_left, 0.001);
                NETW_CHECK_GT(seen.healed_advance, expected * 0.5);
                NETW_CHECK_LT(seen.healed_advance, expected * 1.5);
            }
        }
    }
}

} // namespace TestNetwSimBodiesFrameLaws

#endif

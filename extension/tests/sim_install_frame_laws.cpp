#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/collision_shape.hpp"
#include "godot/engine.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace TestNetwSimInstallFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::sim::Mode;

constexpr const char *BODY = R"(extends Node3D

var sphere: RigidBody3D

var held: bool:
	get:
		return sphere.freeze
	set(value):
		sphere.freeze = value

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

constexpr const char *BALL_ROWS
    = "\tNetw.configure_property(self, &\"sphere_position\").broadcast()\n"
      "\tNetw.configure_property(self, &\"sphere_linear_velocity\")"
      ".broadcast()\n";

constexpr const char *CRATE_ROWS
    = "\tNetw.configure_property(self, &\"sphere_position\").state()\n"
      "\tNetw.configure_property(self, &\"sphere_linear_velocity\").state()\n";

constexpr const char *THROW_ROWS
    = "\tNetw.configure_property(self, &\"held\").broadcast()\n"
      "\tNetw.configure_property(self, &\"sphere_linear_velocity\")"
      ".broadcast()\n";

enum Kind {
    BALL,
    CRATE,
    THROW,
    KINDS,
};

const char *KIND_IDS[KINDS]
    = {"sim_install_ball", "sim_install_crate", "sim_install_throw"};
const char *KIND_ROWS[KINDS] = {BALL_ROWS, CRATE_ROWS, THROW_ROWS};
const int KIND_AUTHOR[KINDS] = {0, -1, 0};
const Vector3 DRIVE(3.0, 0.0, 0.0);
constexpr int CLIENTS = 2;
constexpr int TICKRATE = 30;
constexpr int WARM_FRAMES = 20;
constexpr int SAMPLE_FRAMES = 10;
constexpr int THROW_FRAME = WARM_FRAMES + SAMPLE_FRAMES + 2;
constexpr int THROW_SETTLE_FRAMES = 6;
constexpr int COPIES = CLIENTS + 1;

Script *kind_scripts[KINDS] = {nullptr, nullptr, nullptr};

Node *build(Kind p_kind, const Variant &p_name) {
    if (kind_scripts[p_kind] == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(kind_scripts[p_kind]->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

Node *build_ball(const Variant &p_name) {
    return build(BALL, p_name);
}

Node *build_crate(const Variant &p_name) {
    return build(CRATE, p_name);
}

Node *build_throw(const Variant &p_name) {
    return build(THROW, p_name);
}

struct Observed {
    Mode mode = Mode::NONE;
    bool frozen = false;
    bool plain_record = false;
    int64_t installed = 0;
    int stepped = 0;
    int integrated = 0;
    double worst_step_error = 0.0;
    Vector3 thrown_velocity;
    bool thrown_frozen = true;
};

struct InstallEvidence {
    bool driven = false;
    bool seated = false;
    Observed copies[KINDS][COPIES];
};

InstallEvidence &install_evidence() {
    static InstallEvidence evidence;
    return evidence;
}

double step_length() {
    return DRIVE.length()
        / double(Engine::get_singleton()->get_physics_ticks_per_second());
}

class InstallScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int routes[KINDS] = {0, 0, 0};
    netw_test::SimStand *stand = nullptr;
    Ref<Script> scripts[KINDS];
    Vector3 ended[KINDS][COPIES];

    Node *node_at(int p_kind, int p_client) const {
        return stand->node_at(p_client, routes[p_kind]);
    }

    RigidBody3D *sphere_at(int p_kind, int p_client) const {
        Node *owner = node_at(p_kind, p_client);
        return owner == nullptr
            ? nullptr
            : Object::cast_to<RigidBody3D>(owner->get_node_or_null("Sphere"));
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

    Observed &seen(int p_kind, int p_client) const {
        return install_evidence().copies[p_kind][p_client + 1];
    }

    void declare(int p_kind, int p_client) {
        NetwMultiplayer *session = stand->session(p_client);
        const Ref<NetwEntity> entity = entity_at(p_kind, p_client);
        netw::sim::Row &row = session->sim_row(entity->get_rid_handle());
        row.declaration.bodies.push_back(NodePath("Sphere"));
        if (p_client != KIND_AUTHOR[p_kind]) {
            row.declaration.replicas = netw::sim::Replicas::ACTIVE;
        }
        session->sim_settle_body(entity);
    }

    bool seat(int p_kind, Node *p_arena) {
        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(String(KIND_IDS[p_kind]));
        const Ref<netw::NetwPlayer> owner = KIND_AUTHOR[p_kind] < 0
            ? Ref<netw::NetwPlayer>()
            : server->peer_get_player(stand->peer_id(KIND_AUTHOR[p_kind]));
        const RID made = server->spawn_registered(
            StringName(KIND_IDS[p_kind]),
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
            if (sphere_at(p_kind, client) == nullptr) {
                return false;
            }
        }
        return true;
    }

    bool open() {
        for (int kind = 0; kind < KINDS; ++kind) {
            const String source = String(BODY) + String(KIND_ROWS[kind]);
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
        stand->teach(
            StringName(KIND_IDS[BALL]),
            callable_mp_static(&build_ball)
        );
        stand->teach(
            StringName(KIND_IDS[CRATE]),
            callable_mp_static(&build_crate)
        );
        stand->teach(
            StringName(KIND_IDS[THROW]),
            callable_mp_static(&build_throw)
        );
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        stand->session(0)->session_submit_join(StringName("first"), Array());
        stand->pump(4);
        for (int kind = 0; kind < KINDS; ++kind) {
            if (!seat(kind, arena)) {
                return false;
            }
        }
        for (int kind = 0; kind < KINDS; ++kind) {
            for (int client = -1; client < CLIENTS; ++client) {
                declare(kind, client);
            }
        }
        sphere_at(BALL, KIND_AUTHOR[BALL])->set_linear_velocity(DRIVE);
        sphere_at(CRATE, KIND_AUTHOR[CRATE])->set_linear_velocity(DRIVE);
        node_at(THROW, KIND_AUTHOR[THROW])->set(StringName("held"), true);
        install_evidence().seated = true;
        return true;
    }

    void close() {
        install_evidence().driven = true;
        delete stand;
        stand = nullptr;
        for (int kind = 0; kind < KINDS; ++kind) {
            kind_scripts[kind] = nullptr;
            scripts[kind] = Ref<Script>();
        }
    }

    void sample_start() {
        const double expected = step_length();
        for (int kind = 0; kind < THROW; ++kind) {
            for (int client = -1; client < CLIENTS; ++client) {
                if (client == KIND_AUTHOR[kind]) {
                    continue;
                }
                Observed &copy = seen(kind, client);
                const double moved = sphere_at(kind, client)
                                         ->get_global_position()
                                         .distance_to(ended[kind][client + 1]);
                copy.stepped += 1;
                copy.integrated += moved > expected * 0.5 ? 1 : 0;
                copy.worst_step_error
                    = MAX(copy.worst_step_error, Math::abs(moved - expected));
            }
        }
    }

    void sample_end() {
        for (int kind = 0; kind < KINDS; ++kind) {
            for (int client = -1; client < CLIENTS; ++client) {
                ended[kind][client + 1]
                    = sphere_at(kind, client)->get_global_position();
            }
        }
    }

    void read_modes() {
        for (int kind = 0; kind < KINDS; ++kind) {
            for (int client = -1; client < CLIENTS; ++client) {
                Observed &copy = seen(kind, client);
                const netw::sim::Row *row = row_at(kind, client);
                const Ref<NetwEntity> entity = entity_at(kind, client);
                copy.mode = row != nullptr ? row->bodies.applied : Mode::NONE;
                copy.installed
                    = row != nullptr ? row->installs.stats.installed : 0;
                copy.frozen = sphere_at(kind, client)->is_freeze_enabled();
                copy.plain_record = entity->get_input_binding().is_null()
                    && (kind == CRATE || entity->get_state_binding().is_null());
            }
        }
    }

    void throw_it() {
        Node *author = node_at(THROW, KIND_AUTHOR[THROW]);
        author->set(StringName("held"), false);
        author->set(StringName("sphere_linear_velocity"), DRIVE);
    }

    void read_throw() {
        for (int client = -1; client < CLIENTS; ++client) {
            Observed &copy = seen(THROW, client);
            RigidBody3D *sphere = sphere_at(THROW, client);
            copy.thrown_velocity = sphere->get_linear_velocity();
            copy.thrown_frozen = sphere->is_freeze_enabled();
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
        } else if (frame <= THROW_FRAME + THROW_SETTLE_FRAMES) {
            const bool sampling
                = frame > WARM_FRAMES && frame <= WARM_FRAMES + SAMPLE_FRAMES;
            if (sampling && frame > WARM_FRAMES + 1) {
                sample_start();
            }
            if (frame == THROW_FRAME) {
                throw_it();
            }
            stand->step_ticks(1);
            if (sampling) {
                sample_end();
            }
            if (frame == WARM_FRAMES + SAMPLE_FRAMES) {
                read_modes();
            }
        } else {
            read_throw();
            close();
            frame = -1;
            return false;
        }
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(InstallScenario, install_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] a broadcast-only rigid body with no input or "
    "state row runs as an active copy on every observer with replicas "
    "active, and integrates between the rows it installs"
) {
    const InstallEvidence &evidence = install_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    for (int client = -1; client < CLIENTS; ++client) {
        if (client == KIND_AUTHOR[BALL]) {
            continue;
        }
        const Observed &copy = evidence.copies[BALL][client + 1];
        NETW_CHECK_EQ(int(copy.mode), int(Mode::ACTIVE));
        CHECK_FALSE(copy.frozen);
        CHECK(copy.plain_record);
        NETW_CHECK_GT(copy.installed, int64_t(0));
        NETW_CHECK_EQ(copy.integrated, copy.stepped);
        NETW_CHECK_LT(copy.worst_step_error, 0.01);
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a state body with replicas active runs on every "
    "client with no prediction registered, integrating between the rows "
    "the session authors"
) {
    const InstallEvidence &evidence = install_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    for (int client = 0; client < CLIENTS; ++client) {
        const Observed &copy = evidence.copies[CRATE][client + 1];
        NETW_CHECK_EQ(int(copy.mode), int(Mode::ACTIVE));
        CHECK_FALSE(copy.frozen);
        NETW_CHECK_GT(copy.installed, int64_t(0));
        NETW_CHECK_EQ(copy.integrated, copy.stepped);
        NETW_CHECK_LT(copy.worst_step_error, 0.01);
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a throw row whose first column thaws the body "
    "keeps the velocity it installs through the next step"
) {
    const InstallEvidence &evidence = install_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    for (int client = -1; client < CLIENTS; ++client) {
        if (client == KIND_AUTHOR[THROW]) {
            continue;
        }
        const Observed &copy = evidence.copies[THROW][client + 1];
        CHECK_FALSE(copy.thrown_frozen);
        CHECK(copy.thrown_velocity.is_equal_approx(DRIVE));
    }
}

constexpr const char *REST_ID = "sim_install_rest";
constexpr int REST_BEAT_TICKS = 60;
constexpr int REST_SETTLE_FRAMES = 360;
constexpr int REST_WATCH_FRAMES = REST_BEAT_TICKS * 3 + 10;

constexpr const char *REST_BODY = R"(extends Node3D

var box: RigidBody3D

var box_height: float:
	get:
		return box.position.y
	set(value):
		box.position.y = value

func _init() -> void:
	box = RigidBody3D.new()
	box.name = &"Box"
	box.position = Vector3(0.0, 0.5, 0.0)
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	box.add_child(shape)
	add_child(box)
	Netw.configure_property(self, &"box_height").state() \
			.quantize(NetwQuantizeScalar.new().bits(8).limits(-8.0, 8.0)) \
			.heartbeat(60)
)";

Script *rest_script = nullptr;

Node *build_rest(const Variant &p_name) {
    if (rest_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(rest_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct RestEvidence {
    bool driven = false;
    bool seated = false;
    bool author_settled = false;
    bool copy_settled = false;
    bool neighbour_settled = false;
    int watched = 0;
    int copy_awake = 0;
    int neighbour_awake = 0;
    int64_t skipped = 0;
    int64_t installed = 0;
};

RestEvidence &rest_evidence() {
    static RestEvidence evidence;
    return evidence;
}

RigidBody3D *cube(Node *p_parent, const Vector3 &p_at, uint32_t p_layer) {
    RigidBody3D *made = memnew(RigidBody3D);
    CollisionShape3D *shape = memnew(CollisionShape3D);
    Ref<BoxShape3D> box;
    box.instantiate();
    shape->set_shape(box);
    made->add_child(shape);
    made->set_position(p_at);
    made->set_collision_layer(p_layer);
    made->set_collision_mask(p_layer);
    p_parent->add_child(made);
    return made;
}

class RestScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int route = 0;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    StaticBody3D *floor = nullptr;
    RigidBody3D *neighbours[2] = {nullptr, nullptr};
    int64_t skipped_at_watch = 0;
    int64_t installed_at_watch = 0;

    RigidBody3D *box_at(int p_client) const {
        Node *owner = stand->node_at(p_client, route);
        return owner == nullptr
            ? nullptr
            : Object::cast_to<RigidBody3D>(owner->get_node_or_null("Box"));
    }

    const netw::sim::Row *row_at(int p_client) const {
        const Ref<NetwEntity> entity
            = NetwEntity::of(stand->node_at(p_client, route));
        return entity.is_null()
            ? nullptr
            : stand->session(p_client)->sim_row_of(entity->get_rid_handle());
    }

    void declare(int p_client) {
        NetwMultiplayer *session = stand->session(p_client);
        const Ref<NetwEntity> entity
            = NetwEntity::of(stand->node_at(p_client, route));
        netw::sim::Row &row = session->sim_row(entity->get_rid_handle());
        row.declaration.bodies.push_back(NodePath("Box"));
        if (p_client >= 0) {
            row.declaration.replicas = netw::sim::Replicas::ACTIVE;
        }
        session->sim_settle_body(entity);
    }

    bool open() {
        script = netw_test::minted_script(REST_BODY);
        if (script.is_null()) {
            return false;
        }
        rest_script = script.ptr();
        stand = new netw_test::SimStand(1);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(REST_ID), callable_mp_static(&build_rest));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(30);
        stand->session(0)->session_submit_join(StringName("first"), Array());
        stand->pump(4);

        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(String(REST_ID));
        const RID made
            = server->spawn_registered(StringName(REST_ID), args, nullptr);
        Node *built = server->entity_get_node(made);
        if (built == nullptr) {
            return false;
        }
        arena->add_child(built);
        stand->pump(8);
        route = int(server->entity_get_route(made));
        for (int client = -1; client < 1; ++client) {
            RigidBody3D *box = box_at(client);
            if (box == nullptr) {
                return false;
            }
            const uint32_t layer = 1u << uint32_t(client + 1);
            box->set_collision_layer(layer);
            box->set_collision_mask(layer);
            neighbours[client + 1] = cube(
                stand->node_at(client, route)->get_parent(),
                Vector3(0.0, 1.5, 0.0),
                layer
            );
            declare(client);
        }

        floor = memnew(StaticBody3D);
        CollisionShape3D *shape = memnew(CollisionShape3D);
        Ref<BoxShape3D> slab;
        slab.instantiate();
        slab->set_size(Vector3(10.0, 1.0, 10.0));
        shape->set_shape(slab);
        floor->add_child(shape);
        floor->set_position(Vector3(0.0, -0.5, 0.0));
        floor->set_collision_layer(3);
        floor->set_collision_mask(0);
        arena->add_child(floor);
        rest_evidence().seated = true;
        return true;
    }

    void close() {
        rest_evidence().driven = true;
        for (RigidBody3D *&neighbour : neighbours) {
            if (neighbour != nullptr) {
                neighbour->get_parent()->remove_child(neighbour);
                memdelete(neighbour);
                neighbour = nullptr;
            }
        }
        if (floor != nullptr) {
            floor->get_parent()->remove_child(floor);
            memdelete(floor);
            floor = nullptr;
        }
        delete stand;
        stand = nullptr;
        rest_script = nullptr;
        script = Ref<Script>();
    }

    void read_settled() {
        RestEvidence &evidence = rest_evidence();
        evidence.author_settled
            = box_at(-1)->is_sleeping() && neighbours[0]->is_sleeping();
        evidence.copy_settled = box_at(0)->is_sleeping();
        evidence.neighbour_settled = neighbours[1]->is_sleeping();
        const netw::sim::Row *row = row_at(0);
        skipped_at_watch = row != nullptr ? row->installs.stats.skipped : 0;
        installed_at_watch = row != nullptr ? row->installs.stats.installed : 0;
    }

    void watch() {
        RestEvidence &evidence = rest_evidence();
        evidence.watched += 1;
        evidence.copy_awake += box_at(0)->is_sleeping() ? 0 : 1;
        evidence.neighbour_awake += neighbours[1]->is_sleeping() ? 0 : 1;
        const netw::sim::Row *row = row_at(0);
        if (row != nullptr) {
            evidence.skipped = row->installs.stats.skipped - skipped_at_watch;
            evidence.installed
                = row->installs.stats.installed - installed_at_watch;
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
        } else if (frame <= REST_SETTLE_FRAMES + REST_WATCH_FRAMES) {
            if (frame == REST_SETTLE_FRAMES) {
                read_settled();
            } else if (frame > REST_SETTLE_FRAMES) {
                watch();
            }
            stand->step_ticks(1);
        } else {
            close();
            frame = -1;
            return false;
        }
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(RestScenario, rest_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] a heartbeat that agrees with a sleeping active "
    "copy is skipped, and leaves the copy and the body resting on it asleep"
) {
    const RestEvidence &evidence = rest_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    REQUIRE(evidence.author_settled);
    REQUIRE(evidence.copy_settled);
    REQUIRE(evidence.neighbour_settled);
    NETW_CHECK_EQ(evidence.watched, REST_WATCH_FRAMES);
    NETW_CHECK_GE(evidence.skipped, int64_t(2));
    NETW_CHECK_EQ(evidence.installed, int64_t(0));
    NETW_CHECK_EQ(evidence.copy_awake, 0);
    NETW_CHECK_EQ(evidence.neighbour_awake, 0);
}

} // namespace TestNetwSimInstallFrameLaws

#endif

#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/engine.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/spatial_node.hpp"
#include "godot/world.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace TestNetwSimRunnerFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *COUNTERS = R"(
var calls := 0
var fresh := 0
var repeats := 0
var last_tick := -1

func count(tick: int, is_fresh: bool) -> void:
	calls += 1
	if tick == last_tick:
		repeats += 1
	last_tick = tick
	if is_fresh:
		fresh += 1
)";

constexpr const char *CAR = R"(extends Node3D

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
	Netw.configure_property(self, &"sphere_position").broadcast()
	Netw.configure_property(self, &"sphere_linear_velocity").broadcast()

func _network_tick(_delta: float, tick: int, is_fresh: bool) -> void:
	count(tick, is_fresh)
	if not is_fresh:
		return
	sphere.linear_velocity = Vector3(3.0, 0.0, 0.0)
)";

constexpr const char *SCRIPTED = R"(extends Node3D

func _network_tick(_delta: float, tick: int, is_fresh: bool) -> void:
	count(tick, is_fresh)
)";

constexpr const char *PREDICTED = R"(extends Node3D

var beat := 0

func _init() -> void:
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPredict.ARCHETYPE_SCRIPTED
	Netw.configure_property(self, &"beat").state()

func _network_tick(_delta: float, tick: int, is_fresh: bool) -> void:
	count(tick, is_fresh)
	beat += 1
)";

constexpr const char *SOLVER = R"(extends RigidBody3D

func _init() -> void:
	gravity_scale = 0.0
	collision_layer = 0
	collision_mask = 0
	can_sleep = false
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	add_child(shape)
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPredict.ARCHETYPE_SOLVER_BODY
	Netw.configure_property(self, &"position").state()
)";

enum Kind {
    KIND_CAR,
    KIND_SCRIPTED,
    KIND_PREDICTED,
    KIND_SOLVER,
    KINDS,
};

const char *KIND_IDS[KINDS] = {
    "sim_runner_car",
    "sim_runner_scripted",
    "sim_runner_predicted",
    "sim_runner_solver",
};
const char *KIND_SOURCES[KINDS] = {CAR, SCRIPTED, PREDICTED, SOLVER};
constexpr int CLIENTS = 2;
constexpr int COPIES = CLIENTS + 1;
constexpr int WARM_FRAMES = 30;
constexpr int WATCH_FRAMES = 60;
constexpr double DRIVE_SPEED = 3.0;

Script *kind_scripts[KINDS] = {nullptr, nullptr, nullptr, nullptr};

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

Node *build_car(const Variant &p_name) {
    return build(KIND_CAR, p_name);
}

Node *build_scripted(const Variant &p_name) {
    return build(KIND_SCRIPTED, p_name);
}

Node *build_predicted(const Variant &p_name) {
    return build(KIND_PREDICTED, p_name);
}

Node *build_solver(const Variant &p_name) {
    return build(KIND_SOLVER, p_name);
}

Callable builder_of(Kind p_kind) {
    switch (p_kind) {
        case KIND_CAR:
            return callable_mp_static(&build_car);
        case KIND_SCRIPTED:
            return callable_mp_static(&build_scripted);
        case KIND_PREDICTED:
            return callable_mp_static(&build_predicted);
        default:
            return callable_mp_static(&build_solver);
    }
}

double frame_delta() {
    return 1.0
        / double(Engine::get_singleton()->get_physics_ticks_per_second());
}

struct Counted {
    bool present = false;
    int64_t calls = 0;
    int64_t fresh = 0;
    int64_t repeats = 0;
    int64_t ticks = 0;
};

class Stage {
    netw_test::SimStand *stand = nullptr;
    Node *host_arena = nullptr;
    Ref<Script> scripts[KINDS];
    int routes[KINDS] = {0, 0, 0, 0};
    int64_t window_tick[COPIES] = {0, 0, 0};
    int64_t window_calls[KINDS][COPIES] = {};
    int64_t window_fresh[KINDS][COPIES] = {};
    int64_t window_repeats[KINDS][COPIES] = {};

public:
    netw_test::SimStand *get() const {
        return stand;
    }

    bool open(int p_tickrate) {
        for (int kind = 0; kind < KINDS; ++kind) {
            const String source = String(KIND_SOURCES[kind]) + String(COUNTERS);
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
            stand->teach(StringName(KIND_IDS[kind]), builder_of(Kind(kind)));
        }
        stand->mount();
        host_arena = stand->arena();
        stand->arm(p_tickrate);
        stand->session(0)->session_submit_join(StringName("first"), Array());
        stand->pump(4);
        return true;
    }

    bool seat(Kind p_kind, int p_author) {
        NetwMultiplayer *server = stand->session(-1);
        Node *arena = host_arena;
        Array args;
        args.push_back(String(KIND_IDS[p_kind]));
        const Ref<netw::NetwPlayer> owner = p_author < 0
            ? Ref<netw::NetwPlayer>()
            : server->peer_get_player(stand->peer_id(p_author));
        const RID made = server->spawn_registered(
            StringName(KIND_IDS[p_kind]),
            args,
            owner.ptr()
        );
        Node *built = server->entity_get_node(made);
        if (built == nullptr || arena == nullptr) {
            return false;
        }
        arena->add_child(built);
        stand->pump(8);
        routes[p_kind] = int(server->entity_get_route(made));
        for (int client = -1; client < CLIENTS; ++client) {
            if (node_at(p_kind, client) == nullptr) {
                return false;
            }
        }
        return true;
    }

    Node *node_at(Kind p_kind, int p_client) const {
        return stand->node_at(p_client, routes[p_kind]);
    }

    RID rid_at(Kind p_kind, int p_client) const {
        const Ref<NetwEntity> entity
            = NetwEntity::of(node_at(p_kind, p_client));
        return entity.is_null() ? RID() : entity->get_rid_handle();
    }

    void mark() {
        for (int client = -1; client < CLIENTS; ++client) {
            window_tick[client + 1]
                = stand->session(client)->clock_engine().get_tick();
            for (int kind = 0; kind < KINDS; ++kind) {
                Node *node
                    = routes[kind] == 0 ? nullptr : node_at(Kind(kind), client);
                if (node == nullptr) {
                    continue;
                }
                window_calls[kind][client + 1] = node->get("calls");
                window_fresh[kind][client + 1] = node->get("fresh");
                window_repeats[kind][client + 1] = node->get("repeats");
            }
        }
    }

    Counted counted(Kind p_kind, int p_client) const {
        Counted out;
        Node *node = node_at(p_kind, p_client);
        if (node == nullptr) {
            return out;
        }
        out.present = true;
        out.calls
            = int64_t(node->get("calls")) - window_calls[p_kind][p_client + 1];
        out.fresh
            = int64_t(node->get("fresh")) - window_fresh[p_kind][p_client + 1];
        out.repeats = int64_t(node->get("repeats"))
            - window_repeats[p_kind][p_client + 1];
        out.ticks = stand->session(p_client)->clock_engine().get_tick()
            - window_tick[p_client + 1];
        return out;
    }

    Node *arena() const {
        return host_arena;
    }

    void close() {
        delete stand;
        stand = nullptr;
        host_arena = nullptr;
        for (int kind = 0; kind < KINDS; ++kind) {
            kind_scripts[kind] = nullptr;
            scripts[kind] = Ref<Script>();
        }
    }
};

struct RunEvidence {
    bool driven = false;
    bool seated = false;
    bool lagcomp_seen = false;
    Counted car[COPIES];
    Counted scripted[COPIES];
    double car_moved = 0.0;
    double car_expected = 0.0;
};

RunEvidence &run_evidence(int p_tickrate) {
    static RunEvidence at_frame_rate;
    static RunEvidence at_half_rate;
    return p_tickrate >= 60 ? at_frame_rate : at_half_rate;
}

class RunScenario final : public netw_test::FrameScenario {
    int frame = 0;
    int tickrate = 60;
    Stage stage;
    Vector3 car_started;

    RigidBody3D *car_sphere() const {
        Node *car = stage.node_at(KIND_CAR, 0);
        return car == nullptr
            ? nullptr
            : Object::cast_to<RigidBody3D>(car->get_node_or_null("Sphere"));
    }

    void declare_car(int p_client) {
        NetwMultiplayer *session = stage.get()->session(p_client);
        const Ref<NetwEntity> entity
            = NetwEntity::of(stage.node_at(KIND_CAR, p_client));
        netw::sim::Row &row = session->sim_row(entity->get_rid_handle());
        row.declaration.bodies.push_back(NodePath("Sphere"));
        if (p_client < 0) {
            row.declaration.replicas = netw::sim::Replicas::ACTIVE;
        }
        session->sim_settle_body(entity);
    }

    bool open() {
        if (!stage.open(tickrate) || !stage.seat(KIND_CAR, 0)
            || !stage.seat(KIND_SCRIPTED, -1)) {
            return false;
        }
        for (int client = -1; client < CLIENTS; ++client) {
            declare_car(client);
            stage.get()->session(client)->sim_row(
                stage.rid_at(KIND_SCRIPTED, client)
            );
        }
        run_evidence(tickrate).seated = true;
        return true;
    }

    void note_lagcomp() {
        for (int client = -1; client < CLIENTS; ++client) {
            if (stage.get()->session(client)->lagcomp_is_configured()) {
                run_evidence(tickrate).lagcomp_seen = true;
            }
        }
    }

    void read() {
        RunEvidence &evidence = run_evidence(tickrate);
        for (int client = -1; client < CLIENTS; ++client) {
            evidence.car[client + 1] = stage.counted(KIND_CAR, client);
            evidence.scripted[client + 1]
                = stage.counted(KIND_SCRIPTED, client);
        }
        evidence.car_moved
            = car_sphere()->get_global_position().distance_to(car_started);
        evidence.car_expected
            = DRIVE_SPEED * frame_delta() * double(WATCH_FRAMES);
    }

    void finish() {
        run_evidence(tickrate).driven = true;
        stage.close();
        frame = -1;
    }

public:
    explicit RunScenario(int p_tickrate) : tickrate(p_tickrate) {
    }

    bool advance() override {
        if (netw::gd::scene_root() == nullptr || frame < 0) {
            return false;
        }
        if (frame == 0) {
            if (!open()) {
                finish();
                return false;
            }
        } else if (frame <= WARM_FRAMES + WATCH_FRAMES) {
            if (frame == WARM_FRAMES + 1) {
                stage.mark();
                car_started = car_sphere()->get_global_position();
            }
            stage.get()->physics_frame(frame_delta());
            note_lagcomp();
        } else {
            read();
            finish();
            return false;
        }
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(RunScenario, run_at_frame_rate, 60);
NETW_FRAME_SCENARIO(RunScenario, run_at_half_rate, 30);

struct PaceEvidence {
    bool driven = false;
    bool seated = false;
    int64_t server_gates = -1;
    int64_t proxy_gates = -1;
    int watched = 0;
    int integrated = 0;
    int client_frames_without_tick = 0;
    Counted predicted;
};

PaceEvidence &pace_evidence() {
    static PaceEvidence evidence;
    return evidence;
}

constexpr double PACE_CLIENT_SCALE = 0.75;

class PaceScenario final : public netw_test::FrameScenario {
    int frame = 0;
    Stage stage;
    RigidBody3D *free_body = nullptr;
    Vector3 free_last;
    int64_t client_tick = 0;

    bool open() {
        if (!stage.open(30) || !stage.seat(KIND_SOLVER, -1)
            || !stage.seat(KIND_PREDICTED, -1)) {
            return false;
        }
        Node *arena = stage.arena();
        if (arena == nullptr) {
            return false;
        }
        free_body = memnew(RigidBody3D);
        free_body->set_gravity_scale(0.0);
        free_body->set_collision_layer(0);
        free_body->set_collision_mask(0);
        free_body->set_can_sleep(false);
        free_body->set_linear_damp_mode(RigidBody3D::DAMP_MODE_REPLACE);
        free_body->set_linear_damp(0.0);
        arena->add_child(free_body);
        free_body->set_linear_velocity(Vector3(DRIVE_SPEED, 0.0, 0.0));
        pace_evidence().seated = true;
        return true;
    }

    void watch() {
        PaceEvidence &evidence = pace_evidence();
        const Vector3 at = free_body->get_global_position();
        evidence.watched += 1;
        evidence.integrated
            += at.distance_to(free_last) > DRIVE_SPEED * frame_delta() * 0.5
            ? 1
            : 0;
        const int64_t tick = stage.get()->session(0)->clock_engine().get_tick();
        evidence.client_frames_without_tick += tick == client_tick ? 1 : 0;
        client_tick = tick;
    }

    void read() {
        PaceEvidence &evidence = pace_evidence();
        evidence.server_gates
            = stage.get()->session(-1)->simulation_gate_count();
        evidence.proxy_gates = stage.get()->session(0)->simulation_gate_count();
        evidence.predicted = stage.counted(KIND_PREDICTED, -1);
    }

    void finish() {
        pace_evidence().driven = true;
        RID space;
        if (free_body != nullptr && free_body->get_world_3d().is_valid()) {
            space = free_body->get_world_3d()->get_space();
        }
        stage.close();
        free_body = nullptr;
        if (space.is_valid()) {
            PhysicsServer3D::get_singleton()->space_set_active(space, true);
        }
        frame = -1;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || frame < 0) {
            return false;
        }
        if (frame == 0) {
            if (!open()) {
                finish();
                return false;
            }
        } else if (frame <= WARM_FRAMES + WATCH_FRAMES + 1) {
            if (frame > WARM_FRAMES + 1) {
                watch();
            }
            if (frame == WARM_FRAMES + 1) {
                stage.mark();
                client_tick
                    = stage.get()->session(0)->clock_engine().get_tick();
            }
            free_last = free_body->get_global_position();
            stage.get()->physics_frame(frame_delta(), PACE_CLIENT_SCALE);
        } else {
            read();
            finish();
            return false;
        }
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(PaceScenario, pace_scenario);

void check_steps_once_per_tick(const RunEvidence &p_evidence) {
    const Counted &controller = p_evidence.car[1];
    const Counted &active = p_evidence.car[0];
    const Counted &proxy = p_evidence.car[2];
    const Counted &scripted = p_evidence.scripted[0];
    REQUIRE(controller.present);
    NETW_CHECK_GT(controller.ticks, int64_t(0));
    NETW_CHECK_EQ(controller.calls, controller.ticks);
    NETW_CHECK_EQ(controller.repeats, int64_t(0));
    NETW_CHECK_EQ(active.calls, active.ticks);
    NETW_CHECK_EQ(active.repeats, int64_t(0));
    NETW_CHECK_EQ(proxy.calls, int64_t(0));
    NETW_CHECK_EQ(scripted.calls, scripted.ticks);
    NETW_CHECK_EQ(scripted.repeats, int64_t(0));
    NETW_CHECK_EQ(p_evidence.scripted[1].calls, int64_t(0));
    NETW_CHECK_EQ(p_evidence.scripted[2].calls, int64_t(0));
}

TEST_CASE(
    "[Networked][Sim][Frame] a forward-simulated body's step runs exactly "
    "once per tick at 60 ticks per 60 physics frames, on its author and on "
    "an active copy, and never on a proxy"
) {
    const RunEvidence &evidence = run_evidence(60);
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    check_steps_once_per_tick(evidence);
    NETW_CHECK_EQ(evidence.car[1].ticks, int64_t(WATCH_FRAMES));
}

TEST_CASE(
    "[Networked][Sim][Frame] a forward-simulated body's step runs exactly "
    "once per tick at 30 ticks per 60 physics frames, on its author and on "
    "an active copy, and never on a proxy"
) {
    const RunEvidence &evidence = run_evidence(30);
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    check_steps_once_per_tick(evidence);
    NETW_CHECK_EQ(evidence.car[1].ticks, int64_t(WATCH_FRAMES / 2));
}

TEST_CASE(
    "[Networked][Sim][Frame] a session with no lag compensation configured "
    "drives the step of an entity without prediction"
) {
    for (const int tickrate : {60, 30}) {
        const RunEvidence &evidence = run_evidence(tickrate);
        REQUIRE(evidence.driven);
        REQUIRE(evidence.seated);
        CHECK_FALSE(evidence.lagcomp_seen);
        NETW_CHECK_GT(evidence.car[1].calls, int64_t(0));
        NETW_CHECK_GT(evidence.scripted[0].calls, int64_t(0));
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] an authority step is fresh and an active "
    "copy's step is not"
) {
    for (const int tickrate : {60, 30}) {
        const RunEvidence &evidence = run_evidence(tickrate);
        REQUIRE(evidence.driven);
        REQUIRE(evidence.seated);
        NETW_CHECK_EQ(evidence.car[1].fresh, evidence.car[1].calls);
        NETW_CHECK_EQ(evidence.scripted[0].fresh, evidence.scripted[0].calls);
        NETW_CHECK_GT(evidence.car[0].calls, int64_t(0));
        NETW_CHECK_EQ(evidence.car[0].fresh, int64_t(0));
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a racing-shaped car is driven through its step "
    "on its controller, and its sphere moves the distance the step asked for"
) {
    for (const int tickrate : {60, 30}) {
        const RunEvidence &evidence = run_evidence(tickrate);
        REQUIRE(evidence.driven);
        REQUIRE(evidence.seated);
        NETW_CHECK_GT(evidence.car_moved, evidence.car_expected * 0.9);
        NETW_CHECK_LT(evidence.car_moved, evidence.car_expected * 1.1);
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a free body sharing a space with a frozen solver "
    "proxy is never held, while the solver's author still paces the space"
) {
    const PaceEvidence &evidence = pace_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_GT(evidence.client_frames_without_tick, 0);
    NETW_CHECK_EQ(evidence.server_gates, int64_t(1));
    NETW_CHECK_EQ(evidence.proxy_gates, int64_t(0));
    NETW_CHECK_EQ(evidence.watched, WATCH_FRAMES);
    NETW_CHECK_EQ(evidence.integrated, evidence.watched);
}

TEST_CASE(
    "[Networked][Sim][Frame] a predicted entity with no input rows steps "
    "once per tick on its author, and every one of those steps is fresh"
) {
    const PaceEvidence &evidence = pace_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    const Counted &predicted = evidence.predicted;
    REQUIRE(predicted.present);
    NETW_CHECK_GT(predicted.ticks, int64_t(0));
    NETW_CHECK_EQ(predicted.calls, predicted.ticks);
    NETW_CHECK_EQ(predicted.fresh, predicted.calls);
}

} // namespace TestNetwSimRunnerFrameLaws

#endif

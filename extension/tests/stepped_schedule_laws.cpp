#include "support/fault_log.h"
#include "support/installable_stepper.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction.hpp"
#include "netw/predict/engine.hpp"

using namespace godot;

namespace TestSteppedScheduleLaws {

using netw::NetwMultiplayer;
using netw::NetwSimulation;

constexpr const char *BALL_SOURCE = R"(extends RigidBody3D

var motion := 0

func _init() -> void:
	var hull := CollisionShape3D.new()
	hull.shape = SphereShape3D.new()
	add_child(hull)
	gravity_scale = 0.0
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPrediction.ARCHETYPE_SOLVER_BODY
	e.simulation.schedule = NetwSimulation.SCHEDULE_AUTO
	Netw.configure_property(self, &"position").state()
	Netw.configure_property(self, &"motion").input()
)";

constexpr const char *WALKER_SOURCE = R"(extends CharacterBody3D

var motion := 0

func _init() -> void:
	var hull := CollisionShape3D.new()
	hull.shape = BoxShape3D.new()
	add_child(hull)
	collision_layer = 0
	collision_mask = 0
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPrediction.ARCHETYPE_SCRIPTED
	e.simulation.schedule = NetwSimulation.SCHEDULE_STEPPED
	Netw.configure_property(self, &"position").state()
	Netw.configure_property(self, &"motion").input()

func _network_tick(_delta: float, _tick: int, _is_fresh: bool) -> void:
	velocity = Vector3(1.0, 0.0, 0.0)
	move_and_slide()
)";

constexpr int SERVER = -1;
constexpr int A = 0;
constexpr const char *KINEMATIC_FAULT
    = "cannot restore it without moving it. It runs SCHEDULE_FRAME.";

Script *built_script = nullptr;

Node *build(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(built_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

struct Seated {
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    Ref<netw::NetwEntity> entity;
    Node *node = nullptr;

    explicit Seated(const char *p_source, const char *p_name) {
        script = netw_test::minted_script(p_source);
        REQUIRE(script.is_valid());
        built_script = script.ptr();
        stand = new netw_test::SimStand(1);
        REQUIRE(stand->ready());
        stand->teach(StringName("seated"), callable_mp_static(&build));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(30);
        stand->session(A)->session_submit_join(StringName("a"), Array());
        stand->pump(4);
        NetwMultiplayer *server = stand->session(SERVER);
        const Ref<netw::NetwPlayer> owner
            = server->peer_get_player(stand->peer_id(A));
        Array args;
        args.push_back(String(p_name));
        const RID made = server->spawn_registered(
            StringName("seated"),
            args,
            owner.ptr()
        );
        node = server->entity_get_node(made);
        REQUIRE(node != nullptr);
        arena->add_child(node);
        stand->pump(8);
        entity = server->entity_get_view(made);
        REQUIRE(entity.is_valid());
    }

    ~Seated() {
        entity = Ref<netw::NetwEntity>();
        delete stand;
        built_script = nullptr;
    }

    NetwMultiplayer *server() const {
        return stand->session(SERVER);
    }

    NetwMultiplayer::EntitySpace space() const {
        return server()->entity_space_of(entity);
    }

    int64_t resolved() const {
        netw::NetwPredictionEngine *pool = server()->get_prediction_engine();
        return pool->schedule_of(pool->slot_of(entity));
    }
};

Ref<netw::NetwPhysicsStepper> installable() {
    Ref<netw_test::InstallableStepper> made;
    made.instantiate();
    return made;
}

} // namespace TestSteppedScheduleLaws

TEST_CASE(
    "[Networked][Predict][Stepped] K-ST1 SCHEDULE_AUTO runs a predicted body "
    "STEPPED when its space can step, by the engine or an installed "
    "stepper, and SCHEDULE_FRAME when nothing can step it"
) {
    using namespace TestSteppedScheduleLaws;
    const Seated seated(BALL_SOURCE, "Ball");
    const NetwMultiplayer::EntitySpace held = seated.space();
    REQUIRE(held.space.is_valid());
    const Ref<netw::NetwPrediction> handle = seated.entity->get_prediction();
    REQUIRE(handle.is_valid());
    const NetwSimulation::Schedule alone
        = netw::gd::can_step_space(held.dimension)
        ? NetwSimulation::SCHEDULE_STEPPED
        : NetwSimulation::SCHEDULE_FRAME;
    NETW_CHECK_EQ(int(handle->get_schedule()), int(alone));

    seated.server()->predict_stepper_install(held.space, installable());
    NETW_CHECK_EQ(
        int(handle->get_schedule()),
        int(NetwSimulation::SCHEDULE_STEPPED)
    );
    NETW_CHECK_EQ(
        seated.resolved(),
        int64_t(NetwSimulation::SCHEDULE_STEPPED)
    );
    seated.server()->predict_stepper_install(
        held.space,
        Ref<netw::NetwPhysicsStepper>()
    );
    NETW_CHECK_EQ(int(handle->get_schedule()), int(alone));
}

TEST_CASE(
    "[Networked][Predict][Stepped] K-ST2 a CharacterBody3D declared STEPPED "
    "on a build that cannot flush a kinematic body runs SCHEDULE_FRAME and "
    "says so once, and runs STEPPED where it can"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][prediction] '*' uses SCHEDULE_STEPPED, and this "
        "Godot build cannot step physics on demand, so it runs "
        "SCHEDULE_FRAME. Install a NetwPhysicsStepper with "
        "NetwSession.install_physics_stepper.",
        1
    );

    using namespace TestSteppedScheduleLaws;
    const netw_test::FaultWatch watch;
    const Seated seated(WALKER_SOURCE, "Walker");
    const NetwMultiplayer::EntitySpace held = seated.space();
    REQUIRE(held.space.is_valid());
    seated.server()->predict_stepper_install(held.space, installable());
    for (int frame = 0; frame < 6; ++frame) {
        seated.stand->physics_frame(1.0 / 30.0);
    }
    const bool flushes = netw::gd::can_flush_kinematic();
    NETW_CHECK_EQ(
        seated.resolved(),
        int64_t(
            flushes ? NetwSimulation::SCHEDULE_STEPPED
                    : NetwSimulation::SCHEDULE_FRAME
        )
    );
    NETW_CHECK_EQ(
        watch.warnings_containing(KINEMATIC_FAULT),
        int64_t(flushes ? 0 : 1)
    );
    seated.server()->predict_stepper_install(
        held.space,
        Ref<netw::NetwPhysicsStepper>()
    );
}

#endif

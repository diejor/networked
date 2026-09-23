#include "support/netw_test.h"

#include "godot/collision_shape.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/world.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/object_port.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr double STEP_DELTA = 1.0 / 60.0;
constexpr double NEAR = 1e-4;
constexpr double STEPPED_NEAR = 1e-3;

const Vector3 PARENT_AT(3.0, 1.0, -2.0);
const Vector3 AUTHORED_AT(0.0, 5.0, 0.0);
const Vector3 STEPPED_AT(1.0, 2.0, 3.0);
const Vector3 WRITTEN_AT(-2.0, 4.0, 6.0);
const Vector3 LINEAR(4.0, 0.0, -1.0);
const Vector3 ANGULAR(0.0, 2.0, 0.0);

const Vector2 PARENT_AT_2D(30.0, -12.0);
const Vector2 AUTHORED_AT_2D(0.0, 50.0);
const Vector2 STEPPED_AT_2D(10.0, 20.0);
const Vector2 WRITTEN_AT_2D(-20.0, 40.0);
const Vector2 LINEAR_2D(40.0, -10.0);
constexpr double ANGULAR_2D = 2.0;
constexpr double TURN_2D = 0.3;

const StringName &position_key() {
    static const StringName name("position");
    return name;
}

const StringName &quaternion_key() {
    static const StringName name("quaternion");
    return name;
}

const StringName &rotation_key() {
    static const StringName name("rotation");
    return name;
}

const StringName &linear_key() {
    static const StringName name("linear_velocity");
    return name;
}

const StringName &angular_key() {
    static const StringName name("angular_velocity");
    return name;
}

const StringName &sleeping_key() {
    static const StringName name("sleeping");
    return name;
}

Quaternion stepped_turn() {
    return Quaternion(Vector3(0.0, 0.0, 1.0), real_t(0.3));
}

Quaternion written_turn() {
    return Quaternion(Vector3(1.0, 0.0, 0.0), real_t(-0.4));
}

PhysicsServer3D *server_3d() {
    return PhysicsServer3D::get_singleton();
}

PhysicsServer2D *server_2d() {
    return PhysicsServer2D::get_singleton();
}

struct Solid {
    Node3D *parent = nullptr;
    RigidBody3D *body = nullptr;

    explicit Solid(bool p_root) {
        parent = memnew(Node3D);
        parent->set_position(PARENT_AT);
        parent->set_rotation(Vector3(0.0, 0.5, 0.0));
        body = memnew(RigidBody3D);
        body->set_gravity_scale(0.0);
        CollisionShape3D *shape = memnew(CollisionShape3D);
        Ref<SphereShape3D> sphere;
        sphere.instantiate();
        shape->set_shape(sphere);
        body->add_child(shape);
        body->set_position(AUTHORED_AT);
        if (p_root) {
            body->set_meta(netw::NetwEntityRecord::entity_meta(), true);
        }
        parent->add_child(body);
        netw::gd::scene_root()->add_child(parent);
    }

    ~Solid() {
        netw::gd::scene_root()->remove_child(parent);
        memdelete(parent);
    }

    Variant get(const StringName &p_key) const {
        return netw::port_get(body, p_key);
    }

    void set(const StringName &p_key, const Variant &p_value) {
        netw::port_set(body, p_key, p_value);
    }

    Variant state(PhysicsServer3D::BodyState p_state) const {
        return server_3d()->body_get_state(body->get_rid(), p_state);
    }

    void set_state(PhysicsServer3D::BodyState p_state, const Variant &p_value) {
        server_3d()->body_set_state(body->get_rid(), p_state, p_value);
    }

    Transform3D local_state() const {
        const Transform3D global = state(PhysicsServer3D::BODY_STATE_TRANSFORM);
        return parent->get_global_transform().affine_inverse() * global;
    }
};

struct Flat {
    Node2D *parent = nullptr;
    RigidBody2D *body = nullptr;

    Flat() {
        parent = memnew(Node2D);
        parent->set_position(PARENT_AT_2D);
        parent->set_rotation(0.5);
        body = memnew(RigidBody2D);
        body->set_gravity_scale(0.0);
        CollisionShape2D *shape = memnew(CollisionShape2D);
        Ref<RectangleShape2D> box;
        box.instantiate();
        shape->set_shape(box);
        body->add_child(shape);
        body->set_position(AUTHORED_AT_2D);
        body->set_meta(netw::NetwEntityRecord::entity_meta(), true);
        parent->add_child(body);
        netw::gd::scene_root()->add_child(parent);
    }

    ~Flat() {
        netw::gd::scene_root()->remove_child(parent);
        memdelete(parent);
    }

    Variant get(const StringName &p_key) const {
        return netw::port_get(body, p_key);
    }

    void set(const StringName &p_key, const Variant &p_value) {
        netw::port_set(body, p_key, p_value);
    }

    void set_state(PhysicsServer2D::BodyState p_state, const Variant &p_value) {
        server_2d()->body_set_state(body->get_rid(), p_state, p_value);
    }

    Transform2D local_state() const {
        const Transform2D global = server_2d()->body_get_state(
            body->get_rid(),
            PhysicsServer2D::BODY_STATE_TRANSFORM
        );
        return parent->get_global_transform().affine_inverse() * global;
    }
};

double apart(const Variant &p_a, const Vector3 &p_b) {
    return double(Vector3(p_a).distance_to(p_b));
}

double apart(const Variant &p_a, const Vector2 &p_b) {
    return double(Vector2(p_a).distance_to(p_b));
}

double turn_between(const Variant &p_a, const Quaternion &p_b) {
    return double(Quaternion(p_a).angle_to(p_b));
}

bool engine_steps_spaces() {
    return server_3d() != nullptr
        && server_3d()->has_method(StringName("space_step"));
}

RID arena_space() {
    Window *root = Object::cast_to<Window>(netw::gd::scene_root());
    if (root == nullptr) {
        return RID();
    }
    const Ref<World3D> world = root->find_world_3d();
    return world.is_valid() ? world->get_space() : RID();
}

void step_held_space() {
    const RID space = arena_space();
    server_3d()->space_set_active(space, false);
    Array one;
    one.push_back(space);
    if (server_3d()->has_method(StringName("space_flush_queries"))) {
        server_3d()->callv(StringName("space_flush_queries"), one);
    }
    Array args;
    args.push_back(space);
    args.push_back(STEP_DELTA);
    server_3d()->callv(StringName("space_step"), args);
    server_3d()->space_set_active(space, true);
}

} // namespace

TEST_CASE(
    "[Networked][BodyPort] a root RigidBody3D reads its built-in columns from "
    "the physics state before its node syncs"
) {
    Solid world(true);
    world.set_state(
        PhysicsServer3D::BODY_STATE_TRANSFORM,
        world.parent->get_global_transform()
            * Transform3D(Basis(stepped_turn()), STEPPED_AT)
    );
    world.set_state(PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY, LINEAR);
    world.set_state(PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY, ANGULAR);
    world.set_state(PhysicsServer3D::BODY_STATE_SLEEPING, true);

    REQUIRE(world.body->get_position().distance_to(AUTHORED_AT) < NEAR);
    NETW_CHECK_LT(apart(world.get(position_key()), STEPPED_AT), NEAR);
    NETW_CHECK_LT(
        turn_between(world.get(quaternion_key()), stepped_turn()),
        NEAR
    );
    NETW_CHECK_LT(apart(world.get(linear_key()), LINEAR), NEAR);
    NETW_CHECK_LT(apart(world.get(angular_key()), ANGULAR), NEAR);
    CHECK(bool(world.get(sleeping_key())));
}

TEST_CASE(
    "[Networked][BodyPort] a write to a thawed root RigidBody3D lands in the "
    "physics state before the next step"
) {
    Solid world(true);
    world.set(position_key(), WRITTEN_AT);
    world.set(quaternion_key(), written_turn());
    world.set(linear_key(), LINEAR);
    world.set(angular_key(), ANGULAR);

    const Transform3D local = world.local_state();
    NETW_CHECK_LT(double(local.origin.distance_to(WRITTEN_AT)), NEAR);
    NETW_CHECK_LT(
        double(local.basis.get_rotation_quaternion().angle_to(written_turn())),
        NEAR
    );
    NETW_CHECK_LT(
        apart(world.state(PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY), LINEAR),
        NEAR
    );
    NETW_CHECK_LT(
        apart(
            world.state(PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY),
            ANGULAR
        ),
        NEAR
    );
}

TEST_CASE(
    "[Networked][BodyPort] a write to a frozen root RigidBody3D moves its node"
) {
    Solid world(true);
    world.body->set_freeze_mode(RigidBody3D::FREEZE_MODE_KINEMATIC);
    world.body->set_freeze_enabled(true);
    world.set(position_key(), WRITTEN_AT);
    world.set(quaternion_key(), written_turn());

    NETW_CHECK_LT(
        double(world.body->get_position().distance_to(WRITTEN_AT)),
        NEAR
    );
    NETW_CHECK_LT(
        double(world.body->get_quaternion().angle_to(written_turn())),
        NEAR
    );
    NETW_CHECK_LT(apart(world.get(position_key()), WRITTEN_AT), NEAR);
}

TEST_CASE(
    "[Networked][BodyPort] a RigidBody3D that is not an entity root keeps "
    "reading its node"
) {
    Solid world(false);
    world.set_state(
        PhysicsServer3D::BODY_STATE_TRANSFORM,
        world.parent->get_global_transform()
            * Transform3D(Basis(), STEPPED_AT)
    );
    NETW_CHECK_LT(apart(world.get(position_key()), AUTHORED_AT), NEAR);
}

TEST_CASE(
    "[Networked][BodyPort] a root RigidBody2D reads and writes its built-in "
    "columns through the physics state"
) {
    Flat world;
    world.set_state(
        PhysicsServer2D::BODY_STATE_TRANSFORM,
        world.parent->get_global_transform()
            * Transform2D(real_t(TURN_2D), STEPPED_AT_2D)
    );
    world.set_state(PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY, LINEAR_2D);
    world.set_state(PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY, ANGULAR_2D);

    REQUIRE(world.body->get_position().distance_to(AUTHORED_AT_2D) < NEAR);
    NETW_CHECK_LT(apart(world.get(position_key()), STEPPED_AT_2D), NEAR);
    NETW_CHECK_CLOSE(double(world.get(rotation_key())), TURN_2D, NEAR);
    NETW_CHECK_LT(apart(world.get(linear_key()), LINEAR_2D), NEAR);
    NETW_CHECK_CLOSE(double(world.get(angular_key())), ANGULAR_2D, NEAR);

    world.set(position_key(), WRITTEN_AT_2D);
    world.set(rotation_key(), -TURN_2D);
    const Transform2D local = world.local_state();
    NETW_CHECK_LT(double(local.get_origin().distance_to(WRITTEN_AT_2D)), NEAR);
    NETW_CHECK_CLOSE(double(local.get_rotation()), -TURN_2D, NEAR);
}

TEST_CASE(
    "[Networked][BodyPort] a write to a frozen root RigidBody2D moves its node"
) {
    Flat world;
    world.body->set_freeze_mode(RigidBody2D::FREEZE_MODE_KINEMATIC);
    world.body->set_freeze_enabled(true);
    world.set(position_key(), WRITTEN_AT_2D);
    world.set(rotation_key(), TURN_2D);

    NETW_CHECK_LT(
        double(world.body->get_position().distance_to(WRITTEN_AT_2D)),
        NEAR
    );
    NETW_CHECK_CLOSE(double(world.body->get_rotation()), TURN_2D, NEAR);
    NETW_CHECK_LT(apart(world.get(position_key()), WRITTEN_AT_2D), NEAR);
}

TEST_CASE(
    "[Networked][BodyPort] fork only, a pose written into a held space is "
    "where the next manual step integrates from"
) {
    if (!engine_steps_spaces()) {
        MESSAGE("PhysicsServer3D has no space_step, so this is not the fork");
        return;
    }
    Solid world(true);
    world.set(position_key(), WRITTEN_AT);
    world.set(linear_key(), LINEAR);
    step_held_space();
    const Basis frame = world.parent->get_global_transform().basis;
    const Vector3 expected
        = WRITTEN_AT + frame.xform_inv(LINEAR) * real_t(STEP_DELTA);
    NETW_CHECK_LT(apart(world.get(position_key()), expected), STEPPED_NEAR);
}

} // namespace NetwTests

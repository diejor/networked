#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/sim/body.hpp"

namespace TestNetwSimBodyLaws {

using namespace godot;
using netw::sim::Bodies;
using netw::sim::Mode;

struct Shape {
    Node2D *root = nullptr;
    RigidBody2D *body = nullptr;
    RigidBody2D *wheel = nullptr;
    Node2D *visual = nullptr;
    Bodies bodies;

    Shape() {
        root = memnew(Node2D);
        root->set_name("SimBodyRoot");
        body = memnew(RigidBody2D);
        body->set_name("Body");
        body->set_freeze_enabled(false);
        body->set_freeze_mode(RigidBody2D::FREEZE_MODE_STATIC);
        root->add_child(body);
        wheel = memnew(RigidBody2D);
        wheel->set_name("Wheel");
        wheel->set_freeze_enabled(false);
        wheel->set_freeze_mode(RigidBody2D::FREEZE_MODE_STATIC);
        body->add_child(wheel);
        visual = memnew(Node2D);
        visual->set_name("Visual");
        visual->set_position(Vector2(0.0, -4.0));
        body->add_child(visual);
        netw::gd::scene_root()->add_child(root);

        Vector<NodePath> declared;
        declared.push_back(NodePath("Body"));
        declared.push_back(NodePath("Body/Wheel"));
        netw::sim::record_authored(
            bodies,
            root,
            declared,
            NodePath("Body/Visual")
        );
    }

    ~Shape() {
        netw::gd::scene_root()->remove_child(root);
        memdelete(root);
    }

    Shape(const Shape &) = delete;
    Shape &operator=(const Shape &) = delete;
};

bool frozen_kinematic(RigidBody2D *p_body) {
    return p_body->is_freeze_enabled()
        && p_body->get_freeze_mode() == RigidBody2D::FREEZE_MODE_KINEMATIC;
}

bool authored(RigidBody2D *p_body) {
    return !p_body->is_freeze_enabled()
        && p_body->get_freeze_mode() == RigidBody2D::FREEZE_MODE_STATIC;
}

TEST_CASE(
    "[Networked][Sim][Hosted][SceneTree] a proxy freezes every declared "
    "body kinematically, root or child, and a release returns the "
    "settings the scene authored"
) {
    Shape shape;
    NETW_CHECK_EQ(int(shape.bodies.held.size()), 2);

    CHECK_FALSE(netw::sim::transition(shape.bodies, Mode::PROXY));
    CHECK(frozen_kinematic(shape.body));
    CHECK(frozen_kinematic(shape.wheel));

    netw::sim::release(shape.bodies);
    CHECK(authored(shape.body));
    CHECK(authored(shape.wheel));
    NETW_CHECK_EQ(int(shape.bodies.applied), int(Mode::NONE));
}

TEST_CASE(
    "[Networked][Sim][Hosted][SceneTree] leaving proxy restores the "
    "authored settings and asks for the newest sample, and no other "
    "transition writes the body"
) {
    Shape shape;
    CHECK_FALSE(netw::sim::transition(shape.bodies, Mode::AUTHORITY));
    CHECK(authored(shape.body));
    CHECK_FALSE(netw::sim::transition(shape.bodies, Mode::PROXY));
    CHECK(netw::sim::transition(shape.bodies, Mode::ACTIVE));
    CHECK(authored(shape.body));
    CHECK(authored(shape.wheel));
    CHECK_FALSE(netw::sim::transition(shape.bodies, Mode::AUTHORITY));
    CHECK(authored(shape.body));
}

TEST_CASE(
    "[Networked][Sim][Hosted][SceneTree] a game freeze between transitions "
    "survives until the next transition"
) {
    Shape shape;
    netw::sim::transition(shape.bodies, Mode::AUTHORITY);
    shape.body->set_freeze_enabled(true);

    netw::sim::transition(shape.bodies, Mode::AUTHORITY);
    CHECK(shape.body->is_freeze_enabled());

    netw::sim::transition(shape.bodies, Mode::PROXY);
    netw::sim::transition(shape.bodies, Mode::AUTHORITY);
    CHECK(authored(shape.body));
}

TEST_CASE(
    "[Networked][Sim][Hosted][SceneTree] a body promoted while the game "
    "froze it resumes the setting the scene authored"
) {
    Shape shape;
    netw::sim::transition(shape.bodies, Mode::AUTHORITY);
    shape.body->set_freeze_mode(RigidBody2D::FREEZE_MODE_KINEMATIC);
    shape.body->set_freeze_enabled(true);

    netw::sim::transition(shape.bodies, Mode::PROXY);
    netw::sim::transition(shape.bodies, Mode::AUTHORITY);

    CHECK(authored(shape.body));
}

TEST_CASE(
    "[Networked][Sim][Hosted][SceneTree] a demote to proxy leaves no "
    "visual residual, so the visual root returns to its authored local "
    "transform"
) {
    Shape shape;
    netw::sim::transition(shape.bodies, Mode::AUTHORITY);
    shape.visual->set_position(Vector2(7.0, 3.0));
    shape.visual->set_rotation(0.5);

    netw::sim::transition(shape.bodies, Mode::PROXY);

    CHECK(shape.visual->get_position().is_equal_approx(Vector2(0.0, -4.0)));
    CHECK(Math::is_zero_approx(shape.visual->get_rotation()));
}

} // namespace TestNetwSimBodyLaws

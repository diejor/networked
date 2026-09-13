#include "support/frame_drive.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

#include "godot/collision_shape.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"

using namespace godot;
using netw::NetwMultiplayer;

namespace NetwTests {

namespace {

constexpr double AREA_EXTENT = 64.0;
constexpr double AWAY_X = 4000.0;
constexpr int SETTLE_FRAMES = 4;
constexpr int CARRY_FRAMES = 12;

struct PhantomEvidence {
    bool driven = false;
    int settled_enters = 0;
    int enters_after_move = 0;
    bool arrived = false;
};

PhantomEvidence &phantom_evidence() {
    static PhantomEvidence evidence;
    return evidence;
}

Area2D *seat_area(Node *p_under) {
    Area2D *area = memnew(Area2D);
    area->set_collision_layer(1);
    area->set_collision_mask(1);
    CollisionShape2D *shape = memnew(CollisionShape2D);
    Ref<RectangleShape2D> rectangle;
    rectangle.instantiate();
    rectangle->set_size(
        Vector2(real_t(AREA_EXTENT * 2.0), real_t(AREA_EXTENT * 2.0))
    );
    shape->set_shape(rectangle);
    area->add_child(shape);
    p_under->add_child(area);
    return area;
}

CharacterBody2D *seat_body(Node *p_under) {
    CharacterBody2D *body = memnew(CharacterBody2D);
    body->set_collision_layer(1);
    body->set_collision_mask(1);
    CollisionShape2D *shape = memnew(CollisionShape2D);
    Ref<RectangleShape2D> rectangle;
    rectangle.instantiate();
    rectangle->set_size(Vector2(real_t(8.0), real_t(8.0)));
    shape->set_shape(rectangle);
    body->add_child(shape);
    Node *rider = memnew(Node);
    rider->set_name("Rider");
    body->add_child(rider);
    p_under->add_child(body);
    return body;
}

class PhantomScenario final : public netw_test::FrameScenario {
    int step = 0;
    Node2D *stage = nullptr;
    Node2D *source = nullptr;
    Node2D *destination = nullptr;
    Area2D *area = nullptr;
    CharacterBody2D *body = nullptr;
    netw_test::CallLog *enters = nullptr;
    Ref<NetwMultiplayer> core;
    int settled = 0;

    void open() {
        core.instantiate();
        enters = new netw_test::CallLog();
        stage = memnew(Node2D);
        source = memnew(Node2D);
        destination = memnew(Node2D);
        destination->set_position(Vector2(real_t(AWAY_X), 0.0));
        stage->add_child(source);
        stage->add_child(destination);
        netw::gd::scene_root()->add_child(stage);
        area = seat_area(source);
        body = seat_body(source);
        area->connect(
            StringName("body_entered"),
            enters->callable(StringName("entered"))
        );
    }

    void close() {
        PhantomEvidence &evidence = phantom_evidence();
        evidence.driven = true;
        evidence.settled_enters = settled;
        evidence.enters_after_move
            = int(enters->count(StringName("entered"))) - settled;
        Node *landed = body->get_parent();
        evidence.arrived = landed == destination;

        netw::gd::scene_root()->remove_child(stage);
        memdelete(stage);
        stage = nullptr;
        source = nullptr;
        destination = nullptr;
        area = nullptr;
        body = nullptr;
        delete enters;
        enters = nullptr;
        core.unref();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        if (step == 0) {
            open();
            ++step;
            return true;
        }
        if (step <= SETTLE_FRAMES) {
            ++step;
            return true;
        }
        if (step == SETTLE_FRAMES + 1) {
            settled = int(enters->count(StringName("entered")));
            core->spawn_reparent_node(body, destination, Callable());
            ++step;
            return true;
        }
        if (step <= SETTLE_FRAMES + CARRY_FRAMES) {
            ++step;
            return true;
        }
        close();
        return false;
    }
};

NETW_FRAME_SCENARIO(PhantomScenario, phantom_scenario);

} // namespace

TEST_CASE(
    "[Networked][Spawn][Frame] SR1 a body overlapping an area enters it once "
    "while it stands still, which is the reading the move is measured against"
) {
    const PhantomEvidence &evidence = phantom_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_EQ(evidence.settled_enters, 1);
}

TEST_CASE(
    "[Networked][Spawn][Frame] SR2 a peer applying a reparent frame carries "
    "the body off the physics server first, so the area it leaves reports no "
    "arrival it never saw"
) {
    const PhantomEvidence &evidence = phantom_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.arrived);
    NETW_CHECK_EQ(evidence.enters_after_move, 0);
}

} // namespace NetwTests

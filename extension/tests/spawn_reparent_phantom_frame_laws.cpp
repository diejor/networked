#include "support/frame_drive.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/liveness_core.hpp"

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
    int exits_after_move = 0;
    int landed_on = 0;
    bool arrived = false;
    bool left_area = false;
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
    bool replicated = false;

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
        area->connect(
            StringName("body_exited"),
            enters->callable(StringName("exited"))
        );
        Area2D *sensor = seat_area(body);
        sensor->set_collision_layer(0);
        sensor->set_collision_mask(2);
        sensor->connect(
            StringName("body_entered"),
            enters->callable(StringName("sensed"))
        );
        CharacterBody2D *waiting = seat_body(destination);
        waiting->set_collision_layer(2);
        waiting->set_collision_mask(0);
    }

    void close() {
        PhantomEvidence &evidence = phantom_evidence();
        evidence.driven = true;
        evidence.settled_enters = settled;
        evidence.enters_after_move
            = int(enters->count(StringName("entered"))) - settled;
        evidence.exits_after_move = int(enters->count(StringName("exited")));
        evidence.landed_on = int(enters->count(StringName("sensed")));
        Node *landed = body->get_parent();
        evidence.arrived = landed == destination;
        evidence.left_area
            = double(body->get_global_position().x) > AREA_EXTENT * 2.0;

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
            if (!replicated && body->get_parent() == destination) {
                body->set_position(Vector2());
                replicated = true;
            }
            ++step;
            return true;
        }
        close();
        return false;
    }
};

NETW_FRAME_SCENARIO(PhantomScenario, phantom_scenario);

constexpr double HUSH_X = -20000.0;
constexpr int HUSH_WATCH_FRAMES = 4;
constexpr int HUSH_LEAVE_FRAMES = 3;

enum HushShape {
    HUSH_LEVEL,
    HUSH_CHILD_AREA,
    HUSH_MOVED_BODY,
    HUSH_SHAPES,
};

enum HushCarry {
    HUSH_SERVER_CARRY,
    HUSH_CLIENT_CARRY,
    HUSH_CARRIES,
};

constexpr int HUSH_ROWS = HUSH_SHAPES * HUSH_CARRIES;

struct HushEvidence {
    bool driven = false;
    int settled_enters = 0;
    int exits_in_window = 0;
    int enters_in_window = 0;
    bool overlapping_after = false;
    bool arrived = false;
    int exits_after_leaving = 0;
};

HushEvidence &hush_evidence(int p_row) {
    static HushEvidence rows[HUSH_ROWS];
    return rows[p_row];
}

class AreaHushScenario final : public netw_test::FrameScenario {
    int row = 0;
    int step = 0;
    Ref<NetwMultiplayer> core;
    Node2D *stage = nullptr;
    Node2D *mover = nullptr;
    Node2D *dock = nullptr;
    Area2D *area = nullptr;
    CharacterBody2D *body = nullptr;
    netw_test::CallLog *signals = nullptr;
    int enters_before = 0;
    int exits_before = 0;

    RID seat(Node *p_node) {
        const Ref<netw::NetwEntity> entity = netw::NetwEntity::ensure(p_node);
        const RID handle = core->get_liveness_core()->entity_create();
        entity->get_record()->adopt_handle(handle);
        core->liveness_bind(
            handle,
            core->get_liveness_core()->reserve_route(),
            entity,
            entity->get_record(),
            p_node
        );
        p_node->set_meta(NetwMultiplayer::wrapper_meta(), Variant(entity));
        return handle;
    }

    int heard(const char *p_tag) const {
        return int(signals->count(StringName(p_tag)));
    }

    void open() {
        core.instantiate();
        signals = new netw_test::CallLog();
        stage = memnew(Node2D);
        stage->set_position(Vector2(real_t(HUSH_X), 0.0));
        dock = memnew(Node2D);
        dock->set_name("Dock");
        stage->add_child(dock);
        netw::gd::scene_root()->add_child(stage);
        const int shape = row / HUSH_CARRIES;
        if (shape == HUSH_MOVED_BODY) {
            area = seat_area(stage);
            body = seat_body(stage);
            mover = body;
        } else {
            mover = memnew(Node2D);
            mover->set_name("Mover");
            stage->add_child(mover);
            area = seat_area(mover);
            body = seat_body(shape == HUSH_LEVEL ? mover : stage);
        }
        area->connect(
            StringName("body_entered"),
            signals->callable(StringName("entered"))
        );
        area->connect(
            StringName("body_exited"),
            signals->callable(StringName("exited"))
        );
        seat(mover);
        if (shape == HUSH_LEVEL) {
            netw::NetwEntity::of(mover)->set_declares_scene(true);
        }
        seat(dock);
    }

    void move() {
        enters_before = heard("entered");
        exits_before = heard("exited");
        hush_evidence(row).settled_enters = enters_before;
        if (row % HUSH_CARRIES == HUSH_SERVER_CARRY) {
            core->scene_move_entity(
                netw::NetwEntity::of(mover)->get_rid_handle(),
                netw::NetwEntity::of(dock)->get_rid_handle()
            );
        } else {
            core->spawn_reparent_node(mover, dock, Callable());
        }
    }

    void watch() {
        HushEvidence &evidence = hush_evidence(row);
        evidence.exits_in_window = heard("exited") - exits_before;
        evidence.enters_in_window = heard("entered") - enters_before;
        evidence.overlapping_after
            = area->get_overlapping_bodies().has(Variant(body));
        evidence.arrived = mover->get_parent() == dock;
        body->set_position(Vector2(real_t(AWAY_X), 0.0));
    }

    void close() {
        HushEvidence &evidence = hush_evidence(row);
        evidence.driven = true;
        evidence.exits_after_leaving
            = heard("exited") - exits_before - evidence.exits_in_window;
        core->scene_dispose();
        netw::gd::scene_root()->remove_child(stage);
        memdelete(stage);
        stage = nullptr;
        mover = nullptr;
        dock = nullptr;
        area = nullptr;
        body = nullptr;
        delete signals;
        signals = nullptr;
        core.unref();
        step = 0;
        ++row;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || row >= HUSH_ROWS) {
            return false;
        }
        const int moved_at = SETTLE_FRAMES + 1;
        const int watched_at = moved_at + HUSH_WATCH_FRAMES;
        if (step == 0) {
            open();
        } else if (step == moved_at) {
            move();
        } else if (step == watched_at) {
            watch();
        } else if (step == watched_at + HUSH_LEAVE_FRAMES) {
            close();
            return row < HUSH_ROWS;
        }
        ++step;
        return true;
    }
};

NETW_FRAME_SCENARIO(AreaHushScenario, area_hush_scenario);

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
    "arrival it never saw and one exit, while an area the body carries "
    "reports the body it lands on"
) {
    const PhantomEvidence &evidence = phantom_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.arrived);
    CHECK(evidence.left_area);
    NETW_CHECK_EQ(evidence.enters_after_move, 0);
    NETW_CHECK_EQ(evidence.exits_after_move, 1);
    NETW_CHECK_EQ(evidence.landed_on, 1);
}

TEST_CASE(
    "[Networked][Scene][Frame] LH1 an entity moved in place by the server's "
    "carry or a client's carry fires no area enter or exit, keeps the "
    "overlap, and the area reports a real exit after the move"
) {
    for (int row = 0; row < HUSH_ROWS; ++row) {
        const int shape = row / HUSH_CARRIES;
        const int carry = row % HUSH_CARRIES;
        CAPTURE(shape);
        CAPTURE(carry);
        const HushEvidence &evidence = hush_evidence(row);
        REQUIRE(evidence.driven);
        NETW_CHECK_EQ(evidence.settled_enters, 1);
        CHECK(evidence.arrived);
        NETW_CHECK_EQ(evidence.exits_in_window, 0);
        NETW_CHECK_EQ(evidence.enters_in_window, 0);
        CHECK(evidence.overlapping_after);
        NETW_CHECK_EQ(evidence.exits_after_leaving, 1);
    }
}

} // namespace NetwTests

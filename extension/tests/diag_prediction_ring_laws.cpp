#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED) && defined(DEBUG_ENABLED)

#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction.hpp"
#include "netw/diag/prediction_rings.hpp"
#include "netw/predict/engine.hpp"
#include "support/loopback_rig.h"

using namespace godot;

namespace TestDiagPredictionRingLaws {

using netw::NetwPrediction;
using netw::diag::MARK_OURS;
using netw::diag::MARK_SERVER;
using netw::diag::MARK_SHOWN;
using netw::diag::PredictionRings;
using netw::diag::RingRow;
using netw_test::EntityDecl;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr int WARM = 12;

EntityDecl walker(const StringName &p_name, const Vector2 &p_at = Vector2()) {
    return EntityDecl()
        .named(p_name)
        .on_schema("RingPose")
        .synced("position")
        .synced("lean")
        .placed_at(p_at)
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK)
        .mounted();
}

struct Watch {
    netw::NetwMultiplayer *session = nullptr;
    PredictionRings rings;
    double largest_error = 0.0;
    bool episode_seen = false;

    explicit Watch(netw::NetwMultiplayer *p_session) :
            session(p_session) {
        REQUIRE_MESSAGE(session != nullptr, "a watch needs a session");
    }

    void poll() {
        session->diag_gather(rings.sources);
        rings.sample(*session);
        for (const RingRow &row : rings.rows()) {
            if (row.erred()) {
                largest_error = MAX(largest_error, row.gap);
            }
            if (row.role == NetwPrediction::ROLE_PREDICT
                && !row.text.is_empty()
                && !row.text.begins_with("no episode")) {
                episode_seen = true;
            }
        }
    }

    const RingRow *only(int p_role) const {
        const RingRow *found = nullptr;
        for (const RingRow &row : rings.rows()) {
            if (row.role == p_role) {
                REQUIRE_MESSAGE(found == nullptr, "two rows share a role");
                found = &row;
            }
        }
        return found;
    }
};

void advance(LoopbackRig &p_rig, Watch &p_watch, int p_ticks) {
    for (int at = 0; at < p_ticks; ++at) {
        p_rig.step_ticks(1);
        p_watch.poll();
    }
}

Node2D *node_at(LoopbackRig &p_rig, const StringName &p_name, int p_client) {
    Node2D *node = Object::cast_to<Node2D>(
        p_rig.node_of(p_rig.entity_of(p_name, p_client), p_client)
    );
    REQUIRE(node != nullptr);
    return node;
}

void walking(LoopbackRig &p_rig, const Vector2 &p_motion) {
    WorldDecl world;
    world.clocked(30, 3).lag_compensated().player(walker("P"), 0);
    p_rig.declare_world(world);
    node_at(p_rig, "P", 0)->set("motion", p_motion);
}

uint64_t serial_of(LoopbackRig &p_rig, const StringName &p_name) {
    netw::NetwMultiplayer *session = p_rig.client(0);
    netw::NetwPredictionEngine *pool = session->get_prediction_engine();
    const int64_t slot = pool->slot_of(
        netw::NetwEntity::of(p_rig.node_of(p_rig.entity_of(p_name, 0), 0))
    );
    const netw::predict::EvaluationRecord *record
        = pool->last_evaluation_of(slot);
    return record != nullptr ? record->serial : 0;
}

TEST_CASE(
    "[Networked][Diag] R1 an exact replay draws the server and ours on one "
    "spot and opens no episode"
) {
    LoopbackRig rig(1);
    rig.mount();
    walking(rig, Vector2(1.0, 0.0));
    Watch watch(rig.client(0));
    advance(rig, watch, WARM * 2);

    const RingRow *row = watch.only(NetwPrediction::ROLE_PREDICT);
    REQUIRE_MESSAGE(row != nullptr, "nothing was drawn");
    CHECK(row->has(MARK_SERVER));
    CHECK(row->has(MARK_OURS));
    NETW_CHECK_LE(row->gap, row->epsilon);
    CHECK_FALSE(row->erred());
    CHECK_FALSE(watch.episode_seen);
    const bool worded = row->text.begins_with("no episode");
    CHECK(worded);
}

TEST_CASE(
    "[Networked][Diag] R2 a nudge the client cannot predict draws an error "
    "as long as the nudge"
) {
    LoopbackRig rig(1);
    rig.mount();
    walking(rig, Vector2(1.0, 0.0));
    Watch watch(rig.client(0));
    advance(rig, watch, WARM);

    Node2D *authority = node_at(rig, "P", -1);
    authority->set_position(authority->get_position() + Vector2(0.0, 3.0));
    advance(rig, watch, WARM);

    NETW_CHECK_CLOSE(watch.largest_error, 3.0, 0.001);
}

TEST_CASE(
    "[Networked][Diag] R3 the host draws nothing for the entities it serves "
    "or controls"
) {
    LoopbackRig rig(1);
    rig.mount();
    WorldDecl world;
    world.clocked(30, 3).lag_compensated().player(walker("P"), 0).hosted(
        walker("H", Vector2(40.0, 0.0))
    );
    rig.declare_world(world);
    Watch watch(rig.server());
    advance(rig, watch, WARM);

    NETW_CHECK_EQ(int64_t(watch.rings.sources.size()), int64_t(2));
    NETW_CHECK_EQ(int64_t(watch.rings.rows().size()), int64_t(0));
}

TEST_CASE(
    "[Networked][Diag] R4 a slot with no comparison yet draws neither the "
    "server ring nor ours"
) {
    LoopbackRig rig(1);
    rig.mount();
    walking(rig, Vector2(1.0, 0.0));
    REQUIRE(serial_of(rig, "P") == 0);
    Watch watch(rig.client(0));
    watch.poll();

    const RingRow *row = watch.only(NetwPrediction::ROLE_PREDICT);
    REQUIRE_MESSAGE(row != nullptr, "nothing was drawn");
    CHECK(row->has(MARK_SHOWN));
    CHECK_FALSE(row->has(MARK_SERVER));
    CHECK_FALSE(row->has(MARK_OURS));
}

TEST_CASE(
    "[Networked][Diag] R5 a world holding more predicted entities than the "
    "cap draws the cap, nearest to the camera"
) {
    LoopbackRig rig(1);
    rig.mount();
    WorldDecl world;
    world.clocked(30, 3).lag_compensated();
    const uint32_t count = PredictionRings::ENTITY_CAP + 1;
    for (uint32_t at = 0; at < count; ++at) {
        world.player(
            walker(
                StringName(vformat("P%d", at)),
                Vector2(10000.0 + 10.0 * at, 0.0)
            ),
            0
        );
    }
    rig.declare_world(world);
    Watch watch(rig.client(0));
    advance(rig, watch, 2);

    NETW_CHECK_EQ(
        int64_t(watch.rings.rows().size()),
        int64_t(PredictionRings::ENTITY_CAP)
    );
    for (const RingRow &row : watch.rings.rows()) {
        NETW_CHECK_LT(row.simulated.x, 10000.0 + 10.0 * (count - 1) - 1.0);
    }
}

TEST_CASE(
    "[Networked][Diag] R6 a correction with no position gap still shows its "
    "episode on the state line"
) {
    LoopbackRig rig(1);
    rig.mount();
    walking(rig, Vector2(1.0, 0.0));
    Watch watch(rig.client(0));
    advance(rig, watch, WARM);

    Node2D *authority = node_at(rig, "P", -1);
    authority->set("lean", Vector2(authority->get("lean")) + Vector2(3.0, 0.0));
    advance(rig, watch, WARM);

    CHECK(watch.episode_seen);
}

TEST_CASE(
    "[Networked][Diag] R7 an active replica draws what is shown, what is "
    "simulated and what the server sent, and no prediction of ours"
) {
    LoopbackRig rig(1);
    rig.mount();
    Vector<StringName> members;
    members.push_back(StringName("Ball"));
    WorldDecl world;
    world.clocked(30, 3)
        .lag_compensated()
        .player(walker("P"), 0)
        .hosted(walker("Ball", Vector2(20.0, 0.0)))
        .island("P", members)
        .simulating("Ball");
    rig.declare_world(world);
    Watch watch(rig.client(0));
    advance(rig, watch, WARM);

    const RingRow *row = watch.only(NetwPrediction::ROLE_SIMULATE);
    REQUIRE_MESSAGE(row != nullptr, "nothing was drawn");
    CHECK(row->has(MARK_SHOWN));
    CHECK(row->has(MARK_SERVER));
    CHECK_FALSE(row->has(MARK_OURS));
    NETW_CHECK_CLOSE(row->server.x, 20.0, 0.5);
}

TEST_CASE(
    "[Networked][Diag] R10 a replica's text names how old the server's "
    "state is, and a predicted entity's names its episode"
) {
    LoopbackRig rig(1);
    rig.mount();
    Vector<StringName> members;
    members.push_back(StringName("Ball"));
    WorldDecl world;
    world.clocked(30, 3)
        .lag_compensated()
        .player(walker("P"), 0)
        .hosted(walker("Ball", Vector2(20.0, 0.0)))
        .island("P", members)
        .simulating("Ball");
    rig.declare_world(world);
    Watch watch(rig.client(0));
    advance(rig, watch, WARM);

    const RingRow *own = watch.only(NetwPrediction::ROLE_PREDICT);
    const RingRow *replica = watch.only(NetwPrediction::ROLE_SIMULATE);
    REQUIRE_MESSAGE(own != nullptr, "the predicted player drew nothing");
    REQUIRE_MESSAGE(replica != nullptr, "the replica drew nothing");
    const bool own_worded = own->text.begins_with("no episode");
    const bool replica_worded = replica->text.begins_with("replica  server");
    CHECK(own_worded);
    CHECK(replica_worded);
}

TEST_CASE(
    "[Networked][Diag] R8 a comparison older than a second draws dimmed"
) {
    LoopbackRig rig(1);
    rig.mount();
    walking(rig, Vector2(1.0, 0.0));
    Watch watch(rig.client(0));
    advance(rig, watch, WARM);

    const RingRow *fresh = watch.only(NetwPrediction::ROLE_PREDICT);
    REQUIRE_MESSAGE(fresh != nullptr, "nothing was drawn");
    REQUIRE_MESSAGE(fresh->has(MARK_SERVER), "no server ring was drawn");
    NETW_CHECK_CLOSE(fresh->confirmed_alpha, 1.0, 0.0001);

    rig.hold(0);
    advance(rig, watch, 45);
    const RingRow *stale = watch.only(NetwPrediction::ROLE_PREDICT);
    REQUIRE_MESSAGE(stale != nullptr, "nothing was drawn");
    NETW_CHECK_GT(stale->confirmed_age, PredictionRings::FRESH_SECONDS);
    NETW_CHECK_CLOSE(
        stale->confirmed_alpha,
        PredictionRings::STALE_ALPHA,
        0.0001
    );
    rig.release(0);
}

TEST_CASE(
    "[Networked][Diag] R9 a pose local to a moved parent draws in world space"
) {
    LoopbackRig rig(1);
    rig.mount();
    walking(rig, Vector2());
    Node2D *frame = memnew(Node2D);
    rig.branch(0)->add_child(frame);
    frame->set_position(Vector2(200.0, 100.0));
    node_at(rig, "P", 0)->reparent(frame, false);
    Watch watch(rig.client(0));
    advance(rig, watch, WARM);

    const RingRow *row = watch.only(NetwPrediction::ROLE_PREDICT);
    REQUIRE_MESSAGE(row != nullptr, "nothing was drawn");
    REQUIRE_MESSAGE(row->has(MARK_SERVER), "no server ring was drawn");
    NETW_CHECK_CLOSE(row->simulated.x, 200.0, 0.01);
    NETW_CHECK_CLOSE(row->server.x, row->simulated.x, 0.01);
    NETW_CHECK_CLOSE(row->server.y, row->simulated.y, 0.01);
}

} // namespace TestDiagPredictionRingLaws

#endif

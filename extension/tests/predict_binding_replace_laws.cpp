#include "support/netw_test.h"
#include "support/fault_log.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction.hpp"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"

using namespace godot;

namespace TestNetwPredictBindingReplaceLaws {

using netw::NetwMultiplayer;
using netw_test::EntityDecl;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr int SETTLE_TICKS = 20;

EntityDecl leaning_player() {
    return EntityDecl()
        .named("P")
        .on_schema("RebindPose")
        .synced("position")
        .synced("lean")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK);
}

void open_world(LoopbackRig &r_rig) {
    r_rig.declare_world(
        WorldDecl().clocked(30, 3).lag_compensated().player(leaning_player(), 0)
    );
}

void drive(LoopbackRig &r_rig, int p_ticks, const Vector2 &p_motion) {
    for (int at = 0; at < p_ticks; ++at) {
        Node *owner = r_rig.node_of(r_rig.entity_of("P", 0), 0);
        REQUIRE(owner != nullptr);
        owner->set("motion", p_motion);
        owner->set("bombing", true);
        r_rig.step_ticks(1);
    }
}

Error replace(
    NetwMultiplayer *p_api,
    const RID &p_entity,
    const char *p_schema,
    const char *p_field,
    NetwMultiplayer::ColumnType p_type,
    NetwMultiplayer::RecordKind p_kind
) {
    const RID schema = p_api->schema_create(StringName(p_schema));
    p_api->schema_add_column(schema, StringName(p_field), p_type, 1);
    p_api->schema_seal(schema);
    const RID set = p_api->property_set_create(schema, p_kind);
    p_api->property_set_add_column(set, 0);
    p_api->property_set_seal(set);
    return p_api->entity_add_property_set(p_entity, set, 0);
}

int64_t frames_sent(const LoopbackRig &p_rig) {
    const Dictionary stats = p_rig.prediction_handle(StringName("P"), 0)
                                 ->get_stats()
                                 ->to_dictionary();
    return int64_t(stats[StringName("command_frames_sent")]);
}

TEST_CASE(
    "[Networked][Predict] a state set replaced on a predicted entity is what "
    "its reachability answers from"
) {
    const netw_test::FaultWatch faults;
    LoopbackRig rig(1);
    open_world(rig);
    drive(rig, SETTLE_TICKS, Vector2(1.0, 0.0));
    const Dictionary before
        = rig.prediction_handle(StringName("P"), 0)->reachability();
    REQUIRE(Dictionary(before["fields"]).has(StringName("lean")));

    const Error added = replace(
        rig.client(0),
        rig.entity_of("P", 0),
        "RebindPositionOnly",
        "position",
        NetwMultiplayer::COLUMN_VECTOR2,
        NetwMultiplayer::RECORD_KIND_STATE
    );
    REQUIRE(int(added) == int(OK));
    drive(rig, SETTLE_TICKS, Vector2(0.0, 1.0));

    const Dictionary fields
        = rig.prediction_handle(StringName("P"), 0)->reachability()["fields"];
    CHECK(fields.has(StringName("position")));
    CHECK_FALSE(fields.has(StringName("lean")));
    NETW_CHECK_EQ(
        int64_t(rig.client(0)->property_binding_book().retired_count()),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        faults.warnings_containing("causal state properties"),
        1
    );
}

TEST_CASE(
    "[Networked][Predict] an input set replaced on both peers is what the "
    "commands carry, so a field it dropped no longer reaches the server"
) {
    LoopbackRig rig(1);
    open_world(rig);
    drive(rig, SETTLE_TICKS, Vector2(1.0, 0.0));
    Node *served = rig.node_of(rig.entity_of("P"));
    REQUIRE(served != nullptr);
    REQUIRE(bool(served->get("bombing")));
    served->set("bombing", false);

    for (int client = -1; client < 1; ++client) {
        NetwMultiplayer *api = client < 0 ? rig.server() : rig.client(0);
        const Error added = replace(
            api,
            rig.entity_of("P", client),
            "RebindMotionOnly",
            "motion",
            NetwMultiplayer::COLUMN_VECTOR2,
            NetwMultiplayer::RECORD_KIND_INPUT
        );
        REQUIRE(int(added) == int(OK));
    }
    drive(rig, SETTLE_TICKS, Vector2(0.0, 1.0));

    const Vector2 motion = served->get("motion");
    NETW_CHECK_CLOSE(motion.y, 1.0, 0.001);
    CHECK_FALSE(bool(served->get("bombing")));
}

TEST_CASE(
    "[Networked][Predict] a client that clears its session state sends no "
    "further command frame for the slot it held"
) {
    LoopbackRig rig(1);
    open_world(rig);
    drive(rig, SETTLE_TICKS, Vector2(1.0, 0.0));
    REQUIRE(frames_sent(rig) > 0);

    rig.client(0)->clear_session_state();
    const int64_t at_clear = frames_sent(rig);
    drive(rig, SETTLE_TICKS, Vector2(0.0, 1.0));

    NETW_CHECK_EQ(frames_sent(rig), at_clear);
    NETW_CHECK_EQ(
        int64_t(rig.client(0)->property_binding_book().count()),
        int64_t(0)
    );
}

} // namespace TestNetwPredictBindingReplaceLaws

#endif

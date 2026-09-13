#include "support/loopback_rig.h"

#include "netw/api/sync_pipeline.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestDeclaredReconciliationProbe {

using namespace godot;
using namespace netw_test;

TEST_CASE("[Networked][DeclaredProbe] treeless state reaches encoding") {
    LoopbackRig rig(1);
    netw::NetwMultiplayer *api = rig.server();
    const RID entity
        = rig.declare_entity(EntityDecl().named("DeclaredBody").on_route(71));

    const RID schema = api->schema_create(StringName("DeclaredState"));
    api->schema_add_column(
        schema,
        StringName("position"),
        netw::NetwMultiplayer::ColumnType(int(Variant::VECTOR2)),
        1
    );
    api->schema_seal(schema);

    const RID state = api->property_set_create(
        schema,
        netw::NetwMultiplayer::RECORD_KIND_STATE
    );
    api->property_set_add_column(state, 0);
    api->property_set_seal(state);
    NETW_CHECK_EQ(int(api->entity_add_property_set(entity, state, 0)), int(OK));

    netw::SyncPipeline *pipeline = api->sync_pipeline();
    REQUIRE(pipeline != nullptr);
    const Dictionary before = pipeline->counters();

    pipeline->pump(1);

    const Dictionary after = pipeline->counters();
    NETW_CHECK_EQ(int(after["derived_sets_active"]), 1);
    NETW_CHECK_EQ(int(after["derived_frames_in"]), 0);
    NETW_CHECK_EQ(
        int(after["sync_pump_skips_invalid_node"]),
        int(before["sync_pump_skips_invalid_node"])
    );
}

} // namespace TestDeclaredReconciliationProbe

#endif

#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

namespace TestDeclaredReconciliationProbe {

using namespace godot;
using namespace netw_test;

TEST_CASE(
    "[Networked][DeclaredProbe] treeless state reaches encoding"
) {
    LoopbackRig rig(1);
    Object *api = rig.server();
    const RID entity = rig.declare_entity(
        EntityDecl().named("DeclaredBody").on_route(71)
    );

    const RID schema = api->call(
        "schema_create",
        StringName("DeclaredState")
    );
    api->call(
        "schema_add_column",
        schema,
        StringName("position"),
        int(Variant::VECTOR2)
    );
    api->call("schema_seal", schema);

    const RID state = api->call("property_set_create", schema, 1);
    api->call("property_set_add_column", state, 0);
    api->call("property_set_seal", state);
    NETW_CHECK_EQ(
        int(api->call("entity_add_property_set", entity, state, 0)),
        int(OK)
    );

    Object *replication = api->get("_replication");
    REQUIRE(replication != nullptr);
    Object *pipeline = replication->get("_sync_pipeline");
    REQUIRE(pipeline != nullptr);
    const Dictionary before = pipeline->call("counters");

    pipeline->call("pump", 1);

    const Dictionary after = pipeline->call("counters");
    NETW_CHECK_EQ(int(after["derived_sets_active"]), 1);
    NETW_CHECK_EQ(int(after["derived_frames_in"]), 0);
    NETW_CHECK_EQ(
        int(after["sync_pump_skips_invalid_node"]),
        int(before["sync_pump_skips_invalid_node"])
    );
}

} // namespace TestDeclaredReconciliationProbe

#endif

/* Flat interest and display laws, stated against a real session from C++.
 *
 * Both families are native already, so what these pin is the flat RID surface
 * over them: that a layer's lifecycle, its policy and its callbacks are all
 * addressed by entity handles, and that a display track declares, records,
 * snaps and reports through the same handles.
 *
 * Every case declares its subject rather than assembling one. That is the whole
 * difference from the GDScript suite these replace: there, four lines of tree
 * and node construction stood between the reader and the law, and the tree was
 * never what any of them were about.
 *
 * Not `[Hosted]`: the rig loads the session script from `res://`, which the
 * module tier cannot see.
 */

#include "support/netw_test.h"

#include "support/entity_decl.h"
#include "support/loopback_rig.h"
#include "support/netw_call_log.h"
#include "netw/interpolate.hpp"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/vector2.hpp>

using namespace godot;
using namespace netw;
using namespace netw_test;

namespace {

// The layer param and policy ordinals the flat surface publishes. Named here
// because a case reading a bare 0 says nothing about which selector it meant.
constexpr int LAYER_PARAM_POLICY = 0;
constexpr int LAYER_POLICY_HIDE_FROM_INSIDERS = 1;
constexpr int DISPLAY_PARAM_ROLE = 0;
constexpr int DISPLAY_ROLE_REMOTE = 1;

// The spec every display case declares its track with. Built rather than
// declared because a Ref is not a constant expression.
Ref<NetwInterpolate> lerp_spec() {
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    return spec->lerp();
}

} // namespace

TEST_CASE("[Networked][Interest] a layer's lifecycle is addressed by handles") {
    LoopbackRig rig;
    Object *api = rig.server();
    const RID entity
        = rig.declare_entity(EntityDecl().named("InterestSubject").on_route(31));
    const RID layer = api->call("layer_create", "arena");

    REQUIRE(layer.is_valid());
    CHECK(bool(RID(api->call("layer_find", "arena")) == layer));
    NETW_CHECK_EQ(int(api->call("layer_add_viewer", layer, 7)), OK);
    NETW_CHECK_EQ(int(api->call("layer_add_entity", layer, entity)), OK);
    NETW_CHECK_EQ(int(api->call("interest_flush")), OK);
    CHECK(bool(api->call("interest_is_filtered", entity)));
    CHECK(bool(api->call("interest_admits", entity, 7)));

    const Array membership = api->call("interest_get_membership", entity);
    NETW_CHECK_EQ(membership.size(), 1);
    CHECK(bool(RID(membership[0]) == layer));
    const PackedInt64Array row = api->call("interest_get_row", entity);
    CHECK(bool(row.size() > 0));

    api->call("layer_remove_viewer", layer, 7);
    api->call("interest_flush");
    CHECK_FALSE(bool(api->call("interest_admits", entity, 7)));
    api->call("layer_free", layer);
    CHECK_FALSE(bool(RID(api->call("layer_find", "arena")).is_valid()));
}

TEST_CASE("[Networked][Interest] a driver and a monitor exchange entity handles") {
    LoopbackRig rig;
    Object *api = rig.server();
    const RID entity
        = rig.declare_entity(EntityDecl().named("DrivenSubject").on_route(32));
    const RID layer = api->call("layer_create", "sensor");

    CallLog log;
    Array driven;
    driven.push_back(entity);
    api->call("layer_set_monitor_callback", layer, log.callable("monitor"));
    api->call("layer_set_driver_callback", layer, log.answering("driver", driven));
    api->call("layer_add_viewer", layer, 9);

    NETW_CHECK_EQ(int(api->call("interest_flush")), OK);
    CHECK(bool(api->call("interest_admits", entity, 9)));

    const Array edge = log.args("monitor");
    NETW_CHECK_EQ(edge.size(), 3);
    REQUIRE(bool(edge.size() == 3));
    CHECK(bool(edge[0]));
    CHECK(bool(RID(edge[1]) == entity));
    NETW_CHECK_EQ(int(edge[2]), 9);
}

TEST_CASE("[Networked][Interest] a policy is selected by the flat param") {
    LoopbackRig rig;
    Object *api = rig.server();
    const RID entity
        = rig.declare_entity(EntityDecl().named("PolicySubject").on_route(33));
    const RID layer = api->call("layer_create", "inverse");

    api->call("layer_add_entity", layer, entity);
    api->call("layer_add_viewer", layer, 11);
    api->call(
        "layer_set_param",
        layer,
        LAYER_PARAM_POLICY,
        LAYER_POLICY_HIDE_FROM_INSIDERS
    );
    api->call("interest_flush");

    CHECK_FALSE(bool(api->call("interest_admits", entity, 11)));
    CHECK_FALSE(bool(String(api->call("interest_explain", entity, 11)).is_empty()));
}

TEST_CASE("[Networked][Display] a track declares, records and reports by handle") {
    LoopbackRig rig;
    Object *api = rig.server();
    const RID entity
        = rig.declare_entity(EntityDecl().named("DisplaySubject").on_route(41));

    NETW_CHECK_EQ(int(api->call("display_declare", entity, 0, "position", lerp_spec())), OK);
    api->call("display_set_param", entity, DISPLAY_PARAM_ROLE, DISPLAY_ROLE_REMOTE);
    NETW_CHECK_EQ(int(api->call(
            "display_record",
            entity,
            "position",
            Vector2(2.0, 3.0),
            4
        )), OK);
    api->call("display_snap", entity, "position", Vector2(5.0, 6.0));

    CHECK(bool(
        Vector2(api->call("display_get_value", entity, "position"))
        == Vector2(5.0, 6.0)
    ));
    NETW_CHECK_EQ(
        int(api->call("display_get_track_stat", entity, "position", "buffer_size")),
        0
    );
    NETW_CHECK_EQ(
        int(api->call("display_get_param", entity, DISPLAY_PARAM_ROLE)),
        DISPLAY_ROLE_REMOTE
    );
}

TEST_CASE("[Networked][Display] the output callback carries the public tuple") {
    LoopbackRig rig;
    Object *api = rig.server();
    const RID entity
        = rig.declare_entity(EntityDecl().named("CallbackSubject").on_route(42));

    CallLog log;
    api->call("display_declare", entity, 0, "position", lerp_spec());
    api->call("display_set_callback", entity, log.callable("write"));
    api->call("display_snap", entity, "position", Vector2(1.0, 1.0));

    const Array write = log.args("write");
    NETW_CHECK_EQ(write.size(), 3);
    REQUIRE(bool(write.size() == 3));
    CHECK(bool(RID(write[0]) == entity));
    CHECK(bool(StringName(write[1]) == StringName("position")));
    CHECK(bool(Vector2(write[2]) == Vector2(1.0, 1.0)));
}

#endif

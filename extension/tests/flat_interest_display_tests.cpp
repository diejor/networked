#include "support/netw_test.h"

#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/entity_decl.h"
#include "support/loopback_rig.h"
#include "support/netw_call_log.h"

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

Ref<NetwInterpolate> lerp_spec() {
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    return spec->lerp();
}

} // namespace

TEST_CASE("[Networked][Interest] a layer's lifecycle is addressed by handles") {
    LoopbackRig rig;
    NetwMultiplayer *api = rig.server();
    const RID entity = rig.declare_entity(
        EntityDecl().named("InterestSubject").on_route(31)
    );
    const RID layer = api->interest_layer_create("arena");

    REQUIRE(layer.is_valid());
    CHECK(bool(api->interest_layer_find("arena") == layer));
    NETW_CHECK_EQ(int(api->interest_layer_add_viewer(layer, 7)), OK);
    NETW_CHECK_EQ(int(api->interest_layer_add_entity(layer, entity)), OK);
    NETW_CHECK_EQ(int(api->interest_flush_now()), OK);
    CHECK(api->interest_is_filtered(entity));
    CHECK(api->interest_admits(entity, 7));

    const TypedArray<RID> membership = api->interest_get_membership(entity);
    NETW_CHECK_EQ(membership.size(), 1);
    CHECK(bool(RID(membership[0]) == layer));
    const PackedInt64Array row = api->interest_get_row(entity);
    CHECK(bool(row.size() > 0));

    api->interest_layer_remove_viewer(layer, 7);
    rig.flush_interest();
    CHECK_FALSE(api->interest_admits(entity, 7));
    api->interest_layer_free(layer);
    CHECK_FALSE(api->interest_layer_find("arena").is_valid());
}

TEST_CASE(
    "[Networked][Interest] a driver and a monitor exchange entity handles"
) {
    LoopbackRig rig;
    NetwMultiplayer *api = rig.server();
    const RID entity
        = rig.declare_entity(EntityDecl().named("DrivenSubject").on_route(32));
    const RID layer = api->interest_layer_create("sensor");

    CallLog log;
    Array driven;
    driven.push_back(entity);
    api->interest_layer_set_monitor_callback(layer, log.callable("monitor"));
    api->interest_layer_set_driver_callback(
        layer,
        log.answering("driver", driven)
    );
    api->interest_layer_add_viewer(layer, 9);

    NETW_CHECK_EQ(int(api->interest_flush_now()), OK);
    CHECK(api->interest_admits(entity, 9));

    const Array edge = log.args("monitor");
    NETW_CHECK_EQ(edge.size(), 3);
    REQUIRE(bool(edge.size() == 3));
    CHECK(bool(edge[0]));
    CHECK(bool(RID(edge[1]) == entity));
    NETW_CHECK_EQ(int(edge[2]), 9);
}

TEST_CASE("[Networked][Interest] a policy is selected by the flat param") {
    LoopbackRig rig;
    NetwMultiplayer *api = rig.server();
    const RID entity
        = rig.declare_entity(EntityDecl().named("PolicySubject").on_route(33));
    const RID layer = api->interest_layer_create("inverse");

    api->interest_layer_add_entity(layer, entity);
    api->interest_layer_add_viewer(layer, 11);
    api->interest_layer_set_param(
        layer,
        NetwMultiplayer::LAYER_PARAM_POLICY,
        NetwMultiplayer::LAYER_POLICY_HIDE_FROM_INSIDERS
    );
    rig.flush_interest();

    CHECK_FALSE(api->interest_admits(entity, 11));
    CHECK_FALSE(api->interest_explain(entity, 11).is_empty());
}

TEST_CASE(
    "[Networked][Display] a track declares, records and reports by handle"
) {
    LoopbackRig rig;
    NetwMultiplayer *api = rig.server();
    const RID entity
        = rig.declare_entity(EntityDecl().named("DisplaySubject").on_route(41));

    NETW_CHECK_EQ(
        int(api->display_declare(entity, 0, "position", lerp_spec())),
        OK
    );
    api->display_set_param(
        entity,
        NetwMultiplayer::DISPLAY_PARAM_ROLE,
        NetwMultiplayer::DISPLAY_ROLE_REMOTE
    );
    NETW_CHECK_EQ(
        int(api->display_record_track(
            entity,
            StringName("position"),
            Vector2(2.0, 3.0),
            4
        )),
        OK
    );
    NETW_CHECK_EQ(
        int(api->display_get_track_stat(entity, "position", "buffer_size")),
        1
    );
    api->display_snap(entity, "position", Vector2(5.0, 6.0));

    CHECK(
        bool(
            Vector2(api->display_get_value(entity, "position"))
            == Vector2(5.0, 6.0)
        )
    );
    NETW_CHECK_EQ(
        int(api->display_get_track_stat(entity, "position", "buffer_size")),
        0
    );
    NETW_CHECK_EQ(
        int(
            api->display_get_param(entity, NetwMultiplayer::DISPLAY_PARAM_ROLE)
        ),
        int(NetwMultiplayer::DISPLAY_ROLE_REMOTE)
    );
}

TEST_CASE("[Networked][Display] the output callback carries the public tuple") {
    LoopbackRig rig;
    NetwMultiplayer *api = rig.server();
    const RID entity = rig.declare_entity(
        EntityDecl().named("CallbackSubject").on_route(42)
    );

    CallLog log;
    api->display_declare(entity, 0, "position", lerp_spec());
    api->display_set_callback(entity, log.callable("write"));
    api->display_snap(entity, "position", Vector2(1.0, 1.0));

    const Array write = log.args("write");
    NETW_CHECK_EQ(write.size(), 3);
    REQUIRE(bool(write.size() == 3));
    CHECK(bool(RID(write[0]) == entity));
    CHECK(bool(StringName(write[1]) == StringName("position")));
    CHECK(bool(Vector2(write[2]) == Vector2(1.0, 1.0)));
}

TEST_CASE("[Networked][Display] the stock lane answers from its target") {
    LoopbackRig rig;
    NetwMultiplayer *api = rig.server();
    const RID entity = rig.declare_entity(
        EntityDecl().named("OverrideSubject").on_route(43)
    );

    api->display_declare(entity, 0, "position", lerp_spec());
    api->display_set_target_item(entity, RID());

    NETW_CHECK_EQ(
        int(
            api->display_lane(entity, StringName("position"), Vector2(1.0, 1.0))
        ),
        int(ERR_DOES_NOT_EXIST)
    );
}

#endif

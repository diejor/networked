#include "support/netw_test.h"

#include "support/loopback_rig.h"

#include "netw/api/entity_options.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestZu28SceneSnapLaws {

using namespace godot;
using namespace netw_test;

TEST_CASE("[Networked][Scene] move snaps to the declared marker") {
    LoopbackRig rig(0);
    const RID source = rig.declare_scene("Source");
    const RID destination = rig.declare_scene("Destination");
    rig.enter_scene("Source");
    rig.enter_scene("Destination");
    Node2D *level = Object::cast_to<Node2D>(rig.content_of(destination));
    REQUIRE(level != nullptr);
    if (level == nullptr) {
        return;
    }
    Node2D *marker = level->get_node<Node2D>(NodePath("Marker"));
    REQUIRE(marker != nullptr);
    if (marker == nullptr) {
        return;
    }
    CHECK(marker->get_class() == StringName("Marker2D"));
    marker->set_position(Vector2(40.0, 25.0));

    const RID entity = rig.declare_entity(
        EntityDecl().named("Crate").on_route(91)
    );
    rig.seat(entity, source);

    Ref<netw::NetwReparentOpts> opts;
    opts.instantiate();
    opts->set_target_global_position(marker->get_global_position());

    Ref<netw::NetwPromise> settled = rig.server()->call(
        "scene_move",
        entity,
        destination,
        opts
    );
    REQUIRE(settled.is_valid());
    REQUIRE(settled->get_is_settled());
    REQUIRE(settled->get_code() == 0);

    Node2D *body = Object::cast_to<Node2D>(rig.node_of(entity));
    REQUIRE(body != nullptr);
    if (body != nullptr) {
        CHECK(body->get_parent() == level);
        CHECK(body->get_global_position() == marker->get_global_position());
    }
}

}

#endif

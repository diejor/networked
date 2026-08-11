#include "support/netw_test.h"

#include "support/world_decl.h"

namespace TestDeclaredWorld {

using namespace godot;
using namespace netw_test;

TEST_CASE(
    "[Networked][Interest][Hosted] an entity names its shared wire schema"
) {
    const EntityDecl entity = EntityDecl()
        .named("Tracked")
        .on_schema("TrackedPose")
        .synced("position")
        .placed_at(Vector2(3.0, 4.0));

    CHECK(entity.schema() == StringName("TrackedPose"));
    NETW_CHECK_EQ(entity.synced_columns().size(), 1);
    CHECK(entity.synced_columns()[0] == StringName("position"));
    CHECK(Vector2(entity.initial_pose()) == Vector2(3.0, 4.0));
}

TEST_CASE(
    "[Networked][Interest][Hosted] a player declaration names its client"
) {
    WorldDecl world;
    world.player("Alice", 2);

    NETW_CHECK_EQ(world.entity_count(), 1);
    CHECK(world.entity_at(0).name() == StringName("Alice"));
    NETW_CHECK_EQ(world.player_client_at(0), 2);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a world declares its session services"
) {
    WorldDecl world;
    world.clocked(60, 5).lag_compensated();

    CHECK(world.is_clocked());
    CHECK(world.has_lag_compensation());
    NETW_CHECK_EQ(world.tickrate(), 60);
    NETW_CHECK_EQ(world.display_offset(), 5);
}

} // namespace TestDeclaredWorld

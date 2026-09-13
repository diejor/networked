#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/multiplayer_spawner.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/spawn/spawner_compat.hpp"

#include "support/loopback_rig.h"
#include "support/minted_script.h"
#include "support/value_flow_stand.h"

namespace TestSpawnerWrapReadyLaws {

using namespace godot;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;

constexpr const char *READY_SPAWNER = R"(extends MultiplayerSpawner

func _ready() -> void:
	set_spawn_function(build_one)


func build_one(data: Variant) -> Node:
	var made := Node.new()
	made.name = str(data)
	return made
)";

MultiplayerSpawner *scripted_spawner(Node *p_arena) {
    const Ref<Script> script = netw_test::script_from(READY_SPAWNER);
    REQUIRE_MESSAGE(script.is_valid(), "the spawner script did not load");
    MultiplayerSpawner *spawner
        = Object::cast_to<MultiplayerSpawner>(script->call("new"));
    REQUIRE_MESSAGE(spawner != nullptr, "the script built no spawner");
    spawner->set_name("ReadySpawner");
    spawner->set_spawn_path(p_arena->get_path());
    return spawner;
}

int64_t drops_of(LoopbackRig &p_rig) {
    const Dictionary counters = p_rig.server()
                                    ->get_replication_plane()
                                    ->get_spawner_compat()
                                    ->counters();
    return int64_t(counters[StringName("drops_uncaptured_custom")]);
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] a spawner that takes its spawn "
    "function in _ready captures its FIRST custom spawn, because the "
    "wrap waits for the node rather than for a second enrolment"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);

    rig.server()->emit_signal(StringName("session_entered"));

    Node *arena = memnew(Node);
    arena->set_name("Arena");
    rig.branch(-1)->add_child(arena);

    MultiplayerSpawner *spawner = scripted_spawner(arena);
    rig.branch(-1)->add_child(spawner);
    rig.step_ticks(2);

    const bool wrapped_before_spawning
        = spawner->get_spawn_function().get_object() == rig.server();
    CHECK(wrapped_before_spawning);

    Node *made = spawner->spawn("first");
    REQUIRE(made != nullptr);
    rig.step_ticks(8);

    NETW_CHECK_EQ(drops_of(rig), int64_t(0));
    CHECK(netw::NetwEntity::of(made).is_valid());
}

} // namespace TestSpawnerWrapReadyLaws

#endif

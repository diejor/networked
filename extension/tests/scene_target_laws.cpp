#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneTargets {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw_test::CallLog;

TEST_CASE(
    "[Networked][Scene][Hosted] ST1 a stem and a path naming one scene both "
    "target it, so a handler compares the scene and not the spelling"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    CHECK(core->scene_request_targets(StringName("Arena"), StringName("Arena")));
    CHECK(core->scene_request_targets(String("Arena"), StringName("Arena")));
    CHECK(core->scene_request_targets(
        String("res://levels/Arena.tscn"),
        StringName("Arena")
    ));
    CHECK_FALSE(core->scene_request_targets(
        String("res://levels/Annex.tscn"),
        StringName("Arena")
    ));
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST2 a destination that is not a scene "
    "reference targets nothing"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    CHECK_FALSE(core->scene_request_targets(Variant(7), StringName("Arena")));
    CHECK_FALSE(core->scene_request_targets(Variant(), StringName("Arena")));
    CHECK_FALSE(
        core->scene_request_targets(String("user://Arena.tscn"), StringName("Arena"))
    );
    CHECK_FALSE(
        core->scene_request_targets(String("Annex"), StringName("Arena"))
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] ST3 a stem whose declared file is named "
    "otherwise still targets its own scene"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    CHECK_FALSE(core->scene_request_targets(
        String("res://levels/main_arena.tscn"),
        StringName("Arena")
    ));

    CallLog log;
    core->set_scene_path_reader(
        log.answering("path", String("res://levels/main_arena.tscn"))
    );

    CHECK(core->scene_request_targets(
        String("res://levels/main_arena.tscn"),
        StringName("Arena")
    ));
    NETW_CHECK_EQ(log.count("path"), 1);
    CHECK(StringName(log.args("path")[0]) == StringName("Arena"));

    CHECK_FALSE(core->scene_request_targets(
        String("res://levels/other.tscn"),
        StringName("Arena")
    ));
}

} // namespace TestNetwSceneTargets

#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/object.hpp"
#include "netw/api/debug_join_config.hpp"

namespace TestDebugJoinConfigLaws {

using namespace godot;
using netw::DebugJoinConfig;

Array a_join(const char *p_stem, const char *p_spawner) {
    Array args;
    args.push_back(StringName(p_stem));
    args.push_back(NodePath(p_spawner));
    return args;
}

TEST_CASE(
    "[Networked][Session] DJ1 a fresh config names a player and carries no "
    "join args, so dropping one on a tree auto connects without expressing a "
    "join intent the server has to resolve"
) {
    Ref<DebugJoinConfig> config;
    config.instantiate();

    CHECK(config->get_username() == StringName("DebugPlayer"));
    NETW_CHECK_EQ(config->get_join_args().size(), 0);
}

TEST_CASE(
    "[Networked][Session] DJ2 a config carries the same typed args a live "
    "client sends, so an editor authored join reaches the server's own "
    "handler rather than a second format only this resource can build"
) {
    Ref<DebugJoinConfig> config;
    config.instantiate();
    config->set_username(StringName("Dev"));
    config->set_join_args(a_join("Level1", "Player"));

    const Array args = config->get_join_args();

    CHECK(config->get_username() == StringName("Dev"));
    NETW_CHECK_EQ(args.size(), 2);
    if (args.size() == 2) {
        CHECK(StringName(args[0]) == StringName("Level1"));
        CHECK(NodePath(args[1]) == NodePath("Player"));
    }
}

TEST_CASE(
    "[Networked][Session] DJ3 every read builds its own array, so a tree that "
    "hosts twice off one config cannot have the second join mutated by "
    "whatever the first one did with its args"
) {
    Ref<DebugJoinConfig> config;
    config.instantiate();
    config->set_join_args(a_join("Level1", "Player"));

    Array first = config->get_join_args();
    const Array second = config->get_join_args();

    REQUIRE(first.size() == 2);
    first.push_back(7);
    NETW_CHECK_EQ(second.size(), 2);
}

TEST_CASE(
    "[Networked][Session] DJ4 a config copies the array it was authored with, "
    "so a caller still editing its own array cannot reach inside the resource "
    "the editor saved"
) {
    Ref<DebugJoinConfig> config;
    config.instantiate();
    Array authored = a_join("Level1", "Player");
    config->set_join_args(authored);

    authored.push_back(7);

    NETW_CHECK_EQ(config->get_join_args().size(), 2);
}

} // namespace TestDebugJoinConfigLaws

#endif

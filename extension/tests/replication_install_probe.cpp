#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/wire/registry.hpp"

namespace TestReplicationInstallProbe {

using namespace godot;
using namespace netw_test;
using netw::NetwMultiplayer;
using netw::ReplicationCore;

struct InstallState {
    int channels = 0;
    int entity_live = 0;
    int peer_connected = 0;
    int session_entered = 0;
    int session_ended = 0;
    bool spawn_protocol = false;
    bool despawn_protocol = false;
    bool reparent_protocol = false;
    uint64_t spawn_book = 0;
    uint64_t sync_model = 0;
};

int64_t channel_named(const char *p_name) {
    const netw::wire::WireRegistry registry
        = netw::wire::WireRegistry::create_default();
    const netw::wire::ChannelDecl *decl
        = registry.find_channel_by_name(StringName(p_name));
    return decl != nullptr ? int64_t(decl->id) : -1;
}

int edges(NetwMultiplayer *p_core, const char *p_signal) {
    return int(p_core->get_signal_connection_list(StringName(p_signal)).size());
}

InstallState read_state(NetwMultiplayer *p_core, ReplicationCore *p_plane) {
    netw::NetwChannelBook *book = p_core->get_channel_book();
    InstallState state;
    state.channels = book->size();
    state.entity_live = edges(p_core, "entity_live");
    state.peer_connected = edges(p_core, "peer_connected");
    state.session_entered = edges(p_core, "session_entered");
    state.session_ended = edges(p_core, "session_ended");
    state.spawn_protocol
        = book->protocol_handler_of(channel_named("SPAWN")).is_valid();
    state.despawn_protocol
        = book->protocol_handler_of(channel_named("DESPAWN")).is_valid();
    state.reparent_protocol
        = book->protocol_handler_of(channel_named("REPARENT")).is_valid();
    state.spawn_book
        = uint64_t(p_plane->get_spawn_pipeline()->get_spawn_book());
    state.sync_model = uint64_t(p_plane->get_sync_model());
    return state;
}

TEST_CASE(
    "[Networked][Repl] a second install answers the state the first one "
    "left, so a plane already installed may be handed its owner again"
) {
    LoopbackRig rig(0);
    NetwMultiplayer *core = rig.server();
    Object *api = core;
    REQUIRE(core != nullptr);
    ReplicationCore *plane = core->get_replication_plane();
    REQUIRE(plane != nullptr);

    const InstallState before = read_state(core, plane);
    plane->install(api);
    const InstallState after = read_state(core, plane);

    NETW_CHECK_EQ(after.channels, before.channels);
    NETW_CHECK_EQ(after.entity_live, before.entity_live);
    NETW_CHECK_EQ(after.peer_connected, before.peer_connected);
    NETW_CHECK_EQ(after.session_entered, before.session_entered);
    NETW_CHECK_EQ(after.session_ended, before.session_ended);
    CHECK(after.spawn_protocol == before.spawn_protocol);
    CHECK(after.despawn_protocol == before.despawn_protocol);
    CHECK(after.reparent_protocol == before.reparent_protocol);
    CHECK(after.spawn_book == before.spawn_book);
    CHECK(after.sync_model == before.sync_model);
    CHECK(before.spawn_protocol);

    plane->install(core);
    plane->install(api);
    const InstallState reseamed = read_state(core, plane);

    NETW_CHECK_EQ(reseamed.channels, before.channels);
    NETW_CHECK_EQ(reseamed.entity_live, before.entity_live);
    NETW_CHECK_EQ(reseamed.peer_connected, before.peer_connected);
    NETW_CHECK_EQ(reseamed.session_entered, before.session_entered);
    NETW_CHECK_EQ(reseamed.session_ended, before.session_ended);
    CHECK(reseamed.spawn_protocol == before.spawn_protocol);
    CHECK(reseamed.despawn_protocol == before.despawn_protocol);
    CHECK(reseamed.reparent_protocol == before.reparent_protocol);
    CHECK(reseamed.spawn_book == before.spawn_book);
    CHECK(reseamed.sync_model == before.sync_model);
}

TEST_CASE(
    "[Networked][Repl] a session nobody has configured already owns a plane "
    "seamed to itself, so a bare core can dispatch a frame"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    ReplicationCore *plane = core->get_replication_plane();
    REQUIRE(plane != nullptr);
    CHECK(core->spawn_plane() == plane->get_spawn_pipeline());
    CHECK(core->sync_pipeline() == plane->get_sync_pipeline());

    netw::NetwChannelBook *book = core->get_channel_book();
    REQUIRE(book != nullptr);
    const char *protocols[] = {
        "SPAWN",
        "DESPAWN",
        "REPARENT",
        "TABLE",
        "CLOCK_HANDSHAKE",
        "CLOCK_HANDSHAKE_REPLY",
        "CLOCK_PING",
        "CLOCK_PONG",
        "INTEREST_AWARENESS",
        "LAGCOMP_DENY",
    };
    for (const char *name : protocols) {
        CHECK(book->protocol_handler_of(channel_named(name)).is_valid());
    }
}

} // namespace TestReplicationInstallProbe

#endif

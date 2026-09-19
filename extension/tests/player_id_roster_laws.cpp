#include "support/netw_test.h"

#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "support/joined_peer.h"

namespace TestPlayerIdRosterLaws {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> peered_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    return core;
}

TEST_CASE(
    "[Networked][Session][Hosted] PJ1 a joined player is one an accepted "
    "membership names, so a peer that has connected and not joined is in "
    "neither the row book nor the roster the session publishes"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const int64_t local = int64_t(core->get_unique_id());
    const int64_t guest = local + 7;

    CHECK_FALSE(core->player_has(guest));
    CHECK(core->player_joined_of(guest).is_null());
    CHECK(core->player_joined_all().is_empty());
    CHECK(core->player_local().is_null());

    netw_test::seated_peer(core.ptr(), guest, StringName("guest"));
    CHECK(core->player_has(guest));
    CHECK(core->player_joined_of(guest).is_valid());
    NETW_CHECK_EQ(int(core->player_joined_all().size()), 1);
    CHECK(core->player_local().is_null());

    netw_test::seated_peer(core.ptr(), local, StringName("host"));
    CHECK(core->player_local().is_valid());
    NETW_CHECK_EQ(int(core->player_joined_all().size()), 2);

    core->session_forget_peer(guest);
    CHECK(core->player_joined_of(guest).is_null());
    NETW_CHECK_EQ(int(core->player_joined_all().size()), 1);
}

} // namespace TestPlayerIdRosterLaws

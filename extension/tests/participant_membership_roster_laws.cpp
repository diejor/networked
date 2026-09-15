#include "support/netw_test.h"

#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "support/joined_peer.h"

namespace TestParticipantMembershipRosterLaws {

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
    "[Networked][Session][Hosted] PJ1 a joined participant is one an accepted "
    "membership names, so a peer that has connected and not joined is in "
    "neither the row book nor the roster the session publishes"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const int64_t local = int64_t(core->get_unique_id());
    const int64_t guest = local + 7;

    CHECK_FALSE(core->participant_has(guest));
    CHECK(core->participant_joined_of(guest).is_null());
    CHECK(core->participant_joined_all().is_empty());
    CHECK(core->participant_local().is_null());

    netw_test::seated_peer(core.ptr(), guest, StringName("guest"));
    CHECK(core->participant_has(guest));
    CHECK(core->participant_joined_of(guest).is_valid());
    NETW_CHECK_EQ(int(core->participant_joined_all().size()), 1);
    CHECK(core->participant_local().is_null());

    netw_test::seated_peer(core.ptr(), local, StringName("host"));
    CHECK(core->participant_local().is_valid());
    NETW_CHECK_EQ(int(core->participant_joined_all().size()), 2);

    core->session_forget_peer(guest);
    CHECK(core->participant_joined_of(guest).is_null());
    NETW_CHECK_EQ(int(core->participant_joined_all().size()), 1);
}

} // namespace TestParticipantMembershipRosterLaws

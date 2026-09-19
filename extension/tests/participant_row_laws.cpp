#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "support/joined_peer.h"

namespace TestParticipantRowLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPlayer;
using netw_test::seated_peer;

TEST_CASE(
    "[Networked][Session][Hosted] PR1 one membership has one row, so two asks "
    "about the same peer answer the same object and a game may hold and "
    "compare it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    const Ref<NetwPlayer> first = seated_peer(core.ptr(), 7);
    REQUIRE(first.is_valid());
    const Ref<NetwPlayer> second = core->player_of(7);

    CHECK(bool(first == second));
    CHECK(core->player_has(7));
    CHECK(bool(core->player_of(7) == first));
    NETW_CHECK_EQ(int(first->get_peer_id()), 7);
}

TEST_CASE(
    "[Networked][Session][Hosted] PR2 a row exists exactly from the acceptance "
    "that minted it and stores the username that acceptance carried, so a "
    "connected peer that has not joined has no row to answer with"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    CHECK_FALSE(core->player_has(7));
    CHECK(core->player_of(7).is_null());
    CHECK(core->player_joined_of(7).is_null());

    const Ref<NetwPlayer> row = seated_peer(core.ptr(), 7, "ana");
    REQUIRE(row.is_valid());

    CHECK(bool(row->get_username() == StringName("ana")));
    CHECK(core->session_has_accepted(7));
    CHECK(bool(core->player_joined_of(7) == row));
}

TEST_CASE(
    "[Networked][Session][Hosted] PR9 a membership outlives the roster row "
    "that named it, so a handle a game kept from before a reconnect reads the "
    "username it joined under rather than the one seated at that peer now"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    const Ref<NetwPlayer> before = seated_peer(core.ptr(), 7, "ana");
    REQUIRE(before.is_valid());

    core->session_forget_peer(7);
    const Ref<NetwPlayer> after = seated_peer(core.ptr(), 7, "bo");
    REQUIRE(after.is_valid());

    CHECK(bool(after != before));
    CHECK(bool(before->get_username() == StringName("ana")));
    CHECK(bool(after->get_username() == StringName("bo")));
    CHECK_FALSE(before->get_is_active());
    CHECK(after->get_is_active());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR4 a player answers the bodies the "
    "session actually holds for it, so a membership nobody has spawned for "
    "reads as an empty set rather than as a body it might be given"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwPlayer> row = seated_peer(core.ptr(), 7);
    REQUIRE(row.is_valid());

    CHECK(row->get_bodies().is_empty());
    CHECK(core->player_bodies(7).is_empty());
    CHECK(core->player_bodies(9).is_empty());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR6 forgetting a peer drops its row, so a "
    "later ask about the same peer mints rather than answering the row a "
    "disconnected session left behind"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwPlayer> first = seated_peer(core.ptr(), 7);
    REQUIRE(first.is_valid());

    core->player_forget(7);

    CHECK_FALSE(core->player_has(7));

    const Ref<NetwPlayer> second = seated_peer(core.ptr(), 7);

    REQUIRE(second.is_valid());
    CHECK(bool(second != first));
}

} // namespace TestParticipantRowLaws

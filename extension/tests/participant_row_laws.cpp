#include "support/netw_test.h"

#include "netw/api/netw_identity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "support/joined_peer.h"

namespace TestParticipantRowLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwParticipant;
using netw_test::seated_peer;

TEST_CASE(
    "[Networked][Session][Hosted] PR1 one membership has one row, so two asks "
    "about the same peer answer the same object and a game may hold and "
    "compare it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    const Ref<NetwParticipant> first = seated_peer(core.ptr(), 7);
    REQUIRE(first.is_valid());
    const Ref<NetwParticipant> second = core->participant_of(7);

    CHECK(bool(first == second));
    CHECK(core->participant_has(7));
    CHECK(bool(core->participant_of(7) == first));
    NETW_CHECK_EQ(int(first->get_peer_id()), 7);
}

TEST_CASE(
    "[Networked][Session][Hosted] PR2 a row exists exactly from the acceptance "
    "that minted it and stores the username that acceptance carried, so a "
    "connected peer that has not joined has no row to answer with"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    CHECK_FALSE(core->participant_has(7));
    CHECK(core->participant_of(7).is_null());
    CHECK(core->participant_joined_of(7).is_null());

    const Ref<NetwParticipant> row = seated_peer(core.ptr(), 7, "ana");
    REQUIRE(row.is_valid());

    CHECK(bool(row->get_username() == StringName("ana")));
    CHECK(core->session_has_accepted(7));
    CHECK(bool(core->participant_joined_of(7) == row));
}

TEST_CASE(
    "[Networked][Session][Hosted] PR9 a membership outlives the roster row "
    "that named it, so a handle a game kept from before a reconnect reads the "
    "username it joined under rather than the one seated at that peer now"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    const Ref<NetwParticipant> before = seated_peer(core.ptr(), 7, "ana");
    REQUIRE(before.is_valid());

    core->session_forget_peer(7);
    const Ref<NetwParticipant> after = seated_peer(core.ptr(), 7, "bo");
    REQUIRE(after.is_valid());

    CHECK(bool(after != before));
    CHECK(bool(before->get_username() == StringName("ana")));
    CHECK(bool(after->get_username() == StringName("bo")));
    CHECK_FALSE(before->get_is_active());
    CHECK(after->get_is_active());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR3 identity answers from the session's own "
    "book and is null with no row, which is also what a session carrying no "
    "auth provider answers"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwParticipant> row = seated_peer(core.ptr(), 7);
    REQUIRE(row.is_valid());

    CHECK(row->get_identity().is_null());

    Ref<netw::NetwIdentity> named;
    named.instantiate();
    named->set_username(StringName("ana"));
    core->peer_set_identity(7, named);

    REQUIRE(row->get_identity().is_valid());
    CHECK(bool(row->get_identity() == named));
    CHECK(core->peer_get_identity(8).is_null());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR7 an identity row dies with the peer it "
    "names, so the next peer seated at that id never reads the previous "
    "peer's credentials"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::NetwIdentity> named;
    named.instantiate();
    named->set_username(StringName("ana"));

    core->peer_set_identity(7, named);
    core->peer_set_identity(9, named);
    REQUIRE(core->peer_get_identity(7).is_valid());

    core->session_forget_peer(7);

    CHECK(core->peer_get_identity(7).is_null());
    CHECK(core->peer_get_identity(9).is_valid());

    core->session_clear_roster();

    CHECK(core->peer_get_identity(9).is_null());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR8 writing a null identity erases the row "
    "rather than seating an empty one, so a revoked credential reads the same "
    "as one that never arrived"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::NetwIdentity> named;
    named.instantiate();
    core->peer_set_identity(7, named);
    REQUIRE(core->peer_get_identity(7).is_valid());

    core->peer_set_identity(7, Ref<netw::NetwIdentity>());

    CHECK(core->peer_get_identity(7).is_null());
    CHECK(seated_peer(core.ptr(), 7)->get_identity().is_null());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR4 a participant answers the bodies the "
    "session actually holds for it, so a membership nobody has spawned for "
    "reads as an empty set rather than as a body it might be given"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwParticipant> row = seated_peer(core.ptr(), 7);
    REQUIRE(row.is_valid());

    CHECK(row->get_players().is_empty());
    CHECK(core->participant_players(7).is_empty());
    CHECK(core->participant_players(9).is_empty());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR6 forgetting a peer drops its row, so a "
    "later ask about the same peer mints rather than answering the row a "
    "disconnected session left behind"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwParticipant> first = seated_peer(core.ptr(), 7);
    REQUIRE(first.is_valid());

    core->participant_forget(7);

    CHECK_FALSE(core->participant_has(7));

    const Ref<NetwParticipant> second = seated_peer(core.ptr(), 7);

    REQUIRE(second.is_valid());
    CHECK(bool(second != first));
}

} // namespace TestParticipantRowLaws

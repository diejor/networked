#include "support/netw_test.h"

#include "netw/api/netw_identity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/resolved_join.hpp"
#include "support/netw_call_log.h"

namespace TestParticipantRowLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwParticipant;
using netw::ResolvedJoin;
using netw_test::CallLog;

Ref<ResolvedJoin> a_join(int64_t p_peer, const StringName &p_name) {
    Ref<ResolvedJoin> made;
    made.instantiate();
    made->set_peer_id(p_peer);
    made->set_username(p_name);
    return made;
}

TEST_CASE(
    "[Networked][Session][Hosted] PR1 one peer has one row, so two asks about "
    "the same peer answer the same object and a game may hold and compare it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    const Ref<netw::NetwParticipant> first = core->participant_ensure(7);
    const Ref<netw::NetwParticipant> second = core->participant_ensure(7);

    REQUIRE(first.is_valid());
    CHECK(first == second);
    CHECK(core->participant_has(7));
    CHECK(core->participant_of(7) == first);

    const Ref<NetwParticipant> row = first;
    REQUIRE(row.is_valid());
    NETW_CHECK_EQ(int(row->get_peer_id()), 7);
}

TEST_CASE(
    "[Networked][Session][Hosted] PR2 a row exists before its join does and "
    "reads the roster on every ask, so a join accepted after the row was "
    "minted is answered by the row that already exists"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwParticipant> row = core->participant_ensure(7);
    REQUIRE(row.is_valid());

    CHECK(row->get_join().is_null());
    CHECK(row->get_username() == StringName());
    CHECK(row->get_arg_values().is_empty());

    REQUIRE(core->join_book().remember(a_join(7, "ana")));

    REQUIRE(row->get_join().is_valid());
    CHECK(row->get_username() == StringName("ana"));
}

TEST_CASE(
    "[Networked][Session][Hosted] PR3 identity answers from the session's own "
    "book and is null with no row, which is also what a session carrying no "
    "auth provider answers"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwParticipant> row = core->participant_ensure(7);
    REQUIRE(row.is_valid());

    CHECK(row->get_identity().is_null());

    Ref<netw::NetwIdentity> named;
    named.instantiate();
    named->set_username(StringName("ana"));
    core->peer_set_identity(7, named);

    REQUIRE(row->get_identity().is_valid());
    CHECK(row->get_identity() == named);
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
    CHECK(core->participant_ensure(7)->get_identity().is_null());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR4 a seat written through the row is the "
    "seat the session holds, because the row stores nothing and the session "
    "is the one place a seat lives"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwParticipant> row = core->participant_ensure(7);
    REQUIRE(row.is_valid());

    CHECK(row->get_current_scene().is_null());
    CHECK_FALSE(core->participant_seat(7).is_valid());

    core->participant_seat_move(7, RID());

    CHECK_FALSE(core->participant_seat(7).is_valid());
    CHECK(row->get_current_scene().is_null());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR5 a move naming no reachable scene is "
    "refused rather than clearing the seat, so a destination that failed to "
    "resolve never reads as an instruction to leave"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwParticipant> row = core->participant_ensure(7);
    REQUIRE(row.is_valid());
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));

    row->move_to(Variant());

    NETW_CHECK_EQ(flushed.count("flush"), 0);
    CHECK_FALSE(core->participant_seat(7).is_valid());
}

TEST_CASE(
    "[Networked][Session][Hosted] PR6 forgetting a peer drops its row, so a "
    "later ask about the same peer mints rather than answering the row a "
    "disconnected session left behind"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<netw::NetwParticipant> first = core->participant_ensure(7);
    REQUIRE(first.is_valid());

    core->participant_forget(7);

    CHECK_FALSE(core->participant_has(7));

    const Ref<netw::NetwParticipant> second = core->participant_ensure(7);

    REQUIRE(second.is_valid());
    CHECK(second != first);
}

} // namespace TestParticipantRowLaws

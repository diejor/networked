#include "support/netw_test.h"

#include "netw/join_roster.hpp"
#include "netw/session/frames.hpp"

namespace TestNetwJoinRoster {

using namespace godot;
using netw::JoinRoster;
using netw::session::AcceptFrame;

JoinRoster make_roster() {
    return JoinRoster();
}

AcceptFrame join(int64_t p_peer, const char *p_name, uint64_t p_membership) {
    AcceptFrame out;
    out.peer_id = p_peer;
    out.username = StringName(p_name);
    out.membership = p_membership;
    return out;
}

PackedStringArray names(std::initializer_list<const char *> p_names) {
    PackedStringArray out;
    for (const char *name : p_names) {
        out.push_back(String(name));
    }
    return out;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a peer is remembered again only when the "
    "membership changed, so a duplicated accept seats nobody twice and a "
    "reconnect at the same peer id replaces the record it left"
) {
    JoinRoster roster = make_roster();

    CHECK(roster.remember(join(4, "ana", 1)));
    NETW_CHECK_EQ(roster.size(), 1);

    SUBCASE("the same membership twice teaches nothing") {
        CHECK_FALSE(roster.remember(join(4, "ana", 1)));
        NETW_CHECK_EQ(roster.size(), 1);
    }

    SUBCASE("a new membership at the same peer replaces the record") {
        CHECK(roster.remember(join(4, "bo", 2)));
        NETW_CHECK_EQ(roster.size(), 1);
        NETW_CHECK_EQ(int(roster.accepted_join(4).membership), 2);
        CHECK(bool(roster.accepted_join(4).username == StringName("bo")));
    }

    SUBCASE("a record naming no membership is not a record") {
        CHECK_FALSE(roster.remember(join(5, "bo", 0)));
        CHECK_FALSE(roster.has_accepted(5));
    }

    SUBCASE("another peer is another record") {
        CHECK(roster.remember(join(5, "bo", 2)));
        NETW_CHECK_EQ(roster.size(), 2);
        NETW_CHECK_EQ(int(roster.accepted_joins().size()), 2);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a name collision has three answers, and "
    "which one depends on the peer asking"
) {
    JoinRoster roster = make_roster();
    const PackedStringArray taken = names({"ana", "ana1"});

    NETW_CHECK_EQ(
        roster.name_verdict("bo", taken, false, false),
        int(JoinRoster::ADMIT)
    );
    NETW_CHECK_EQ(
        roster.name_verdict("ana", taken, true, false),
        int(JoinRoster::RENAME)
    );
    NETW_CHECK_EQ(
        roster.name_verdict("ana", taken, false, true),
        int(JoinRoster::REFUSE)
    );

    SUBCASE("an unauthenticated collision is admitted, not refused") {
        NETW_CHECK_EQ(
            roster.name_verdict("ana", taken, false, false),
            int(JoinRoster::ADMIT)
        );
    }

    SUBCASE(
        "an authenticated refusal outranks the rename, because a peer "
        "that proved who it is may not take a seated player's name even "
        "on a build that renames everyone else"
    ) {
        NETW_CHECK_EQ(
            roster.name_verdict("ana", taken, true, true),
            int(JoinRoster::REFUSE)
        );
    }

    SUBCASE("the free name skips every suffix already held") {
        CHECK(bool(roster.free_name("ana", taken) == StringName("ana2")));
        CHECK(bool(roster.free_name("bo", taken) == StringName("bo1")));
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 forgetting a peer forgets both books"
) {
    JoinRoster roster = make_roster();
    roster.remember(join(4, "ana", 1));
    roster.refuse(4, "Username 'ana' is already in use");

    CHECK(bool(roster.refusal(4) == String("Username 'ana' is already in use"))
    );

    roster.forget(4);

    NETW_CHECK_EQ(roster.size(), 0);
    CHECK_FALSE(roster.has_accepted(4));
    CHECK(roster.refusal(4).is_empty());

    SUBCASE("and a session teardown forgets every peer at once") {
        roster.remember(join(5, "bo", 2));
        roster.refuse(6, "no");
        roster.clear();
        NETW_CHECK_EQ(roster.size(), 0);
        CHECK(roster.refusal(6).is_empty());
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] the roster serializes in peer order rather "
    "than the order its book happens to hash to"
) {
    JoinRoster roster = make_roster();
    roster.remember(join(9, "cy", 3));
    roster.remember(join(4, "ana", 1));
    roster.remember(join(7, "bo", 2));

    const LocalVector<AcceptFrame> joins = roster.accepted_joins();
    NETW_CHECK_EQ(int(joins.size()), 3);
    NETW_CHECK_EQ(int(joins[0].peer_id), 4);
    NETW_CHECK_EQ(int(joins[1].peer_id), 7);
    NETW_CHECK_EQ(int(joins[2].peer_id), 9);

    LocalVector<AcceptFrame> rows;
    const bool framed = netw::session::roster_read(roster.roster_frame(), rows);
    CHECK(framed);
    NETW_CHECK_EQ(int(rows.size()), 3);
    NETW_CHECK_EQ(int(rows[0].peer_id), 4);
    NETW_CHECK_EQ(int(rows[0].membership), 1);
    NETW_CHECK_EQ(int(rows[2].peer_id), 9);
    NETW_CHECK_EQ(int(rows[2].membership), 3);
}

} // namespace TestNetwJoinRoster

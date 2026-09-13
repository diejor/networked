#include "support/netw_test.h"

#include "netw/join_roster.hpp"
#include "netw/session/frames.hpp"

namespace TestNetwJoinRoster {

using namespace godot;
using netw::JoinRoster;
using netw::ResolvedJoin;

JoinRoster make_roster() {
    return JoinRoster();
}

Ref<ResolvedJoin> join(int64_t peer_id, const char *name, bool with_args) {
    Ref<ResolvedJoin> rj;
    rj.instantiate();
    rj->set_peer_id(peer_id);
    rj->set_username(StringName(name));
    if (with_args) {
        Array args;
        args.push_back(7);
        rj->set_arg_values(args);
    }
    return rj;
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
    "record learned something"
) {
    JoinRoster roster = make_roster();

    CHECK(roster.remember(join(4, "ana", false)));
    NETW_CHECK_EQ(roster.size(), 1);

    SUBCASE("the same argless record twice teaches nothing") {
        CHECK_FALSE(roster.remember(join(4, "ana", false)));
        NETW_CHECK_EQ(roster.size(), 1);
    }

    SUBCASE("args arriving where there were none enrich it") {
        CHECK(roster.remember(join(4, "ana", true)));
        CHECK_FALSE(roster.accepted_join(4)->get_arg_values().is_empty());
    }

    SUBCASE("an argless echo does not replace a record that carries args") {
        roster.remember(join(4, "ana", true));
        CHECK_FALSE(roster.remember(join(4, "ana", false)));
        CHECK_FALSE(roster.accepted_join(4)->get_arg_values().is_empty());
    }

    SUBCASE("another peer is another record") {
        CHECK(roster.remember(join(5, "bo", false)));
        NETW_CHECK_EQ(roster.size(), 2);
        NETW_CHECK_EQ(roster.accepted_joins().size(), 2);
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
        CHECK(roster.free_name("ana", taken) == StringName("ana2"));
        CHECK(roster.free_name("bo", taken) == StringName("bo1"));
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 forgetting a peer forgets both books"
) {
    JoinRoster roster = make_roster();
    roster.remember(join(4, "ana", true));
    roster.refuse(4, "Username 'ana' is already in use");

    CHECK(roster.refusal(4) == String("Username 'ana' is already in use"));

    roster.forget(4);

    NETW_CHECK_EQ(roster.size(), 0);
    CHECK(roster.accepted_join(4).is_null());
    CHECK(roster.refusal(4).is_empty());

    SUBCASE("and a session teardown forgets every peer at once") {
        roster.remember(join(5, "bo", false));
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
    roster.remember(join(9, "cy", false));
    roster.remember(join(4, "ana", false));
    roster.remember(join(7, "bo", false));

    const Array joins = roster.accepted_joins();
    NETW_CHECK_EQ(joins.size(), 3);
    NETW_CHECK_EQ(int(Ref<ResolvedJoin>(joins[0])->get_peer_id()), 4);
    NETW_CHECK_EQ(int(Ref<ResolvedJoin>(joins[1])->get_peer_id()), 7);
    NETW_CHECK_EQ(int(Ref<ResolvedJoin>(joins[2])->get_peer_id()), 9);
    LocalVector<netw::AcceptFrame> rows;
    const bool framed = netw::session::roster_read(roster.roster_frame(), rows);
    CHECK(framed);
    NETW_CHECK_EQ(int(rows.size()), 3);
    NETW_CHECK_EQ(int(rows[0].peer_id), 4);
    NETW_CHECK_EQ(int(rows[2].peer_id), 9);
}

} // namespace TestNetwJoinRoster

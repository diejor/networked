// Laws for NetwJoinRoster.
//
// Two books that must move together, and one rule about names that has three
// answers rather than two. The rule is a verdict rather than an act, so the
// same decision serves a join arriving on the wire and one a test hands in.

#include "support/netw_test.h"

#include "netw/join_roster.hpp"

namespace TestNetwJoinRoster {

using namespace godot;
using netw::NetwJoinRoster;
using netw::ResolvedJoin;

Ref<NetwJoinRoster> make_roster() {
    Ref<NetwJoinRoster> roster;
    roster.instantiate();
    return roster;
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
    Ref<NetwJoinRoster> roster = make_roster();

    CHECK(roster->remember(join(4, "ana", false)));
    NETW_CHECK_EQ(roster->size(), 1);

    SUBCASE("the same argless record twice teaches nothing") {
        CHECK_FALSE(roster->remember(join(4, "ana", false)));
        NETW_CHECK_EQ(roster->size(), 1);
    }

    SUBCASE("args arriving where there were none enrich it") {
        CHECK(roster->remember(join(4, "ana", true)));
        CHECK_FALSE(roster->accepted_join(4)->get_arg_values().is_empty());
    }

    SUBCASE("an argless echo does not replace a record that carries args") {
        roster->remember(join(4, "ana", true));
        CHECK_FALSE(roster->remember(join(4, "ana", false)));
        CHECK_FALSE(roster->accepted_join(4)->get_arg_values().is_empty());
    }

    SUBCASE("another peer is another record") {
        CHECK(roster->remember(join(5, "bo", false)));
        NETW_CHECK_EQ(roster->size(), 2);
        NETW_CHECK_EQ(roster->accepted_joins().size(), 2);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a name collision has three answers, and "
    "which one depends on the peer asking"
) {
    Ref<NetwJoinRoster> roster = make_roster();
    const PackedStringArray taken = names({"ana", "ana1"});

    NETW_CHECK_EQ(
        roster->name_verdict("bo", taken, false, false),
        int(NetwJoinRoster::ADMIT)
    );
    NETW_CHECK_EQ(
        roster->name_verdict("ana", taken, true, false),
        int(NetwJoinRoster::RENAME)
    );
    NETW_CHECK_EQ(
        roster->name_verdict("ana", taken, false, true),
        int(NetwJoinRoster::REFUSE)
    );

    SUBCASE("an unauthenticated collision is admitted, not refused") {
        // Nothing proves the name belongs to the peer already holding it, so
        // the session says so and lets both in rather than locking one out on
        // a first-come claim.
        NETW_CHECK_EQ(
            roster->name_verdict("ana", taken, false, false),
            int(NetwJoinRoster::ADMIT)
        );
    }

    SUBCASE("a debug rename outranks an authenticated refusal") {
        NETW_CHECK_EQ(
            roster->name_verdict("ana", taken, true, true),
            int(NetwJoinRoster::RENAME)
        );
    }

    SUBCASE("the free name skips every suffix already held") {
        CHECK(roster->free_name("ana", taken) == StringName("ana2"));
        CHECK(roster->free_name("bo", taken) == StringName("bo1"));
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 forgetting a peer forgets both books"
) {
    // A peer remembered as accepted while a refusal reason still names it is a
    // peer the session disagrees with itself about.
    Ref<NetwJoinRoster> roster = make_roster();
    roster->remember(join(4, "ana", true));
    roster->refuse(4, "Username 'ana' is already in use");

    CHECK(roster->refusal(4) == String("Username 'ana' is already in use"));

    roster->forget(4);

    NETW_CHECK_EQ(roster->size(), 0);
    CHECK(roster->accepted_join(4).is_null());
    CHECK(roster->refusal(4).is_empty());

    SUBCASE("and a session teardown forgets every peer at once") {
        roster->remember(join(5, "bo", false));
        roster->refuse(6, "no");
        roster->clear();
        NETW_CHECK_EQ(roster->size(), 0);
        CHECK(roster->refusal(6).is_empty());
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] the roster serializes in peer order"
) {
    // A backfill and the roster it backfills have to agree about order, and a
    // hash walk does not.
    Ref<NetwJoinRoster> roster = make_roster();
    roster->remember(join(9, "cy", false));
    roster->remember(join(4, "ana", false));
    roster->remember(join(7, "bo", false));

    const Array joins = roster->accepted_joins();
    NETW_CHECK_EQ(joins.size(), 3);
    NETW_CHECK_EQ(int(Ref<ResolvedJoin>(joins[0])->get_peer_id()), 4);
    NETW_CHECK_EQ(int(Ref<ResolvedJoin>(joins[1])->get_peer_id()), 7);
    NETW_CHECK_EQ(int(Ref<ResolvedJoin>(joins[2])->get_peer_id()), 9);
    NETW_CHECK_EQ(roster->serialize_accepted().size(), 3);
}

} // namespace TestNetwJoinRoster

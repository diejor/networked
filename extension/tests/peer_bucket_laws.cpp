#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)
#include "godot/script.hpp"
#include <godot_cpp/classes/class_db_singleton.hpp>
#endif

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/resolved_join.hpp"

namespace TestNetwPeerBucket {

using namespace godot;
using netw::NetwMultiplayer;
using netw::ResolvedJoin;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

void admit(const Ref<NetwMultiplayer> &p_session, int64_t p_peer) {
    Ref<ResolvedJoin> join;
    join.instantiate();
    join->set_peer_id(p_peer);
    join->set_username(StringName(String("peer") + String::num_int64(p_peer)));
    p_session->session_remember_join(join);
}

bool same_row(const Ref<RefCounted> &p_a, const Ref<RefCounted> &p_b) {
    return p_a.ptr() == p_b.ptr();
}

#if defined(NETW_TIER_HOSTED)

Ref<Script> a_bucket_type(const char *p_member) {
    const Ref<Script> made
        = ClassDBSingleton::get_singleton()->instantiate("GDScript");
    made->set_source_code(
        String("extends RefCounted\nvar ") + String(p_member)
        + String(" := 0\n")
    );
    made->reload();
    return made;
}

TEST_CASE(
    "[Networked][Session][Bucket] L1 a bucket is minted once per peer and "
    "type and answered every time after, so a game reads back what it wrote "
    "rather than a fresh row"
) {
    Ref<NetwMultiplayer> session = make_session();
    admit(session, 7);
    admit(session, 8);
    const Ref<Script> kind = a_bucket_type("value");

    CHECK_FALSE(session->peer_has_bucket(7, kind));

    const Ref<RefCounted> first = session->peer_get_bucket(7, kind);
    REQUIRE(first.is_valid());
    CHECK(session->peer_has_bucket(7, kind));

    SUBCASE("a second read answers the row the first minted") {
        CHECK(same_row(session->peer_get_bucket(7, kind), first));

        first->set(StringName("value"), 42);
        const Ref<RefCounted> again = session->peer_get_bucket(7, kind);
        NETW_CHECK_EQ(int(again->get(StringName("value"))), 42);
    }

    SUBCASE("another peer holds a row of its own under the same type") {
        const Ref<RefCounted> other = session->peer_get_bucket(8, kind);
        REQUIRE(other.is_valid());
        CHECK_FALSE(same_row(other, first));

        first->set(StringName("value"), 42);
        other->set(StringName("value"), 7);
        NETW_CHECK_EQ(
            int(Ref<RefCounted>(session->peer_get_bucket(7, kind))
                    ->get(StringName("value"))),
            42
        );
        NETW_CHECK_EQ(
            int(Ref<RefCounted>(session->peer_get_bucket(8, kind))
                    ->get(StringName("value"))),
            7
        );
    }

    SUBCASE("a second type on one peer is a second bucket") {
        const Ref<Script> unrelated = a_bucket_type("label");
        CHECK_FALSE(same_row(session->peer_get_bucket(7, unrelated), first));
    }
}

TEST_CASE(
    "[Networked][Session][Bucket] L2 a peer the session does not hold "
    "answers nothing and is minted nothing, because reading a bucket also "
    "creates one and no read may invent a peer"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<Script> kind = a_bucket_type("value");

    const Variant refused = session->peer_get_bucket(9, kind);

    NETW_CHECK_EQ(int(refused.get_type()), int(Variant::NIL));
    CHECK_FALSE(session->peer_has_bucket(9, kind));

    admit(session, 9);
    CHECK(Ref<RefCounted>(session->peer_get_bucket(9, kind)).is_valid());
}

TEST_CASE(
    "[Networked][Session][Bucket] L3 forgetting a peer drops the buckets it "
    "held, so a re-join mints fresh ones and a clear leaves nothing keyed by "
    "a peer that is gone"
) {
    Ref<NetwMultiplayer> session = make_session();
    admit(session, 7);
    admit(session, 8);
    const Ref<Script> kind = a_bucket_type("value");

    const Ref<RefCounted> before = session->peer_get_bucket(7, kind);
    session->peer_get_bucket(8, kind);
    REQUIRE(before.is_valid());

    SUBCASE(
        "forgetting one peer drops its bucket and its participant, and "
        "leaves the other peer's alone"
    ) {
        NETW_CHECK_EQ(int(session->peer_get_participant(7).is_valid()), 1);
        session->peer_forget(7);

        CHECK_FALSE(session->peer_has_bucket(7, kind));
        NETW_CHECK_EQ(int(session->peer_get_participant(7).is_valid()), 0);
        CHECK(session->peer_has_bucket(8, kind));
        NETW_CHECK_EQ(int(session->peer_get_participant(8).is_valid()), 1);

        admit(session, 7);
        CHECK_FALSE(same_row(session->peer_get_bucket(7, kind), before));
    }

    SUBCASE("clearing the roster drops every bucket") {
        session->session_clear_roster();
        CHECK_FALSE(session->peer_has_bucket(7, kind));
        CHECK_FALSE(session->peer_has_bucket(8, kind));
    }
}

TEST_CASE(
    "[Networked][Session][Bucket] L4 a bucket type that answers no "
    "RefCounted is refused, so a game learns at the ask rather than through "
    "a null it stored"
) {
    Ref<NetwMultiplayer> session = make_session();
    admit(session, 7);

    const Variant refused = session->peer_get_bucket(7, Variant(11));

    NETW_CHECK_EQ(int(refused.get_type()), int(Variant::NIL));
    CHECK_FALSE(session->peer_has_bucket(7, Variant(11)));
}

#endif

} // namespace TestNetwPeerBucket

#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/resolved_join.hpp"

namespace TestNetwSessionUsername {

using namespace godot;
using netw::NetwEntity;
using netw::NetwIdentity;
using netw::NetwMultiplayer;
using netw::ResolvedJoin;

Ref<NetwEntity> seated_as(Node *p_node, const StringName &p_id) {
    p_node->set_name("Held");
    const Ref<NetwEntity> entity = NetwEntity::ensure(p_node);
    entity->set_entity_id(p_id);
    return entity;
}

Ref<ResolvedJoin> claiming(int64_t p_peer, const StringName &p_name) {
    Ref<ResolvedJoin> join;
    join.instantiate();
    join->set_peer_id(p_peer);
    join->set_username(p_name);
    return join;
}

TEST_CASE(
    "[Networked][Session][Hosted] U1 a name no seated player holds is "
    "admitted exactly as it was claimed"
) {
    Node *held = memnew(Node);
    TypedArray<NetwEntity> seated;
    seated.push_back(seated_as(held, "ana"));

    Ref<NetwMultiplayer> session;
    session.instantiate();
    const Ref<ResolvedJoin> join = claiming(4, "bo");

    CHECK(session->session_admit_username(join, seated, Callable()));
    CHECK(join->get_username() == StringName("bo"));

    memdelete(held);
}

TEST_CASE(
    "[Networked][Session][Hosted] U2 an unauthenticated collision renames "
    "around the seated name on a debug build rather than seating two players "
    "under one name, which is what a dev loop launching the same client "
    "twice needs"
) {
    Node *held = memnew(Node);
    TypedArray<NetwEntity> seated;
    seated.push_back(seated_as(held, "ana"));

    Ref<NetwMultiplayer> session;
    session.instantiate();
    const Ref<ResolvedJoin> join = claiming(4, "ana");

    CHECK(session->session_admit_username(join, seated, Callable()));
    CHECK(join->get_username() == StringName("ana1"));

    memdelete(held);
}

TEST_CASE(
    "[Networked][Session][Hosted] U3 a collision the peer authenticated "
    "under is refused, the reason is recorded, and the peer is dropped"
) {
    Node *held = memnew(Node);
    TypedArray<NetwEntity> seated;
    seated.push_back(seated_as(held, "ana"));

    Ref<NetwMultiplayer> session;
    session.instantiate();
    Ref<NetwIdentity> identity;
    identity.instantiate();
    session->peer_set_identity(4, identity);

    netw_test::CallLog dropped;
    const Ref<ResolvedJoin> join = claiming(4, "ana");

    CHECK_FALSE(
        session->session_admit_username(join, seated, dropped.callable("drop"))
    );
    NETW_CHECK_EQ(dropped.count("drop"), 1);
    NETW_CHECK_EQ(int64_t(dropped.args("drop")[0]), int64_t(4));
    CHECK_FALSE(session->session_refusal(4).is_empty());
    CHECK(join->get_username() == StringName("ana"));

    memdelete(held);
}

TEST_CASE(
    "[Networked][Session][Hosted] U4 a collision nothing authenticated is "
    "admitted under the claimed name on a build that does not rename, "
    "because no credential settles which player owns it"
) {
    PackedStringArray taken;
    taken.push_back("ana");

    Ref<NetwMultiplayer> session;
    session.instantiate();

    NETW_CHECK_EQ(
        session->session_name_verdict("ana", taken, false, false),
        int(netw::JoinRoster::ADMIT)
    );
    NETW_CHECK_EQ(
        session->session_name_verdict("ana", taken, true, false),
        int(netw::JoinRoster::RENAME)
    );
    NETW_CHECK_EQ(
        session->session_name_verdict("ana", taken, true, true),
        int(netw::JoinRoster::REFUSE)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] U5 a seated player with no stamped id "
    "holds the leading slice of its node name, and no join row admits "
    "nothing"
) {
    Node *held = memnew(Node);
    held->set_name("ana|7");
    TypedArray<NetwEntity> seated;
    seated.push_back(NetwEntity::ensure(held));

    Ref<NetwMultiplayer> session;
    session.instantiate();
    const Ref<ResolvedJoin> join = claiming(4, "ana");

    CHECK(session->session_admit_username(join, seated, Callable()));
    CHECK(join->get_username() == StringName("ana1"));
    CHECK_FALSE(
        session->session_admit_username(Ref<ResolvedJoin>(), seated, Callable())
    );

    memdelete(held);
}

} // namespace TestNetwSessionUsername

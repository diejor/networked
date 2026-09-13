// Laws for NetwIdentity's two naming statics.
//
// The four data members carry no rule, so they carry no case. What is worth
// stating is the fallback ORDER each static walks, because every step of it
// exists to survive something: an entity that was never bound, a node whose
// name is all the identity there is, and a node that has moved.
//
// The serialize/deserialize pair the GDScript form carried is not here. It had
// no caller anywhere in the tree, so the port deleted it rather than
// translating it.

#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwIdentity {

using namespace godot;
using netw::NetwIdentity;
using netw::NetwMultiplayer;

TEST_CASE(
    "[Networked][Session][Hosted] D1 a display name prefers the bound entity "
    "id and falls back to the name that carries it"
) {
    Node *bound = memnew(Node);
    NetwMultiplayer::wrapper_bind(bound, "valeria", 42);
    CHECK(NetwIdentity::username_of(bound) == String("valeria"));

    // Never bound, so the name is the whole of the identity there is, and the
    // separator is where a bound name would have kept it.
    Node *named = memnew(Node);
    named->set_name("valeria|42");
    CHECK(NetwIdentity::username_of(named) == String("valeria"));

    Node *plain = memnew(Node);
    plain->set_name("Player");
    CHECK(NetwIdentity::username_of(plain) == String("Player"));

    CHECK(NetwIdentity::username_of(nullptr) == String());

    memdelete(plain);
    memdelete(named);
    memdelete(bound);
}

TEST_CASE(
    "[Networked][Session][Hosted] D2 a stable key prefers the peer, because a "
    "path stops matching the moment the node moves"
) {
    Node *bound = memnew(Node);
    NetwMultiplayer::wrapper_bind(bound, "valeria", 42);
    NETW_CHECK_EQ(int64_t(NetwIdentity::stable_id_of(bound)), 42);

    // No record, so the name is asked instead, and it spells the same peer.
    Node *named = memnew(Node);
    named->set_name("valeria|7");
    NETW_CHECK_EQ(int64_t(NetwIdentity::stable_id_of(named)), 7);

    // Nothing names a peer, so the answer is the path and it is a String
    // rather than a peer that would collide with peer 0.
    Node *plain = memnew(Node);
    plain->set_name("Player");
    const Variant fallback = NetwIdentity::stable_id_of(plain);
    NETW_CHECK_EQ(fallback.get_type() == Variant::STRING, true);

    CHECK(String(NetwIdentity::stable_id_of(nullptr)) == String());

    memdelete(plain);
    memdelete(named);
    memdelete(bound);
}

} // namespace TestNetwIdentity

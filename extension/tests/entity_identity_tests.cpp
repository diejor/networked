// The entity identity codec's laws.
//
// The name is the only place an entity's identity survives a trip between
// processes, so the codec's whole obligation is a round trip: a pair goes out
// as a name and the same pair has to come back. Every case below is that one
// sentence read against a different corner of the domain, and the corner that
// matters is the separator, because it is the one character an entity id can
// hold that destroys the name it is put into.

#include "support/netw_test.h"

#include "netw/entity_identity.hpp"

namespace TestNetwEntityIdentity {

using godot::String;
using godot::StringName;
using netw::EntityIdentity;

// Whether the pair survives being spelled as a name and read back.
bool round_trips(const String &p_entity_id, int64_t p_peer_id) {
    const String name = EntityIdentity::format(p_entity_id, p_peer_id);
    return EntityIdentity::parse_entity(name) == StringName(p_entity_id)
        && EntityIdentity::parse_peer(name) == p_peer_id;
}

TEST_CASE(
    "[Networked][Entity][Hosted] I1 a named entity survives every peer it can "
    "represent"
) {
    const int64_t peers[] = {0, 1, 2, 42, 2147483647LL, 9007199254740991LL};
    for (int64_t peer : peers) {
        NETW_FORMAT_INT(peer_text, peer);
        CAPTURE(peer_text);
        CHECK(round_trips("valeria", peer));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] I2 the unnamed entity is a pair like any "
    "other, and its peer still survives"
) {
    CHECK(round_trips("", 0));
    CHECK(round_trips("", 42));
    NETW_CHECK_EQ(EntityIdentity::parse_peer(EntityIdentity::format("", 42)), 42);
}

TEST_CASE(
    "[Networked][Entity][Hosted] I3 a name that does not spell one identity "
    "answers no entity and no peer"
) {
    const String names[] = {"", "no_separator", "|", "a|b|c"};
    for (const String &name : names) {
        NETW_FORMAT_TEXT(name_text, name.utf8().get_data());
        CAPTURE(name_text);
        CHECK(EntityIdentity::parse_entity(name) == StringName());
        NETW_CHECK_EQ(EntityIdentity::parse_peer(name), 0);
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] I4 a peer that is not a number is no peer, "
    "and the entity beside it still reads"
) {
    NETW_CHECK_EQ(EntityIdentity::parse_peer("valeria|abc"), 0);
    CHECK(EntityIdentity::parse_entity("valeria|abc") == StringName("valeria"));

    // An absent peer half reads the same way, so a name the codec never
    // produces still names the entity a caller wrote by hand.
    NETW_CHECK_EQ(EntityIdentity::parse_peer("valeria|"), 0);
    CHECK(EntityIdentity::parse_entity("valeria|") == StringName("valeria"));
}

TEST_CASE(
    "[Networked][Entity][Hosted] I5 an entity id holding the separator is "
    "refused, because the name it would make parses back to nothing"
) {
    ERR_PRINT_OFF;
    const String refused = EntityIdentity::format("a|b", 42);
    CHECK(refused.is_empty());

    // What the refusal is worth: the name it declined to produce is one this
    // codec cannot read, so answering with it would have lost the identity
    // silently at the far end.
    CHECK(EntityIdentity::parse_entity("a|b|42") == StringName());
    NETW_CHECK_EQ(EntityIdentity::parse_peer("a|b|42"), 0);
}

} // namespace TestNetwEntityIdentity

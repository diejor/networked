#include "support/netw_test.h"

#include "netw/entity/identity.hpp"

using namespace godot;

namespace TestNetwEntityIdentity {

using godot::String;
using godot::StringName;
using netw::entity::Identity;

bool round_trips(const String &p_entity_id, int64_t p_peer_id) {
    const String name = Identity::format(p_entity_id, p_peer_id);
    return Identity::parse_entity(name) == StringName(p_entity_id)
        && Identity::parse_peer(name) == p_peer_id;
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
    NETW_CHECK_EQ(Identity::parse_peer(Identity::format("", 42)), 42);
}

TEST_CASE(
    "[Networked][Entity][Hosted] I3 a name that does not spell one identity "
    "answers no entity and no peer"
) {
    const String names[] = {"", "no_separator", "|", "a|b|c"};
    for (const String &name : names) {
        NETW_FORMAT_TEXT(name_text, name.utf8().get_data());
        CAPTURE(name_text);
        CHECK(Identity::parse_entity(name) == StringName());
        NETW_CHECK_EQ(Identity::parse_peer(name), 0);
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] I4 a peer that is not a number is no peer, "
    "and the entity beside it still reads"
) {
    NETW_CHECK_EQ(Identity::parse_peer("valeria|abc"), 0);
    CHECK(Identity::parse_entity("valeria|abc") == StringName("valeria"));

    NETW_CHECK_EQ(Identity::parse_peer("valeria|"), 0);
    CHECK(Identity::parse_entity("valeria|") == StringName("valeria"));
}

TEST_CASE(
    "[Networked][Entity][Hosted] I5 an entity id holding the separator is "
    "refused, because the name it would make parses back to nothing"
) {
    ERR_PRINT_OFF;
    const String refused = Identity::format("a|b", 42);
    CHECK(refused.is_empty());

    CHECK(Identity::parse_entity("a|b|42") == StringName());
    NETW_CHECK_EQ(Identity::parse_peer("a|b|42"), 0);
}

} // namespace TestNetwEntityIdentity

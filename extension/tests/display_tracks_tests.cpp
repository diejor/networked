// Laws for NetwDisplayTracks.
//
// A channel has two names and they answer different questions. Its key is an
// identity, so the same key twice is one channel described twice. Its name is
// what a caller holds, and a name two channels both write is ambiguous rather
// than a second channel.

#include "support/netw_test.h"

#include "netw/display_tracks.hpp"

namespace TestNetwDisplayTracks {

using namespace godot;
using netw::NetwDisplayTracks;

Ref<NetwDisplayTracks> make_tracks() {
    Ref<NetwDisplayTracks> tracks;
    tracks.instantiate();
    return tracks;
}

TEST_CASE(
    "[Networked][Display][Hosted] L1 the key is the channel's identity"
) {
    Ref<NetwDisplayTracks> tracks = make_tracks();

    NETW_CHECK_EQ(tracks->declare("Body:position", "position"), 0);
    NETW_CHECK_EQ(tracks->declare("Turret:rotation", "rotation"), 1);
    NETW_CHECK_EQ(tracks->size(), 2);

    SUBCASE("the same key twice is one channel described twice") {
        NETW_CHECK_EQ(tracks->declare("Body:position", "position"), 0);
        NETW_CHECK_EQ(tracks->size(), 2);
    }

    SUBCASE("a channel is found by the key it was declared with") {
        NETW_CHECK_EQ(tracks->by_key("Turret:rotation"), 1);
        NETW_CHECK_EQ(tracks->by_key("Nothing:here"), -1);
    }

    SUBCASE("a re-declaration claims no second name") {
        tracks->declare("Body:position", "velocity");
        NETW_CHECK_EQ(tracks->by_name("velocity"), -1);
        CHECK_FALSE(tracks->is_ambiguous("position"));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L2 a name two channels write is ambiguous "
    "and still answers the first of them"
) {
    Ref<NetwDisplayTracks> tracks = make_tracks();
    tracks->declare("Body:position", "position");

    NETW_CHECK_EQ(tracks->by_name("position"), 0);
    CHECK_FALSE(tracks->is_ambiguous("position"));

    tracks->declare("Visual:position", "position");

    CHECK(tracks->is_ambiguous("position"));
    // The second claimant does not displace the answer, so a lookup that was
    // right before it appeared stays right.
    NETW_CHECK_EQ(tracks->by_name("position"), 0);
    NETW_CHECK_EQ(tracks->ambiguous_count(), 1);

    SUBCASE("a third claimant is the same one ambiguity") {
        tracks->declare("Shadow:position", "position");
        NETW_CHECK_EQ(tracks->ambiguous_count(), 1);
        NETW_CHECK_EQ(tracks->by_name("position"), 0);
    }

    SUBCASE("another name is untouched by it") {
        tracks->declare("Turret:rotation", "rotation");
        CHECK_FALSE(tracks->is_ambiguous("rotation"));
        NETW_CHECK_EQ(tracks->by_name("rotation"), 2);
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] a rebuild starts from no channels at all"
) {
    Ref<NetwDisplayTracks> tracks = make_tracks();
    tracks->declare("Body:position", "position");
    tracks->declare("Visual:position", "position");

    tracks->clear();

    NETW_CHECK_EQ(tracks->size(), 0);
    NETW_CHECK_EQ(tracks->ambiguous_count(), 0);
    NETW_CHECK_EQ(tracks->by_key("Body:position"), -1);
    NETW_CHECK_EQ(tracks->by_name("position"), -1);

    // Indices count from zero again, because the caller's row is empty too.
    NETW_CHECK_EQ(tracks->declare("Body:position", "position"), 0);
}

} // namespace TestNetwDisplayTracks

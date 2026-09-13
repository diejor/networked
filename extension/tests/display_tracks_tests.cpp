#include "support/netw_test.h"

#include "netw/display/tracks.hpp"

namespace TestNetwTracks {

using namespace godot;
using netw::display::Tracks;

TEST_CASE("[Networked][Display][Hosted] L1 the key is the channel's identity") {
    Tracks tracks;

    NETW_CHECK_EQ(tracks.declare("Body:position", "position"), 0);
    NETW_CHECK_EQ(tracks.declare("Turret:rotation", "rotation"), 1);
    NETW_CHECK_EQ(tracks.size(), 2);

    SUBCASE("the same key twice is one channel described twice") {
        NETW_CHECK_EQ(tracks.declare("Body:position", "position"), 0);
        NETW_CHECK_EQ(tracks.size(), 2);
    }

    SUBCASE("a channel is found by the key it was declared with") {
        NETW_CHECK_EQ(tracks.by_key("Turret:rotation"), 1);
        NETW_CHECK_EQ(tracks.by_key("Nothing:here"), -1);
    }

    SUBCASE("a re-declaration claims no second name") {
        tracks.declare("Body:position", "velocity");
        NETW_CHECK_EQ(tracks.by_name("velocity"), -1);
        CHECK_FALSE(tracks.is_ambiguous("position"));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L2 a name two channels write is ambiguous "
    "and still answers the first of them"
) {
    Tracks tracks;
    tracks.declare("Body:position", "position");

    NETW_CHECK_EQ(tracks.by_name("position"), 0);
    CHECK_FALSE(tracks.is_ambiguous("position"));

    tracks.declare("Visual:position", "position");

    CHECK(tracks.is_ambiguous("position"));
    NETW_CHECK_EQ(tracks.by_name("position"), 0);
    NETW_CHECK_EQ(tracks.ambiguous_count(), 1);

    SUBCASE("a third claimant is the same one ambiguity") {
        tracks.declare("Shadow:position", "position");
        NETW_CHECK_EQ(tracks.ambiguous_count(), 1);
        NETW_CHECK_EQ(tracks.by_name("position"), 0);
    }

    SUBCASE("another name is untouched by it") {
        tracks.declare("Turret:rotation", "rotation");
        CHECK_FALSE(tracks.is_ambiguous("rotation"));
        NETW_CHECK_EQ(tracks.by_name("rotation"), 2);
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] a rebuild starts from no channels at all"
) {
    Tracks tracks;
    tracks.declare("Body:position", "position");
    tracks.declare("Visual:position", "position");

    tracks.clear();

    NETW_CHECK_EQ(tracks.size(), 0);
    NETW_CHECK_EQ(tracks.ambiguous_count(), 0);
    NETW_CHECK_EQ(tracks.by_key("Body:position"), -1);
    NETW_CHECK_EQ(tracks.by_name("position"), -1);

    NETW_CHECK_EQ(tracks.declare("Body:position", "position"), 0);
}

} // namespace TestNetwTracks

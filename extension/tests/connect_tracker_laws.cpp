#include "support/netw_test.h"

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/connect/tracker_client.hpp"

namespace netw::connect {

struct TrackerClientProbe {
    static void put(TrackerClient &p_client, const godot::Dictionary &p_data) {
        TrackerEvent one;
        one.kind = TrackerEvent::MESSAGE;
        one.data = p_data;
        p_client.record(one);
    }

    static int64_t held(const TrackerClient &p_client) {
        return int64_t(p_client.events.size());
    }

    static godot::PackedStringArray failed(const TrackerClient &p_client) {
        return p_client.failures;
    }
};

} // namespace netw::connect

namespace TestNetwConnectTracker {

using namespace godot;
using netw::connect::tracker_key;
using netw::connect::TrackerClient;
using netw::connect::TrackerClientProbe;
using netw::connect::TrackerEvent;

Dictionary card_of(const char *p_tag) {
    Dictionary data;
    data["tag"] = String(p_tag);
    return data;
}

TEST_CASE(
    "[Networked][Connect][Hosted] two subscribers each read every message, "
    "because a shared tracker socket carries one swarm for the signaler and "
    "another for the board and neither may consume the other's packet"
) {
    TrackerClient client;
    const int64_t signaler = client.subscribe();
    const int64_t board = client.subscribe();

    TrackerClientProbe::put(client, card_of("first"));
    TrackerClientProbe::put(client, card_of("second"));

    LocalVector<TrackerEvent> to_signaler;
    client.drain(signaler, to_signaler);
    LocalVector<TrackerEvent> to_board;
    client.drain(board, to_board);

    NETW_CHECK_EQ(int(to_signaler.size()), 2);
    NETW_CHECK_EQ(int(to_board.size()), 2);
    CHECK(String(to_signaler[0].data["tag"]) == String("first"));
    CHECK(String(to_board[1].data["tag"]) == String("second"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a drained message is held until the last "
    "subscriber has read it, so a board that polls on a slower cadence than "
    "the signaler still sees the packets that arrived between its reads"
) {
    TrackerClient client;
    const int64_t eager = client.subscribe();
    const int64_t slow = client.subscribe();

    TrackerClientProbe::put(client, card_of("only"));
    LocalVector<TrackerEvent> read;
    client.drain(eager, read);

    NETW_CHECK_EQ(int(TrackerClientProbe::held(client)), 1);

    LocalVector<TrackerEvent> late;
    client.drain(slow, late);

    NETW_CHECK_EQ(int(late.size()), 1);
    NETW_CHECK_EQ(int(TrackerClientProbe::held(client)), 0);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a subscriber reads only what arrived after "
    "it subscribed, because a transport that opens a room mid-session owes "
    "nothing to the announces that preceded it"
) {
    TrackerClient client;
    TrackerClientProbe::put(client, card_of("before"));
    const int64_t late = client.subscribe();
    TrackerClientProbe::put(client, card_of("after"));

    LocalVector<TrackerEvent> read;
    client.drain(late, read);

    NETW_CHECK_EQ(int(read.size()), 1);
    CHECK(String(read[0].data["tag"]) == String("after"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] the last subscriber leaving releases the "
    "ring, because a client the book still holds must not accumulate every "
    "announce of a swarm nobody is reading"
) {
    TrackerClient client;
    const int64_t only = client.subscribe();
    TrackerClientProbe::put(client, card_of("held"));

    NETW_CHECK_EQ(int(TrackerClientProbe::held(client)), 1);
    client.unsubscribe(only);
    NETW_CHECK_EQ(int(TrackerClientProbe::held(client)), 0);

    TrackerClientProbe::put(client, card_of("after"));
    client.unsubscribe(only);
    NETW_CHECK_EQ(int(TrackerClientProbe::held(client)), 0);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a tracker key folds url order away, so two "
    "transports authored with the same trackers listed differently share one "
    "socket set rather than opening the swarm twice"
) {
    PackedStringArray one;
    one.push_back("wss://b.example");
    one.push_back("wss://a.example");
    PackedStringArray other;
    other.push_back("wss://a.example");
    other.push_back("wss://b.example");

    CHECK(tracker_key(one) == tracker_key(other));

    PackedStringArray third;
    third.push_back("wss://a.example");
    CHECK(tracker_key(third) != tracker_key(one));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a tracker that never opens is recorded by "
    "url and reason, so the one warning a game sees names the endpoint to "
    "drop rather than an mbedtls error code with no address in it"
) {
    PackedStringArray urls;
    urls.push_back("wss-typo://first.example");
    urls.push_back("wss-typo://second.example");

    TrackerClient client;
    CHECK(client.connect_to(urls) == ERR_CANT_CONNECT);

    const PackedStringArray failed = TrackerClientProbe::failed(client);
    NETW_CHECK_EQ(int(failed.size()), 2);
    CHECK(failed[0].contains("first.example"));
    CHECK(failed[0].contains("refused to open"));
    CHECK(failed[1].contains("second.example"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a per-url tracker warning is off until a "
    "game asks for it, because a redundant list losing one endpoint is not a "
    "fault and the aggregate warning already covers losing every endpoint"
) {
    CHECK_FALSE(TrackerClient::warns_on_failure());
}

} // namespace TestNetwConnectTracker

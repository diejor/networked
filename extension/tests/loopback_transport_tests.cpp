// Laws for the in-process loopback transport: LocalMultiplayerPeer, the
// session that links peers together, and the conditions that impair the link
// between them.
//
// The cases exist because every other family's tests run over this pair, so a
// wrong answer here reads as a bug somewhere else. They divide in two: what a
// peer does with a packet (who it reaches, who it stops reaching once a peer
// closes), and what the session does to a packet in flight (delay it, drop it,
// duplicate it, hold it, forget it).
//
// Nothing here is timed against a clock. Session time advances only when a
// poll advances it, which is what makes a latency countable in polls and a run
// reproducible.

#include "support/netw_recorder.h"
#include "support/netw_test.h"

#include <cstring>

#include "netw/api/loopback.hpp"

namespace TestNetwLoopbackTransport {

using namespace godot;
using netw::LocalLinkConditions;
using netw::LocalLoopbackSession;
using netw::LocalMultiplayerPeer;
using netw_test::Recorder;

constexpr double PERIOD_MS = 1000.0 / 60.0;

struct Pair {
    Ref<LocalLoopbackSession> session;
    Ref<LocalMultiplayerPeer> server;
    Ref<LocalMultiplayerPeer> client;
};

Pair connected_pair() {
    Pair pair;
    pair.session.instantiate();
    pair.server = pair.session->get_server_peer();
    pair.client = pair.session->create_client_peer();
    pair.session->poll();
    return pair;
}

// PacketPeer's C++ surface takes a raw buffer and its script surface takes a
// PackedByteArray. The cases want one spelling, and this is it.
Error send(
    const Ref<LocalMultiplayerPeer> &p_peer,
    const PackedByteArray &p_bytes
) {
#if defined(NETW_TIER_MODULE)
    return p_peer->put_packet(p_bytes.ptr(), p_bytes.size());
#else
    return p_peer->put_packet(p_bytes);
#endif
}

PackedByteArray receive(const Ref<LocalMultiplayerPeer> &p_peer) {
#if defined(NETW_TIER_MODULE)
    const uint8_t *buffer = nullptr;
    int size = 0;
    if (p_peer->get_packet(&buffer, size) != Error::OK) {
        return PackedByteArray();
    }
    PackedByteArray bytes;
    bytes.resize(size);
    if (size > 0) {
        memcpy(bytes.ptrw(), buffer, size);
    }
    return bytes;
#else
    return p_peer->get_packet();
#endif
}

// MultiplayerPeer::is_server is a C++ virtual that the extension tier does not
// bind, so the two tiers reach it under different names.
bool is_server(const Ref<LocalMultiplayerPeer> &p_peer) {
    return p_peer->NETW_PEER_VIRTUAL(is_server)();
}

PackedByteArray one_byte(int p_value) {
    PackedByteArray bytes;
    bytes.push_back(uint8_t(p_value));
    return bytes;
}

void send_counted(
    const Pair &p_pair,
    int p_count,
    MultiplayerPeer::TransferMode p_mode,
    bool p_poll_between
) {
    p_pair.client->set_target_peer(1);
    p_pair.client->set_transfer_mode(p_mode);
    for (int value = 0; value < p_count; ++value) {
        send(p_pair.client, one_byte(value));
        if (p_poll_between) {
            p_pair.session->poll();
        }
    }
}

String drain(const Ref<LocalMultiplayerPeer> &p_peer) {
    String text;
    while (p_peer->get_available_packet_count() > 0) {
        if (!text.is_empty()) {
            text += String(",");
        }
        text += String::num_int64(receive(p_peer)[0]);
    }
    return text;
}

void check_sequence(
    const char *p_what,
    const String &p_actual,
    const String &p_expected
) {
    const CharString actual = p_actual.utf8();
    const CharString expected = p_expected.utf8();
    CAPTURE(p_what);
    CAPTURE(actual.get_data());
    CAPTURE(expected.get_data());
    CHECK(bool(p_actual == p_expected));
}

int survives(
    const Ref<LocalLoopbackSession> &p_session,
    const Ref<LocalMultiplayerPeer> &p_server,
    const Ref<LocalMultiplayerPeer> &p_client
) {
    p_client->set_target_peer(1);
    p_client->set_transfer_mode(MultiplayerPeer::TRANSFER_MODE_UNRELIABLE);
    send(p_client, one_byte(0));
    p_session->poll();
    const int arrived = p_server->get_available_packet_count();
    drain(p_server);
    return arrived;
}

Ref<LocalLinkConditions> with_latency(int64_t p_seed, double p_periods) {
    Ref<LocalLinkConditions> conditions = LocalLinkConditions::create(p_seed);
    conditions->set_latency_ms(p_periods * PERIOD_MS);
    return conditions;
}

TEST_CASE(
    "[Networked][Transport][Hosted] A peer knows its side, and a client "
    "reaches CONNECTED on its first poll"
) {
    Ref<LocalMultiplayerPeer> server;
    server.instantiate();
    Ref<LocalMultiplayerPeer> client;
    client.instantiate();
    server->create_server();
    client->create_client(42);
    server->force_connect_peer(42, client.ptr());
    client->force_connect_peer(1, server.ptr());

    CHECK(is_server(server));
    CHECK_FALSE(is_server(client));
    NETW_CHECK_EQ(server->get_unique_id(), 1);
    NETW_CHECK_EQ(client->get_unique_id(), 42);
    CHECK(server->is_linked_to(42));
    NETW_CHECK_EQ(
        client->get_connection_status(),
        MultiplayerPeer::CONNECTION_CONNECTING
    );

    Recorder events(server.ptr(), {"peer_connected"});
    server->poll();
    NETW_CHECK_EQ(events.count("peer_connected"), 1);
    CHECK(bool(events.args("peer_connected")[0] == Variant(42)));

    client->poll();
    NETW_CHECK_EQ(
        client->get_connection_status(),
        MultiplayerPeer::CONNECTION_CONNECTED
    );
}

TEST_CASE(
    "[Networked][Transport][Hosted] A packet reaches the peer it is addressed "
    "to, and target zero reaches every link"
) {
    Ref<LocalMultiplayerPeer> server;
    server.instantiate();
    Ref<LocalMultiplayerPeer> first;
    first.instantiate();
    Ref<LocalMultiplayerPeer> second;
    second.instantiate();
    server->create_server();
    first->create_client(42);
    second->create_client(99);
    server->force_connect_peer(42, first.ptr());
    server->force_connect_peer(99, second.ptr());
    first->force_connect_peer(1, server.ptr());
    second->force_connect_peer(1, server.ptr());
    first->poll();
    second->poll();

    const PackedByteArray payload = one_byte(7);
    first->set_target_peer(1);
    CHECK_EQ(send(first, payload), Error::OK);
    NETW_CHECK_EQ(server->get_available_packet_count(), 1);
    NETW_CHECK_EQ(server->get_packet_peer(), 42);
    CHECK(bool(receive(server) == payload));

    server->set_target_peer(0);
    CHECK_EQ(send(server, one_byte(8)), Error::OK);
    NETW_CHECK_EQ(first->get_available_packet_count(), 1);
    NETW_CHECK_EQ(second->get_available_packet_count(), 1);
}

TEST_CASE(
    "[Networked][Transport][Hosted] A negative target excludes exactly that "
    "peer"
) {
    Ref<LocalMultiplayerPeer> server;
    server.instantiate();
    Ref<LocalMultiplayerPeer> first;
    first.instantiate();
    Ref<LocalMultiplayerPeer> second;
    second.instantiate();
    server->create_server();
    first->create_client(42);
    second->create_client(99);
    server->force_connect_peer(42, first.ptr());
    server->force_connect_peer(99, second.ptr());

    server->set_target_peer(-42);
    CHECK_EQ(send(server, one_byte(5)), Error::OK);

    NETW_CHECK_EQ(first->get_available_packet_count(), 0);
    NETW_CHECK_EQ(second->get_available_packet_count(), 1);
}

TEST_CASE(
    "[Networked][Transport][Hosted] A client addressing another client routes "
    "through the server"
) {
    Ref<LocalMultiplayerPeer> server;
    server.instantiate();
    Ref<LocalMultiplayerPeer> first;
    first.instantiate();
    Ref<LocalMultiplayerPeer> second;
    second.instantiate();
    server->create_server();
    first->create_client(42);
    second->create_client(99);
    server->force_connect_peer(42, first.ptr());
    server->force_connect_peer(99, second.ptr());
    first->force_connect_peer(1, server.ptr());
    second->force_connect_peer(1, server.ptr());

    CHECK_FALSE(first->is_linked_to(99));
    first->set_target_peer(99);
    CHECK_EQ(send(first, one_byte(3)), Error::OK);

    NETW_CHECK_EQ(second->get_available_packet_count(), 0);
    NETW_CHECK_EQ(server->get_available_packet_count(), 1);
}

TEST_CASE(
    "[Networked][Transport][Hosted] Closing a peer disconnects its partner, "
    "and disconnect_peer unlinks both sides"
) {
    Ref<LocalMultiplayerPeer> server;
    server.instantiate();
    Ref<LocalMultiplayerPeer> client;
    client.instantiate();
    server->create_server();
    client->create_client(42);
    server->force_connect_peer(42, client.ptr());
    client->force_connect_peer(1, server.ptr());
    client->poll();
    server->poll();

    Recorder events(server.ptr(), {"peer_disconnected"});
    client->close();
    server->poll();
    NETW_CHECK_EQ(events.count("peer_disconnected"), 1);
    CHECK(bool(events.args("peer_disconnected")[0] == Variant(42)));
    CHECK_FALSE(server->is_linked_to(42));

    Ref<LocalMultiplayerPeer> rejoined;
    rejoined.instantiate();
    rejoined->create_client(42);
    server->force_connect_peer(42, rejoined.ptr());
    rejoined->force_connect_peer(1, server.ptr());
    rejoined->poll();

    server->disconnect_peer(42);

    CHECK_FALSE(server->is_linked_to(42));
    CHECK_FALSE(rejoined->is_linked_to(1));
    NETW_CHECK_EQ(
        rejoined->get_connection_status(),
        MultiplayerPeer::CONNECTION_DISCONNECTED
    );
}

TEST_CASE(
    "[Networked][Transport][Hosted] A session hands out one server and "
    "distinct clients, and reset unlinks all of them"
) {
    Ref<LocalLoopbackSession> session;
    session.instantiate();

    Ref<LocalMultiplayerPeer> server = session->get_server_peer();
    Ref<LocalMultiplayerPeer> first = session->create_client_peer();
    Ref<LocalMultiplayerPeer> second = session->create_client_peer();

    CHECK(server.is_valid());
    CHECK(is_server(server));
    CHECK(bool(first.ptr() != second.ptr()));
    CHECK(first->get_unique_id() != second->get_unique_id());
    CHECK(server->is_linked_to(first->get_unique_id()));
    CHECK(server->is_linked_to(second->get_unique_id()));
    NETW_CHECK_EQ(session->get_client_peers().size(), 2);

    session->poll();
    NETW_CHECK_EQ(
        first->get_connection_status(),
        MultiplayerPeer::CONNECTION_CONNECTED
    );
    NETW_CHECK_EQ(
        second->get_connection_status(),
        MultiplayerPeer::CONNECTION_CONNECTED
    );

    Recorder events(server.ptr(), {"peer_disconnected"});
    const int first_id = first->get_unique_id();
    first->close();
    session->poll();
    CHECK_FALSE(server->is_linked_to(first_id));
    NETW_CHECK_EQ(events.count("peer_disconnected"), 1);
    CHECK(bool(events.args("peer_disconnected")[0] == Variant(first_id)));

    Ref<LocalMultiplayerPeer> third = session->get_client_peer();
    session->poll();
    NETW_CHECK_EQ(
        third->get_connection_status(),
        MultiplayerPeer::CONNECTION_CONNECTED
    );
    NETW_CHECK_EQ(session->get_client_peers().size(), 3);

    session->set_server_app_id("test-app");
    session->reset();
    CHECK_FALSE(session->has_live_server());
    CHECK(bool(session->get_server_app_id() == StringName()));
    NETW_CHECK_EQ(session->get_client_peers().size(), 0);
    CHECK(bool(server->get_loopback_session() == nullptr));
    CHECK(bool(second->get_loopback_session() == nullptr));
    CHECK(bool(third->get_loopback_session() == nullptr));
}

TEST_CASE(
    "[Networked][Transport][Hosted] Latency holds a packet for as many polls "
    "as it is periods long"
) {
    Pair pair = connected_pair();
    pair.session->set_link_conditions(pair.server.ptr(), with_latency(10, 3.0));

    const PackedByteArray payload = one_byte(1);
    pair.client->set_target_peer(1);
    CHECK_EQ(send(pair.client, payload), Error::OK);

    pair.session->poll();
    NETW_CHECK_EQ(pair.server->get_available_packet_count(), 0);
    pair.session->poll();
    NETW_CHECK_EQ(pair.server->get_available_packet_count(), 0);
    pair.session->poll();
    NETW_CHECK_EQ(pair.server->get_available_packet_count(), 1);
    CHECK(bool(receive(pair.server) == payload));
}

TEST_CASE(
    "[Networked][Transport][Hosted] Installing conditions captures the "
    "packets already queued"
) {
    Pair pair = connected_pair();

    const PackedByteArray payload = one_byte(9);
    pair.client->set_target_peer(1);
    CHECK_EQ(send(pair.client, payload), Error::OK);
    NETW_CHECK_EQ(pair.server->get_available_packet_count(), 1);

    pair.session->set_link_conditions(pair.server.ptr(), with_latency(10, 3.0));
    NETW_CHECK_EQ(pair.server->get_available_packet_count(), 0);

    for (int poll = 0; poll < 3; ++poll) {
        pair.session->poll();
    }
    NETW_CHECK_EQ(pair.server->get_available_packet_count(), 1);
    CHECK(bool(receive(pair.server) == payload));
}

TEST_CASE(
    "[Networked][Transport][Hosted] A lost reliable packet is retransmitted "
    "and a lost unreliable one is gone"
) {
    Pair reliable = connected_pair();
    Ref<LocalLinkConditions> always_lost = LocalLinkConditions::create(10);
    always_lost->set_packet_loss(1.0);
    always_lost->set_retransmit_ms(3.0 * PERIOD_MS);
    reliable.session->set_link_conditions(reliable.server.ptr(), always_lost);

    const PackedByteArray payload = one_byte(4);
    reliable.client->set_target_peer(1);
    CHECK_EQ(send(reliable.client, payload), Error::OK);
    reliable.session->poll();
    NETW_CHECK_EQ(reliable.server->get_available_packet_count(), 0);
    reliable.session->poll();
    NETW_CHECK_EQ(reliable.server->get_available_packet_count(), 0);
    reliable.session->poll();
    NETW_CHECK_EQ(reliable.server->get_available_packet_count(), 1);
    CHECK(bool(receive(reliable.server) == payload));

    Pair unreliable = connected_pair();
    Ref<LocalLinkConditions> dropped = LocalLinkConditions::create(10);
    dropped->set_packet_loss(1.0);
    unreliable.session->set_link_conditions(unreliable.server.ptr(), dropped);
    send_counted(
        unreliable,
        1,
        MultiplayerPeer::TRANSFER_MODE_UNRELIABLE,
        true
    );
    NETW_CHECK_EQ(unreliable.server->get_available_packet_count(), 0);
}

TEST_CASE(
    "[Networked][Transport][Hosted] Reliable delivery keeps per-channel order "
    "under jitter"
) {
    Pair pair = connected_pair();
    Ref<LocalLinkConditions> shuffling = LocalLinkConditions::create(123);
    shuffling->set_jitter_ms(6.0 * PERIOD_MS);
    shuffling->set_reorder(1.0);
    pair.session->set_link_conditions(pair.server.ptr(), shuffling);

    send_counted(pair, 12, MultiplayerPeer::TRANSFER_MODE_RELIABLE, true);
    for (int poll = 0; poll < 12; ++poll) {
        pair.session->poll();
    }
    check_sequence(
        "reliable order",
        drain(pair.server),
        String("0,1,2,3,4,5,6,7,8,9,10,11")
    );
}

TEST_CASE(
    "[Networked][Transport][Hosted] Duplication delivers the same packet "
    "twice, one period apart"
) {
    Pair pair = connected_pair();
    Ref<LocalLinkConditions> doubling = LocalLinkConditions::create(10);
    doubling->set_duplicate(1.0);
    pair.session->set_link_conditions(pair.server.ptr(), doubling);

    pair.client->set_target_peer(1);
    pair.client->set_transfer_mode(MultiplayerPeer::TRANSFER_MODE_UNRELIABLE);
    CHECK_EQ(send(pair.client, one_byte(7)), Error::OK);

    pair.session->poll();
    pair.session->poll();
    check_sequence("duplicated", drain(pair.server), String("7,7"));
}

TEST_CASE(
    "[Networked][Transport][Hosted] Throttling holds every packet until the "
    "window closes"
) {
    Pair pair = connected_pair();
    Ref<LocalLinkConditions> stalling = LocalLinkConditions::create(10);
    stalling->set_throttle(1.0);
    stalling->set_throttle_ms(4.0 * PERIOD_MS);
    pair.session->set_link_conditions(pair.server.ptr(), stalling);

    pair.client->set_target_peer(1);
    pair.client->set_transfer_mode(MultiplayerPeer::TRANSFER_MODE_UNRELIABLE);
    for (int value = 0; value < 3; ++value) {
        CHECK_EQ(send(pair.client, one_byte(value)), Error::OK);
        pair.session->poll();
        NETW_CHECK_EQ(pair.server->get_available_packet_count(), 0);
    }

    pair.session->poll();
    check_sequence("throttled", drain(pair.server), String("0,1,2"));
}

TEST_CASE(
    "[Networked][Transport][Hosted] Conditions installed for one sender leave "
    "another sender alone"
) {
    Ref<LocalLoopbackSession> session;
    session.instantiate();
    Ref<LocalMultiplayerPeer> server = session->get_server_peer();
    Ref<LocalMultiplayerPeer> delayed = session->create_client_peer();
    Ref<LocalMultiplayerPeer> immediate = session->create_client_peer();
    session->poll();

    session->set_link_conditions(
        server.ptr(),
        with_latency(10, 3.0),
        delayed->get_unique_id()
    );

    delayed->set_target_peer(1);
    immediate->set_target_peer(1);
    CHECK_EQ(send(delayed, one_byte(1)), Error::OK);
    CHECK_EQ(send(immediate, one_byte(2)), Error::OK);

    session->poll();
    check_sequence("unconditioned sender", drain(server), String("2"));
    session->poll();
    NETW_CHECK_EQ(server->get_available_packet_count(), 0);
    session->poll();
    check_sequence("delayed sender", drain(server), String("1"));
}

TEST_CASE(
    "[Networked][Transport][Hosted] purge_packets_from drops one sender's "
    "in-flight packets"
) {
    Ref<LocalLoopbackSession> session;
    session.instantiate();
    Ref<LocalMultiplayerPeer> server = session->get_server_peer();
    Ref<LocalMultiplayerPeer> first = session->create_client_peer();
    Ref<LocalMultiplayerPeer> second = session->create_client_peer();
    session->poll();
    session->set_link_conditions(server.ptr(), with_latency(10, 3.0));

    first->set_target_peer(1);
    second->set_target_peer(1);
    CHECK_EQ(send(first, one_byte(1)), Error::OK);
    CHECK_EQ(send(second, one_byte(2)), Error::OK);
    NETW_CHECK_EQ(session->in_flight_count(server.ptr()), 2);

    session->purge_packets_from(first->get_unique_id());
    NETW_CHECK_EQ(session->in_flight_count(server.ptr()), 1);

    for (int poll = 0; poll < 3; ++poll) {
        session->poll();
    }
    check_sequence("surviving sender", drain(server), String("2"));
}

TEST_CASE(
    "[Networked][Transport][Hosted] Held packets release ahead of the packets "
    "that arrived after them"
) {
    Pair pair = connected_pair();
    pair.session->hold_inbound_packets(pair.server.ptr());
    CHECK(pair.session->is_holding_inbound(pair.server.ptr()));

    pair.client->set_target_peer(1);
    CHECK_EQ(send(pair.client, one_byte(1)), Error::OK);
    pair.session->poll();
    CHECK_EQ(send(pair.client, one_byte(2)), Error::OK);
    NETW_CHECK_EQ(pair.server->get_available_packet_count(), 0);

    pair.session->release_inbound_packets(pair.server.ptr());
    CHECK_FALSE(pair.session->is_holding_inbound(pair.server.ptr()));
    check_sequence("held then queued", drain(pair.server), String("1,2"));
}

TEST_CASE(
    "[Networked][Transport][Hosted] Re-installing conditions resets only that "
    "sender's random streams"
) {
    Ref<LocalLinkConditions> half_lost = LocalLinkConditions::create(21);
    half_lost->set_packet_loss(0.5);

    Pair reference = connected_pair();
    reference.session->set_link_conditions(reference.server.ptr(), half_lost);
    const int first_draw
        = survives(reference.session, reference.server, reference.client);
    const int second_draw
        = survives(reference.session, reference.server, reference.client);
    // Without two different draws the case cannot tell a reset stream from a
    // running one, and would pass on an implementation that resets nothing.
    REQUIRE(first_draw != second_draw);

    Ref<LocalLoopbackSession> session;
    session.instantiate();
    Ref<LocalMultiplayerPeer> server = session->get_server_peer();
    Ref<LocalMultiplayerPeer> reset_sender = session->create_client_peer();
    Ref<LocalMultiplayerPeer> untouched = session->create_client_peer();
    session->poll();
    session->set_link_conditions(
        server.ptr(),
        half_lost,
        reset_sender->get_unique_id()
    );
    session->set_link_conditions(
        server.ptr(),
        half_lost,
        untouched->get_unique_id()
    );

    NETW_CHECK_EQ(survives(session, server, reset_sender), first_draw);
    NETW_CHECK_EQ(survives(session, server, untouched), first_draw);

    session->set_link_conditions(
        server.ptr(),
        half_lost,
        reset_sender->get_unique_id()
    );

    NETW_CHECK_EQ(survives(session, server, reset_sender), first_draw);
    NETW_CHECK_EQ(survives(session, server, untouched), second_draw);
}

} // namespace TestNetwLoopbackTransport

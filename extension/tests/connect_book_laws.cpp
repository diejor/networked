#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/variant.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/transport.hpp"
#include "netw/connect/transports.hpp"

namespace TestNetwConnectBook {

using namespace godot;
using netw::connect::ENetTransport;
using netw::connect::LocalTransport;
using netw::connect::peer_class_of;
using netw::connect::Transport;
using netw::connect::TransportBook;
using netw::connect::WebSocketTransport;

class RedTransport : public Transport {
public:
    StringName peer_class() const override {
        return StringName("RedPeer");
    }
    String display_name() const override {
        return String("Red");
    }
    void make_peer(int, const String &, const Dictionary &) override {
        fail(ERR_UNCONFIGURED, String("red"));
    }
};

class BluePeerTransport : public Transport {
public:
    StringName peer_class() const override {
        return StringName("BluePeer");
    }
    String display_name() const override {
        return String("Blue");
    }
    bool recognizes_peer(const Ref<MultiplayerPeer> &p_peer) const override {
        return p_peer.is_valid();
    }
    void make_peer(int, const String &, const Dictionary &) override {
        fail(ERR_UNCONFIGURED, String("blue"));
    }
};

Transport *make_red() {
    return new RedTransport();
}

Transport *make_blue() {
    return new BluePeerTransport();
}

struct BookScope {
    TransportBook book;
};

TEST_CASE(
    "[Networked][Connect][Hosted] a transport is minted per caller rather "
    "than shared, because one transport belongs to one session and a second "
    "session asking for the same peer class must not be handed the first "
    "session's live carrier"
) {
    BookScope scope;
    scope.book.install_native(StringName("RedPeer"), make_red, String("Red"));

    Transport *first = scope.book.make(StringName("RedPeer"));
    Transport *second = scope.book.make(StringName("RedPeer"));

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first != second);

    delete first;
    delete second;
}

TEST_CASE(
    "[Networked][Connect][Hosted] a peer class no row owns mints nothing, "
    "which is the ERR_UNCONFIGURED a builder reports rather than a crash"
) {
    BookScope scope;
    scope.book.install_native(StringName("RedPeer"), make_red, String("Red"));

    CHECK(scope.book.make(StringName("GreenPeer")) == nullptr);
    CHECK_FALSE(scope.book.holds(StringName("GreenPeer")));
    CHECK(scope.book.holds(StringName("RedPeer")));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a second registration for one peer class "
    "replaces the first and keeps its place, so a game overriding a stock "
    "transport does not also reorder the book behind it"
) {
    BookScope scope;
    scope.book.install_native(StringName("RedPeer"), make_red, String("Red"));
    scope.book
        .install_native(StringName("BluePeer"), make_blue, String("Blue"));
    scope.book
        .install_native(StringName("RedPeer"), make_blue, String("Red II"));

    const Array classes = scope.book.peer_classes();

    NETW_CHECK_EQ(int(classes.size()), 2);
    CHECK(StringName(classes[0]) == StringName("RedPeer"));
    CHECK(StringName(classes[1]) == StringName("BluePeer"));

    const netw::connect::TransportRow *row
        = scope.book.row(StringName("RedPeer"));
    REQUIRE(row != nullptr);
    CHECK(row->display_name == String("Red II"));

    SUBCASE("and forgetting one leaves the other standing") {
        scope.book.forget(StringName("RedPeer"));

        CHECK_FALSE(scope.book.holds(StringName("RedPeer")));
        CHECK(scope.book.holds(StringName("BluePeer")));
        NETW_CHECK_EQ(int(scope.book.peer_classes().size()), 1);
    }
}

TEST_CASE(
    "[Networked][Connect][Hosted] a peer is offered to every row in order "
    "and the first taker adopts it, which is how a peer a game built by "
    "hand is elevated without the game naming a transport at all"
) {
    BookScope scope;
    scope.book.install_native(StringName("RedPeer"), make_red, String("Red"));
    scope.book
        .install_native(StringName("BluePeer"), make_blue, String("Blue"));

    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();

    Transport *taker = scope.book.make_for_peer(peer);

    REQUIRE(taker != nullptr);
    CHECK(taker->peer_class() == StringName("BluePeer"));

    delete taker;

    SUBCASE("and a book whose every row refuses mints nothing") {
        BookScope refusing;
        refusing.book
            .install_native(StringName("RedPeer"), make_red, String("Red"));

        CHECK(refusing.book.make_for_peer(peer) == nullptr);
    }
}

TEST_CASE(
    "[Networked][Connect][Hosted] a peer answers the class it is, which is "
    "the only key the book has and the reason a scheme string is not needed"
) {
    Ref<netw::LocalMultiplayerPeer> local;
    local.instantiate();

    CHECK(peer_class_of(local) == StringName("LocalMultiplayerPeer"));
    CHECK(peer_class_of(Ref<MultiplayerPeer>()) == StringName());
}

TEST_CASE(
    "[Networked][Connect][Hosted] each stock transport publishes the peer "
    "class it is keyed by and the settings a form draws its fields from, so "
    "a Host form knows every field without knowing any transport by name"
) {
    ENetTransport enet;
    WebSocketTransport websocket;
    LocalTransport local;

    CHECK(enet.peer_class() == StringName("ENetMultiplayerPeer"));
    CHECK(websocket.peer_class() == StringName("WebSocketMultiplayerPeer"));
    CHECK(local.peer_class() == StringName("LocalMultiplayerPeer"));

    const Dictionary enet_settings = enet.host_settings();
    CHECK(enet_settings.has("port"));
    CHECK(enet_settings.has("max_players"));
    NETW_CHECK_EQ(int64_t(enet_settings["port"]), int64_t(21253));
    NETW_CHECK_EQ(int64_t(enet_settings["max_players"]), int64_t(32));

    CHECK(websocket.host_settings().has("port"));

    SUBCASE("and a client is offered exactly what its join path reads") {
        const Dictionary enet_client = enet.client_settings();
        CHECK(enet_client.has("port"));
        CHECK_FALSE(enet_client.has("max_players"));
        NETW_CHECK_EQ(int64_t(enet_client["port"]), int64_t(21253));

        CHECK(websocket.client_settings().is_empty());
        CHECK(local.client_settings().is_empty());
    }

    SUBCASE("and each reach is its own answer rather than a bitfield") {
        CHECK(enet.can_probe());
        CHECK(local.can_probe());
        CHECK_FALSE(enet.can_browse());
        CHECK_FALSE(local.can_browse());
    }

    SUBCASE("and the four address readers are what a field renders from") {
        CHECK(enet.address_label() == String("Server IP"));
        CHECK(enet.address_placeholder() == String("localhost"));
        CHECK(enet.address_help().is_empty());
        CHECK(enet.accepts_empty_address());
    }
}

TEST_CASE(
    "[Networked][Connect][Hosted] the shared book carries the three stock "
    "peer classes at load, so a game that registered nothing still hosts "
    "and joins over ENet, WebSocket and the loopback"
) {
    const TransportBook &book = TransportBook::shared();

    CHECK(book.holds(StringName("ENetMultiplayerPeer")));
    CHECK(book.holds(StringName("WebSocketMultiplayerPeer")));
    CHECK(book.holds(StringName("LocalMultiplayerPeer")));

    Transport *made = book.make(StringName("ENetMultiplayerPeer"));
    REQUIRE(made != nullptr);
    CHECK(made->display_name() == String("ENet"));
    delete made;
}

TEST_CASE(
    "[Networked][Connect][Hosted] a websocket address is normalised to a url "
    "and a bare host defaults to the secure scheme, because a typed host "
    "that silently downgraded would be worse than one that failed"
) {
    CHECK(
        WebSocketTransport::build_url(String())
        == String("ws://localhost:21253")
    );
    CHECK(
        WebSocketTransport::build_url(String("localhost"))
        == String("ws://localhost:21253")
    );
    CHECK(
        WebSocketTransport::build_url(String("127.0.0.1"))
        == String("ws://localhost:21253")
    );
    CHECK(
        WebSocketTransport::build_url(String("ws://example.org:9000"))
        == String("ws://example.org:9000")
    );
    CHECK(
        WebSocketTransport::build_url(String("wss://example.org"))
        == String("wss://example.org")
    );
    CHECK(
        WebSocketTransport::build_url(String("example.org"))
        == String("wss://example.org")
    );
}

TEST_CASE(
    "[Networked][Connect][Hosted] an address splits on its last colon, so a "
    "host that carries colons of its own still yields the port a caller meant"
) {
    String host;
    int64_t port = 0;

    netw::connect::split_host_port(
        String("203.0.113.42:9000"),
        21253,
        host,
        port
    );
    CHECK(host == String("203.0.113.42"));
    NETW_CHECK_EQ(port, int64_t(9000));

    netw::connect::split_host_port(String("example.org"), 21253, host, port);
    CHECK(host == String("example.org"));
    NETW_CHECK_EQ(port, int64_t(21253));

    netw::connect::split_host_port(String(), 21253, host, port);
    CHECK(host == String("localhost"));
    NETW_CHECK_EQ(port, int64_t(21253));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a local probe answers only while a server "
    "is live, so join_or_host hosts rather than binding the shared server "
    "peer a second time"
) {
    netw::LocalLoopbackSession::get_shared_session()->reset();

    LocalTransport local;
    Ref<netw::NetwPromise> answered;
    answered.instantiate();

    local.arm(RID(), answered);
    local.probe(String());

    CHECK(answered->get_is_failed());
    NETW_CHECK_EQ(answered->get_code(), int(ERR_CANT_CONNECT));

    SUBCASE("and answers once one is") {
        Ref<netw::NetwPromise> opened;
        opened.instantiate();
        local.arm(RID(), opened);
        local.make_peer(netw::connect::PEER_MODE_HOST, String(), Dictionary());
        REQUIRE(opened->get_is_completed());

        Ref<netw::NetwPromise> again;
        again.instantiate();
        local.arm(RID(), again);
        local.probe(String());

        CHECK(again->get_is_completed());
    }

    netw::LocalLoopbackSession::get_shared_session()->reset();
}

} // namespace TestNetwConnectBook

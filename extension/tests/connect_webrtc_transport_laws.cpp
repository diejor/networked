#include "support/netw_test.h"

#include "godot/json.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session_config.hpp"
#include "netw/connect/transport.hpp"
#include "netw/connect/turn_credentials.hpp"
#include "netw/connect/webrtc_transport.hpp"

namespace netw::connect {

struct WebRTCTransportProbe {
    static void apply(
        WebRTCTransport &p_transport,
        const godot::Dictionary &p_settings
    ) {
        p_transport.apply_settings(p_settings);
    }

    static godot::String space(const WebRTCTransport &p_transport) {
        return p_transport.signaling_namespace;
    }
};

} // namespace netw::connect

namespace TestNetwConnectWebRTCTransport {

using namespace godot;
using netw::connect::Transport;
using netw::connect::TransportBook;
using netw::connect::WebRTCTransport;
using netw::connect::WebRTCTransportProbe;

Dictionary server_of(const Array &p_urls) {
    Dictionary one;
    one["urls"] = p_urls;
    return one;
}

Array urls_of(const char *p_first, const char *p_second = nullptr) {
    Array urls;
    urls.push_back(String(p_first));
    if (p_second != nullptr) {
        urls.push_back(String(p_second));
    }
    return urls;
}

TEST_CASE(
    "[Networked][Connect][Hosted] a TURN url the native ICE stack cannot "
    "dial is filtered out, because libjuice is UDP-only and a TLS or TCP "
    "relay would be gathered as a candidate that can never connect"
) {
    CHECK(!WebRTCTransport::is_url_supported_native("turns:relay.example:443"));
    CHECK(!WebRTCTransport::is_url_supported_native(
        "turn:relay.example:443?transport=tcp"
    ));
    CHECK(!WebRTCTransport::is_url_supported_native(
        "TURN:relay.example:443?transport=TLS"
    ));
    CHECK(WebRTCTransport::is_url_supported_native("turn:relay.example:3478"));
    CHECK(WebRTCTransport::is_url_supported_native("stun:stun.example:19302"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] filtering keeps a server that still has a "
    "dialable url and drops one left with none, because a server entry with "
    "an empty url list is configuration the engine will warn about"
) {
    Array authored;
    authored.push_back(
        server_of(urls_of("turns:relay.example:443", "turn:relay.example:3478"))
    );
    authored.push_back(server_of(urls_of("turns:only.example:443")));
    authored.push_back(server_of(urls_of("stun:stun.example:19302")));

    const Array kept = WebRTCTransport::filter_ice_servers(authored);

    NETW_CHECK_EQ(int(kept.size()), 2);
    const Dictionary trimmed = kept[0];
    const Array surviving = trimmed["urls"];
    NETW_CHECK_EQ(int(surviving.size()), 1);
    CHECK(String(surviving[0]) == String("turn:relay.example:3478"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] filtering carries a server's credentials "
    "across, because a TURN entry stripped of its username would be dialled "
    "as an anonymous relay and refused"
) {
    Dictionary authored_turn = server_of(
        urls_of("turns:relay.example:443", "turn:relay.example:3478")
    );
    authored_turn["username"] = "who";
    authored_turn["credential"] = "secret";
    Array authored;
    authored.push_back(authored_turn);

    const Array kept = WebRTCTransport::filter_ice_servers(authored);

    NETW_CHECK_EQ(int(kept.size()), 1);
    const Dictionary carried = kept[0];
    CHECK(String(carried["username"]) == String("who"));
    CHECK(String(carried["credential"]) == String("secret"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] the connect deadline covers a whole gather "
    "plus every offer the retry budget allows, because a deadline shorter "
    "than the budget ends the attempt while it is still legitimately trying"
) {
    WebRTCTransport transport;
    Dictionary settings = transport.host_settings();

    const double gather = double(settings["gather_timeout"]);
    const double retry = double(settings["connect_retry"]);
    const int64_t attempts = int64_t(settings["max_connect_attempts"]);

    CHECK(transport.timeout_hint() > gather + retry * double(attempts));
}

TEST_CASE(
    "[Networked][Connect][Hosted] the transport publishes every key it reads "
    "in its defaults, so a form and a settings reader discover the whole "
    "vocabulary at runtime rather than from prose"
) {
    WebRTCTransport transport;
    const Dictionary settings = transport.host_settings();

    CHECK(settings.has("signaling_namespace"));
    CHECK(settings.has("ice_servers"));
    CHECK(settings.has("trackers"));
    CHECK(settings.has("connect_retry"));
    CHECK(settings.has("max_connect_attempts"));
    CHECK(settings.has("gather_timeout"));
    CHECK(settings.has("signaler"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a client is offered every key join_peer "
    "reads and none of the three a host alone acts on, because a client "
    "that cannot name the tracker the host announced on never finds the room"
) {
    WebRTCTransport transport;
    const Dictionary client = transport.client_settings();

    CHECK(client.has("signaling_namespace"));
    CHECK(client.has("ice_servers"));
    CHECK(client.has("trackers"));
    CHECK(client.has("connect_retry"));
    CHECK(client.has("max_connect_attempts"));
    CHECK(client.has("gather_timeout"));

    CHECK_FALSE(client.has("signaler"));
    CHECK_FALSE(client.has("room_name"));
    CHECK_FALSE(client.has("max_players"));

    SUBCASE("and a host is offered those same keys and three more") {
        const Dictionary host = transport.host_settings();
        const Array offered = client.keys();
        for (int64_t at = 0; at < offered.size(); at++) {
            CHECK(host.has(offered[at]));
        }
        NETW_CHECK_EQ(host.size(), client.size() + 3);
    }
}

TEST_CASE(
    "[Networked][Connect][Hosted] WebRTC offers no probe peer, so a browser "
    "refresh does not force a full ICE handshake per row; discovery is what "
    "the board already answered and a probe would only re-ask it expensively"
) {
    WebRTCTransport transport;
    CHECK_FALSE(transport.can_probe());
    CHECK(transport.make_probe_peer("deadbeefdeadbeefdead").is_null());
}

TEST_CASE(
    "[Networked][Connect][Hosted] the book answers a WebRTC peer class with "
    "the native transport, so a session assigned a WebRTCMultiplayerPeer by "
    "hand elevates onto the plane rather than reporting no owner"
) {
    TransportBook &book = TransportBook::shared();
    CHECK(book.holds(StringName("WebRTCMultiplayerPeer")));

    Transport *made = book.make(StringName("WebRTCMultiplayerPeer"));
    REQUIRE(made != nullptr);
    CHECK(made->peer_class() == StringName("WebRTCMultiplayerPeer"));
    CHECK(made->display_name() == String("WebRTC"));
    CHECK_FALSE(made->can_probe());
    delete made;
}

TEST_CASE(
    "[Networked][Connect][Hosted] an unset signaling namespace takes the "
    "session's app id, so a room code is scoped by the build tag that "
    "already decides who may join and two tags never meet on the rendezvous"
) {
    Ref<netw::NetwMultiplayer> api;
    api.instantiate();
    Ref<netw::NetwSessionConfig> config;
    config.instantiate();
    config->set_app_id(StringName("netw-example-racing"));
    api->session_initialize(config);

    WebRTCTransport transport;
    transport.bind_session(api.ptr());
    WebRTCTransportProbe::apply(transport, Dictionary());

    CHECK(
        WebRTCTransportProbe::space(transport) == String("netw-example-racing")
    );
}

TEST_CASE(
    "[Networked][Connect][Hosted] an authored signaling namespace outranks "
    "the app id, so a game that scopes its own rooms keeps that scope"
) {
    Ref<netw::NetwMultiplayer> api;
    api.instantiate();
    Ref<netw::NetwSessionConfig> config;
    config.instantiate();
    config->set_app_id(StringName("netw-example-racing"));
    api->session_initialize(config);

    Dictionary settings;
    settings["signaling_namespace"] = String("authored");

    WebRTCTransport transport;
    transport.bind_session(api.ptr());
    WebRTCTransportProbe::apply(transport, settings);

    CHECK(WebRTCTransportProbe::space(transport) == String("authored"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a session carrying no app id leaves the "
    "namespace empty, so an unconfigured game keeps the globally unique long "
    "room id rather than a short code nothing scopes"
) {
    Ref<netw::NetwMultiplayer> api;
    api.instantiate();

    WebRTCTransport transport;
    transport.bind_session(api.ptr());
    WebRTCTransportProbe::apply(transport, Dictionary());

    CHECK(WebRTCTransportProbe::space(transport).is_empty());
}

TEST_CASE(
    "[Networked][Connect][Hosted] a relay the host typed outranks a fetched "
    "one, a fetched relay outranks the compiled-in default, and the default "
    "stands when neither exists, so the credential fetch can never overwrite "
    "the one server a human chose"
) {
    netw::connect::TurnCredentials::forget();

    Array authored;
    authored.push_back(server_of(urls_of("turn:typed.example:3478")));
    Array fetched;
    fetched.push_back(server_of(urls_of("turn:fetched.example:3478")));

    WebRTCTransport bare;
    CHECK(
        JSON::stringify(bare.effective_ice_servers())
        == JSON::stringify(WebRTCTransport::default_ice_servers())
    );

    netw::connect::TurnCredentials::adopt(fetched);

    WebRTCTransport served;
    CHECK(
        JSON::stringify(served.effective_ice_servers())
        == JSON::stringify(fetched)
    );

    Dictionary settings;
    settings["ice_servers"] = authored;
    WebRTCTransport chosen;
    WebRTCTransportProbe::apply(chosen, settings);
    CHECK(
        JSON::stringify(chosen.effective_ice_servers())
        == JSON::stringify(authored)
    );

    Dictionary echoed;
    echoed["ice_servers"] = WebRTCTransport::default_ice_servers();
    WebRTCTransport untouched;
    WebRTCTransportProbe::apply(untouched, echoed);
    CHECK(
        JSON::stringify(untouched.effective_ice_servers())
        == JSON::stringify(fetched)
    );

    netw::connect::TurnCredentials::forget();
}

} // namespace TestNetwConnectWebRTCTransport

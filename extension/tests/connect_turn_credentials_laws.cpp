#include "support/netw_test.h"

#include "godot/variant.hpp"
#include "netw/connect/turn_credentials.hpp"

namespace TestNetwConnectTurnCredentials {

using namespace godot;
using netw::connect::TurnCredentials;

PackedByteArray bytes_of(const char *p_text) {
    return String(p_text).to_utf8_buffer();
}

TEST_CASE(
    "[Networked][Connect][Hosted] a credential url splits into the host the "
    "client dials and the path it requests, and the scheme stays on the host "
    "so an https url selects TLS and port 443 without either being named"
) {
    String host;
    String path;

    TurnCredentials::split_url("https://relay.example/ice", host, path);
    CHECK(host == String("https://relay.example"));
    CHECK(path == String("/ice"));

    TurnCredentials::split_url("https://relay.example", host, path);
    CHECK(host == String("https://relay.example"));
    CHECK(path == String("/"));

    TurnCredentials::split_url("https://relay.example/", host, path);
    CHECK(host == String("https://relay.example"));
    CHECK(path == String("/"));

    TurnCredentials::split_url(
        "  http://relay.example:8080/ice?token=1  ",
        host,
        path
    );
    CHECK(host == String("http://relay.example:8080"));
    CHECK(path == String("/ice?token=1"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a credential setting beginning with a slash "
    "is a path on whatever page loaded the build, so one setting serves the "
    "production site, every branch alias and a custom domain, while an "
    "absolute setting is taken as written"
) {
    const String origin = "https://netw-examples.pages.dev";

    CHECK(
        TurnCredentials::resolve_url("/turn", origin)
        == String("https://netw-examples.pages.dev/turn")
    );
    CHECK(
        TurnCredentials::resolve_url("/turn", origin + String("/"))
        == String("https://netw-examples.pages.dev/turn")
    );
    CHECK(
        TurnCredentials::resolve_url(
            "/turn",
            "https://fix-ice.netw-examples.pages.dev"
        )
        == String("https://fix-ice.netw-examples.pages.dev/turn")
    );

    CHECK(
        TurnCredentials::resolve_url("https://relay.example/ice", origin)
        == String("https://relay.example/ice")
    );
    CHECK(
        TurnCredentials::resolve_url("https://relay.example/ice", String())
        == String("https://relay.example/ice")
    );

    CHECK(TurnCredentials::resolve_url("/turn", String()).is_empty());
    CHECK(TurnCredentials::resolve_url(String(), origin).is_empty());
}

TEST_CASE(
    "[Networked][Connect][Hosted] only a 200 carrying a JSON array of ice "
    "server dictionaries yields servers, because an error page and a "
    "truncated body both parse to something and neither may reach the ICE "
    "stack as configuration"
) {
    const Array pair = TurnCredentials::servers_from_body(
        200,
        bytes_of(
            "[{\"urls\":[\"turn:relay.example:3478\"],\"username\":\"u\","
            "\"credential\":\"c\"},{\"urls\":[\"stun:relay.example:3478\"]}]"
        )
    );
    REQUIRE(pair.size() == 2);
    CHECK(Dictionary(pair[0])["username"] == Variant(String("u")));

    CHECK(
        TurnCredentials::servers_from_body(
            403,
            bytes_of("[{\"urls\":[\"turn:relay.example:3478\"]}]")
        )
            .is_empty()
    );
    CHECK(
        TurnCredentials::servers_from_body(
            200,
            bytes_of("{\"error\":\"Access Denied\"}")
        )
            .is_empty()
    );
    CHECK(
        TurnCredentials::servers_from_body(200, bytes_of("not json")).is_empty()
    );
    CHECK(TurnCredentials::servers_from_body(200, bytes_of("[]")).is_empty());
    CHECK(
        TurnCredentials::servers_from_body(
            200,
            bytes_of("[{\"username\":\"u\"}]")
        )
            .is_empty()
    );
}

TEST_CASE(
    "[Networked][Connect][Hosted] the fetch is attempted once per process, so "
    "a second host or join after a refusal spends no further request and the "
    "configured servers stand"
) {
    TurnCredentials::forget();
    CHECK(!TurnCredentials::attempted());
    CHECK(TurnCredentials::cached().is_empty());

    TurnCredentials held;
    CHECK(held.begin(String(), PackedStringArray(), 0) != OK);

    CHECK(TurnCredentials::attempted());
    CHECK(held.phase() == TurnCredentials::PHASE_FAILED);
    CHECK(TurnCredentials::cached().is_empty());

    TurnCredentials::forget();
}

} // namespace TestNetwConnectTurnCredentials

#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/resource_loader.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/endpoint.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/core.hpp"
#include "support/directory_stubs.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/node.hpp>

namespace TestNetwConnectDirectory {

using namespace godot;
using netw::NetwMultiplayer;

namespace {

const char *STUB_PEER_CLASS = "StubLobbyPeer";

struct AnswerLog {
    int calls = 0;
    Ref<MultiplayerPeer> peer;
    int64_t error = 0;
    String detail;
};

AnswerLog &answer() {
    static AnswerLog instance;
    return instance;
}

void on_created(Ref<MultiplayerPeer> p_peer, int64_t p_error, String p_detail) {
    answer().calls++;
    answer().peer = p_peer;
    answer().error = p_error;
    answer().detail = p_detail;
}

struct Bench {
    Ref<NetwMultiplayer> core;
    netw_test::StubDirectory *directory = nullptr;

    Bench() {
        answer() = AnswerLog();
        core.instantiate();
        directory = memnew(netw_test::StubDirectory);
        core->service_register(directory, nullptr);
    }

    ~Bench() {
        if (directory != nullptr) {
            core->service_unregister(directory, nullptr);
            memdelete(directory);
            directory = nullptr;
        }
        if (core.is_valid()) {
            core->embed_dispose();
            core = Ref<NetwMultiplayer>();
        }
        answer() = AnswerLog();
    }

    RID transport() const {
        return core->transport_find(StringName(STUB_PEER_CLASS));
    }

    RID ask(
        NetwMultiplayer::TransportMode p_mode,
        const String &p_address,
        const Dictionary &p_settings
    ) {
        return core->transport_create_peer(
            transport(),
            p_mode,
            p_address,
            p_settings,
            callable_mp_static(&on_created)
        );
    }

    void drive() {
        core->connect_plane().on_poll(0.0);
    }
};

} // namespace

TEST_CASE(
    "[Networked][Connect][Directory] a host request reaches the directory's "
    "own seam carrying the caller's settings, and the peer that directory "
    "delivers answers the request, because a registered directory is the "
    "provider for its peer class rather than a listing beside one"
) {
    Bench bench;
    REQUIRE(bench.directory != nullptr);
    REQUIRE(bench.transport().is_valid());
    REQUIRE(callable_mp_static(&on_created).is_valid());
    Dictionary settings;
    settings["max_players"] = 6;

    const RID ticket
        = bench.ask(NetwMultiplayer::TRANSPORT_MODE_HOST, String(), settings);
    REQUIRE(ticket.is_valid());
    bench.drive();

    NETW_CHECK_EQ(bench.directory->host_calls, 1);
    const Dictionary carried = bench.directory->hosted_with;
    NETW_CHECK_EQ(int64_t(carried.get("max_players", 0)), 6);
    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(answer().error, int64_t(OK));
    CHECK(answer().peer.is_valid());
}

TEST_CASE(
    "[Networked][Connect][Directory] a directory answers a join form its own "
    "client settings, because a provider that publishes host fields and no "
    "client fields leaves a joining player unable to say what a host could"
) {
    Bench bench;
    REQUIRE(bench.transport().is_valid());

    const Dictionary client = bench.core->transport_get_param(
        bench.transport(),
        NetwMultiplayer::TRANSPORT_PARAM_CLIENT_SETTINGS
    );
    const Dictionary host = bench.core->transport_get_param(
        bench.transport(),
        NetwMultiplayer::TRANSPORT_PARAM_HOST_SETTINGS
    );

    CHECK(String(client.get("region", String())) == String("eu"));
    CHECK_FALSE(client.has("max_players"));
    CHECK(host.has("region"));
    NETW_CHECK_EQ(int64_t(host.get("max_players", 0)), int64_t(4));
}

TEST_CASE(
    "[Networked][Connect][Directory] a join request hands the directory the "
    "address the caller named, so a lobby id a browse published and one a "
    "host shared by hand reach the provider the same way"
) {
    Bench bench;
    REQUIRE(bench.directory != nullptr);

    bench.ask(
        NetwMultiplayer::TRANSPORT_MODE_CLIENT,
        String("109775241"),
        Dictionary()
    );
    bench.drive();

    NETW_CHECK_EQ(bench.directory->join_calls, 1);
    NETW_CHECK_EQ(bench.directory->host_calls, 0);
    const bool addressed
        = bench.directory->joined_address == String("109775241");
    CHECK(addressed);
    CHECK(answer().peer.is_valid());
}

TEST_CASE(
    "[Networked][Connect][Directory] a directory that refuses settles the "
    "request with its own error and message, because a provider that cannot "
    "answer must say so rather than leave the caller on a deadline"
) {
    Bench bench;
    REQUIRE(bench.directory != nullptr);
    bench.directory->answering = netw_test::StubDirectory::ANSWER_REFUSAL;
    bench.directory->refusal = ERR_UNAUTHORIZED;

    bench
        .ask(NetwMultiplayer::TRANSPORT_MODE_CLIENT, String("7"), Dictionary());
    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(answer().error, int64_t(ERR_UNAUTHORIZED));
    CHECK_FALSE(answer().peer.is_valid());
    const bool explained = answer().detail.find("refused") >= 0;
    CHECK(explained);
}

TEST_CASE(
    "[Networked][Connect][Directory] browsing reaches the directory's list "
    "seam and the rows it publishes become this session's targets at the "
    "addresses it published, so a listing is joinable without a second lookup"
) {
    Bench bench;
    REQUIRE(bench.directory != nullptr);
    PackedStringArray offered;
    offered.push_back(String("42"));
    offered.push_back(String("43"));
    bench.directory->rows = offered;

    bench.core->endpoint_set_browsing(true);
    bench.core->connect_plane().on_poll(0.1);

    NETW_CHECK_EQ(bench.directory->browse_calls, 1);
    const Array targets = bench.core->endpoint_list();
    PackedStringArray seen;
    for (int64_t at = 0; at < targets.size(); at++) {
        seen.push_back(String(bench.core->endpoint_get_param(
            targets[at],
            NetwMultiplayer::ENDPOINT_PARAM_ADDRESS
        )));
    }
    const bool listed = seen.has(String("42")) && seen.has(String("43"));
    CHECK(listed);
}

class EnetListing : public netw_test::StubDirectory {
public:
    StringName peer_class() override {
        return StringName("ENetMultiplayerPeer");
    }
};

TEST_CASE(
    "[Networked][Connect][Directory] a directory for a class that already has "
    "a creator is still browsed, and its rows join through that creator"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    EnetListing *listing = memnew(EnetListing);
    listing->rows.push_back(String("10.0.0.5:7000"));
    core->service_register(listing, nullptr);

    core->endpoint_set_browsing(true);
    core->connect_plane().on_poll(0.1);

    NETW_CHECK_EQ(listing->browse_calls, 1);
    const Array targets = core->endpoint_list();
    NETW_CHECK_EQ(int(targets.size()), 1);
    if (targets.size() == 1) {
        const RID through = core->endpoint_get_param(
            targets[0],
            NetwMultiplayer::ENDPOINT_PARAM_TRANSPORT
        );
        CHECK(
            through == core->transport_find(StringName("ENetMultiplayerPeer"))
        );
    }

    core->service_unregister(listing, nullptr);
    memdelete(listing);
    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Directory] DF1 a transport's defaults fill only "
    "the settings each side declares, so a host default stays out of the "
    "join form"
) {
    Bench bench;
    REQUIRE(bench.directory != nullptr);
    const Ref<netw::NetwConnection> connection = bench.core->get_connection();
    Dictionary preset;
    preset["region"] = String("us");
    preset["max_players"] = int64_t(16);
    NETW_REQUIRE_EQ(
        int64_t(connection->set_defaults(StringName(STUB_PEER_CLASS), preset)),
        int64_t(OK)
    );

    const Dictionary described
        = connection->transport(StringName(STUB_PEER_CLASS));
    const Dictionary client = described["client_settings"];
    const Dictionary host = described["host_settings"];

    CHECK(String(client.get("region", String())) == String("us"));
    CHECK_FALSE(client.has("max_players"));
    CHECK(String(host.get("region", String())) == String("us"));
    NETW_CHECK_EQ(int64_t(host.get("max_players", 0)), int64_t(16));
}

TEST_CASE(
    "[Networked][Connect][Directory] a directory that leaves the session "
    "takes its rows with it"
) {
    Bench bench;
    REQUIRE(bench.directory != nullptr);
    bench.directory->rows.push_back(String("42"));
    bench.core->endpoint_set_browsing(true);
    bench.core->connect_plane().on_poll(0.1);
    NETW_CHECK_EQ(int(bench.core->endpoint_list().size()), 1);

    bench.core->service_unregister(bench.directory, nullptr);

    NETW_CHECK_EQ(int(bench.core->endpoint_list().size()), 0);
}

struct InviteLog {
    int calls = 0;
    Ref<netw::NetwEndpoint> endpoint;
    String from_name;
};

InviteLog &invites() {
    static InviteLog instance;
    return instance;
}

void on_invited(Ref<netw::NetwEndpoint> p_endpoint, String p_from_name) {
    invites().calls++;
    invites().endpoint = p_endpoint;
    invites().from_name = p_from_name;
}

TEST_CASE(
    "[Networked][Connect][Directory] INV1 an invite becomes an endpoint "
    "carrying the directory as its source and the sender's name, and joining "
    "it reaches the directory's join seam at the invited address"
) {
    Bench bench;
    REQUIRE(bench.directory != nullptr);
    invites() = InviteLog();
    bench.directory->answering = netw_test::StubDirectory::ANSWER_NOTHING;
    const Ref<netw::NetwConnection> connection = bench.core->get_connection();
    connection->connect(StringName("invited"), callable_mp_static(&on_invited));

    bench.directory->invite(String("77"), String("Ada"));

    NETW_CHECK_EQ(invites().calls, 1);
    REQUIRE(invites().endpoint.is_valid());
    CHECK(invites().from_name == String("Ada"));
    CHECK(invites().endpoint->get_address() == String("77"));
    CHECK(invites().endpoint->get_source() == String("Stub Lobbies"));

    connection->join(invites().endpoint, Dictionary());
    bench.drive();

    NETW_CHECK_EQ(bench.directory->join_calls, 1);
    CHECK(bench.directory->joined_address == String("77"));
    invites() = InviteLog();
}

TEST_CASE(
    "[Networked][Connect][Directory] INV2 an invited endpoint stays listed "
    "through a listing that leaves it out, and still joins at its address"
) {
    Bench bench;
    REQUIRE(bench.directory != nullptr);
    invites() = InviteLog();
    bench.directory->answering = netw_test::StubDirectory::ANSWER_NOTHING;
    const Ref<netw::NetwConnection> connection = bench.core->get_connection();
    connection->connect(StringName("invited"), callable_mp_static(&on_invited));
    bench.directory->invite(String("77"), String("Ada"));
    REQUIRE(invites().endpoint.is_valid());

    bench.directory->rows.push_back(String("42"));
    bench.core->endpoint_set_browsing(true);
    bench.core->connect_plane().on_poll(0.1);

    NETW_CHECK_EQ(bench.directory->browse_calls, 1);
    NETW_CHECK_EQ(int(bench.core->endpoint_list().size()), 2);
    CHECK(invites().endpoint->get_is_listed());
    CHECK(
        connection->find(StringName(STUB_PEER_CLASS), String("77"))
        == invites().endpoint
    );
    connection->join(invites().endpoint, Dictionary());
    bench.drive();

    NETW_CHECK_EQ(bench.directory->join_calls, 1);
    CHECK(bench.directory->joined_address == String("77"));

    NETW_CHECK_EQ(int(connection->remove(invites().endpoint)), int(OK));
    CHECK_FALSE(invites().endpoint->get_is_listed());
    invites() = InviteLog();
}

TEST_CASE(
    "[Networked][Connect][Directory] invites from a social service need no "
    "directory and reuse the endpoint at the same address"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<netw::NetwConnection> connection = core->get_connection();
    invites() = InviteLog();
    connection->connect(StringName("invited"), callable_mp_static(&on_invited));
    const StringName transport("ENetMultiplayerPeer");
    const Ref<netw::NetwEndpoint> first = connection->invite(
        transport, String("127.0.0.1:7000"), String("Ada")
    );
    REQUIRE(first.is_valid());
    NETW_CHECK_EQ(invites().calls, 1);
    CHECK(bool(invites().endpoint == first));
    CHECK(bool(invites().from_name == String("Ada")));
    CHECK(bool(first->get_address() == String("127.0.0.1:7000")));
    CHECK(first->get_is_listed());
    CHECK(bool(connection->invite(transport, first->get_address(), String())
        == first));
    NETW_CHECK_EQ(invites().calls, 2);
    NETW_CHECK_EQ(int(connection->endpoints().size()), 1);
    CHECK(connection->invite(transport, String(), String()).is_null());
    CHECK(connection->invite(StringName("Missing"), String("1"), String())
        .is_null());
    NETW_CHECK_EQ(invites().calls, 2);
    invites() = InviteLog();
    core->embed_dispose();
    core.unref();
    CHECK(connection->invite(transport, String("1"), String()).is_null());
}

} // namespace TestNetwConnectDirectory

#endif

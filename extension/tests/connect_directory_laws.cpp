#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/resource_loader.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
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

    RID ask(int p_mode, const String &p_address, const Dictionary &p_settings) {
        return core->transport_create_peer(
            transport(),
            p_mode,
            p_address,
            p_settings,
            callable_mp_static(&on_created),
            Callable()
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
    "[Networked][Connect][Directory] a refresh reaches the directory's browse "
    "seam and the rows it publishes become this session's targets at the "
    "addresses it published, so a listing is joinable without a second lookup"
) {
    Bench bench;
    REQUIRE(bench.directory != nullptr);
    PackedStringArray offered;
    offered.push_back(String("42"));
    offered.push_back(String("43"));
    bench.directory->rows = offered;

    bench.core->endpoint_refresh();

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

} // namespace TestNetwConnectDirectory

#endif

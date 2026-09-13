#include "support/netw_recorder.h"
#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/os.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/connect_handle.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/netw_transport.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/api/server_info.hpp"
#include "netw/api/session_config.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/transport.hpp"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/class_db_singleton.hpp>

namespace TestNetwConnectPlane {

using namespace godot;
using netw::NetwConnectHandle;
using netw::NetwLinkConditions;
using netw::NetwMultiplayer;
using netw::NetwServerInfo;
using netw::NetwTransport;
using netw_test::CallLog;

constexpr const char *SHAPING_VARIABLE = "NETW_SHAPING";

Ref<NetwMultiplayer> lone_session() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    return core;
}

RID local_target(
    const Ref<NetwMultiplayer> &p_core,
    const char *p_address = ""
) {
    const RID transport
        = p_core->transport_find(StringName("LocalMultiplayerPeer"));
    return p_core->endpoint_add(transport, String(p_address), String());
}

namespace {

const char *DIRECTORY_PEER_CLASS = "SourcedDirectoryPeer";

class SourcedTransport : public netw::connect::Transport {
public:
    StringName peer_class() const override {
        return StringName(DIRECTORY_PEER_CLASS);
    }
    String display_name() const override {
        return String("the registration");
    }
    void make_peer(int, const String &, const Dictionary &) override {
        fail(ERR_UNCONFIGURED, String("sourced"));
    }
};

netw::connect::Transport *make_sourced() {
    return new SourcedTransport();
}

class SourcedDirectory : public netw::LobbyDirectory {
public:
    StringName peer_class() override {
        return StringName("SourcedDirectoryPeer");
    }
    String display_name() override {
        return String("the directory");
    }
    void join_lobby(const String &) override {
    }
};

netw::LobbyDirectory *directory_stub() {
    return memnew(SourcedDirectory);
}

} // namespace

TEST_CASE(
    "[Networked][Connect] the connection is one handle per session, "
    "minted on the first read and the same object thereafter, because a game "
    "holds it across an await and two of them would hold two states"
) {
    const Ref<NetwMultiplayer> core = lone_session();

    const Ref<NetwConnectHandle> first = core->get_connection();
    const Ref<NetwConnectHandle> second = core->get_connection();

    REQUIRE(first.is_valid());
    CHECK(first == second);

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] an endpoint names its peer class through "
    "the class itself, so no saved row and no call site spells a transport "
    "as a string a rename would silently break"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<NetwConnectHandle> handle = core->get_connection();

    Ref<netw::LocalMultiplayerPeer> shape;
    shape.instantiate();
    const bool added
        = handle->endpoint_add(shape, String("somewhere"), String("Friday"));

    CHECK(added);
    const Dictionary snapshot = handle->endpoint(shape, String("somewhere"));
    CHECK_FALSE(snapshot.is_empty());
    CHECK(
        StringName(snapshot["peer_class"]) == StringName("LocalMultiplayerPeer")
    );
    CHECK(String(snapshot["address"]) == String("somewhere"));
    CHECK(String(snapshot["display_name"]) == String("Friday"));

    SUBCASE("and the same pair answers the same handle, spelled not minted") {
        const RID transport
            = core->transport_find(StringName("LocalMultiplayerPeer"));
        const RID first = core->endpoint_find(transport, String("somewhere"));
        const RID again
            = core->endpoint_add(transport, String("somewhere"), String());

        CHECK(again == first);
    }

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] link conditions leave the peer alone until "
    "a game asks for impairment, so a shipped build carries no wrapper it "
    "did not author"
) {
    Ref<NetwLinkConditions> conditions;
    conditions.instantiate();

    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();

    CHECK(conditions->wrap_peer(peer) == Ref<MultiplayerPeer>(peer));
    CHECK(conditions->wrap_peer(Ref<MultiplayerPeer>()).is_null());

    SUBCASE("and the authored values are milliseconds and a percent") {
        conditions->set_one_way_delay_min(20.0);
        conditions->set_one_way_delay_max(80.0);
        conditions->set_lag_packet_loss_percent(2.5);

        NETW_CHECK_CLOSE(conditions->get_one_way_delay_min(), 20.0, 0.001);
        NETW_CHECK_CLOSE(conditions->get_one_way_delay_max(), 80.0, 0.001);
        NETW_CHECK_CLOSE(conditions->get_lag_packet_loss_percent(), 2.5, 0.001);
    }
}

TEST_CASE(
    "[Networked][Connect] a session polls the peer it shaped and "
    "answers the peer it was handed, so an authored impairment reaches the "
    "wire while the game keeps the reference it assigned"
) {
    REQUIRE_MESSAGE(
        ClassDB::class_exists(StringName("LaggyMultiplayerPeer")),
        "shaping is only observable where the impairment peer is installed"
    );

    const Ref<netw::LocalLoopbackSession> link
        = netw::LocalLoopbackSession::get_shared_session();
    link->reset();
    const Ref<netw::LocalMultiplayerPeer> server = link->get_server_peer();
    const Ref<netw::LocalMultiplayerPeer> client = link->create_client_peer();

    Ref<NetwLinkConditions> conditions;
    conditions.instantiate();
    conditions->set_simulate_lag(true);
    conditions->set_one_way_delay_min(0.0);
    conditions->set_one_way_delay_max(0.0);
    conditions->set_lag_packet_loss_percent(100.0);

    const Ref<NetwMultiplayer> core = lone_session();
    Ref<netw::NetwSessionConfig> shaping;
    shaping.instantiate();
    shaping->set_link_conditions(conditions);
    core->session_initialize(shaping);
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(client);

    CHECK(
        core->NETW_API_VIRTUAL(get_multiplayer_peer)()
        == Ref<MultiplayerPeer>(client)
    );

    const Ref<MultiplayerPeer> polled
        = core->session_get_inner()->get_multiplayer_peer();
    REQUIRE(polled.is_valid());
    CHECK(polled != Ref<MultiplayerPeer>(client));

    PackedByteArray payload;
    payload.push_back(7);
    polled->set_target_peer(1);
    NETW_CHECK_EQ(int(polled->put_packet(payload)), int(OK));
    for (int round = 0; round < 8; ++round) {
        polled->poll();
        link->poll();
    }
    NETW_CHECK_EQ(server->get_available_packet_count(), 0);

    SUBCASE("and the unshaped twin on the same link delivers that packet") {
        const Ref<netw::LocalMultiplayerPeer> plain
            = link->create_client_peer();
        const Ref<NetwMultiplayer> bare = lone_session();
        bare->NETW_API_VIRTUAL(set_multiplayer_peer)(plain);

        const Ref<MultiplayerPeer> unshaped
            = bare->session_get_inner()->get_multiplayer_peer();
        CHECK(unshaped == Ref<MultiplayerPeer>(plain));

        unshaped->set_target_peer(1);
        NETW_CHECK_EQ(int(unshaped->put_packet(payload)), int(OK));
        for (int round = 0; round < 8; ++round) {
            unshaped->poll();
            link->poll();
        }
        CHECK(server->get_available_packet_count() > 0);

        bare->embed_dispose();
    }

    core->embed_dispose();
    link->reset();
}

TEST_CASE(
    "[Networked][Connect] a transport that overrides nothing still "
    "answers every seam, because a subclass that only builds a peer must not "
    "have to restate what a generic transport already is"
) {
    Ref<NetwTransport> stock;
    stock.instantiate();

    CHECK(stock->peer_class() == StringName());
    CHECK(stock->display_name() == String("Generic"));
    CHECK(stock->is_available());
    CHECK(stock->can_host_here());
    CHECK_FALSE(stock->can_probe());
    CHECK_FALSE(stock->can_browse());
    NETW_CHECK_CLOSE(stock->timeout_hint(), 5.0, 0.001);
    CHECK(stock->join_address().is_empty());
    CHECK(stock->diagnostics(1).is_empty());
    CHECK(stock->make_probe_peer(String()).is_null());

    CHECK(stock->address_label() == String("Address"));
    CHECK(stock->address_placeholder().is_empty());
    CHECK(stock->address_help().is_empty());
    CHECK_FALSE(stock->accepts_empty_address());
    CHECK(stock->host_settings().is_empty());
    CHECK(stock->client_settings().is_empty());

    SUBCASE("and a transport that answers no probe peer rejects the probe") {
        Ref<netw::NetwPromise> answered;
        answered.instantiate();
        const RID ticket
            = netw::connect::mint_creation(netw::connect::Creation());
        stock->arm(ticket, answered);
        stock->probe(ticket, String("anywhere"));
        netw::connect::free_creation(ticket);

        CHECK(answered->get_is_failed());
        NETW_CHECK_EQ(answered->get_code(), int(ERR_UNAVAILABLE));
    }
}

TEST_CASE(
    "[Networked][Connect] the advert a host declared is what a probe "
    "reads back, so a lobby card and a probe reply say what the host said "
    "rather than two different things"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    Ref<NetwServerInfo> declared;
    declared.instantiate();
    declared->set_max_players(8);
    declared->set_motd(String("Friday"));
    declared->set_visibility(NetwServerInfo::VISIBILITY_PRIVATE);
    Ref<netw::NetwSessionConfig> advertising;
    advertising.instantiate();
    advertising->set_server_info(declared);
    core->session_initialize(advertising);

    const Ref<NetwServerInfo> read = NetwServerInfo::from_session(core.ptr());

    REQUIRE(read.is_valid());
    NETW_CHECK_EQ(read->get_max_players(), int64_t(8));
    CHECK(read->get_motd() == String("Friday"));
    NETW_CHECK_EQ(
        read->get_visibility(),
        int64_t(NetwServerInfo::VISIBILITY_PRIVATE)
    );

    SUBCASE(
        "and the live fields overlay the declaration rather than the "
        "other way round, so a host that never touches the config still "
        "answers a probe honestly"
    ) {
        CHECK(read->get_is_local_listener());
        CHECK(read != declared);
        NETW_CHECK_EQ(declared->get_players(), int64_t(0));
    }

    SUBCASE("and it survives the wire the probe reply travels on") {
        const Ref<NetwServerInfo> wired
            = NetwServerInfo::from_payload(NetwServerInfo::to_payload(read));

        REQUIRE(wired.is_valid());
        CHECK(wired->get_motd() == String("Friday"));
        NETW_CHECK_EQ(wired->get_visibility(), int64_t(2));
        NETW_CHECK_EQ(wired->get_max_players(), int64_t(8));
    }

    SUBCASE("and a latency nobody measured reads as absent, not as zero") {
        Ref<NetwServerInfo> fresh;
        fresh.instantiate();

        NETW_CHECK_EQ(fresh->get_latency_ms(), int64_t(-1));
    }

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] the handle answers empty rather than "
    "crashing for every reader before anything has been brought up, because "
    "a browser binds and renders before a person has chosen anything"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<NetwConnectHandle> handle = core->get_connection();

    CHECK(handle->get_join_address().is_empty());
    CHECK(handle->diagnostics(1).is_empty());
    CHECK(handle->endpoints().is_empty());
    CHECK(handle->endpoint(StringName("ENetMultiplayerPeer"), String("nowhere"))
              .is_empty());
    NETW_CHECK_GT(int(handle->transports().size()), 0);

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a form renders a transport it was never "
    "told about, because a transport handle publishes the settings keys and "
    "the address hint the fields are drawn from"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<NetwConnectHandle> handle = core->get_connection();

    const Array known = handle->transports();
    REQUIRE(known.size() > 0);

    const Dictionary enet
        = handle->transport(StringName("ENetMultiplayerPeer"));
    CHECK_FALSE(enet.is_empty());
    CHECK(StringName(enet["peer_class"]) == StringName("ENetMultiplayerPeer"));
    CHECK_FALSE(String(enet["display_name"]).is_empty());
    CHECK_FALSE(String(enet["address_label"]).is_empty());
    CHECK(Dictionary(enet["host_settings"]).has("port"));
    CHECK(Dictionary(enet["client_settings"]).has("port"));
    CHECK_FALSE(Dictionary(enet["client_settings"]).has("max_players"));
    NETW_CHECK_EQ(
        int64_t(enet["capabilities"]) & NetwMultiplayer::TRANSPORT_CAN_PROBE,
        int64_t(NetwMultiplayer::TRANSPORT_CAN_PROBE)
    );

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a transport handle answers for the source "
    "it was minted from, so a directory's handle keeps reaching that "
    "directory even after a project-wide registration claims the same peer "
    "class and would win a lookup by name"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    netw::LobbyDirectory *directory = directory_stub();
    REQUIRE(directory != nullptr);
    core->service_register(directory, nullptr);

    const RID watched = core->transport_find(StringName(DIRECTORY_PEER_CLASS));
    REQUIRE(watched.is_valid());
    CHECK(
        core->transport_get_param(
            watched,
            NetwMultiplayer::TRANSPORT_PARAM_DISPLAY_NAME
        )
        == String("the directory")
    );

    netw::connect::TransportBook::shared().install_native(
        StringName(DIRECTORY_PEER_CLASS),
        make_sourced,
        String("the registration")
    );

    CHECK(
        core->transport_get_param(
            watched,
            NetwMultiplayer::TRANSPORT_PARAM_DISPLAY_NAME
        )
        == String("the directory")
    );
    CHECK(core->transport_find(StringName(DIRECTORY_PEER_CLASS)) != watched);

    netw::connect::TransportBook::shared().forget(
        StringName(DIRECTORY_PEER_CLASS)
    );
    core->service_unregister(directory, nullptr);
    memdelete(directory);
    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a registration keeps its handle when its "
    "provider is replaced, because the handle names the registration rather "
    "than the factory standing behind it, and a form already holding one "
    "reads the new provider through it"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    netw::connect::TransportBook::shared().install_native(
        StringName(DIRECTORY_PEER_CLASS),
        make_sourced,
        String("the registration")
    );

    const RID first = core->transport_find(StringName(DIRECTORY_PEER_CLASS));
    REQUIRE(first.is_valid());

    netw::connect::TransportBook::shared().install_native(
        StringName(DIRECTORY_PEER_CLASS),
        make_sourced,
        String("the replacement")
    );

    NETW_CHECK_EQ(
        int(core->transport_find(StringName(DIRECTORY_PEER_CLASS)) == first),
        1
    );
    CHECK(
        core->transport_get_param(
            first,
            NetwMultiplayer::TRANSPORT_PARAM_PEER_CLASS
        )
        == StringName(DIRECTORY_PEER_CLASS)
    );

    netw::connect::TransportBook::shared().forget(
        StringName(DIRECTORY_PEER_CLASS)
    );
    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Session] a submitted join owns its arguments, so a "
    "caller that reuses its array after the verb cannot rewrite what is "
    "already in flight"
) {
    const Ref<NetwMultiplayer> core = lone_session();

    Array mine;
    mine.push_back(StringName("north"));
    core->session_set_prepared_join(StringName("ana"), mine);
    mine.push_back(StringName("south"));

    REQUIRE(core->session_prepared_join().has_value());
    NETW_CHECK_EQ(core->session_prepared_join()->arg_values.size(), 1);

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a transport handle is minted once and stays "
    "the same handle, and a name no transport answers to mints nothing"
) {
    const Ref<NetwMultiplayer> core = lone_session();

    const RID first = core->transport_find(StringName("ENetMultiplayerPeer"));
    const RID again = core->transport_find(StringName("ENetMultiplayerPeer"));
    CHECK(first.is_valid());
    CHECK(first == again);

    const RID absent = core->transport_find(StringName("NoSuchPeer"));
    CHECK_FALSE(absent.is_valid());
    CHECK(
        core->transport_get_param(
                absent,
                NetwMultiplayer::TRANSPORT_PARAM_DISPLAY_NAME
        )
            .get_type()
        == Variant::NIL
    );

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a row added to the list is announced and "
    "readable, and one removed is announced too, so a browser never polls "
    "for what changed"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<NetwConnectHandle> handle = core->get_connection();
    CallLog announced;
    handle->connect(StringName("endpoint_added"), announced.callable("added"));
    handle->connect(
        StringName("endpoint_removed"),
        announced.callable("removed")
    );

    const RID row = local_target(core, "first");
    const StringName peer_class = StringName("LocalMultiplayerPeer");
    const String address = String("first");

    NETW_CHECK_EQ(announced.count("added"), 1);
    NETW_CHECK_EQ(int(handle->endpoints().size()), 1);
    const Dictionary snapshot = handle->endpoint(peer_class, address);
    CHECK(bool(snapshot["is_caller_added"]));

    SUBCASE("and adding the same target twice announces once") {
        const RID again = local_target(core, "first");

        CHECK(again == row);
        NETW_CHECK_EQ(announced.count("added"), 1);
        NETW_CHECK_EQ(int(handle->endpoints().size()), 1);
    }

    SUBCASE("and removing it announces and empties the list") {
        handle->endpoint_remove(peer_class, address);

        NETW_CHECK_EQ(announced.count("removed"), 1);
        CHECK(handle->endpoints().is_empty());
    }

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a host arms the session's auth callback "
    "before it takes the peer, because an unarmed server admits a peer with "
    "no handshake and then rejects the hello that peer sends"
) {
    const Ref<NetwMultiplayer> core = lone_session();

    Ref<netw::LocalMultiplayerPeer> server;
    server.instantiate();
    NETW_CHECK_EQ(int(server->create_server()), int(OK));

    REQUIRE(core->session_get_inner().is_valid());
    CHECK_FALSE(core->session_get_inner()->get_auth_callback().is_valid());

    core->NETW_API_VIRTUAL(set_multiplayer_peer)(server);

    CHECK(core->session_get_inner()->get_auth_callback().is_valid());

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a loopback client polled before a session "
    "adopts it holds its link announcement, because create_client and join "
    "are two awaited steps and a peer_connected emitted between them would "
    "reach nobody and never come again"
) {
    Ref<netw::LocalLoopbackSession> bus;
    bus.instantiate();
    bus->get_server_peer();
    const Ref<netw::LocalMultiplayerPeer> client = bus->create_client_peer();

    bus->poll();
    bus->poll();

    NETW_CHECK_EQ(
        client->get_connection_status(),
        MultiplayerPeer::CONNECTION_CONNECTED
    );

    netw_test::Recorder linked(client.ptr(), {StringName("peer_connected")});
    bus->poll();

    REQUIRE(linked.count("peer_connected") == 1);
    CHECK(bool(linked.args("peer_connected")[0] == Variant(1)));

    bus->reset();
}

TEST_CASE(
    "[Networked][Connect] a host reaches ONLINE at the assignment "
    "and answers back the very peer it was handed, because the assignment is "
    "the verb and a game that never learned connect_host loses nothing"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    Ref<netw::NetwSessionConfig> dedicated;
    dedicated.instantiate();
    dedicated->set_desired_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    core->session_initialize(dedicated);

    Ref<netw::LocalMultiplayerPeer> server;
    server.instantiate();
    NETW_CHECK_EQ(int(server->create_server()), int(OK));

    core->NETW_API_VIRTUAL(set_multiplayer_peer)(server);

    NETW_CHECK_EQ(
        int(core->session_get_state()),
        int(NetwMultiplayer::SESSION_STATE_ONLINE)
    );
    NETW_CHECK_EQ(
        int(core->session_get_role()),
        int(NetwMultiplayer::ROLE_DEDICATED_SERVER)
    );
    CHECK(
        core->NETW_API_VIRTUAL(get_multiplayer_peer)()
        == Ref<MultiplayerPeer>(server)
    );
    CHECK(core->participant_local().is_null());
    CHECK(core->session_get_inner()->get_auth_callback().is_valid());

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a transport's listing arrives as rows the "
    "session announces, so a browser draws a lobby service it was never "
    "told about and redraws only what changed"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    CallLog announced;
    core->connect(StringName("endpoint_added"), announced.callable("added"));
    core->connect(
        StringName("endpoint_removed"),
        announced.callable("removed")
    );

    PackedStringArray addresses;
    addresses.push_back(String("lobby-a"));
    addresses.push_back(String("lobby-b"));
    PackedStringArray names;
    names.push_back(String("Lobby A"));
    names.push_back(String("Lobby B"));
    core->discovery_publish(
        StringName("LocalMultiplayerPeer"),
        addresses,
        names,
        Array()
    );

    const Array drawn = core->endpoint_list();
    NETW_CHECK_EQ(int(drawn.size()), 2);
    NETW_CHECK_EQ(announced.count("added"), 2);
    const RID first_row = drawn[0];
    CHECK(
        core->endpoint_get_param(
            first_row,
            NetwMultiplayer::ENDPOINT_PARAM_DISPLAY_NAME
        )
        == String("Lobby A")
    );
    CHECK_FALSE(
        (int64_t(core->endpoint_get_state(
             first_row,
             NetwMultiplayer::ENDPOINT_STATE_FLAGS
         ))
         & NetwMultiplayer::ENDPOINT_FLAG_CALLER)
        != 0
    );

    SUBCASE(
        "and a listing that lost a lobby announces ONLY the row that "
        "left, because a republish is a diff over standing handles "
        "rather than a rebuild of the whole listing"
    ) {
        PackedStringArray fewer;
        fewer.push_back(String("lobby-b"));
        core->discovery_publish(
            StringName("LocalMultiplayerPeer"),
            fewer,
            PackedStringArray(),
            Array()
        );

        NETW_CHECK_EQ(int(core->endpoint_list().size()), 1);
        NETW_CHECK_EQ(announced.count("removed"), 1);
    }

    core->embed_dispose();
}

struct ShapingEnvironment {
    String held;
    bool was_held = false;

    explicit ShapingEnvironment(const String &p_value) {
        OS *os = OS::get_singleton();
        was_held = os->has_environment(SHAPING_VARIABLE);
        held = os->get_environment(SHAPING_VARIABLE);
        os->set_environment(SHAPING_VARIABLE, p_value);
    }

    ~ShapingEnvironment() {
        OS *os = OS::get_singleton();
        if (was_held) {
            os->set_environment(SHAPING_VARIABLE, held);
        } else {
            os->unset_environment(SHAPING_VARIABLE);
        }
    }
};

TEST_CASE(
    "[Networked][Connect] a build states whether it may impair its own link, "
    "and only a word it recognises overrides that, so a typo cannot slow a "
    "shipped player down"
) {
    SUBCASE("an off word refuses the impairment a debug build would allow") {
        const ShapingEnvironment shaping("off");
        CHECK_FALSE(netw::link_shaping_allowed());
    }

    SUBCASE("an on word allows it") {
        const ShapingEnvironment shaping("on");
        CHECK(netw::link_shaping_allowed());
    }

    SUBCASE("and neither word is no override at all") {
        const ShapingEnvironment shaping("offf");
        NETW_CHECK_EQ(
            netw::link_shaping_allowed(),
            OS::get_singleton()->has_feature("debug")
        );
    }
}

TEST_CASE(
    "[Networked][Connect] a build that may not impair its link installs the "
    "peer it was handed, so an authored impairment that ships costs the "
    "player who runs it nothing"
) {
    REQUIRE_MESSAGE(
        ClassDB::class_exists(StringName("LaggyMultiplayerPeer")),
        "the refusal is only worth asserting where shaping is available"
    );
    const ShapingEnvironment shaping("off");

    const Ref<netw::LocalLoopbackSession> link
        = netw::LocalLoopbackSession::get_shared_session();
    link->reset();
    const Ref<netw::LocalMultiplayerPeer> server = link->get_server_peer();
    const Ref<netw::LocalMultiplayerPeer> client = link->create_client_peer();

    Ref<NetwLinkConditions> conditions;
    conditions.instantiate();
    conditions->set_simulate_lag(true);
    conditions->set_lag_packet_loss_percent(100.0);

    CHECK(conditions->wrap_peer(client) == Ref<MultiplayerPeer>(client));

    const Ref<NetwMultiplayer> core = lone_session();
    core->session_get_config()->set_link_conditions(conditions);
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(client);

    const Ref<MultiplayerPeer> polled
        = core->session_get_inner()->get_multiplayer_peer();
    CHECK(polled == Ref<MultiplayerPeer>(client));

    PackedByteArray payload;
    payload.push_back(11);
    polled->set_target_peer(1);
    NETW_CHECK_EQ(int(polled->put_packet(payload)), int(OK));
    for (int round = 0; round < 8; ++round) {
        polled->poll();
        link->poll();
    }
    CHECK(server->get_available_packet_count() > 0);

    core->embed_dispose();
    link->reset();
}

TEST_CASE(
    "[Networked][Connect] the server info a running session is given "
    "is the record its probe reply is built from, because a player names "
    "their server long after configuration settled and the config snapshot "
    "that reads the name back cannot be written through"
) {
    const Ref<NetwMultiplayer> core = lone_session();

    Ref<NetwServerInfo> named;
    named.instantiate();
    named->set_motd("Ana's game");
    named->set_max_players(8);

    SUBCASE("the snapshot refuses the write that looks like it works") {
        core->session_get_config()->set_server_info(named);
        CHECK(core->session_get_config()->get_server_info().is_null());
        CHECK(NetwServerInfo::from_session(core.ptr())->get_motd().is_empty());
    }

    SUBCASE("the verb is what a probe answers from") {
        core->session_set_server_info(named);

        const Ref<NetwServerInfo> answered
            = NetwServerInfo::from_session(core.ptr());
        CHECK(answered->get_motd() == String("Ana's game"));
        NETW_CHECK_EQ(answered->get_max_players(), 8);

        bool replied = false;
        const Ref<NetwServerInfo> probed
            = NetwServerInfo::from_payload(core->probe_reply_payload(replied));
        CHECK(replied);
        REQUIRE(probed.is_valid());
        CHECK(probed->get_motd() == String("Ana's game"));
        NETW_CHECK_EQ(probed->get_max_players(), 8);
    }

    SUBCASE("what it took is a copy, so the caller's record stays its own") {
        core->session_set_server_info(named);
        named->set_motd("renamed after the fact");
        CHECK(
            NetwServerInfo::from_session(core.ptr())->get_motd()
            == String("Ana's game")
        );
    }

    SUBCASE("clearing it leaves the session answering only what it knows") {
        core->session_set_server_info(named);
        core->session_set_server_info(Ref<NetwServerInfo>());
        CHECK(NetwServerInfo::from_session(core.ptr())->get_motd().is_empty());
    }

    core->embed_dispose();
}

} // namespace TestNetwConnectPlane

#endif

#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/variant.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/endpoint.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/connect/transport.hpp"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwConnectEndpoint {

using namespace godot;
using netw::NetwConnection;
using netw::NetwEndpoint;
using netw::NetwMultiplayer;
using netw_test::CallLog;

namespace {

const char *LOCAL_PEER_CLASS = "LocalMultiplayerPeer";
const char *TWIN_PEER_CLASS = "TwinListingPeer";

struct RemovalSeen {
    int calls = 0;
    Ref<NetwEndpoint> endpoint;
    bool listed = true;
};

RemovalSeen &removal() {
    static RemovalSeen seen;
    return seen;
}

void on_removed(Ref<NetwEndpoint> p_endpoint) {
    removal().calls++;
    removal().endpoint = p_endpoint;
    removal().listed = p_endpoint.is_valid() && p_endpoint->get_is_listed();
}

class TwinTransport : public netw::connect::Transport {
public:
    StringName peer_class() const override {
        return StringName(TWIN_PEER_CLASS);
    }
    String display_name() const override {
        return String("the registration");
    }
    void make_peer(int, const String &, const Dictionary &) override {
        fail(ERR_UNCONFIGURED, String("twin"));
    }
};

netw::connect::Transport *make_twin() {
    return new TwinTransport();
}

class TwinDirectory : public netw::LobbyDirectory {
public:
    StringName peer_class() override {
        return StringName(TWIN_PEER_CLASS);
    }
    String display_name() override {
        return String("the directory");
    }
    void join_lobby(const String &) override {
    }
};

struct StatusRow {
    const char *claim;
    int64_t flags;
    int64_t error;
    NetwEndpoint::Status expected;
};

const int64_t AVAILABLE = NetwMultiplayer::ENDPOINT_FLAG_AVAILABLE;
const int64_t ANSWERED = NetwMultiplayer::ENDPOINT_FLAG_AVAILABLE
                         | NetwMultiplayer::ENDPOINT_FLAG_OBSERVED;

const StatusRow STATUS_ROWS[] = {
    {"a transport this platform lacks",
     NetwMultiplayer::ENDPOINT_FLAG_OBSERVED,
     OK,
     NetwEndpoint::STATUS_UNSUPPORTED},
    {"no answer yet", AVAILABLE, FAILED, NetwEndpoint::STATUS_UNKNOWN},
    {"an answer", ANSWERED, OK, NetwEndpoint::STATUS_ONLINE},
    {"a full server", ANSWERED, ERR_BUSY, NetwEndpoint::STATUS_FULL},
    {"another game or build",
     ANSWERED,
     ERR_UNAUTHORIZED,
     NetwEndpoint::STATUS_INCOMPATIBLE},
    {"a server that does not probe",
     ANSWERED,
     ERR_UNAVAILABLE,
     NetwEndpoint::STATUS_UNKNOWN},
    {"silence", ANSWERED, ERR_TIMEOUT, NetwEndpoint::STATUS_UNREACHABLE},
    {"a refusal",
     ANSWERED,
     ERR_CANT_CONNECT,
     NetwEndpoint::STATUS_UNREACHABLE},
};

} // namespace

TEST_CASE(
    "[Networked][Connect] one row is one endpoint object across every update, "
    "so a game can key a Dictionary by it and connect to it once"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwConnection> handle = core->get_connection();
    CallLog announced;
    handle->connect(StringName("endpoint_added"), announced.callable("added"));

    const Ref<NetwEndpoint> added = handle->add(
        StringName(LOCAL_PEER_CLASS),
        String("first"),
        String("Before")
    );
    REQUIRE(added.is_valid());
    CallLog changed;
    added->connect(StringName("changed"), changed.callable("changed"));

    core->endpoint_set_param(
        added->get_row(),
        NetwMultiplayer::ENDPOINT_PARAM_DISPLAY_NAME,
        String("After")
    );

    NETW_CHECK_EQ(changed.count("changed"), 1);
    const Ref<NetwEndpoint> announced_one = announced.args("added")[0];
    const bool announced_same = announced_one == added;
    const bool found_same
        = handle->find(StringName(LOCAL_PEER_CLASS), String("first")) == added;
    const Ref<NetwEndpoint> listed_one = handle->endpoints()[0];
    const bool listed_same = listed_one == added;
    const bool renamed = added->get_display_name() == String("After");
    CHECK(announced_same);
    CHECK(found_same);
    CHECK(listed_same);
    CHECK(renamed);

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a removed endpoint is already unlisted when "
    "endpoint_removed carries it, and it is the object the game held"
) {
    removal() = RemovalSeen();
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwConnection> handle = core->get_connection();
    handle->connect(
        StringName("endpoint_removed"),
        callable_mp_static(&on_removed)
    );

    const Ref<NetwEndpoint> held
        = handle->add(StringName(LOCAL_PEER_CLASS), String("gone"), String());
    REQUIRE(held.is_valid());
    CHECK(held->get_is_listed());

    NETW_CHECK_EQ(int(handle->remove(held)), int(OK));

    NETW_CHECK_EQ(removal().calls, 1);
    const bool carried_same = removal().endpoint == held;
    CHECK(carried_same);
    CHECK_FALSE(removal().listed);
    CHECK_FALSE(held->get_is_listed());
    const bool kept_address = held->get_address() == String("gone");
    CHECK(kept_address);
    CHECK(held->get_info().is_valid());
    NETW_CHECK_EQ(int(handle->remove(held)), int(ERR_DOES_NOT_EXIST));

    removal() = RemovalSeen();
    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] a listed row and a game's own row for one peer class "
    "and address are two endpoint objects, because identity follows the row"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwConnection> handle = core->get_connection();
    TwinDirectory *directory = memnew(TwinDirectory);
    core->service_register(directory, nullptr);

    PackedStringArray addresses;
    addresses.push_back(String("shared"));
    core->discovery_publish(
        StringName(TWIN_PEER_CLASS),
        addresses,
        PackedStringArray(),
        Array()
    );
    netw::connect::TransportBook::shared().install_native(
        StringName(TWIN_PEER_CLASS),
        make_twin,
        String("the registration")
    );
    const Ref<NetwEndpoint> own
        = handle->add(StringName(TWIN_PEER_CLASS), String("shared"), String());

    const TypedArray<NetwEndpoint> listed = handle->endpoints();
    NETW_CHECK_EQ(int(listed.size()), 2);
    if (listed.size() == 2) {
        const Ref<NetwEndpoint> first = listed[0];
        const Ref<NetwEndpoint> second = listed[1];
        const bool distinct = first.is_valid() && second.is_valid()
                              && first != second;
        const bool own_listed = first == own || second == own;
        const bool same_pair
            = first->get_peer_class() == second->get_peer_class()
              && first->get_address() == second->get_address();
        CHECK(distinct);
        CHECK(own_listed);
        CHECK(same_pair);
    }

    netw::connect::TransportBook::shared().forget(StringName(TWIN_PEER_CLASS));
    core->service_unregister(directory, nullptr);
    memdelete(directory);
    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect] an endpoint's status is read from what the last "
    "probe answered, and a server that does not probe stays unknown"
) {
    for (const StatusRow &row : STATUS_ROWS) {
        NETW_FORMAT_TEXT(claim_text, row.claim);
        CAPTURE(claim_text);
        NETW_CHECK_EQ(
            int(netw::endpoint_status(row.flags, row.error)),
            int(row.expected)
        );
    }
}

} // namespace TestNetwConnectEndpoint

#endif

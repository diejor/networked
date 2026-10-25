#include "support/directory_stubs.h"
#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/connection.hpp"
#include "netw/api/endpoint.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/session.hpp"

namespace TestSessionTransferAccountLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPlayer;
using netw::NetwSession;
using netw_test::LoopbackRig;

constexpr int CLIENTS = 3;
constexpr int SETTLE_PUMPS = 8;

String account_for(int64_t p_peer) {
    return String("test:") + String::num_int64(p_peer);
}

String name_for(int64_t p_peer) {
    return String("Name ") + String::num_int64(p_peer);
}

class AccountDirectory : public netw_test::StubDirectory {
public:
    Ref<netw::LocalMultiplayerPeer> handing;

    void host_lobby(const Dictionary &) override {
        deliver(handing);
    }

    void join_lobby(const String &) override {
        deliver(handing);
    }

    String account_of(int64_t p_peer) override {
        return account_for(p_peer);
    }

    String member_name(int64_t p_peer) override {
        return name_for(p_peer);
    }
};

Vector<AccountDirectory *> &served() {
    static Vector<AccountDirectory *> held;
    return held;
}

void serve_accounts(
    NetwMultiplayer *p_api,
    const Ref<netw::LocalMultiplayerPeer> &p_peer
) {
    AccountDirectory *made = memnew(AccountDirectory);
    made->handing = p_peer;
    served().push_back(made);
    p_api->service_register(made, nullptr);
    const Ref<netw::NetwConnection> connection = p_api->get_connection();
    if (p_peer->get_unique_id() == 1) {
        connection->host(StringName("StubLobbyPeer"), Dictionary());
    } else {
        connection->join(
            connection->add(
                StringName("StubLobbyPeer"),
                String("loopback"),
                String()
            ),
            Dictionary()
        );
    }
    p_api->connect_plane().on_poll(0.0);
}

struct ServedDirectories {
    ~ServedDirectories() {
        for (AccountDirectory *directory : served()) {
            memdelete(directory);
        }
        served().clear();
    }
};

Vector<Ref<NetwPlayer>> players_of(NetwMultiplayer *p_api) {
    Vector<Ref<NetwPlayer>> found;
    const TypedArray<NetwPlayer> held = p_api->get_session()->get_players();
    for (int at = 0; at < held.size(); ++at) {
        found.push_back(Ref<NetwPlayer>(held[at]));
    }
    return found;
}

struct Seated {
    ServedDirectories cleanup;
    LoopbackRig rig;

    explicit Seated(NetwSession::TransferMode p_mode)
        : rig(CLIENTS, true, false, &serve_accounts) {
        rig.mount();
        rig.join(-1, StringName("host"));
        for (int at = 0; at < CLIENTS; ++at) {
            rig.join(at, StringName(vformat("guest%d", at)));
        }
        for (int at = -1; at < CLIENTS; ++at) {
            seat(at)->transfer_set_mode(p_mode);
        }
        rig.pump(SETTLE_PUMPS);
    }

    NetwMultiplayer *seat(int p_at) const {
        return p_at < 0 ? rig.server() : rig.client(p_at);
    }
};

void check_reads_every_account(NetwMultiplayer *p_api) {
    const Vector<Ref<NetwPlayer>> players = players_of(p_api);
    NETW_CHECK_EQ(int(players.size()), CLIENTS + 1);
    for (const Ref<NetwPlayer> &player : players) {
        const int64_t peer = player->get_peer_id();
        CHECK(bool(player->get_account() == account_for(peer)));
        CHECK(bool(player->get_account_name() == name_for(peer)));
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] after transfer_server the "
    "successor reads every player's account from the serving directory, and "
    "the demoted server keeps only its own"
) {
    for (const NetwSession::TransferMode mode :
         {NetwSession::TRANSFER_CONTINUE, NetwSession::TRANSFER_REJOIN}) {
        CAPTURE(int(mode));
        Seated seated(mode);
        NetwMultiplayer *host = seated.seat(-1);
        NetwMultiplayer *successor = seated.seat(0);
        REQUIRE(host->session_serving_directory() != nullptr);
        for (const Ref<NetwPlayer> &player : players_of(successor)) {
            if (player->get_peer_id() != successor->get_unique_id()) {
                CHECK(player->get_account().is_empty());
            }
        }

        REQUIRE(
            int(
                host->get_session()->transfer_server(successor->get_unique_id())
            )
            == int(OK)
        );
        seated.rig.pump(SETTLE_PUMPS);

        NETW_CHECK_EQ(int64_t(successor->get_unique_id()), int64_t(1));
        check_reads_every_account(successor);
        const int64_t own = host->get_unique_id();
        for (const Ref<NetwPlayer> &player : players_of(host)) {
            if (player->get_peer_id() == own) {
                CHECK_FALSE(player->get_account().is_empty());
                continue;
            }
            CHECK(player->get_account().is_empty());
            CHECK_FALSE(player->get_account_name().is_empty());
        }
    }
}

TEST_CASE(
    "[Networked][Session][Transfer][SceneTree] after the server is lost the "
    "successor reads every survivor's account from the serving directory"
) {
    Seated seated(NetwSession::TRANSFER_CONTINUE);
    REQUIRE(int(seated.rig.session()->lose_server()) == int(OK));
    seated.rig.pump(SETTLE_PUMPS);

    NetwMultiplayer *successor = nullptr;
    for (int at = 0; at < CLIENTS; ++at) {
        if (seated.seat(at)->get_unique_id() == 1) {
            successor = seated.seat(at);
        }
    }
    REQUIRE(successor != nullptr);
    const Vector<Ref<NetwPlayer>> players = players_of(successor);
    NETW_CHECK_EQ(int(players.size()), CLIENTS);
    for (const Ref<NetwPlayer> &player : players) {
        CHECK(
            bool(player->get_account() == account_for(player->get_peer_id()))
        );
    }
}

} // namespace TestSessionTransferAccountLaws

#endif

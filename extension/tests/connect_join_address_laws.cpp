#include "support/directory_stubs.h"
#include "support/fault_log.h"
#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/connection.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session.hpp"
#include "netw/wire/stream.hpp"

namespace TestConnectJoinAddressLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::LoopbackRig;

constexpr int SETTLE_PUMPS = 8;

String &served_address() {
    static String held;
    return held;
}

class MatchDirectory : public netw_test::StubDirectory {
public:
    Ref<netw::LocalMultiplayerPeer> handing;

    void join_lobby(const String &) override {
        deliver(handing);
    }

    String join_address() override {
        return served_address();
    }
};

Vector<MatchDirectory *> &served() {
    static Vector<MatchDirectory *> held;
    return held;
}

void join_through_directory(
    NetwMultiplayer *p_api,
    const Ref<netw::LocalMultiplayerPeer> &p_peer
) {
    MatchDirectory *made = memnew(MatchDirectory);
    made->handing = p_peer;
    served().push_back(made);
    p_api->service_register(made, nullptr);
    const Ref<netw::NetwConnection> connection = p_api->get_connection();
    connection->join(
        connection->add(
            StringName("StubLobbyPeer"),
            String("matched:ticket"),
            String()
        ),
        Dictionary()
    );
    p_api->connect_plane().on_poll(0.0);
}

struct ServedDirectories {
    explicit ServedDirectories(const String &p_address) {
        served_address() = p_address;
    }

    ~ServedDirectories() {
        for (MatchDirectory *directory : served()) {
            memdelete(directory);
        }
        served().clear();
    }
};

struct Seated {
    ServedDirectories directories;
    LoopbackRig rig;

    explicit Seated(const String &p_address)
        : directories(p_address),
          rig(1, false, false, &join_through_directory) {
        rig.mount();
        rig.join(-1, StringName("host"));
        rig.join(0, StringName("guest"));
        rig.pump(SETTLE_PUMPS);
    }

    String join_address_at(int p_at) const {
        NetwMultiplayer *api = p_at < 0 ? rig.server() : rig.client(p_at);
        return api->get_connection()->get_join_address();
    }

    int64_t players_at(int p_at) const {
        NetwMultiplayer *api = p_at < 0 ? rig.server() : rig.client(p_at);
        return int64_t(api->get_session()->get_players().size());
    }
};

TEST_CASE(
    "[Networked][Connect][SceneTree] a server that came up through join "
    "advertises its transport's join address, not the address it joined"
) {
    Seated seated(String("match-7"));

    CHECK(bool(seated.join_address_at(-1) == String("match-7")));
    CHECK(bool(seated.join_address_at(0) == String("match-7")));
}

TEST_CASE(
    "[Networked][Connect][SceneTree] a join address too long for a packet "
    "is refused when set, and a transport's long address is named in the "
    "error while the client still joins"
) {
    netw_test::FaultWatch watch;
    watch.expect_error(String("*joined without it*"), 1);
    watch.expect_error(String("*got an address*"), 1);
    const String long_address = String("m").repeat(netw::wire::STRING_CAP + 1);
    Seated seated(long_address);

    NETW_CHECK_EQ(seated.players_at(0), int64_t(2));
    NETW_CHECK_EQ(
        int(seated.rig.server()->connection_set_join_address(long_address)),
        int(ERR_INVALID_PARAMETER)
    );
}

} // namespace TestConnectJoinAddressLaws

#endif

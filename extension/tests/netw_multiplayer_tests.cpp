#include "support/netw_test.h"

#include "netw/netw_multiplayer.hpp"

namespace TestNetwMultiplayer {

using namespace godot;
using netw::NetwMultiplayerCore;

TEST_CASE("[Networked][Multiplayer][Hosted] offline core is server-shaped") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->get_unique_id(), 1);
    NETW_CHECK_EQ(core->is_server(), true);
    NETW_CHECK_EQ(core->has_multiplayer_peer(), false);
    NETW_CHECK_EQ(core->get_peer_ids().size(), 0);
}

TEST_CASE("[Networked][Multiplayer][Hosted] peer ids are a native read view") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    PackedInt32Array ids;
    ids.push_back(2);
    ids.push_back(7);

    core->set_peer_ids(ids);

    NETW_CHECK_EQ(core->get_peer_ids().size(), 2);
    NETW_CHECK_EQ(core->get_peer_ids()[0], 2);
    NETW_CHECK_EQ(core->get_peer_ids()[1], 7);
}

} // namespace TestNetwMultiplayer

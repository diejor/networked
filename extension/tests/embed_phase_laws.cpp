#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwEmbedPhase {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> declaring_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] E1 a session starts DECLARING and settle "
    "advances it to LIVE"
) {
    Ref<NetwMultiplayer> session = declaring_session();
    NETW_CHECK_EQ(
        int(session->embed_phase()),
        int(NetwMultiplayer::EMBED_PHASE_DECLARING)
    );
    NETW_CHECK_EQ(int(session->embed_settle()), int(OK));
    NETW_CHECK_EQ(
        int(session->embed_phase()),
        int(NetwMultiplayer::EMBED_PHASE_LIVE)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] ER1 a removal naming the root "
    "window disposes the session, and a removal naming any other path only "
    "clears its mounted path"
) {
    const NodePath window = netw::gd::scene_root()->get_path();
    const NodePath branch("/root/RD1Branch");

    Ref<NetwMultiplayer> nested = declaring_session();
    NETW_CHECK_EQ(
        int(nested->NETW_API_VIRTUAL(object_configuration_add)(
            nullptr,
            branch
        )),
        int(OK)
    );
    NETW_CHECK_EQ(
        int(nested->NETW_API_VIRTUAL(object_configuration_remove)(
            nullptr,
            branch
        )),
        int(OK)
    );
    NETW_CHECK_EQ(int(nested->get_root_path().is_empty()), 1);
    NETW_CHECK_EQ(int(nested->embed_is_disposing()), 0);

    Ref<NetwMultiplayer> rooted = declaring_session();
    NETW_CHECK_EQ(
        int(rooted->NETW_API_VIRTUAL(object_configuration_add)(
            nullptr,
            window
        )),
        int(OK)
    );
    NETW_CHECK_EQ(int(rooted->embed_is_disposing()), 0);
    NETW_CHECK_EQ(
        int(rooted->NETW_API_VIRTUAL(object_configuration_remove)(
            nullptr,
            window
        )),
        int(OK)
    );
    NETW_CHECK_EQ(int(rooted->embed_is_disposing()), 1);
}

} // namespace TestNetwEmbedPhase

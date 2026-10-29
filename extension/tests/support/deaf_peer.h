#pragma once

#if defined(DEBUG_ENABLED)

#include "godot/multiplayer.hpp"
#include "link.h"
#include "netw/link/shaped_peer.hpp"

namespace netw_test {

class DeafPeer final : public netw::link::ShapedPeer {
public:
    void NETW_PEER_VIRTUAL(disconnect_peer)(int, bool) override {
    }

    static godot::Ref<godot::MultiplayerPeer> over(
        const godot::Ref<godot::MultiplayerPeer> &p_base
    ) {
        godot::Ref<DeafPeer> deaf;
        deaf.instantiate();
        deaf->initialize(p_base, link());
        return deaf;
    }
};

} // namespace netw_test

#endif

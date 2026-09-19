#pragma once

#include "netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/session/frames.hpp"

namespace netw_test {

inline netw::session::AcceptFrame accepted_row(
    netw::NetwMultiplayer *p_core,
    int64_t p_peer,
    const godot::StringName &p_username = godot::StringName()
) {
    netw::session::AcceptFrame out;
    out.peer_id = p_peer;
    out.username = p_username;
    out.player_id = uint64_t(p_core->player_mint_id());
    return out;
}

inline godot::Ref<netw::NetwPlayer> seated_peer(
    netw::NetwMultiplayer *p_core,
    int64_t p_peer,
    const godot::StringName &p_username = godot::StringName()
) {
    p_core->session_admit(accepted_row(p_core, p_peer, p_username));
    return p_core->player_of(p_peer);
}

} // namespace netw_test

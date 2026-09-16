#include "netw/persist/write_fence.hpp"

#include "godot/object.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw::persist {

bool write_fence_holds(const WriteFence &p_issued) {
    if (!p_issued.armed) {
        return true;
    }
    NetwMultiplayer *session = Object::cast_to<NetwMultiplayer>(
        gd::object_of(p_issued.session)
    );
    return session != nullptr
        && session->session_authority_peer() == p_issued.authority;
}

} // namespace netw::persist

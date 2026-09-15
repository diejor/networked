#include "netw/api/netw_multiplayer.hpp"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/viewport.hpp"
#include "netw/api/entity.hpp"
#include "netw/log.hpp"

using namespace godot;
using namespace netw;

namespace netw {

namespace {

const char *SIG_PARTICIPANT_VIEWPORT_CHANGED = "participant_viewport_changed";

} // namespace

SubViewport *NetwMultiplayer::scene_resolve_participant_viewport() {
    if (session_get_role() != ROLE_LISTEN_SERVER) {
        return nullptr;
    }
    return scene_world_of(scene_get_node(scene_presented()));
}

SubViewport *NetwMultiplayer::scene_participant_viewport() {
    return scene_resolve_participant_viewport();
}

void NetwMultiplayer::scene_participant_display_invalidate() {
    if (scene_participant_display_pending) {
        return;
    }
    scene_participant_display_pending = true;
    callable_mp(this, &NetwMultiplayer::scene_participant_display_settle)
        .call_deferred();
}

void NetwMultiplayer::scene_participant_display_settle() {
    scene_participant_display_pending = false;
    SubViewport *resolved = scene_resolve_participant_viewport();
    const ObjectID settled = resolved != nullptr
        ? ObjectID(resolved->get_instance_id())
        : ObjectID();
    if (settled == scene_participant_display_id) {
        return;
    }
    scene_participant_display_id = settled;
    NETW_TRACE(
        sys::SCENE,
        "the participant display resolved to %s",
        resolved != nullptr ? "an isolated world" : "no viewport"
    );
    emit_signal(SIG_PARTICIPANT_VIEWPORT_CHANGED, resolved);
}

} // namespace netw

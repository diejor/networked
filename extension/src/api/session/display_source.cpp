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

SubViewport *viewport_of(Node *p_scene_root) {
    return NetwMultiplayer::scene_world_of(p_scene_root);
}

} // namespace

Node *NetwMultiplayer::scene_participant_player() {
    const Ref<NetwEntity> seated = scene_player_local();
    if (seated.is_valid() && seated->get_owner() != nullptr) {
        return seated->get_owner();
    }
    const int64_t local_id = get_unique_id();
    const TypedArray<RID> scenes = scene_list();
    for (int at = 0; at < scenes.size(); ++at) {
        const TypedArray<NetwEntity> players = scene_get_players(scenes[at]);
        for (int seat = 0; seat < players.size(); ++seat) {
            const Ref<NetwEntity> entity = players[seat];
            if (entity.is_null()) {
                continue;
            }
            Node *owner = entity->get_owner();
            if (owner == nullptr) {
                continue;
            }
            if (entity->get_peer_id() == local_id
                || NetwEntity::parse_peer(owner->get_name()) == local_id) {
                return owner;
            }
        }
    }
    return nullptr;
}

SubViewport *NetwMultiplayer::scene_first_active_viewport() {
    const TypedArray<RID> scenes = scene_list();
    for (int at = 0; at < scenes.size(); ++at) {
        const RID scene = scenes[at];
        Node *level = scene_get_node(scene);
        if (level == nullptr
            || level->get_process_mode() == Node::PROCESS_MODE_DISABLED) {
            continue;
        }
        SubViewport *viewport = viewport_of(level);
        if (viewport != nullptr) {
            return viewport;
        }
    }
    return nullptr;
}

SubViewport *NetwMultiplayer::scene_resolve_participant_viewport() {
    if (session_get_role() != ROLE_LISTEN_SERVER) {
        return nullptr;
    }
    Node *player = scene_participant_player();
    if (player != nullptr) {
        SubViewport *seated
            = viewport_of(scene_get_node(scene_of(entity_of(player))));
        if (seated != nullptr) {
            return seated;
        }
    }
    SubViewport *current = viewport_of(scene_get_node(scene_get_current()));
    if (current != nullptr) {
        return current;
    }
    return scene_first_active_viewport();
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

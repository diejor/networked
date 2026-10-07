#include "netw/api/netw_multiplayer.hpp"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/resource.hpp"
#include "godot/script.hpp"
#include "godot/utility.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/scene_core.hpp"
#include "netw/subsystems.hpp"

using namespace godot;
using namespace netw;

namespace netw {

void NetwMultiplayer::embed_set_phase(EmbedPhase p_phase) {
    embed_phase_value = p_phase;
}

bool NetwMultiplayer::embed_claim_dispose() {
    if (embed_disposing) {
        return false;
    }
    embed_disposing = true;
    return true;
}

Node *NetwMultiplayer::embed_declared_child() {
    Node *root = session_root();
    if (root == nullptr) {
        return nullptr;
    }
    Node *declared = nullptr;
    int count = 0;
    String listed;
    for (int at = 0; at < root->get_child_count(); ++at) {
        Node *child = root->get_child(at);
        const SceneDecl decl = scene_decl_of(
            Ref<Script>(Object::cast_to<Script>(child->get_script()))
        );
        if (!decl.declared) {
            continue;
        }
        declared = child;
        count += 1;
        listed += (listed.is_empty() ? String("'") : String(", '"))
            + String(child->get_name()) + String("'");
    }
    if (count > 1) {
        NETW_WARN(
            sys::SCENE,
            "Several scenes are placed under '%s' (%s), so none of them was "
            "adopted. Place one there and spawn the others with Netw.spawn.",
            String(root->get_name()),
            listed
        );
        return nullptr;
    }
    return declared;
}

void NetwMultiplayer::scene_adopt_bare_level(Node *p_level) {
    if (p_level == nullptr) {
        return;
    }
    const SceneDecl decl = scene_decl_of(
        Ref<Script>(Object::cast_to<Script>(p_level->get_script()))
    );
    const bool spawns_the_authority_scene = is_host();
    const String path = NetwSceneCore::verify_requested_path(
        gd::ensure_path(p_level->get_scene_file_path())
    );
    if (spawns_the_authority_scene && path.is_empty()) {
        NETW_ERR(
            sys::SCENE,
            "'%s' is a multiplayer scene with no .tscn file under res://, so "
            "it cannot be spawned. Save the level as its own scene.",
            String(p_level->get_name())
        );
    }
    Node *parent = p_level->get_parent();
    if (parent != nullptr) {
        parent->remove_child(p_level);
    }
    memdelete(p_level);
    if (!spawns_the_authority_scene) {
        NETW_TRACE(
            sys::SCENE,
            "A placed level was dropped on a peer without scene authority, "
            "so it receives the authority's scene instead."
        );
        return;
    }
    if (scene_spawn(path, SceneIsolation(decl.isolation)) == nullptr) {
        NETW_ERROR(
            sys::SCENE,
            "The level at '%s' could not be loaded, so the session has no "
            "scene to show or send.",
            path
        );
    }
}

Error NetwMultiplayer::embed_settle() {
    NETW_ZONE_NC("embed_settle", colors::SCENE);
    if (embed_phase_value != EMBED_PHASE_DECLARING) {
        return OK;
    }
    embed_set_phase(EMBED_PHASE_SETTLING);
    scene_adopt_bare_level(embed_declared_child());
    const Array live = scene_core->live_scenes();
    for (int at = 0; at < live.size(); ++at) {
        scene_warn_unwrapped(wrapper_owner(RID(live[at])));
    }
    ReplicationCore *plane = (get_replication_plane());
    const Error wired = plane != nullptr ? plane->settle_channels() : OK;
    if (wired != OK) {
        NETW_TRACE(
            sys::SCENE,
            "The session went live with a built-in channel that has no "
            "handler."
        );
    }
    embed_set_phase(EMBED_PHASE_LIVE);
    return wired;
}

Error NetwMultiplayer::embed_poll_transport() {
    NETW_ZONE_NC("Transport receive", colors::TRANSPORT);
    if (inner.is_null()) {
        NETW_TRACE(
            sys::TRANSPORT,
            "The transport was polled before the session existed."
        );
        return ERR_UNCONFIGURED;
    }
    const Error err = inner->poll();
    session_announce_transfer();
    transfer_send_resync();
    transfer_watch_resync();
    liveness_poll_now();
    rpc_sweep_deferred_calls();
    rpc_sweep_transactions(receive_tick());
    return err;
}

} // namespace netw

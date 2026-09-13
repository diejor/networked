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

namespace {

const char *SIG_EMBED_PHASE_CHANGED = "embed_phase_changed";

} // namespace

void NetwMultiplayer::embed_set_phase(EmbedPhase p_phase) {
    if (embed_phase_value == p_phase) {
        return;
    }
    embed_phase_value = p_phase;
    emit_signal(SIG_EMBED_PHASE_CHANGED, int(embed_phase_value));
}

bool NetwMultiplayer::embed_claim_dispose() {
    if (embed_disposing) {
        return false;
    }
    embed_disposing = true;
    return true;
}

void NetwMultiplayer::embed_offer_bare_level(Node *p_level) {
    embed_bare_level
        = p_level != nullptr ? p_level->get_instance_id() : ObjectID();
}

void NetwMultiplayer::scene_adopt_bare_level(Node *p_level) {
    if (p_level == nullptr) {
        return;
    }
    Node *root = session_root();
    if (root == nullptr) {
        NETW_TRACE(sys::SCENE, "a bare level was offered with no session root");
        return;
    }
    const SceneDecl decl = scene_decl_of(
        Ref<Script>(Object::cast_to<Script>(p_level->get_script()))
    );
    if (!decl.declared) {
        NETW_TRACE(
            sys::SCENE,
            "'%s' declares no multiplayer scene, so it is the game's own root "
            "rather than a level to adopt",
            p_level->get_name()
        );
        return;
    }
    const String path = gd::ensure_path(p_level->get_scene_file_path());
    Node *parent = p_level->get_parent();
    if (parent != nullptr) {
        parent->remove_child(p_level);
    }
    memdelete(p_level);
    if (!is_server()) {
        NETW_TRACE(
            sys::SCENE,
            "a bare level was offered to a client, which receives the "
            "authority's scene instead of spawning its own"
        );
        return;
    }
    scene_spawn(path, SceneIsolation(decl.isolation));
}

Error NetwMultiplayer::embed_settle() {
    NETW_ZONE_NC("embed_settle", colors::SCENE);
    if (embed_phase_value != EMBED_PHASE_DECLARING) {
        return OK;
    }
    embed_set_phase(EMBED_PHASE_SETTLING);
    Node *level = Object::cast_to<Node>(gd::object_of(embed_bare_level));
    embed_bare_level = ObjectID();
    scene_adopt_bare_level(level);
    scene_ensure_host_view();
    ReplicationCore *plane = (get_replication_plane());
    const Error wired = plane != nullptr ? plane->settle_channels() : OK;
    if (wired != OK) {
        NETW_TRACE(
            sys::SCENE,
            "settle reached LIVE with an unhandled peer-scoped channel"
        );
    }
    embed_set_phase(EMBED_PHASE_LIVE);
    return wired;
}

Error NetwMultiplayer::embed_poll_transport() {
    NETW_ZONE_NC("embed_poll_transport", colors::TRANSPORT);
    if (inner.is_null()) {
        NETW_TRACE(
            sys::TRANSPORT,
            "a transport poll ran with no inner session"
        );
        return ERR_UNCONFIGURED;
    }
    const Error err = inner->poll();
    liveness_poll_now();
    rpc_sweep_deferred_calls();
    rpc_sweep_transactions(receive_tick());
    return err;
}

} // namespace netw

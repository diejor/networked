#include "netw/api/nodes/multiplayer_viewport.hpp"

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/object.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

bool in_editor() {
    Engine *engine = Engine::get_singleton();
    return engine != nullptr && engine->is_editor_hint();
}

} // namespace

MultiplayerViewport::MultiplayerViewport() {
    set_use_own_world_3d(true);
    if (!in_editor()) {
        api = NetwMultiplayer::create(Ref<Script>());
    }
}

void MultiplayerViewport::set_api(const Ref<NetwMultiplayer> &p_value) {
    NETW_ERR_COND(
        is_inside_tree(),
        sys::SESSION,
        "MultiplayerViewport.api cannot change once the tree is in the scene "
        "tree. Set it before adding the MultiplayerViewport."
    );
    NETW_ERR_COND(
        p_value.is_null(),
        sys::SESSION,
        "MultiplayerViewport.api cannot be null. Pass a NetwMultiplayer."
    );
    if (api == p_value) {
        return;
    }
    if (api.is_valid()) {
        api->embed_dispose();
    }
    api = p_value;
}

void MultiplayerViewport::enter_tree() {
    if (in_editor()) {
        return;
    }
    mount_api();
}

void MultiplayerViewport::mount_api() {
    if (api.is_null()) {
        return;
    }
    register_path();
}

void MultiplayerViewport::unmount_api() {
    if (api.is_null()) {
        return;
    }
    unregister_path();
}

void MultiplayerViewport::register_path() {
    const NodePath root_path = get_path();
    api->session_get_inner()->set_root_path(root_path);
    get_tree()->set_multiplayer(api, root_path);
    const Ref<MultiplayerAPI> mounted = get_tree()->get_multiplayer(root_path);
    NETW_ASSERT(
        mounted == api,
        sys::SESSION,
        "mounting is the only installation point on this branch"
    );
}

void MultiplayerViewport::unregister_path() {
    const NodePath mounted = api->session_get_inner()->get_root_path();
    if (!mounted.is_empty()) {
        get_tree()->set_multiplayer(Ref<MultiplayerAPI>(), mounted);
    }
}

void MultiplayerViewport::follow_rename() {
    if (in_editor() || api.is_null() || !is_inside_tree()) {
        return;
    }
    if (api->session_get_inner()->get_root_path() == get_path()) {
        return;
    }
    unregister_path();
    register_path();
}

void MultiplayerViewport::release_peer() {
    if (api.is_valid() && api->has_multiplayer_peer()) {
        api->get_multiplayer_peer()->close();
        api->set_multiplayer_peer(Ref<MultiplayerPeer>());
    }
}

void MultiplayerViewport::exiting() {
    NETW_TRACE(sys::SESSION, "The MultiplayerViewport is leaving the tree.");
    if (!is_queued_for_deletion()) {
        unmount_api();
        return;
    }
    release_peer();
    unmount_api();
    deletion_finalized = true;
}

void MultiplayerViewport::close_peer_on_delete() {
    if (deletion_finalized || in_editor() || is_inside_tree()) {
        return;
    }
    deletion_finalized = true;
    release_peer();
}

void MultiplayerViewport::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE:
            enter_tree();
            break;
        case NOTIFICATION_PATH_RENAMED:
            follow_rename();
            break;
        case NOTIFICATION_EXIT_TREE:
            exiting();
            break;
        case NOTIFICATION_PREDELETE:
            close_peer_on_delete();
            if (api.is_valid()) {
                api->embed_dispose();
            }
            break;
        default:
            break;
    }
}

void MultiplayerViewport::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_api", "value"),
        &MultiplayerViewport::set_api
    );
    ClassDB::bind_method(D_METHOD("get_api"), &MultiplayerViewport::get_api);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "api",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwMultiplayer",
            PROPERTY_USAGE_NONE
        ),
        "set_api",
        "get_api"
    );
}

} // namespace netw

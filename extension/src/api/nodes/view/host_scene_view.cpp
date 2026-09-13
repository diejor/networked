#include "netw/api/nodes/view/host_scene_view.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/node.hpp"
#include "netw/api/nodes/view/cameras.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_PARTICIPANT_VIEWPORT_CHANGED = "participant_viewport_changed";
const char *SIG_SCENE_LOCAL_PLAYER_CHANGED = "scene_local_player_changed";
const char *SIG_VIEW_ACTIVATED = "view_activated";

} // namespace

NetwMultiplayer *HostSceneView::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void HostSceneView::attach() {
    set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
    set_mouse_filter(Control::MOUSE_FILTER_PASS);
    set_forwards_unhandled_input(!suppressed);

    NetwMultiplayer *api = Object::cast_to<NetwMultiplayer>(
        NetwMultiplayer::session_of(this).ptr()
    );
    if (api == nullptr) {
        return;
    }
    session_id = gd::instance_id(api);
    api->service_register(this, nullptr);

    const Callable display
        = callable_mp(this, &HostSceneView::on_display_changed);
    if (!api->is_connected(SIG_PARTICIPANT_VIEWPORT_CHANGED, display)) {
        api->connect(SIG_PARTICIPANT_VIEWPORT_CHANGED, display);
    }
    const Callable seated
        = callable_mp(this, &HostSceneView::on_local_player_changed);
    if (!api->is_connected(SIG_SCENE_LOCAL_PLAYER_CHANGED, seated)) {
        api->connect(SIG_SCENE_LOCAL_PLAYER_CHANGED, seated);
    }
    on_display_changed(api->scene_participant_viewport());
}

void HostSceneView::detach() {
    NetwMultiplayer *api = session();
    session_id = ObjectID();
    if (api == nullptr) {
        return;
    }
    const Callable display
        = callable_mp(this, &HostSceneView::on_display_changed);
    if (api->is_connected(SIG_PARTICIPANT_VIEWPORT_CHANGED, display)) {
        api->disconnect(SIG_PARTICIPANT_VIEWPORT_CHANGED, display);
    }
    const Callable seated
        = callable_mp(this, &HostSceneView::on_local_player_changed);
    if (api->is_connected(SIG_SCENE_LOCAL_PLAYER_CHANGED, seated)) {
        api->disconnect(SIG_SCENE_LOCAL_PLAYER_CHANGED, seated);
    }
    api->service_unregister(this, nullptr);
}

void HostSceneView::on_local_player_changed(const Ref<NetwEntity> &) {
    callable_mp(this, &HostSceneView::reannounce).call_deferred();
}

void HostSceneView::reannounce() {
    NetwMultiplayer *api = session();
    if (suppressed || !is_inside_tree() || api == nullptr) {
        return;
    }
    announce(api->scene_participant_viewport());
}

void HostSceneView::set_suppressed(bool p_suppressed) {
    if (suppressed == p_suppressed) {
        return;
    }
    suppressed = p_suppressed;
    set_visible(!suppressed);
    set_forwards_unhandled_input(!suppressed);
    if (suppressed) {
        clear_target();
        return;
    }
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        on_display_changed(api->scene_participant_viewport());
    }
}

void HostSceneView::on_display_changed(SubViewport *p_viewport) {
    if (suppressed) {
        set_target(nullptr);
        return;
    }
    set_target(p_viewport);
    announce(p_viewport);
}

void HostSceneView::announce(SubViewport *p_viewport) {
    NetwMultiplayer *api = session();
    if (p_viewport == nullptr || api == nullptr) {
        return;
    }
    const Ref<NetwEntity> seated = api->scene_player_local();
    if (seated.is_null()) {
        return;
    }
    Node *player = seated->get_owner();
    if (player == nullptr || !p_viewport->is_ancestor_of(player)) {
        return;
    }
    if (!seated->has_connections(SIG_VIEW_ACTIVATED)) {
        const Ref<NetwSceneHandle> scene = seated->get_scene();
        view::adopt_camera(
            player,
            scene.is_valid() ? scene->get_root() : nullptr
        );
    }
    seated->emit_signal(SIG_VIEW_ACTIVATED);
}

void HostSceneView::_notification(int p_what) {
    ParticipantView::_notification(p_what);
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    if (p_what == NOTIFICATION_ENTER_TREE) {
        attach();
    } else if (p_what == NOTIFICATION_EXIT_TREE) {
        detach();
    }
}

void HostSceneView::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_suppressed", "suppressed"),
        &HostSceneView::set_suppressed
    );
    ClassDB::bind_method(
        D_METHOD("get_suppressed"),
        &HostSceneView::get_suppressed
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "suppressed"),
        "set_suppressed",
        "get_suppressed"
    );
}

} // namespace netw

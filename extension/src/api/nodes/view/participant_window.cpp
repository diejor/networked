#include "netw/api/nodes/view/participant_window.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

namespace {

const char *HINT_MODE = "Inherit,Disabled,Canvas Items,Viewport";
const char *HINT_ASPECT = "Inherit,Ignore,Keep,Keep Width,Keep Height,Expand";
const char *HINT_SCALE_MODE = "Inherit,Fractional,Integer";

Window::ContentScaleMode window_mode(view::StretchMode p_mode) {
    switch (p_mode) {
        case view::STRETCH_MODE_CANVAS_ITEMS:
            return Window::CONTENT_SCALE_MODE_CANVAS_ITEMS;
        case view::STRETCH_MODE_VIEWPORT:
            return Window::CONTENT_SCALE_MODE_VIEWPORT;
        default:
            return Window::CONTENT_SCALE_MODE_DISABLED;
    }
}

Window::ContentScaleAspect window_aspect(view::StretchAspect p_aspect) {
    switch (p_aspect) {
        case view::STRETCH_ASPECT_IGNORE:
            return Window::CONTENT_SCALE_ASPECT_IGNORE;
        case view::STRETCH_ASPECT_KEEP_WIDTH:
            return Window::CONTENT_SCALE_ASPECT_KEEP_WIDTH;
        case view::STRETCH_ASPECT_KEEP_HEIGHT:
            return Window::CONTENT_SCALE_ASPECT_KEEP_HEIGHT;
        case view::STRETCH_ASPECT_EXPAND:
            return Window::CONTENT_SCALE_ASPECT_EXPAND;
        default:
            return Window::CONTENT_SCALE_ASPECT_KEEP;
    }
}

Window::ContentScaleStretch window_stretch(view::StretchScaleMode p_mode) {
    if (p_mode == view::STRETCH_SCALE_MODE_INTEGER) {
        return Window::CONTENT_SCALE_STRETCH_INTEGER;
    }
    return Window::CONTENT_SCALE_STRETCH_FRACTIONAL;
}

} // namespace

ParticipantWindow::ParticipantWindow() {
    set_flag(Window::FLAG_BORDERLESS, true);
    set_oversampling_override(1.0);
    set_visible(false);
}

Node *ParticipantWindow::get_mounted_tree() const {
    return Object::cast_to<Node>(gd::object_of(mounted_tree_id));
}

void ParticipantWindow::set_mounted_tree(Node *p_tree) {
    mounted_tree_id = gd::instance_id(p_tree);
}

void ParticipantWindow::set_tiled_rect(const Rect2i &p_rect) {
    set_position(p_rect.position);
    set_size(p_rect.size);
    apply_stretch();
}

void ParticipantWindow::send_input(const Ref<InputEvent> &p_event) {
    pending_input.push_back(p_event);
    if (flush_queued) {
        return;
    }
    flush_queued = true;
    callable_mp(this, &ParticipantWindow::flush_input).call_deferred();
}

void ParticipantWindow::flush_input() {
    flush_queued = false;
    draining.clear();
    for (const Ref<InputEvent> &event : pending_input) {
        draining.push_back(event);
    }
    pending_input.clear();
    for (const Ref<InputEvent> &event : draining) {
        push_input(event, true);
    }
    draining.clear();
}

void ParticipantWindow::apply_stretch() {
    const view::Stretch stretch = view::stretch_resolve(declared);
    set_content_scale_mode(window_mode(stretch.mode));
    set_content_scale_aspect(window_aspect(stretch.aspect));
    set_content_scale_stretch(window_stretch(stretch.scale_mode));
    set_content_scale_factor(stretch.scale);
    set_content_scale_size(stretch.design_size);
}

void ParticipantWindow::set_stretch_mode(ParticipantView::StretchMode p_mode) {
    declared.mode = view::StretchMode(p_mode);
    apply_stretch();
}

void ParticipantWindow::set_stretch_aspect(
    ParticipantView::StretchAspect p_aspect
) {
    declared.aspect = view::StretchAspect(p_aspect);
    apply_stretch();
}

void ParticipantWindow::set_stretch_scale_mode(
    ParticipantView::StretchScaleMode p_mode
) {
    declared.scale_mode = view::StretchScaleMode(p_mode);
    apply_stretch();
}

void ParticipantWindow::set_stretch_scale(double p_scale) {
    declared.scale = p_scale;
    apply_stretch();
}

void ParticipantWindow::set_stretch_design_size(const Vector2i &p_size) {
    declared.design_size = p_size;
    apply_stretch();
}

void ParticipantWindow::_notification(int p_what) {
    if (p_what == NOTIFICATION_READY) {
        apply_stretch();
    }
}

void ParticipantWindow::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_tiled_rect", "rect"),
        &ParticipantWindow::set_tiled_rect
    );
    ClassDB::bind_method(
        D_METHOD("send_input", "event"),
        &ParticipantWindow::send_input
    );

    ClassDB::bind_method(
        D_METHOD("set_mounted_tree", "tree"),
        &ParticipantWindow::set_mounted_tree
    );
    ClassDB::bind_method(
        D_METHOD("get_mounted_tree"),
        &ParticipantWindow::get_mounted_tree
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "mounted_tree",
            PROPERTY_HINT_NODE_TYPE,
            "Node",
            PROPERTY_USAGE_DEFAULT,
            "Node"
        ),
        "set_mounted_tree",
        "get_mounted_tree"
    );
    ClassDB::bind_method(
        D_METHOD("set_peer_id", "peer_id"),
        &ParticipantWindow::set_peer_id
    );
    ClassDB::bind_method(
        D_METHOD("get_peer_id"),
        &ParticipantWindow::get_peer_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "peer_id"),
        "set_peer_id",
        "get_peer_id"
    );
    ClassDB::bind_method(
        D_METHOD("set_username", "username"),
        &ParticipantWindow::set_username
    );
    ClassDB::bind_method(
        D_METHOD("get_username"),
        &ParticipantWindow::get_username
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "username"),
        "set_username",
        "get_username"
    );

    ADD_GROUP("Stretch", "stretch_");
    ClassDB::bind_method(
        D_METHOD("set_stretch_mode", "mode"),
        &ParticipantWindow::set_stretch_mode
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_mode"),
        &ParticipantWindow::get_stretch_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "stretch_mode",
            PROPERTY_HINT_ENUM,
            HINT_MODE
        ),
        "set_stretch_mode",
        "get_stretch_mode"
    );
    ClassDB::bind_method(
        D_METHOD("set_stretch_aspect", "aspect"),
        &ParticipantWindow::set_stretch_aspect
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_aspect"),
        &ParticipantWindow::get_stretch_aspect
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "stretch_aspect",
            PROPERTY_HINT_ENUM,
            HINT_ASPECT
        ),
        "set_stretch_aspect",
        "get_stretch_aspect"
    );
    ClassDB::bind_method(
        D_METHOD("set_stretch_scale_mode", "mode"),
        &ParticipantWindow::set_stretch_scale_mode
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_scale_mode"),
        &ParticipantWindow::get_stretch_scale_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "stretch_scale_mode",
            PROPERTY_HINT_ENUM,
            HINT_SCALE_MODE
        ),
        "set_stretch_scale_mode",
        "get_stretch_scale_mode"
    );
    ClassDB::bind_method(
        D_METHOD("set_stretch_scale", "scale"),
        &ParticipantWindow::set_stretch_scale
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_scale"),
        &ParticipantWindow::get_stretch_scale
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "stretch_scale"),
        "set_stretch_scale",
        "get_stretch_scale"
    );
    ClassDB::bind_method(
        D_METHOD("set_stretch_design_size", "size"),
        &ParticipantWindow::set_stretch_design_size
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_design_size"),
        &ParticipantWindow::get_stretch_design_size
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::VECTOR2I,
            "stretch_design_size",
            PROPERTY_HINT_NONE,
            "suffix:px"
        ),
        "set_stretch_design_size",
        "get_stretch_design_size"
    );
}

} // namespace netw

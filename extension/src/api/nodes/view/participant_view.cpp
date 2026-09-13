#include "netw/api/nodes/view/participant_view.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/texture.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

const char *META_OWNER = "netw_participant_view_owner";
const char *SIG_SIZE_CHANGED = "size_changed";
const char *SIG_TREE_EXITING = "tree_exiting";

const char *HINT_MODE = "Inherit,Disabled,Canvas Items,Viewport";
const char *HINT_ASPECT = "Inherit,Ignore,Keep,Keep Width,Keep Height,Expand";
const char *HINT_SCALE_MODE = "Inherit,Fractional,Integer";

} // namespace

ParticipantView *ParticipantView::owner_of(SubViewport *p_target) {
    if (p_target == nullptr || !p_target->has_meta(META_OWNER)) {
        return nullptr;
    }
    return Object::cast_to<ParticipantView>(gd::object_of(
        ObjectID(uint64_t(int64_t(p_target->get_meta(META_OWNER))))
    ));
}

SubViewport *ParticipantView::get_target() const {
    return Object::cast_to<SubViewport>(gd::object_of(target_id));
}

void ParticipantView::claim(SubViewport *p_target) {
    p_target->set_meta(META_OWNER, int64_t(uint64_t(gd::instance_id(this))));
    held_update_mode = p_target->get_update_mode();
    held_clear_mode = p_target->get_clear_mode();
    held_size_2d_override = p_target->get_size_2d_override();
    held_override_stretch = p_target->is_size_2d_override_stretch_enabled();
    p_target->set_update_mode(SubViewport::UPDATE_ALWAYS);
    p_target->set_clear_mode(SubViewport::CLEAR_MODE_ALWAYS);
}

void ParticipantView::release(SubViewport *p_target) {
    if (owner_of(p_target) == this) {
        p_target->remove_meta(META_OWNER);
    }
}

void ParticipantView::restore(SubViewport *p_target) {
    p_target->set_update_mode(held_update_mode);
    p_target->set_clear_mode(held_clear_mode);
    p_target->set_size_2d_override(held_size_2d_override);
    p_target->set_size_2d_override_stretch(held_override_stretch);
}

void ParticipantView::relinquish() {
    SubViewport *standing = get_target();
    target_id = ObjectID();
    if (standing == nullptr) {
        return;
    }
    release(standing);
    restore(standing);
    const Callable exiting
        = callable_mp(this, &ParticipantView::on_target_exiting);
    if (standing->is_connected(SIG_TREE_EXITING, exiting)) {
        standing->disconnect(SIG_TREE_EXITING, exiting);
    }
}

void ParticipantView::set_target(SubViewport *p_target) {
    if (get_target() == p_target) {
        return;
    }
    relinquish();

    if (p_target != nullptr) {
        ParticipantView *standing = owner_of(p_target);
        if (standing != nullptr && standing != this) {
            NETW_ERROR(
                sys::SCENE,
                "'%s' already displays '%s', and a SubViewport borrows its "
                "render state to one view at a time, so '%s' is left blank "
                "rather than overwriting what the standing view saved",
                standing->get_name(),
                p_target->get_name(),
                get_name()
            );
            set_process(false);
            queue_redraw();
            return;
        }
        target_id = gd::instance_id(p_target);
        claim(p_target);
        lay_out();
        const Callable exiting
            = callable_mp(this, &ParticipantView::on_target_exiting);
        if (!p_target->is_connected(SIG_TREE_EXITING, exiting)) {
            p_target->connect(SIG_TREE_EXITING, exiting);
            NETW_INFO(
                sys::SCENE,
                "%s now displays '%s'",
                get_name(),
                p_target->get_name()
            );
        }
    }

    set_process(p_target != nullptr);
    queue_redraw();
}

void ParticipantView::clear_target() {
    set_target(nullptr);
}

void ParticipantView::on_target_exiting() {
    relinquish();
    set_process(false);
    queue_redraw();
}

void ParticipantView::forward_input(const Ref<InputEvent> &p_event) {
    SubViewport *target = get_target();
    if (target == nullptr) {
        return;
    }
    target->push_input(p_event, true);
}

void ParticipantView::set_forwards_unhandled_input(bool p_forwards) {
    forwards_unhandled_input = p_forwards;
}

void ParticipantView::lay_out() {
    layout = view::layout_compute(view::stretch_resolve(declared), get_size());
    laid_out = true;
    SubViewport *target = get_target();
    if (target != nullptr) {
        target->set_size(layout.target_size);
        target->set_size_2d_override(layout.size_2d_override);
        target->set_size_2d_override_stretch(layout.override_stretch);
    }
    queue_redraw();
}

void ParticipantView::draw_target() {
    SubViewport *target = get_target();
    if (target == nullptr || !laid_out) {
        return;
    }
    const Ref<Texture2D> texture = target->get_texture();
    if (texture.is_valid()) {
        draw_texture_rect(texture, layout.inner_rect, false);
    }
}

void ParticipantView::watch_enclosing_viewport(bool p_watch) {
    Viewport *enclosing = get_viewport();
    if (enclosing == nullptr) {
        return;
    }
    const Callable fill
        = callable_mp(this, &ParticipantView::fill_enclosing_viewport);
    const bool watching = enclosing->is_connected(SIG_SIZE_CHANGED, fill);
    if (p_watch && !watching) {
        enclosing->connect(SIG_SIZE_CHANGED, fill);
    } else if (!p_watch && watching) {
        enclosing->disconnect(SIG_SIZE_CHANGED, fill);
    }
}

void ParticipantView::fill_enclosing_viewport() {
    if (Object::cast_to<Control>(get_parent()) != nullptr) {
        return;
    }
    Viewport *enclosing = get_viewport();
    if (enclosing == nullptr) {
        return;
    }
    set_anchors_preset(Control::PRESET_TOP_LEFT);
    set_position(Vector2());
    set_deferred("size", enclosing->get_visible_rect().size);
}

void ParticipantView::_notification(int p_what) {
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE:
            set_mouse_filter(Control::MOUSE_FILTER_STOP);
            set_process(false);
            break;
        case NOTIFICATION_READY:
            fill_enclosing_viewport();
            watch_enclosing_viewport(true);
            break;
        case NOTIFICATION_EXIT_TREE:
            watch_enclosing_viewport(false);
            clear_target();
            break;
        case NOTIFICATION_PROCESS:
            if (get_target() != nullptr) {
                queue_redraw();
            }
            break;
        case NOTIFICATION_RESIZED:
            lay_out();
            break;
        case NOTIFICATION_DRAW:
            draw_target();
            break;
        default:
            break;
    }
}

void ParticipantView::NETW_GUI_INPUT(const Ref<InputEvent> &p_event) {
    SubViewport *target = get_target();
    if (target == nullptr || !laid_out) {
        return;
    }
    Ref<InputEvent> pushed = p_event;
    const bool remaps
        = Object::cast_to<InputEventMouse>(p_event.ptr()) != nullptr
        && layout.inner_rect.size.x > 0.0f && layout.inner_rect.size.y > 0.0f;
    if (remaps) {
        const Vector2i override = target->get_size_2d_override();
        const Vector2 logical = override != Vector2i()
            ? Vector2(override)
            : Vector2(target->get_size());
        const Vector2 scale = logical / layout.inner_rect.size;
        const Transform2D xform = Transform2D().scaled(scale).translated(
            -layout.inner_rect.position * scale
        );
        pushed = p_event->xformed_by(xform);
    }
    forward_input(pushed);
    accept_event();
}

void ParticipantView::NETW_UNHANDLED_INPUT(const Ref<InputEvent> &p_event) {
    if (!forwards_unhandled_input) {
        return;
    }
    if (Object::cast_to<InputEventMouse>(p_event.ptr()) != nullptr) {
        return;
    }
    forward_input(p_event);
}

void ParticipantView::set_stretch_mode(StretchMode p_mode) {
    declared.mode = view::StretchMode(p_mode);
    lay_out();
}

void ParticipantView::set_stretch_aspect(StretchAspect p_aspect) {
    declared.aspect = view::StretchAspect(p_aspect);
    lay_out();
}

void ParticipantView::set_stretch_scale_mode(StretchScaleMode p_mode) {
    declared.scale_mode = view::StretchScaleMode(p_mode);
    lay_out();
}

void ParticipantView::set_stretch_scale(double p_scale) {
    declared.scale = p_scale;
    lay_out();
}

void ParticipantView::set_stretch_design_size(const Vector2i &p_size) {
    declared.design_size = p_size;
    lay_out();
}

void ParticipantView::_bind_methods() {
    ClassDB::bind_static_method(
        "ParticipantView",
        D_METHOD("owner_of", "target"),
        &ParticipantView::owner_of
    );
    ClassDB::bind_method(
        D_METHOD("set_target", "target"),
        &ParticipantView::set_target
    );
    ClassDB::bind_method(D_METHOD("get_target"), &ParticipantView::get_target);
    ClassDB::bind_method(
        D_METHOD("clear_target"),
        &ParticipantView::clear_target
    );
    ClassDB::bind_method(
        D_METHOD("forward_input", "event"),
        &ParticipantView::forward_input
    );
    ClassDB::bind_method(
        D_METHOD("get_inner_rect"),
        &ParticipantView::get_inner_rect
    );

    ClassDB::bind_method(
        D_METHOD("set_forwards_unhandled_input", "forwards"),
        &ParticipantView::set_forwards_unhandled_input
    );
    ClassDB::bind_method(
        D_METHOD("get_forwards_unhandled_input"),
        &ParticipantView::get_forwards_unhandled_input
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "forwards_unhandled_input"),
        "set_forwards_unhandled_input",
        "get_forwards_unhandled_input"
    );

    ADD_GROUP("Stretch", "stretch_");
    ClassDB::bind_method(
        D_METHOD("set_stretch_mode", "mode"),
        &ParticipantView::set_stretch_mode
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_mode"),
        &ParticipantView::get_stretch_mode
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
        &ParticipantView::set_stretch_aspect
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_aspect"),
        &ParticipantView::get_stretch_aspect
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
        &ParticipantView::set_stretch_scale_mode
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_scale_mode"),
        &ParticipantView::get_stretch_scale_mode
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
        &ParticipantView::set_stretch_scale
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_scale"),
        &ParticipantView::get_stretch_scale
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "stretch_scale"),
        "set_stretch_scale",
        "get_stretch_scale"
    );
    ClassDB::bind_method(
        D_METHOD("set_stretch_design_size", "size"),
        &ParticipantView::set_stretch_design_size
    );
    ClassDB::bind_method(
        D_METHOD("get_stretch_design_size"),
        &ParticipantView::get_stretch_design_size
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

    BIND_ENUM_CONSTANT(STRETCH_MODE_INHERIT);
    BIND_ENUM_CONSTANT(STRETCH_MODE_DISABLED);
    BIND_ENUM_CONSTANT(STRETCH_MODE_CANVAS_ITEMS);
    BIND_ENUM_CONSTANT(STRETCH_MODE_VIEWPORT);

    BIND_ENUM_CONSTANT(STRETCH_ASPECT_INHERIT);
    BIND_ENUM_CONSTANT(STRETCH_ASPECT_IGNORE);
    BIND_ENUM_CONSTANT(STRETCH_ASPECT_KEEP);
    BIND_ENUM_CONSTANT(STRETCH_ASPECT_KEEP_WIDTH);
    BIND_ENUM_CONSTANT(STRETCH_ASPECT_KEEP_HEIGHT);
    BIND_ENUM_CONSTANT(STRETCH_ASPECT_EXPAND);

    BIND_ENUM_CONSTANT(STRETCH_SCALE_MODE_INHERIT);
    BIND_ENUM_CONSTANT(STRETCH_SCALE_MODE_FRACTIONAL);
    BIND_ENUM_CONSTANT(STRETCH_SCALE_MODE_INTEGER);
}

} // namespace netw

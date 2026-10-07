#include "netw/api/nodes/view/stretch_viewport_container.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/rendering_server.hpp"
#include "godot/texture.hpp"
#include "godot/world.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_SIZE_CHANGED = "size_changed";
const char *SIG_CHILD_ENTERED_TREE = "child_entered_tree";

const Vector2i HIDDEN_TARGET_SIZE(2, 2);

void copy_render_settings(Viewport *p_from, SubViewport *p_to) {
    p_to->set_msaa_2d(p_from->get_msaa_2d());
    p_to->set_msaa_3d(p_from->get_msaa_3d());
    p_to->set_screen_space_aa(p_from->get_screen_space_aa());
    p_to->set_use_taa(p_from->is_using_taa());
    p_to->set_use_debanding(p_from->is_using_debanding());
    p_to->set_scaling_3d_mode(p_from->get_scaling_3d_mode());
    p_to->set_scaling_3d_scale(p_from->get_scaling_3d_scale());
    p_to->set_use_hdr_2d(p_from->is_using_hdr_2d());
    p_to->set_positional_shadow_atlas_size(
        p_from->get_positional_shadow_atlas_size()
    );
    p_to->set_physics_object_picking(p_from->get_physics_object_picking());
    p_to->set_default_canvas_item_texture_filter(
        p_from->get_default_canvas_item_texture_filter()
    );
    p_to->set_snap_2d_transforms_to_pixel(
        p_from->is_snap_2d_transforms_to_pixel_enabled()
    );
    p_to->set_snap_2d_vertices_to_pixel(
        p_from->is_snap_2d_vertices_to_pixel_enabled()
    );
}

void copy_fallback_environment(Viewport *p_from, SubViewport *p_to) {
    if (!p_to->is_using_own_world_3d()) {
        return;
    }
    const Ref<World3D> own = p_to->find_world_3d();
    const Ref<World3D> enclosing = p_from->find_world_3d();
    if (own.is_null() || enclosing.is_null()
        || own->get_fallback_environment().is_valid()) {
        return;
    }
    own->set_fallback_environment(enclosing->get_fallback_environment());
}

bool is_positional(const Ref<InputEvent> &p_event) {
    const InputEvent *event = p_event.ptr();
    return Object::cast_to<InputEventMouse>(event) != nullptr
        || Object::cast_to<InputEventScreenTouch>(event) != nullptr
        || Object::cast_to<InputEventScreenDrag>(event) != nullptr
        || Object::cast_to<InputEventGesture>(event) != nullptr;
}

} // namespace

void StretchViewportContainer::watch_enclosing_viewport() {
    Viewport *enclosing = get_viewport();
    if (enclosing == nullptr) {
        return;
    }
    const Callable follow = callable_mp(
        this,
        &StretchViewportContainer::follow_enclosing_viewport
    );
    if (!enclosing->is_connected(SIG_SIZE_CHANGED, follow)) {
        enclosing->connect(SIG_SIZE_CHANGED, follow);
    }
    watched_viewport = gd::instance_id(enclosing);
}

void StretchViewportContainer::unwatch_enclosing_viewport() {
    Viewport *watched
        = Object::cast_to<Viewport>(gd::object_of(watched_viewport));
    watched_viewport = ObjectID();
    if (watched == nullptr) {
        return;
    }
    const Callable follow = callable_mp(
        this,
        &StretchViewportContainer::follow_enclosing_viewport
    );
    if (watched->is_connected(SIG_SIZE_CHANGED, follow)) {
        watched->disconnect(SIG_SIZE_CHANGED, follow);
    }
}

void StretchViewportContainer::follow_enclosing_viewport() {
    fill_enclosing_viewport();
    lay_out(get_size());
}

void StretchViewportContainer::fill_enclosing_viewport() {
    Viewport *enclosing = get_viewport();
    if (enclosing == nullptr || is_laid_out_by_parent()) {
        return;
    }
    const Rect2 visible = enclosing->get_visible_rect();
    const Vector2 pixels_per_unit
        = enclosing->get_stretch_transform().get_scale();
    const Vector2 pixel_size = (visible.size * pixels_per_unit).round();
    set_anchors_preset(Control::PRESET_TOP_LEFT);
    set_position(visible.position);
    set_scale(Vector2(1, 1) / pixels_per_unit);
    lay_out(pixel_size);
    set_size(pixel_size);
}

bool StretchViewportContainer::is_laid_out_by_parent() const {
    return Object::cast_to<Control>(get_parent()) != nullptr;
}

void StretchViewportContainer::lay_out(const Vector2 &p_control_size) {
    const view::Stretch enclosing
        = view::stretch_from_window(Object::cast_to<Window>(get_viewport()));
    layout = view::layout_compute(
        is_laid_out_by_parent() ? view::stretch_filling(enclosing) : enclosing,
        p_control_size
    );
    for (int index = 0; index < get_child_count(); ++index) {
        SubViewport *target = Object::cast_to<SubViewport>(get_child(index));
        if (target == nullptr) {
            continue;
        }
        target->set_size_2d_override(layout.size_2d_override);
        target->set_size_2d_override_stretch(layout.override_stretch);
        show_or_hide(target);
    }
    queue_redraw();
}

void StretchViewportContainer::show_or_hide(SubViewport *p_target) {
    const bool shown = is_visible();
    const bool sized_by_engine = shown && is_laid_out_by_parent();
    set_stretch(sized_by_engine);
    set_stretch_shrink(1);
    if (!sized_by_engine) {
        p_target->set_size(shown ? layout.target_size : HIDDEN_TARGET_SIZE);
    }
    p_target->set_disable_input(!shown);
    p_target->set_as_audio_listener_2d(shown);
    p_target->set_as_audio_listener_3d(shown);
}

void StretchViewportContainer::_validate_property(
    PropertyInfo &p_property
) const {
    if (p_property.name == StringName("stretch")
        || p_property.name == StringName("stretch_shrink")) {
        p_property.usage = PROPERTY_USAGE_NONE;
    }
}

void StretchViewportContainer::adopt(Node *p_child) {
    SubViewport *target = Object::cast_to<SubViewport>(p_child);
    if (target == nullptr) {
        return;
    }
    Viewport *enclosing = get_viewport();
    if (enclosing != nullptr) {
        copy_render_settings(enclosing, target);
        copy_fallback_environment(enclosing, target);
    }
    target->set_update_mode(
        is_visible_in_tree() ? SubViewport::UPDATE_ALWAYS
                             : SubViewport::UPDATE_DISABLED
    );
    target->set_handle_input_locally(false);
    lay_out(get_size());
}

void StretchViewportContainer::draw_targets() {
    RenderingServer::get_singleton()->canvas_item_clear(get_canvas_item());
    for (int index = 0; index < get_child_count(); ++index) {
        SubViewport *target = Object::cast_to<SubViewport>(get_child(index));
        if (target != nullptr) {
            draw_texture_rect(target->get_texture(), layout.inner_rect, false);
        }
    }
}

void StretchViewportContainer::_notification(int p_what) {
    if (Engine::get_singleton()->is_editor_hint()) {
        return;
    }
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE: {
            const Callable adopting
                = callable_mp(this, &StretchViewportContainer::adopt);
            if (!is_connected(SIG_CHILD_ENTERED_TREE, adopting)) {
                connect(SIG_CHILD_ENTERED_TREE, adopting);
            }
            watch_enclosing_viewport();
            fill_enclosing_viewport();
        } break;
        case NOTIFICATION_EXIT_TREE:
            unwatch_enclosing_viewport();
            break;
        case NOTIFICATION_RESIZED:
        case NOTIFICATION_VISIBILITY_CHANGED:
            lay_out(get_size());
            break;
        case NOTIFICATION_DRAW:
            draw_targets();
            break;
        default:
            break;
    }
}

void StretchViewportContainer::NETW_GUI_INPUT(const Ref<InputEvent> &p_event) {
    if (!is_positional(p_event)) {
        return;
    }
    for (int index = 0; index < get_child_count(); ++index) {
        SubViewport *target = Object::cast_to<SubViewport>(get_child(index));
        if (target == nullptr || target->is_input_disabled()) {
            continue;
        }
        const Transform2D to_target_pixels = view::inner_rect_to_target_pixels(
            layout.inner_rect,
            target->get_size()
        );
        target->push_input(p_event->xformed_by(to_target_pixels), false);
    }
    accept_event();
}

} // namespace netw

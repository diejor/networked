#pragma once

#include "godot/gui.hpp"
#include "godot/input_event.hpp"
#include "godot/object.hpp"
#include "godot/viewport.hpp"
#include "netw/api/nodes/view/stretch.hpp"

namespace netw {

class ParticipantView : public godot::Control {
    GDCLASS(ParticipantView, godot::Control)

    godot::ObjectID target_id;
    view::Stretch declared;
    view::Layout layout;
    bool forwards_unhandled_input = false;
    bool laid_out = false;

    godot::SubViewport::UpdateMode held_update_mode
        = godot::SubViewport::UPDATE_DISABLED;
    godot::SubViewport::ClearMode held_clear_mode
        = godot::SubViewport::CLEAR_MODE_NEVER;
    godot::Vector2i held_size_2d_override;
    bool held_override_stretch = false;

    void claim(godot::SubViewport *p_target);
    void release(godot::SubViewport *p_target);
    void restore(godot::SubViewport *p_target);
    void relinquish();
    void watch_enclosing_viewport(bool p_watch);
    void fill_enclosing_viewport();
    void lay_out();
    void draw_target();
    void on_target_exiting();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void NETW_GUI_INPUT(const godot::Ref<godot::InputEvent> &p_event) override;
    void NETW_UNHANDLED_INPUT(
        const godot::Ref<godot::InputEvent> &p_event
    ) override;

    enum StretchMode {
        STRETCH_MODE_INHERIT = view::STRETCH_MODE_INHERIT,
        STRETCH_MODE_DISABLED = view::STRETCH_MODE_DISABLED,
        STRETCH_MODE_CANVAS_ITEMS = view::STRETCH_MODE_CANVAS_ITEMS,
        STRETCH_MODE_VIEWPORT = view::STRETCH_MODE_VIEWPORT,
    };

    enum StretchAspect {
        STRETCH_ASPECT_INHERIT = view::STRETCH_ASPECT_INHERIT,
        STRETCH_ASPECT_IGNORE = view::STRETCH_ASPECT_IGNORE,
        STRETCH_ASPECT_KEEP = view::STRETCH_ASPECT_KEEP,
        STRETCH_ASPECT_KEEP_WIDTH = view::STRETCH_ASPECT_KEEP_WIDTH,
        STRETCH_ASPECT_KEEP_HEIGHT = view::STRETCH_ASPECT_KEEP_HEIGHT,
        STRETCH_ASPECT_EXPAND = view::STRETCH_ASPECT_EXPAND,
    };

    enum StretchScaleMode {
        STRETCH_SCALE_MODE_INHERIT = view::STRETCH_SCALE_MODE_INHERIT,
        STRETCH_SCALE_MODE_FRACTIONAL = view::STRETCH_SCALE_MODE_FRACTIONAL,
        STRETCH_SCALE_MODE_INTEGER = view::STRETCH_SCALE_MODE_INTEGER,
    };

    static ParticipantView *owner_of(godot::SubViewport *p_target);

    void set_target(godot::SubViewport *p_target);
    godot::SubViewport *get_target() const;
    void clear_target();

    void forward_input(const godot::Ref<godot::InputEvent> &p_event);

    void set_forwards_unhandled_input(bool p_forwards);
    bool get_forwards_unhandled_input() const {
        return forwards_unhandled_input;
    }

    void set_stretch_mode(StretchMode p_mode);
    StretchMode get_stretch_mode() const {
        return StretchMode(declared.mode);
    }

    void set_stretch_aspect(StretchAspect p_aspect);
    StretchAspect get_stretch_aspect() const {
        return StretchAspect(declared.aspect);
    }

    void set_stretch_scale_mode(StretchScaleMode p_mode);
    StretchScaleMode get_stretch_scale_mode() const {
        return StretchScaleMode(declared.scale_mode);
    }

    void set_stretch_scale(double p_scale);
    double get_stretch_scale() const {
        return declared.scale;
    }

    void set_stretch_design_size(const godot::Vector2i &p_size);
    godot::Vector2i get_stretch_design_size() const {
        return declared.design_size;
    }

    godot::Rect2 get_inner_rect() const {
        return layout.inner_rect;
    }
};

} // namespace netw

VARIANT_ENUM_CAST(netw::ParticipantView::StretchMode);
VARIANT_ENUM_CAST(netw::ParticipantView::StretchAspect);
VARIANT_ENUM_CAST(netw::ParticipantView::StretchScaleMode);

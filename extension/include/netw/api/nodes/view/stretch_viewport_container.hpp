#pragma once

#include "godot/gui.hpp"
#include "godot/input_event.hpp"
#include "godot/object.hpp"
#include "godot/viewport.hpp"
#include "netw/api/nodes/view/stretch.hpp"

namespace netw {

class StretchViewportContainer : public godot::SubViewportContainer {
    GDCLASS(StretchViewportContainer, godot::SubViewportContainer)

    view::Layout layout;
    godot::ObjectID watched_viewport;

    void watch_enclosing_viewport();
    void unwatch_enclosing_viewport();
    void follow_enclosing_viewport();
    void fill_enclosing_viewport();
    bool is_laid_out_by_parent() const;
    void lay_out(const godot::Vector2 &p_control_size);
    void adopt(godot::Node *p_child);
    void show_or_hide(godot::SubViewport *p_target);
    void draw_targets();

protected:
    static void _bind_methods() {
    }
    void _notification(int p_what);
    void _validate_property(godot::PropertyInfo &p_property) const;

public:
    void NETW_GUI_INPUT(const godot::Ref<godot::InputEvent> &p_event) override;
};

} // namespace netw

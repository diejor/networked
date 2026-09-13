#pragma once

#include "godot/input_event.hpp"
#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/nodes/view/participant_view.hpp"

namespace netw {

class ParticipantWindow : public godot::Window {
    GDCLASS(ParticipantWindow, godot::Window)

    godot::ObjectID mounted_tree_id;
    int64_t peer_id = 0;
    godot::StringName username;

    view::Stretch declared;
    godot::LocalVector<godot::Ref<godot::InputEvent>> pending_input;
    godot::LocalVector<godot::Ref<godot::InputEvent>> draining;
    bool flush_queued = false;

    void flush_input();
    void apply_stretch();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    ParticipantWindow();

    void set_tiled_rect(const godot::Rect2i &p_rect);
    void send_input(const godot::Ref<godot::InputEvent> &p_event);

    void set_mounted_tree(godot::Node *p_tree);
    godot::Node *get_mounted_tree() const;

    void set_peer_id(int64_t p_peer_id) {
        peer_id = p_peer_id;
    }
    int64_t get_peer_id() const {
        return peer_id;
    }

    void set_username(const godot::StringName &p_username) {
        username = p_username;
    }
    godot::StringName get_username() const {
        return username;
    }

    void set_stretch_mode(ParticipantView::StretchMode p_mode);
    ParticipantView::StretchMode get_stretch_mode() const {
        return ParticipantView::StretchMode(declared.mode);
    }

    void set_stretch_aspect(ParticipantView::StretchAspect p_aspect);
    ParticipantView::StretchAspect get_stretch_aspect() const {
        return ParticipantView::StretchAspect(declared.aspect);
    }

    void set_stretch_scale_mode(ParticipantView::StretchScaleMode p_mode);
    ParticipantView::StretchScaleMode get_stretch_scale_mode() const {
        return ParticipantView::StretchScaleMode(declared.scale_mode);
    }

    void set_stretch_scale(double p_scale);
    double get_stretch_scale() const {
        return declared.scale;
    }

    void set_stretch_design_size(const godot::Vector2i &p_size);
    godot::Vector2i get_stretch_design_size() const {
        return declared.design_size;
    }
};

} // namespace netw

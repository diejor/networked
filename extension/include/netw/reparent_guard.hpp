#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"

namespace netw {

class ReparentGuard {
    struct Hold {
        godot::ObjectID body;
        godot::ObjectID tree;
        godot::Callable answered;
        godot::Callable frame_callback;
        int64_t prior_layer = 0;
        int64_t prior_mask = 0;
        int prior_mode = godot::Node::PROCESS_MODE_INHERIT;
        int frames = 0;
        int holders = 0;
        bool has_layer = false;
        bool has_mask = false;
    };

    mutable godot::RID_Owner<Hold> holds;
    godot::HashMap<uint64_t, godot::RID> by_body;

public:
    ReparentGuard();
    ~ReparentGuard();

    godot::RID open(godot::Node *p_body);
    bool is_open(const godot::RID &p_guard) const;
    bool tracks_physics(const godot::RID &p_guard) const;
    godot::Node *body_of(const godot::RID &p_guard) const;

    void arm(
        const godot::RID &p_guard,
        godot::Object *p_tree,
        int p_frames,
        const godot::Callable &p_answered,
        const godot::Callable &p_frame_callback
    );
    bool spend(const godot::RID &p_guard);
    godot::Callable take_answer(const godot::RID &p_guard);
    godot::Callable close_window(const godot::RID &p_guard);
    godot::Object *window_tree(const godot::RID &p_guard) const;

    void resume_processing(const godot::RID &p_guard);
    void release(const godot::RID &p_guard);
    void clear();
};

} // namespace netw

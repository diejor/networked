#pragma once

#include "godot/hash_map.hpp"
#include "godot/input_event.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "netw/api/nodes/view/participant_window.hpp"

namespace netw {

class ParticipantViewport : public godot::Node {
    GDCLASS(ParticipantViewport, godot::Node)

    godot::LocalVector<godot::ObjectID> slots;
    godot::HashMap<int64_t, godot::ObjectID> device_slots;
    bool embeds_subwindows = true;

    ParticipantWindow *slot_at(uint32_t p_index) const;
    void watch_enclosing_viewport(bool p_watch);
    void relayout();
    void forget(const godot::ObjectID &p_slot);

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void NETW_NODE_INPUT(const godot::Ref<godot::InputEvent> &p_event) override;

    ParticipantWindow *add_slot(ParticipantWindow *p_slot);
    void remove_slot(ParticipantWindow *p_slot);
    bool has_slot(ParticipantWindow *p_slot) const;
    godot::TypedArray<ParticipantWindow> get_slots() const;

    void assign_device(int64_t p_device_id, ParticipantWindow *p_slot);
    ParticipantWindow *device_slot(int64_t p_device_id) const;

    void set_embeds_subwindows(bool p_embeds);
    bool get_embeds_subwindows() const {
        return embeds_subwindows;
    }
};

} // namespace netw

#pragma once

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/view/participant_view.hpp"
#include "netw/api/scene_handle.hpp"

namespace netw {

class HostSceneView : public ParticipantView {
    GDCLASS(HostSceneView, ParticipantView)

    godot::ObjectID session_id;
    bool suppressed = false;
    bool announce_pending = false;

    NetwMultiplayer *session() const;
    void attach();
    void detach();
    void on_display_changed(godot::SubViewport *p_viewport);
    void on_roster_changed();
    void reannounce();
    void announce(godot::SubViewport *p_viewport);
    void activate(
        const godot::Ref<NetwEntity> &p_player,
        bool p_adopts_camera
    );

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    void set_suppressed(bool p_suppressed);
    bool get_suppressed() const {
        return suppressed;
    }
};

} // namespace netw

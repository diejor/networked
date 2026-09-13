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

    NetwMultiplayer *session() const;
    void attach();
    void detach();
    void on_display_changed(godot::SubViewport *p_viewport);
    void on_local_player_changed(const godot::Ref<NetwEntity> &p_player);
    void reannounce();
    void announce(godot::SubViewport *p_viewport);

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

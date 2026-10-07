#include "netw/api/netw_multiplayer.hpp"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/nodes/view/stretch_viewport_container.hpp"
#include "netw/log.hpp"
#include "netw/scene_core.hpp"

using namespace godot;
using namespace netw;

namespace netw {

RID NetwMultiplayer::scene_shown_derive() {
    if (scene_presented().is_valid()) {
        return scene_presented();
    }
    const Array live = scene_core->live_scenes();
    return live.size() == 1 ? RID(live[0]) : RID();
}

void NetwMultiplayer::scene_player_display_invalidate() {
    scene_presentation_settle();
    if (scene_player_display_pending) {
        return;
    }
    scene_player_display_pending = true;
    callable_mp(this, &NetwMultiplayer::scene_player_display_settle)
        .call_deferred();
}

void NetwMultiplayer::scene_player_display_settle() {
    scene_player_display_pending = false;
    StretchViewportContainer *shown
        = scene_view_of(scene_get_node(scene_shown_derive()));
    const ObjectID settled
        = shown != nullptr ? ObjectID(shown->get_instance_id()) : ObjectID();
    if (settled == scene_player_display_id) {
        return;
    }
    StretchViewportContainer *was = Object::cast_to<StretchViewportContainer>(
        gd::object_of(scene_player_display_id)
    );
    scene_player_display_id = settled;
    if (was != nullptr) {
        was->hide();
    }
    if (shown != nullptr) {
        shown->show();
    }
    NETW_TRACE(
        sys::SCENE,
        "This peer now shows %s.",
        shown != nullptr ? String(shown->get_name())
                         : String("no isolated world")
    );
}

} // namespace netw

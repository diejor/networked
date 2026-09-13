#include "netw/api/spawn_slot.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw {

void SpawnSlot::_bind_methods() {
    ClassDB::bind_static_method(
        "SpawnSlot",
        D_METHOD("in_scene", "scene"),
        &SpawnSlot::in_scene
    );
    ClassDB::bind_static_method(
        "SpawnSlot",
        D_METHOD("under", "parent"),
        &SpawnSlot::under
    );
    ClassDB::bind_static_method(
        "SpawnSlot",
        D_METHOD("for_scene", "session", "scene_stem"),
        &SpawnSlot::for_scene
    );

    ClassDB::bind_method(D_METHOD("has_scene"), &SpawnSlot::has_scene);
    ClassDB::bind_method(D_METHOD("is_valid"), &SpawnSlot::is_valid);
    ClassDB::bind_method(D_METHOD("get_scene"), &SpawnSlot::get_scene);
    ClassDB::bind_method(
        D_METHOD("place_player", "player"),
        &SpawnSlot::place_player
    );
}

Node *SpawnSlot::parent() const {
    return Object::cast_to<Node>(gd::object_of(parent_id));
}

Ref<SpawnSlot> SpawnSlot::in_scene(const Ref<NetwSceneHandle> &p_scene) {
    Ref<SpawnSlot> slot(memnew(SpawnSlot));
    slot->scene = p_scene;
    return slot;
}

Ref<SpawnSlot> SpawnSlot::under(Node *p_parent) {
    Ref<SpawnSlot> slot(memnew(SpawnSlot));
    slot->parent_id = gd::instance_id(p_parent);
    return slot;
}

Ref<SpawnSlot> SpawnSlot::for_scene(
    NetwMultiplayer *p_session,
    const StringName &p_scene_stem
) {
    if (p_session == nullptr) {
        return Ref<SpawnSlot>(memnew(SpawnSlot));
    }
    const RID scene_rid = p_session->scene_find(p_scene_stem);
    if (!scene_rid.is_valid() || !p_session->scene_is_declared(scene_rid)) {
        return Ref<SpawnSlot>(memnew(SpawnSlot));
    }
    const Ref<NetwEntity> view = p_session->entity_get_view(scene_rid);
    if (view.is_null()) {
        return Ref<SpawnSlot>(memnew(SpawnSlot));
    }
    return in_scene(view->get_scene());
}

bool SpawnSlot::has_scene() const {
    return scene.is_valid() && scene->get_is_declared();
}

bool SpawnSlot::is_valid() const {
    return has_scene() || parent() != nullptr;
}

Ref<NetwSceneHandle> SpawnSlot::get_scene() const {
    return has_scene() ? scene : Ref<NetwSceneHandle>();
}

void SpawnSlot::place_player(Node *p_player) {
    if (has_scene()) {
        scene->add_player(NetwEntity::of(p_player));
        return;
    }
    Node *host = parent();
    if (host != nullptr) {
        host->add_child(p_player);
    }
}

} // namespace netw

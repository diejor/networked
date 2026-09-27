#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwEntity;
class NetwMultiplayer;
class NetwPlayer;

class NetwSceneHandle : public godot::RefCounted {
    GDCLASS(NetwSceneHandle, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    void bind(NetwEntity *p_entity);

    godot::RID get_entity() const;
    bool get_is_declared() const;
    godot::Node *get_root() const;
    godot::Node *get_world() const;

    godot::StringName get_label() const;
    godot::TypedArray<NetwEntity> get_bodies() const;
    godot::TypedArray<NetwPlayer> get_viewers() const;
    godot::TypedArray<NetwEntity> get_local_bodies() const;

    godot::TypedArray<NetwEntity> get_entities() const;
    void observe(int64_t p_event, const godot::Callable &p_callback);
    void unobserve(int64_t p_event, const godot::Callable &p_callback);
    godot::Error watch(const godot::Ref<NetwPlayer> &p_player);
    godot::Error unwatch(const godot::Ref<NetwPlayer> &p_player);
    bool is_watching(const godot::Ref<NetwPlayer> &p_player) const;

    void announce_body(const godot::Ref<NetwEntity> &p_body, bool p_present);
    void announce_viewer(
        const godot::Ref<NetwPlayer> &p_player,
        bool p_present
    );

    godot::Ref<NetwEntity> entity_of_handle() const;

private:
    struct Relay {
        int64_t event;
        godot::Callable target;
        godot::Callable mounted;
    };

    godot::ObjectID entity_id;
    godot::LocalVector<Relay> relays;

    godot::Variant translate_edge(
        bool p_present,
        const godot::Variant &p_subject,
        int64_t p_event,
        const godot::Callable &p_target
    );

    NetwMultiplayer *core() const;
    godot::Node *scene_node() const;
};

godot::Ref<NetwSceneHandle> build_scene_handle(godot::Object *p_entity);

} // namespace netw

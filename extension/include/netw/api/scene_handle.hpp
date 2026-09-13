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
class NetwParticipant;
class NetwReparentOpts;

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
    godot::Error add_player(const godot::Ref<NetwEntity> &p_player);

    godot::StringName get_label() const;
    godot::TypedArray<NetwEntity> get_players() const;
    godot::TypedArray<NetwParticipant> get_participants() const;
    godot::Ref<NetwEntity> get_local_player() const;

    godot::TypedArray<NetwEntity> get_entities() const;
    void observe(int64_t p_event, const godot::Callable &p_callback);
    void unobserve(int64_t p_event, const godot::Callable &p_callback);
    godot::Ref<NetwPromise> move(
        const godot::Ref<NetwEntity> &p_entity,
        const godot::Ref<NetwReparentOpts> &p_opts
    );

    godot::Error admit(const godot::Ref<NetwParticipant> &p_participant);
    godot::Error release(const godot::Ref<NetwParticipant> &p_participant);
    bool admits(const godot::Ref<NetwParticipant> &p_participant) const;

    void announce_player(
        const godot::Ref<NetwEntity> &p_player,
        bool p_present
    );
    void announce_participant(
        const godot::Ref<NetwParticipant> &p_participant,
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

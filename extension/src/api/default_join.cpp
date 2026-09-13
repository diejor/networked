#include "netw/api/default_join.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

NetwMultiplayer *NetwDefaultJoin::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwDefaultJoin::bind_session(NetwMultiplayer *p_session) {
    session_id = gd::instance_id(p_session);
}

Ref<NetwPromise> NetwDefaultJoin::spawn(
    const Ref<NetwParticipant> &p_participant,
    const StringName &p_scene_stem,
    const NodePath &p_spawner_path
) {
    NetwMultiplayer *api = session();
    if (p_scene_stem == StringName() || api == nullptr
        || p_participant.is_null()) {
        return NetwPromise::resolved(Variant());
    }
    Node *container = api->scene_activate(p_scene_stem);
    NETW_ERR_COND_V(
        container == nullptr,
        NetwPromise::resolved(Variant()),
        sys::SPAWN,
        "join activated scene '%s' and got no container",
        String(p_scene_stem)
    );
    const Ref<NetwEntity> entered = NetwEntity::of(container);
    const Ref<NetwSceneHandle> scene
        = entered.is_valid() ? entered->get_scene() : Ref<NetwSceneHandle>();
    Node *level = scene.is_valid() ? scene->get_root() : nullptr;
    Node *template_node
        = level != nullptr ? level->get_node_or_null(p_spawner_path) : nullptr;
    const Ref<NetwEntity> spawner = NetwEntity::ensure(template_node);
    NETW_ERR_COND_V(
        spawner.is_null(),
        NetwPromise::resolved(Variant()),
        sys::SPAWN,
        "join args' spawner_path '%s' resolved to no template node",
        String(p_spawner_path)
    );
    Node *player = spawner->instantiate_player(p_participant);

    Ref<NetwPromise> seated;
    seated.instantiate();
    const Ref<NetwEntity> spawned = NetwEntity::of(player);
    const Ref<NetwPersistenceEngine> engine = spawned.is_valid()
        ? spawned->get_persistence()
        : Ref<NetwPersistenceEngine>();
    if (engine.is_valid() && engine->wants_spawn_hydration()) {
        const Ref<NetwPromise> hydrated = engine->hydrate();
        if (hydrated.is_valid()) {
            hydrated->when_settled(
                callable_mp(this, &NetwDefaultJoin::settle_entry)
                    .bind(player, container, seated)
            );
            return seated;
        }
    }
    settle_entry(player, container, seated);
    return seated;
}

void NetwDefaultJoin::settle_entry(
    Node *p_player,
    Node *p_container,
    const Ref<NetwPromise> &p_answer
) {
    const Ref<NetwEntity> host = NetwEntity::of(p_container);
    const Ref<NetwSceneHandle> entered
        = host.is_valid() ? host->get_scene() : Ref<NetwSceneHandle>();
    if (entered.is_valid()) {
        entered->add_player(NetwEntity::of(p_player));
    }
    p_answer->resolve(entered);
}

void NetwDefaultJoin::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("spawn", "participant", "scene_stem", "spawner_path"),
        &NetwDefaultJoin::spawn
    );
}

} // namespace netw

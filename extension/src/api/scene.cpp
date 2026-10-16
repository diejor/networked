#include "netw/api/scene.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SNAME_BODY_ENTERED = "body_entered";
const char *SNAME_BODY_LEFT = "body_left";
const char *SNAME_VIEWER_ENTERED = "viewer_entered";
const char *SNAME_VIEWER_EXITED = "viewer_exited";

} // namespace

void NetwScene::bind(NetwEntity *p_entity) {
    entity_id = gd::instance_id(p_entity);
}

Ref<NetwEntity> NetwScene::entity_of_handle() const {
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(gd::object_of(entity_id))
    );
}

NetwMultiplayer *NetwScene::core() const {
    const Ref<NetwEntity> bound = entity_of_handle();
    return bound.is_valid() ? NetwEntity::session_core_for(bound->get_owner())
                            : nullptr;
}

RID NetwScene::get_rid() const {
    const Ref<NetwEntity> bound = entity_of_handle();
    NetwMultiplayer *session = core();
    if (session == nullptr || bound.is_null()) {
        return RID();
    }
    Node *owner = bound->get_owner();
    if (owner == nullptr) {
        return RID();
    }
    return session->scene_of(session->entity_of(owner));
}

Node *NetwScene::scene_node() const {
    NetwMultiplayer *session = core();
    if (session != nullptr) {
        const RID resolved = get_rid();
        if (resolved.is_valid()) {
            return session->scene_entity_node(resolved);
        }
    }
    const Ref<NetwEntity> bound = entity_of_handle();
    if (bound.is_null()) {
        return nullptr;
    }
    const StringName marker = NetwMultiplayer::scene_container_meta();
    Node *walker = bound->get_owner();
    while (walker != nullptr) {
        if (walker->has_meta(marker)) {
            return walker;
        }
        walker = walker->get_parent();
    }
    return nullptr;
}

Ref<NetwEntity> NetwScene::get_entity() const {
    return NetwEntity::of(scene_node());
}

bool NetwScene::get_is_declared() const {
    return get_rid().is_valid() || scene_node() != nullptr;
}

Node *NetwScene::get_root() const {
    return scene_node();
}

Node *NetwScene::get_world() const {
    return NetwMultiplayer::scene_world_of(scene_node());
}

StringName NetwScene::get_label() const {
    NetwMultiplayer *session = core();
    return session != nullptr ? session->scene_get_label(get_rid())
                              : StringName();
}

TypedArray<NetwEntity> NetwScene::get_bodies() const {
    NetwMultiplayer *session = core();
    return session != nullptr ? session->scene_get_bodies(get_rid())
                              : TypedArray<NetwEntity>();
}

TypedArray<NetwPlayer> NetwScene::get_viewers() const {
    NetwMultiplayer *session = core();
    return session != nullptr ? session->scene_get_viewers(get_rid())
                              : TypedArray<NetwPlayer>();
}

TypedArray<NetwEntity> NetwScene::get_local_bodies() const {
    NetwMultiplayer *session = core();
    return session != nullptr ? session->scene_get_local_bodies(get_rid())
                              : TypedArray<NetwEntity>();
}

TypedArray<NetwEntity> NetwScene::get_entities() const {
    TypedArray<NetwEntity> views;
    NetwMultiplayer *session = core();
    if (session == nullptr) {
        return views;
    }
    const TypedArray<RID> rows = session->scene_get_entities(get_rid());
    views.resize(rows.size());
    for (int at = 0; at < rows.size(); ++at) {
        views[at] = session->entity_get_view(RID(rows[at]));
    }
    return views;
}

Variant NetwScene::translate_edge(
    bool p_present,
    const Variant &p_subject,
    int64_t p_event,
    const Callable &p_target
) {
    NetwMultiplayer *session = core();
    Variant subject;
    if (session != nullptr) {
        if (p_event == NetwScene::SCENE_EVENT_VIEWER) {
            subject = session->player_of(int64_t(p_subject));
        } else {
            subject = session->entity_get_view(RID(p_subject));
        }
    }
    Array args;
    args.push_back(p_present);
    args.push_back(subject);
    return p_target.callv(args);
}

void NetwScene::observe(
    NetwScene::SceneEvent p_event,
    const Callable &p_callback
) {
    NetwMultiplayer *session = core();
    if (session == nullptr || !p_callback.is_valid()) {
        return;
    }
    for (uint32_t at = 0; at < relays.size(); ++at) {
        if (relays[at].event == p_event && relays[at].target == p_callback) {
            return;
        }
    }
    const Callable mounted = callable_mp(this, &NetwScene::translate_edge)
                                 .bind(int64_t(p_event), p_callback);
    relays.push_back(Relay{int64_t(p_event), p_callback, mounted});
    session->scene_observe(get_rid(), p_event, mounted);
}

void NetwScene::unobserve(
    NetwScene::SceneEvent p_event,
    const Callable &p_callback
) {
    for (uint32_t at = 0; at < relays.size(); ++at) {
        if (relays[at].event != int64_t(p_event)
            || relays[at].target != p_callback) {
            continue;
        }
        NetwMultiplayer *session = core();
        if (session != nullptr) {
            session->scene_unobserve(get_rid(), p_event, relays[at].mounted);
        }
        relays.remove_at(at);
        return;
    }
}

Error NetwScene::watch(const Ref<NetwPlayer> &p_player) {
    NetwMultiplayer *session = core();
    if (session == nullptr || !session->player_holds(p_player)) {
        return ERR_INVALID_PARAMETER;
    }
    return session->scene_watch(get_rid(), p_player->get_peer_id());
}

Error NetwScene::unwatch(const Ref<NetwPlayer> &p_player) {
    NetwMultiplayer *session = core();
    if (session == nullptr || !session->player_holds(p_player)) {
        return ERR_INVALID_PARAMETER;
    }
    return session->scene_unwatch(get_rid(), p_player->get_peer_id())
        ? OK
        : ERR_DOES_NOT_EXIST;
}

bool NetwScene::is_watching(const Ref<NetwPlayer> &p_player) const {
    NetwMultiplayer *session = core();
    return session != nullptr && p_player.is_valid()
        && session->scene_subscribes(get_rid(), p_player->get_peer_id());
}

void NetwScene::announce_body(const Ref<NetwEntity> &p_body, bool p_present) {
    emit_signal(p_present ? SNAME_BODY_ENTERED : SNAME_BODY_LEFT, p_body);
}

void NetwScene::announce_viewer(
    const Ref<NetwPlayer> &p_player,
    bool p_present
) {
    emit_signal(
        p_present ? SNAME_VIEWER_ENTERED : SNAME_VIEWER_EXITED,
        p_player
    );
}

void NetwScene::_bind_methods() {
    BIND_ENUM_CONSTANT(SCENE_EVENT_VIEWER);
    BIND_ENUM_CONSTANT(SCENE_EVENT_BODY);
    BIND_ENUM_CONSTANT(SCENE_EVENT_ENTITY);
    BIND_ENUM_CONSTANT(SCENE_CHANGE_SESSION);
    BIND_ENUM_CONSTANT(SCENE_CHANGE_PLAYER);
    BIND_ENUM_CONSTANT(SCENE_CHANGE_SCENE);
    BIND_ENUM_CONSTANT(SCENE_ISOLATION_NONE);
    BIND_ENUM_CONSTANT(SCENE_ISOLATION_OWN_WORLD);
    ClassDB::bind_method(D_METHOD("get_rid"), &NetwScene::get_rid);
    ADD_PROPERTY(PropertyInfo(Variant::RID, "rid"), "", "get_rid");
    ClassDB::bind_method(D_METHOD("get_entity"), &NetwScene::get_entity);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "entity",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwEntity"
        ),
        "",
        "get_entity"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_declared"),
        &NetwScene::get_is_declared
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_declared"),
        "",
        "get_is_declared"
    );
    ClassDB::bind_method(D_METHOD("get_root"), &NetwScene::get_root);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "root",
            PROPERTY_HINT_NODE_TYPE,
            "Node",
            PROPERTY_USAGE_DEFAULT,
            "Node"
        ),
        "",
        "get_root"
    );
    ClassDB::bind_method(D_METHOD("get_world"), &NetwScene::get_world);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "world",
            PROPERTY_HINT_NODE_TYPE,
            "SubViewport",
            PROPERTY_USAGE_DEFAULT,
            "SubViewport"
        ),
        "",
        "get_world"
    );
    ClassDB::bind_method(D_METHOD("get_label"), &NetwScene::get_label);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "label"), "", "get_label");
    ClassDB::bind_method(D_METHOD("get_bodies"), &NetwScene::get_bodies);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "bodies",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEntity"
        ),
        "",
        "get_bodies"
    );
    ClassDB::bind_method(D_METHOD("get_viewers"), &NetwScene::get_viewers);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "viewers",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwPlayer"
        ),
        "",
        "get_viewers"
    );
    ClassDB::bind_method(
        D_METHOD("get_local_bodies"),
        &NetwScene::get_local_bodies
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "local_bodies",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEntity"
        ),
        "",
        "get_local_bodies"
    );
    ClassDB::bind_method(D_METHOD("get_entities"), &NetwScene::get_entities);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "entities",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwEntity"
        ),
        "",
        "get_entities"
    );
    ClassDB::bind_method(
        D_METHOD("observe", "event", "callback"),
        &NetwScene::observe
    );
    ClassDB::bind_method(
        D_METHOD("unobserve", "event", "callback"),
        &NetwScene::unobserve
    );
    ClassDB::bind_method(D_METHOD("watch", "player"), &NetwScene::watch);
    ClassDB::bind_method(D_METHOD("unwatch", "player"), &NetwScene::unwatch);
    ClassDB::bind_method(
        D_METHOD("is_watching", "player"),
        &NetwScene::is_watching
    );

    ADD_SIGNAL(MethodInfo(
        SNAME_BODY_ENTERED,
        PropertyInfo(
            Variant::OBJECT,
            "body",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwEntity"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SNAME_BODY_LEFT,
        PropertyInfo(
            Variant::OBJECT,
            "body",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwEntity"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SNAME_VIEWER_ENTERED,
        PropertyInfo(
            Variant::OBJECT,
            "player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPlayer"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SNAME_VIEWER_EXITED,
        PropertyInfo(
            Variant::OBJECT,
            "player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPlayer"
        )
    ));
}

Ref<NetwScene> build_scene_handle(Object *p_entity) {
    Ref<NetwScene> made;
    made.instantiate();
    made->bind(Object::cast_to<NetwEntity>(p_entity));
    return made;
}

} // namespace netw

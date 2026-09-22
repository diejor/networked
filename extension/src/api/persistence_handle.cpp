#include "netw/api/persistence_handle.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *SIG_PERSIST_LOADED = "persist_loaded";
constexpr const char *SIG_PERSIST_SAVED = "persist_saved";

Ref<NetwPromise> unbound() {
    return NetwPromise::rejected(
        ERR_UNCONFIGURED,
        "this entity binds no persistence, so there is no row to reach"
    );
}

} // namespace

void NetwPersistenceHandle::bind(NetwEntity *p_entity) {
    entity_id = gd::instance_id(p_entity);
    entity = p_entity != nullptr ? p_entity->get_rid_handle() : RID();
}

NetwMultiplayer *NetwPersistenceHandle::session() const {
    NetwEntity *held = Object::cast_to<NetwEntity>(gd::object_of(entity_id));
    return held != nullptr ? NetwEntity::session_core_for(held->get_owner())
                           : nullptr;
}

void NetwPersistenceHandle::bind_session(NetwMultiplayer *p_session) {
    if (p_session == nullptr) {
        return;
    }
    const ObjectID session_id = gd::instance_id(p_session);
    if (session_id == relay_session_id) {
        return;
    }
    relay_session_id = session_id;
    p_session->connect(
        StringName(SIG_PERSIST_LOADED),
        callable_mp(this, &NetwPersistenceHandle::relay_loaded)
    );
    p_session->connect(
        StringName(SIG_PERSIST_SAVED),
        callable_mp(this, &NetwPersistenceHandle::relay_saved)
    );
}

void NetwPersistenceHandle::relay_loaded(const RID &p_entity, bool p_found) {
    if (p_entity == entity) {
        emit_signal(StringName("loaded"), p_found);
    }
}

void NetwPersistenceHandle::relay_saved(const RID &p_entity) {
    if (p_entity == entity) {
        emit_signal(StringName("saved"));
    }
}

Ref<NetwPromise> NetwPersistenceHandle::load() {
    NetwMultiplayer *api = session();
    if (api == nullptr || !entity.is_valid()) {
        return unbound();
    }
    return api->persist_load(entity);
}

Ref<NetwPromise> NetwPersistenceHandle::save() {
    NetwMultiplayer *api = session();
    if (api == nullptr || !entity.is_valid()) {
        return unbound();
    }
    return api->persist_save(entity);
}

bool NetwPersistenceHandle::get_dirty() {
    NetwMultiplayer *api = session();
    return api != nullptr && entity.is_valid() && api->persist_is_dirty(entity);
}

StringName NetwPersistenceHandle::get_record_id() {
    NetwMultiplayer *api = session();
    return api != nullptr && entity.is_valid()
        ? api->persist_get_record_id(entity)
        : StringName();
}

StringName NetwPersistenceHandle::get_database() {
    NetwMultiplayer *api = session();
    if (api == nullptr || !entity.is_valid()) {
        return StringName();
    }
    const RID binding = api->persist_binding_of_entity(entity);
    return binding.is_valid()
        ? api->database_get_name(api->persist_get_database(binding))
        : StringName();
}

void NetwPersistenceHandle::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load"), &NetwPersistenceHandle::load);
    ClassDB::bind_method(D_METHOD("save"), &NetwPersistenceHandle::save);

    ClassDB::bind_method(
        D_METHOD("get_dirty"),
        &NetwPersistenceHandle::get_dirty
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::BOOL,
            "dirty",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_dirty"
    );

    ClassDB::bind_method(
        D_METHOD("get_record_id"),
        &NetwPersistenceHandle::get_record_id
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::STRING_NAME,
            "record_id",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_record_id"
    );

    ClassDB::bind_method(
        D_METHOD("get_database"),
        &NetwPersistenceHandle::get_database
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::STRING_NAME,
            "database",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_database"
    );

    ADD_SIGNAL(MethodInfo("loaded", PropertyInfo(Variant::BOOL, "found")));
    ADD_SIGNAL(MethodInfo("saved"));
}

Ref<NetwPersistenceHandle> build_persistence_handle(Object *p_entity) {
    Ref<NetwPersistenceHandle> made;
    made.instantiate();
    made->bind(Object::cast_to<NetwEntity>(p_entity));
    return made;
}

} // namespace netw

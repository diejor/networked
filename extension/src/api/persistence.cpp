#include "netw/api/persistence.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

Ref<NetwPromise> unbound() {
    return NetwPromise::rejected(
        ERR_UNCONFIGURED,
        "This root has no saved record. Configure it with "
        "Netw.configure_persistence."
    );
}

} // namespace

void NetwPersistence::bind(NetwEntity *p_entity) {
    entity_id = gd::instance_id(p_entity);
    entity = p_entity != nullptr ? p_entity->get_rid_handle() : RID();
}

NetwMultiplayer *NetwPersistence::session() const {
    NetwEntity *held = Object::cast_to<NetwEntity>(gd::object_of(entity_id));
    if (held == nullptr) {
        return nullptr;
    }
    NetwMultiplayer *mounted = NetwEntity::session_core_for(held->get_owner());
    if (mounted != nullptr) {
        return mounted;
    }
    return Object::cast_to<NetwMultiplayer>(held->get_multiplayer().ptr());
}

void NetwPersistence::announce_loaded(const RID &p_entity, bool p_found) {
    if (p_entity == entity) {
        emit_signal(StringName("loaded"), p_found);
    }
}

void NetwPersistence::announce_saved(const RID &p_entity) {
    if (p_entity == entity) {
        emit_signal(StringName("saved"));
    }
}

Ref<NetwPromise> NetwPersistence::load() {
    NetwMultiplayer *api = session();
    if (api == nullptr || !entity.is_valid()) {
        return unbound();
    }
    return api->persist_load(entity);
}

Ref<NetwPromise> NetwPersistence::save() {
    NetwMultiplayer *api = session();
    if (api == nullptr || !entity.is_valid()) {
        return unbound();
    }
    return api->persist_save(entity);
}

bool NetwPersistence::get_dirty() {
    NetwMultiplayer *api = session();
    return api != nullptr && entity.is_valid() && api->persist_is_dirty(entity);
}

StringName NetwPersistence::get_record_id() {
    NetwMultiplayer *api = session();
    return api != nullptr && entity.is_valid()
        ? api->persist_get_record_id(entity)
        : StringName();
}

StringName NetwPersistence::get_database() {
    NetwMultiplayer *api = session();
    if (api == nullptr || !entity.is_valid()) {
        return StringName();
    }
    const RID binding = api->persist_binding_of_entity(entity);
    return binding.is_valid()
        ? api->database_get_name(api->persist_get_database(binding))
        : StringName();
}

Ref<NetwSchema> NetwPersistence::get_schema() {
    NetwMultiplayer *api = session();
    const persist::Binding *held = api != nullptr && entity.is_valid()
        ? api->get_bindings()->at(api->persist_binding_of_entity(entity))
        : nullptr;
    if (held == nullptr) {
        schema.unref();
        schema_id = RID();
        return Ref<NetwSchema>();
    }
    if (schema_id != held->schema) {
        const table::SchemaRecord *record
            = api->get_schema_core()->record_of(held->schema);
        schema = record != nullptr ? NetwSchema::from_record(*record)
                                   : Ref<NetwSchema>();
        schema_id = held->schema;
    }
    return schema;
}

void NetwPersistence::_bind_methods() {
    ClassDB::bind_method(D_METHOD("load"), &NetwPersistence::load);
    ClassDB::bind_method(D_METHOD("save"), &NetwPersistence::save);

    ClassDB::bind_method(D_METHOD("get_dirty"), &NetwPersistence::get_dirty);
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
        &NetwPersistence::get_record_id
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
        &NetwPersistence::get_database
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

    ClassDB::bind_method(D_METHOD("get_schema"), &NetwPersistence::get_schema);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "schema",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSchema",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_schema"
    );

    ADD_SIGNAL(MethodInfo("loaded", PropertyInfo(Variant::BOOL, "found")));
    ADD_SIGNAL(MethodInfo("saved"));
}

Ref<NetwPersistence> build_persistence_handle(Object *p_entity) {
    Ref<NetwPersistence> made;
    made.instantiate();
    made->bind(Object::cast_to<NetwEntity>(p_entity));
    return made;
}

} // namespace netw

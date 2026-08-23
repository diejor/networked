#include "netw/api/netw_identity.hpp"

#include "godot/class_db.hpp"
#include "netw/api/entity.hpp"

using namespace godot;

namespace netw {

StringName NetwIdentity::get_username() const {
    return username;
}

void NetwIdentity::set_username(const StringName &p_value) {
    username = p_value;
}

String NetwIdentity::get_external_id() const {
    return external_id;
}

void NetwIdentity::set_external_id(const String &p_value) {
    external_id = p_value;
}

StringName NetwIdentity::get_service() const {
    return service;
}

void NetwIdentity::set_service(const StringName &p_value) {
    service = p_value;
}

Dictionary NetwIdentity::get_metadata() const {
    return metadata;
}

void NetwIdentity::set_metadata(const Dictionary &p_value) {
    metadata = p_value;
}

String NetwIdentity::username_of(Object *p_node) {
    Node *node = Object::cast_to<Node>(p_node);
    if (node == nullptr) {
        return String();
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_valid() && !String(entity->get_entity_id()).is_empty()) {
        return String(entity->get_entity_id());
    }
    return String(node->get_name()).get_slice("|", 0);
}

Variant NetwIdentity::stable_id_of(Object *p_node) {
    Node *node = Object::cast_to<Node>(p_node);
    if (node == nullptr) {
        return String();
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_valid() && entity->get_peer_id() != 0) {
        return entity->get_peer_id();
    }
    const int64_t parsed = NetwEntity::parse_peer(String(node->get_name()));
    if (parsed != 0) {
        return parsed;
    }
    return String(node->get_path());
}

void NetwIdentity::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_username"),
        &NetwIdentity::get_username
    );
    ClassDB::bind_method(
        D_METHOD("set_username", "value"),
        &NetwIdentity::set_username
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "username"),
        "set_username",
        "get_username"
    );

    ClassDB::bind_method(
        D_METHOD("get_external_id"),
        &NetwIdentity::get_external_id
    );
    ClassDB::bind_method(
        D_METHOD("set_external_id", "value"),
        &NetwIdentity::set_external_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "external_id"),
        "set_external_id",
        "get_external_id"
    );

    ClassDB::bind_method(D_METHOD("get_service"), &NetwIdentity::get_service);
    ClassDB::bind_method(
        D_METHOD("set_service", "value"),
        &NetwIdentity::set_service
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "service"),
        "set_service",
        "get_service"
    );

    ClassDB::bind_method(D_METHOD("get_metadata"), &NetwIdentity::get_metadata);
    ClassDB::bind_method(
        D_METHOD("set_metadata", "value"),
        &NetwIdentity::set_metadata
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "metadata"),
        "set_metadata",
        "get_metadata"
    );

    ClassDB::bind_static_method(
        "NetwIdentity",
        D_METHOD("username_of", "node"),
        &NetwIdentity::username_of
    );
    ClassDB::bind_static_method(
        "NetwIdentity",
        D_METHOD("stable_id_of", "node"),
        &NetwIdentity::stable_id_of
    );
}

} // namespace netw

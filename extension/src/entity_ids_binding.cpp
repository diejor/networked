#include "netw/entity_ids_binding.hpp"

#include "godot/class_db.hpp"
#include "netw/entity_ids.hpp"

using namespace godot;

namespace netw {

RID NetwEntityIds::mint() {
    return entity_ids::mint();
}

bool NetwEntityIds::is_minted(const RID &p_entity) {
    return entity_ids::minted(p_entity);
}

bool NetwEntityIds::retain(const RID &p_entity) {
    return entity_ids::retain(p_entity);
}

void NetwEntityIds::release(const RID &p_entity) {
    entity_ids::release(p_entity);
}

int NetwEntityIds::holders(const RID &p_entity) {
    return entity_ids::holders(p_entity);
}

int NetwEntityIds::outstanding() {
    return entity_ids::outstanding();
}

void NetwEntityIds::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwEntityIds",
        D_METHOD("mint"),
        &NetwEntityIds::mint
    );
    ClassDB::bind_static_method(
        "NetwEntityIds",
        D_METHOD("is_minted", "entity"),
        &NetwEntityIds::is_minted
    );
    ClassDB::bind_static_method(
        "NetwEntityIds",
        D_METHOD("retain", "entity"),
        &NetwEntityIds::retain
    );
    ClassDB::bind_static_method(
        "NetwEntityIds",
        D_METHOD("release", "entity"),
        &NetwEntityIds::release
    );
    ClassDB::bind_static_method(
        "NetwEntityIds",
        D_METHOD("holders", "entity"),
        &NetwEntityIds::holders
    );
    ClassDB::bind_static_method(
        "NetwEntityIds",
        D_METHOD("outstanding"),
        &NetwEntityIds::outstanding
    );
}

} // namespace netw

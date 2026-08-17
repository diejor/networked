#include "netw/entity_stage.hpp"

#include "godot/class_db.hpp"

namespace netw {

using namespace godot;

bool NetwEntityStage::edge_is_legal(int64_t p_from, int64_t p_to) {
    switch (EntityStage(p_from)) {
        case EntityStage::UNBOUND:
            return p_to == int(EntityStage::TEMPLATE)
                || p_to == int(EntityStage::ARMED)
                || p_to == int(EntityStage::DESPAWNING);
        case EntityStage::ARMED:
            return p_to == int(EntityStage::LIVE)
                || p_to == int(EntityStage::DESPAWNING);
        case EntityStage::LIVE:
            return p_to == int(EntityStage::DESPAWNING);
        case EntityStage::DESPAWNING:
            return p_to == int(EntityStage::LINGERING)
                || p_to == int(EntityStage::FREED);
        case EntityStage::LINGERING:
            return p_to == int(EntityStage::FREED);
        case EntityStage::TEMPLATE:
        case EntityStage::FREED:
            return false;
    }
    return false;
}

bool NetwEntityStage::can_begin_despawn(int64_t p_stage) {
    return p_stage == int(EntityStage::UNBOUND)
        || p_stage == int(EntityStage::ARMED)
        || p_stage == int(EntityStage::LIVE);
}

void NetwEntityStage::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwEntityStage",
        D_METHOD("edge_is_legal", "from", "to"),
        &NetwEntityStage::edge_is_legal
    );
    ClassDB::bind_static_method(
        "NetwEntityStage",
        D_METHOD("can_begin_despawn", "stage"),
        &NetwEntityStage::can_begin_despawn
    );
}

} // namespace netw

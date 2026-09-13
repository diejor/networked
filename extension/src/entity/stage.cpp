#include "netw/entity/stage.hpp"

namespace netw::entity {

bool stage_edge_is_legal(int64_t p_from, int64_t p_to) {
    switch (Stage(p_from)) {
        case Stage::UNBOUND:
            return p_to == int(Stage::TEMPLATE) || p_to == int(Stage::ARMED)
                || p_to == int(Stage::DESPAWNING);
        case Stage::ARMED:
            return p_to == int(Stage::LIVE) || p_to == int(Stage::DESPAWNING);
        case Stage::LIVE:
            return p_to == int(Stage::DESPAWNING);
        case Stage::DESPAWNING:
            return p_to == int(Stage::LINGERING) || p_to == int(Stage::FREED);
        case Stage::LINGERING:
            return p_to == int(Stage::FREED);
        case Stage::TEMPLATE:
        case Stage::FREED:
            return false;
    }
    return false;
}

bool stage_can_begin_despawn(int64_t p_stage) {
    return p_stage == int(Stage::UNBOUND) || p_stage == int(Stage::ARMED)
        || p_stage == int(Stage::LIVE);
}

} // namespace netw::entity

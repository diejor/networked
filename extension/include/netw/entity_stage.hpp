#pragma once

#include <cstdint>

namespace netw {

enum class EntityStage : int {
    UNBOUND = 0,
    TEMPLATE = 1,
    ARMED = 2,
    LIVE = 3,
    DESPAWNING = 4,
    LINGERING = 5,
    FREED = 6,
};

bool stage_edge_is_legal(int64_t p_from, int64_t p_to);

bool stage_can_begin_despawn(int64_t p_stage);

} // namespace netw

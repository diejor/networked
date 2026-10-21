#pragma once

#include <cstdint>

namespace netw::entity {

enum class Stage : int {
    UNBOUND = 0,
    ARMED = 1,
    LIVE = 2,
    DESPAWNING = 3,
    LINGERING = 4,
    FREED = 5,
};

bool stage_edge_is_legal(int64_t p_from, int64_t p_to);

bool stage_can_begin_despawn(int64_t p_stage);

bool stage_is_leaving(int64_t p_stage);

} // namespace netw::entity

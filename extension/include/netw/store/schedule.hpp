#pragma once

#include <cstdint>

namespace netw::store {

class World;
class Table;

enum class RowClass : uint8_t { RESTING, MOVING, INTERACTING, PLAYER };

enum SlotMark : uint8_t {
    MARK_PLAYER = 1,
    MARK_INTERACTING = 2,
};

constexpr int64_t MOVING_TICKS = 8;

float class_weight(RowClass p_class);

const char *class_name(RowClass p_class);

RowClass row_class(
    const World &p_world,
    const Table &p_table,
    uint32_t p_row,
    int64_t p_tick,
    bool p_refresh
);

float importance(
    const World &p_world,
    const Table &p_table,
    uint32_t p_row,
    int64_t p_tick,
    bool p_refresh
);

} // namespace netw::store

#include "netw/store/schedule.hpp"

#include "netw/store/world.hpp"

namespace netw::store {

float class_weight(RowClass p_class) {
    switch (p_class) {
        case RowClass::PLAYER:
            return 10000.0f;
        case RowClass::INTERACTING:
            return 1000.0f;
        case RowClass::MOVING:
            return 100.0f;
        case RowClass::RESTING:
            return 1.0f;
    }
    return 1.0f;
}

const char *class_name(RowClass p_class) {
    switch (p_class) {
        case RowClass::PLAYER:
            return "player";
        case RowClass::INTERACTING:
            return "interacting";
        case RowClass::MOVING:
            return "moving";
        case RowClass::RESTING:
            return "resting";
    }
    return "resting";
}

RowClass row_class(
    const World &p_world,
    const Table &p_table,
    uint32_t p_row,
    int64_t p_tick,
    bool p_refresh
) {
    if (p_refresh) {
        return RowClass::RESTING;
    }
    const uint8_t marks = p_world.marks_of(p_table.slots[p_row]);
    if ((marks & MARK_PLAYER) != 0) {
        return RowClass::PLAYER;
    }
    if ((marks & MARK_INTERACTING) != 0) {
        return RowClass::INTERACTING;
    }
    const int64_t changed = p_table.changed_ticks[p_row];
    if (changed >= 0 && p_tick - changed < MOVING_TICKS) {
        return RowClass::MOVING;
    }
    return RowClass::RESTING;
}

float importance(
    const World &p_world,
    const Table &p_table,
    uint32_t p_row,
    int64_t p_tick,
    bool p_refresh
) {
    return class_weight(row_class(p_world, p_table, p_row, p_tick, p_refresh))
        * p_world.weight_of(p_table.slots[p_row]);
}

} // namespace netw::store

#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw::store {
class Table;
} // namespace netw::store

namespace netw::predict {

struct FieldCodec;
struct StateRow;

enum class Listener : uint8_t {
    NONE = 0,
    STATE = 1,
    INPUT = 2,
    SIMULATED = 3,
    QUARANTINE = 4,
};

struct Arrival {
    store::Table *table = nullptr;
    uint32_t row = 0;
    int64_t tick = -1;
    int64_t ack = -1;
    bool whole = true;

    static Arrival of(store::Table &p_table, uint32_t p_row);

    uint32_t sample_count() const;
    int64_t sample_tick(uint32_t p_sample) const;

    bool read(const FieldCodec &p_codec, StateRow &r_row) const;
    bool read_sample(
        const FieldCodec &p_codec,
        uint32_t p_sample,
        StateRow &r_row
    ) const;
};

} // namespace netw::predict

#include "netw/predict/arrival.hpp"

#include "netw/predict/compare.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/store/table.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace netw::predict {

namespace {

int column_of(const store::Table &p_table, const StringName &p_key) {
    const int columns = p_table.schema.column_count();
    for (int column = 0; column < columns; ++column) {
        if (p_table.schema.at(column)->key == p_key) {
            return column;
        }
    }
    return -1;
}

void open_row(const FieldCodec &p_codec, StateRow &r_row) {
    const int count = p_codec.count();
    if (int(r_row.values.size()) != count) {
        r_row.resize(count);
    }
    for (int at = 0; at < count; ++at) {
        r_row.values[uint32_t(at)] = Variant();
        r_row.present[uint32_t(at)] = 0;
    }
}

} // namespace

Arrival Arrival::of(store::Table &p_table, uint32_t p_row) {
    Arrival out;
    out.table = &p_table;
    out.row = p_row;
    out.tick = p_table.arrived_tick;
    out.ack = p_table.arrived_ack;
    out.whole = p_table.arrived_whole;
    return out;
}

uint32_t Arrival::sample_count() const {
    if (table == nullptr || table->family != store::Family::WINDOW) {
        return 0;
    }
    return table->arrived_window.size();
}

int64_t Arrival::sample_tick(uint32_t p_sample) const {
    return p_sample < sample_count() ? table->arrived_window[p_sample].tick
                                     : -1;
}

bool Arrival::read(const FieldCodec &p_codec, StateRow &r_row) const {
    open_row(p_codec, r_row);
    if (table == nullptr || row >= table->row_count()) {
        return false;
    }
    for (int at = 0; at < p_codec.count(); ++at) {
        const int column = column_of(*table, p_codec.keys[uint32_t(at)]);
        if (column >= 0) {
            r_row.set(at, table->read_value(row, uint32_t(column)));
        }
    }
    return true;
}

bool Arrival::read_sample(
    const FieldCodec &p_codec,
    uint32_t p_sample,
    StateRow &r_row
) const {
    open_row(p_codec, r_row);
    if (p_sample >= sample_count()) {
        return false;
    }
    const wire::CodeRow &codes = table->arrived_window[p_sample].row;
    if (!codes.valid_for(table->plan)) {
        return false;
    }
    for (int at = 0; at < p_codec.count(); ++at) {
        const int column = column_of(*table, p_codec.keys[uint32_t(at)]);
        if (column < 0) {
            continue;
        }
        Variant value;
        if (!wire::decode_scalar_column(
                *table->schema.at(column),
                table->plan.column(uint32_t(column)),
                uint32_t(column),
                codes,
                table->decode_codes[uint32_t(column)],
                value
            )) {
            return false;
        }
        r_row.set(at, value);
    }
    return true;
}

} // namespace netw::predict

#include "netw/wire/plan.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::wire {

namespace {

const int ELEMENT_WIDTHS[SchemaCore::COLUMN_TYPE_COUNT] = {
    32,
    64,
    8,
    8,
    16,
    16,
    32,
    64,
    1,
    32,
    32,
    32,
    32,
    32,
    32,
    0,
};

const int ELEMENT_COUNTS[SchemaCore::COLUMN_TYPE_COUNT] = {
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    2,
    3,
    4,
    4,
    4,
    1,
    1,
};

bool type_in_range(int column_type) {
    return column_type >= 0 && column_type < SchemaCore::COLUMN_TYPE_COUNT;
}

} // namespace

int WirePlan::element_width(int column_type) {
    return type_in_range(column_type) ? ELEMENT_WIDTHS[column_type] : 0;
}

int WirePlan::element_count(int column_type) {
    return type_in_range(column_type) ? ELEMENT_COUNTS[column_type] : 0;
}

WirePlan WirePlan::compile(const SchemaRecord &record) {
    NETW_ZONE_NC("WirePlan compile", colors::WIRE);
    WirePlan plan;
    if (!record.sealed) {
        return plan;
    }
    const int count = record.column_count();
    for (int index = 0; index < count; ++index) {
        const SchemaColumn *column = record.at(index);
        const int declared = column->stride > 0 ? column->stride : 1;
        ColumnPlan slot;
        slot.delta = column->delta;
        if (column->quantizer.is_valid()) {
            const Variant::Type element = static_cast<Variant::Type>(
                SchemaCore::element_type(column->type)
            );
            slot.width = column->quantizer->bit_width(element);
            slot.stride = declared * column->quantizer->stride(element);
        } else {
            slot.width = element_width(column->type);
            slot.stride = declared * element_count(column->type);
        }
        if (slot.width <= 0 || slot.stride <= 0) {
            return WirePlan();
        }
        slot.offset = plan.total_bits;
        plan.columns.push_back(slot);
        plan.total_bits += slot.bits();
    }
    plan.plannable = true;
    return plan;
}

} // namespace netw::wire

#include "netw/wire/plan.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::wire {

namespace {

// Bits one element of each SchemaCore::ColumnType occupies unquantized,
// indexed by the enum. The storage table beside the declaration answers what an
// element is HELD in, which is a different question: five integer widths share
// one storage array and each carries its own width here.
//
// VARIANT is zero because a self-describing element has no fixed width, and
// that zero is what makes a schema carrying one unplannable.
const int ELEMENT_WIDTHS[SchemaCore::COLUMN_TYPE_COUNT] = {
    32,  // F32
    64,  // F64
    8,   // I8
    8,   // U8
    16,  // I16
    16,  // U16
    32,  // I32
    64,  // I64
    1,   // BOOL
    64,  // VECTOR2
    96,  // VECTOR3
    128, // VECTOR4
    128, // COLOR
    128, // QUATERNION
    64,  // ENTITY
    0,   // VARIANT
};

} // namespace

int WirePlan::element_width(int column_type) {
    if (column_type < 0 || column_type >= SchemaCore::COLUMN_TYPE_COUNT) {
        return 0;
    }
    return ELEMENT_WIDTHS[column_type];
}

WirePlan WirePlan::compile(const Ref<SchemaRecord> &record) {
    NETW_ZONE_NC("WirePlan compile", colors::WIRE);
    WirePlan plan;
    if (record.is_null() || !record->sealed) {
        return plan;
    }
    const int count = record->column_count();
    if (count > int(MAX_COLUMNS)) {
        return plan;
    }
    for (int index = 0; index < count; ++index) {
        const Ref<SchemaColumn> column = record->at(index);
        if (column.is_null()) {
            return WirePlan();
        }
        ColumnPlan slot;
        slot.stride = column->stride > 0 ? column->stride : 1;
        if (column->quantizer.is_valid()) {
            slot.width = column->quantizer->bit_width(
                SchemaCore::element_type(column->type)
            );
        } else {
            slot.width = element_width(column->type);
        }
        if (slot.width <= 0) {
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

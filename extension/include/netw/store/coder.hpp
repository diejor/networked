#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/table/schema_record.hpp"
#include "netw/wire/plan.hpp"

namespace netw {
class NetwQuantizeScalar;
class NetwQuantizeQuaternion;
} // namespace netw

namespace netw::store {

enum class Coder : uint8_t { VARIANT, RAW, SCALAR, TURN };

struct ColumnCoder {
    Coder kind = Coder::VARIANT;
    int type = 0;
    godot::Variant::Type expected = godot::Variant::NIL;
    wire::ColumnPlan slot;
    const NetwQuantizeScalar *scalar = nullptr;
    const NetwQuantizeQuaternion *turn = nullptr;
};

ColumnCoder coder_of(
    const netw::table::SchemaColumn &p_column,
    const wire::ColumnPlan &p_slot
);

bool typed_from_variant(
    const ColumnCoder &p_coder,
    const godot::Variant &p_value,
    uint8_t *r_value
);

bool code_value(
    const ColumnCoder &p_coder,
    const uint8_t *p_value,
    uint64_t *r_codes
);

void canonical_value(
    const ColumnCoder &p_coder,
    const uint64_t *p_codes,
    uint8_t *r_value
);

uint64_t read_code(
    const uint64_t *p_words,
    const wire::ColumnPlan &p_slot,
    int p_element
);

void write_code(
    uint64_t *r_words,
    const wire::ColumnPlan &p_slot,
    int p_element,
    uint64_t p_code
);

} // namespace netw::store

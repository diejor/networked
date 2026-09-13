#pragma once

#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/code_row.hpp"

namespace netw::wire {

using netw::table::SchemaRecord;

bool encode_scalar_row(
    const SchemaRecord &p_schema,
    const godot::Array &p_values,
    CodeRow &r_row
);

bool gather_scalar_row(
    const SchemaRecord &p_schema,
    const WirePlan &p_plan,
    const godot::Array &p_values,
    CodeRow &r_row
);

bool decode_scalar_row(
    const SchemaRecord &p_schema,
    const CodeRow &p_row,
    godot::Array &r_values
);

} // namespace netw::wire

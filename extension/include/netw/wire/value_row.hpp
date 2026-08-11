#pragma once

/* The gather/apply boundary between Godot values and code-row currency.
 *
 * A sealed scalar schema fixes every type and quantizer before a value reaches
 * this boundary. Encoding quantizes once into the packed row. Decoding applies
 * that same declaration without type tags, so every layer below this one sees
 * codes and never repeats Variant dispatch.
 *
 * Strided columns are refused because one Variant does not declare how its
 * elements are indexed. Their column-store gather owns that mapping directly.
 */

#include "godot/variant.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/wire/code_row.hpp"

namespace netw::wire {

bool encode_scalar_row(
    const godot::Ref<SchemaRecord> &p_schema,
    const godot::Array &p_values,
    CodeRow &r_row
);

bool decode_scalar_row(
    const godot::Ref<SchemaRecord> &p_schema,
    const CodeRow &p_row,
    godot::Array &r_values
);

} // namespace netw::wire

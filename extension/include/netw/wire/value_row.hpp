#pragma once

#include "godot/utility.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/code_row.hpp"

namespace netw::wire {

using netw::table::SchemaRecord;

template <class Stream>
bool carry_value(Stream &p_stream, godot::Variant &r_value) {
    godot::PackedByteArray encoded;
    if constexpr (!Stream::is_reading) {
        encoded = gd::var_to_bytes(r_value);
    }
    if (!p_stream.bytes_capped(encoded, VARIABLE_BYTES_CAP)) {
        return false;
    }
    if constexpr (Stream::is_reading) {
        r_value = gd::bytes_to_var(encoded);
    }
    return true;
}

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

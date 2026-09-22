#pragma once

#include <cstdint>

#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/schema_core.hpp"

namespace netw::persist {

constexpr int FORMAT_VERSION = 1;

enum class Kind {
    RECORD = 0,
    SNAPSHOT = 1,
};

godot::Array descriptor_of(const SchemaRecord &p_schema);

bool descriptors_agree(const godot::Array &p_left, const godot::Array &p_right);

godot::Error validate_row(
    const SchemaRecord &p_schema,
    const godot::Dictionary &p_values,
    godot::String &r_detail
);

godot::Dictionary seal_record(
    const SchemaRecord &p_schema,
    int p_version,
    const godot::Dictionary &p_values
);

godot::Error open_record(
    const SchemaCore &p_core,
    const godot::RID &p_schema,
    const godot::Dictionary &p_envelope,
    godot::Dictionary &r_values,
    godot::String &r_detail
);

godot::Dictionary address_of(
    Kind p_kind,
    const godot::StringName &p_schema_name,
    const godot::String &p_key
);

godot::String address_text(const godot::Dictionary &p_address);

} // namespace netw::persist

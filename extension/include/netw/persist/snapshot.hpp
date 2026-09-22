#pragma once

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/persist/envelope.hpp"
#include "netw/schema_core.hpp"

namespace netw::persist {

godot::Dictionary seal_snapshot(
    const SchemaRecord &p_schema,
    int p_version,
    const godot::PackedStringArray &p_ids,
    const godot::LocalVector<godot::Variant> &p_columns
);

godot::Error open_snapshot(
    const SchemaCore &p_core,
    const godot::RID &p_schema,
    const godot::Dictionary &p_envelope,
    godot::PackedStringArray &r_ids,
    godot::LocalVector<godot::Variant> &r_columns,
    godot::String &r_detail
);

godot::Error validate_ids(
    const godot::PackedStringArray &p_ids,
    int p_rows,
    godot::String &r_detail
);

} // namespace netw::persist

#pragma once

#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {

godot::Dictionary database_read_hit(
    const godot::StringName &p_id,
    const godot::Dictionary &p_values
);
godot::Dictionary database_read_miss(const godot::StringName &p_id);
godot::Dictionary database_read_failure(
    const godot::StringName &p_id,
    godot::Error p_error,
    const godot::String &p_detail
);

godot::Dictionary database_page_of(
    const godot::Array &p_records,
    const godot::String &p_cursor
);
godot::Dictionary database_page_failure(
    godot::Error p_error,
    const godot::String &p_detail
);

godot::Dictionary database_slots_of(const godot::PackedStringArray &p_slots);
godot::Dictionary database_slots_failure(
    godot::Error p_error,
    const godot::String &p_detail
);

godot::Dictionary database_batch_result_of(
    godot::Error p_error,
    const godot::String &p_detail,
    const godot::PackedInt32Array &p_errors,
    const godot::PackedByteArray &p_uncertain
);
godot::Dictionary database_batch_result_refused(
    int p_count,
    godot::Error p_error,
    const godot::String &p_detail
);

godot::Dictionary table_load_of(
    const godot::PackedStringArray &p_ids,
    const godot::PackedInt64Array &p_routes
);
godot::Dictionary table_load_miss();
godot::Dictionary table_load_failure(
    godot::Error p_error,
    const godot::String &p_detail
);

} // namespace netw

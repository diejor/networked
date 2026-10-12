#pragma once

#include "godot/variant.hpp"

namespace netw::persist {

enum class RecordFormat {
    BINARY,
    TEXT,
};

const char *extension_of(RecordFormat p_format);
bool format_of_path(const godot::String &p_path, RecordFormat &r_format);
RecordFormat other_format(RecordFormat p_format);

godot::PackedByteArray encoded(
    const godot::Dictionary &p_envelope,
    RecordFormat p_format
);
godot::Dictionary decoded(
    const godot::PackedByteArray &p_bytes,
    RecordFormat p_format
);

godot::String staging_path_for(const godot::String &p_path);
godot::Error read_record_file(
    const godot::String &p_path,
    godot::Dictionary &r_envelope
);
godot::Error write_record_file(
    const godot::String &p_path,
    const godot::Dictionary &p_envelope
);

} // namespace netw::persist

#pragma once

#include "godot/variant.hpp"

namespace netw {
class SchemaCore;
} // namespace netw

namespace netw::wire {

godot::Dictionary spec_records();
godot::Array spec_channels();
godot::Array spec_reserved();
godot::Array spec_schemas(const netw::SchemaCore &p_schemas);
godot::Dictionary spec_document();

} // namespace netw::wire

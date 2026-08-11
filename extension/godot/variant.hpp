#pragma once

#if defined(NETW_MODULE)
#include "core/error/error_list.h"
#include "core/variant/binder_common.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"

namespace godot {
using ::Array;
using ::Dictionary;
using ::Error;
using ::PackedByteArray;
using ::PackedInt32Array;
using ::PackedInt64Array;
using ::PackedStringArray;
using ::String;
using ::StringName;
using ::real_t;
using ::TypedArray;
using ::Variant;
using ::Vector2;
using ::Vector2i;
using ::vformat;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/transform2d.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector2i.hpp>
#include <godot_cpp/variant/vector3.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

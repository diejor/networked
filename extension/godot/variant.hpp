#pragma once

#include <cstdint>

#if defined(NETW_MODULE)
#include "core/error/error_list.h"
#include "core/variant/binder_common.h"
#include "core/variant/typed_array.h"
#include "core/variant/variant.h"

namespace godot {
using ::Array;
using ::Dictionary;
using ::Error;
using ::NodePath;
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
using ::Vector3;
using ::Vector3i;
using ::vformat;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/node_path.hpp>
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
#include <godot_cpp/variant/node_path.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

template <class Packed> Packed zeroed(int64_t p_size) {
    Packed out;
    out.resize(p_size);
    out.fill(0);
    return out;
}

inline godot::String utf8_string(const godot::PackedByteArray &p_bytes) {
    if (p_bytes.is_empty()) {
        return godot::String();
    }
#ifdef NETW_MODULE
    return godot::String::utf8(
        reinterpret_cast<const char *>(p_bytes.ptr()),
        p_bytes.size()
    );
#else
    return p_bytes.get_string_from_utf8();
#endif
}

} // namespace netw::gd

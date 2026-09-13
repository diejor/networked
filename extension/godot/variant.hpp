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
using ::ERR_ALREADY_EXISTS;
using ::ERR_ALREADY_IN_USE;
using ::ERR_BUG;
using ::ERR_BUSY;
using ::ERR_CANT_ACQUIRE_RESOURCE;
using ::ERR_CANT_CONNECT;
using ::ERR_CANT_CREATE;
using ::ERR_CANT_FORK;
using ::ERR_CANT_OPEN;
using ::ERR_CANT_RESOLVE;
using ::ERR_COMPILATION_FAILED;
using ::ERR_CONNECTION_ERROR;
using ::ERR_CYCLIC_LINK;
using ::ERR_DATABASE_CANT_READ;
using ::ERR_DATABASE_CANT_WRITE;
using ::ERR_DOES_NOT_EXIST;
using ::ERR_DUPLICATE_SYMBOL;
using ::ERR_FILE_ALREADY_IN_USE;
using ::ERR_FILE_BAD_DRIVE;
using ::ERR_FILE_BAD_PATH;
using ::ERR_FILE_CANT_OPEN;
using ::ERR_FILE_CANT_READ;
using ::ERR_FILE_CANT_WRITE;
using ::ERR_FILE_CORRUPT;
using ::ERR_FILE_EOF;
using ::ERR_FILE_MISSING_DEPENDENCIES;
using ::ERR_FILE_NO_PERMISSION;
using ::ERR_FILE_NOT_FOUND;
using ::ERR_FILE_UNRECOGNIZED;
using ::ERR_HELP;
using ::ERR_INVALID_DATA;
using ::ERR_INVALID_DECLARATION;
using ::ERR_INVALID_PARAMETER;
using ::ERR_LINK_FAILED;
using ::ERR_LOCKED;
using ::ERR_METHOD_NOT_FOUND;
using ::ERR_OUT_OF_MEMORY;
using ::ERR_PARAMETER_RANGE_ERROR;
using ::ERR_PARSE_ERROR;
using ::ERR_PRINTER_ON_FIRE;
using ::ERR_QUERY_FAILED;
using ::ERR_SCRIPT_FAILED;
using ::ERR_SKIP;
using ::ERR_TIMEOUT;
using ::ERR_UNAUTHORIZED;
using ::ERR_UNAVAILABLE;
using ::ERR_UNCONFIGURED;
using ::Error;
using ::FAILED;
using ::NodePath;
using ::OK;
using ::PackedByteArray;
using ::PackedFloat32Array;
using ::PackedFloat64Array;
using ::PackedInt32Array;
using ::PackedInt64Array;
using ::PackedStringArray;
using ::PROPERTY_HINT_RESOURCE_TYPE;
using ::real_t;
using ::Rect2;
using ::Rect2i;
using ::String;
using ::StringName;
using ::Transform2D;
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
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/quaternion.hpp>
#include <godot_cpp/variant/rect2.hpp>
#include <godot_cpp/variant/rect2i.hpp>
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

namespace netw::gd {

template <typename... A> godot::Array array_of(const A &...p_values) {
    godot::Array list;
    (list.push_back(p_values), ...);
    return list;
}

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

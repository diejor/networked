#pragma once

#include <cstdint>
#include <cstdlib>

#include "godot/object.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/error/error_macros.h"
#include "core/object/ref_counted.h"
#include "core/string/print_string.h"
#include "core/variant/variant_utility.h"

namespace godot {
using ::WeakRef;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/weak_ref.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline void print(const godot::String &message) {
#if defined(NETW_MODULE)
    print_line(message);
#else
    godot::UtilityFunctions::print(message);
#endif
}

inline void push_warning(const godot::String &message) {
#if defined(NETW_MODULE)
    WARN_PRINT(message);
#else
    godot::UtilityFunctions::push_warning(message);
#endif
}

inline void push_error(const godot::String &message) {
#if defined(NETW_MODULE)
    ERR_PRINT(message);
#else
    godot::UtilityFunctions::push_error(message);
#endif
}

inline godot::String error_name(int64_t code) {
#if defined(NETW_MODULE)
    return godot::String(::error_names[code]);
#else
    return godot::UtilityFunctions::error_string(code);
#endif
}

inline void push_warning_at(
    const char *function,
    const char *file,
    int line,
    const godot::String &message
) {
#if defined(NETW_MODULE)
    _err_print_error(function, file, line, message, false, ERR_HANDLER_WARNING);
#else
    godot::_err_print_error(function, file, line, message, false, true);
#endif
}

inline void push_error_at(
    const char *function,
    const char *file,
    int line,
    const godot::String &message
) {
#if defined(NETW_MODULE)
    _err_print_error(function, file, line, message);
#else
    godot::_err_print_error(function, file, line, message);
#endif
}

[[noreturn]] inline void crash() {
    GENERATE_TRAP();
    std::abort();
}

inline godot::Variant weak_ref(const godot::Variant &value) {
#if defined(NETW_MODULE)
    Callable::CallError error;
    return VariantUtilityFunctions::weakref(value, error);
#else
    return godot::UtilityFunctions::weakref(value);
#endif
}

inline godot::Object *held_by_weak_ref(const godot::Variant &value) {
    const godot::Ref<godot::WeakRef> holder = value;
    return holder.is_valid() ? holder->get_ref().operator godot::Object *()
                             : nullptr;
}

inline godot::PackedByteArray var_to_bytes(const godot::Variant &value) {
#if defined(NETW_MODULE)
    return VariantUtilityFunctions::var_to_bytes(value);
#else
    return godot::UtilityFunctions::var_to_bytes(value);
#endif
}

inline godot::Variant bytes_to_var(const godot::PackedByteArray &bytes) {
#if defined(NETW_MODULE)
    return VariantUtilityFunctions::bytes_to_var(bytes);
#else
    return godot::UtilityFunctions::bytes_to_var(bytes);
#endif
}

inline void encode_u8(godot::PackedByteArray &bytes, int at, uint8_t value) {
    ERR_FAIL_INDEX(at, bytes.size());
    bytes.ptrw()[at] = value;
}

inline void encode_u32(godot::PackedByteArray &bytes, int at, uint32_t value) {
    ERR_FAIL_INDEX(at + 3, bytes.size());
    uint8_t *out = bytes.ptrw() + at;
    out[0] = uint8_t(value & 0xFF);
    out[1] = uint8_t((value >> 8) & 0xFF);
    out[2] = uint8_t((value >> 16) & 0xFF);
    out[3] = uint8_t((value >> 24) & 0xFF);
}

inline uint8_t decode_u8(const godot::PackedByteArray &bytes, int at) {
    ERR_FAIL_INDEX_V(at, bytes.size(), 0);
    return bytes.ptr()[at];
}

inline uint32_t decode_u32(const godot::PackedByteArray &bytes, int at) {
    ERR_FAIL_INDEX_V(at + 3, bytes.size(), 0);
    const uint8_t *in = bytes.ptr() + at;
    return uint32_t(in[0]) | (uint32_t(in[1]) << 8) | (uint32_t(in[2]) << 16)
        | (uint32_t(in[3]) << 24);
}

inline godot::String hex_of(const godot::PackedByteArray &bytes) {
    static const char DIGITS[] = "0123456789abcdef";
    godot::String out;
    for (int at = 0; at < bytes.size(); at++) {
        const uint8_t byte = bytes[at];
        out += godot::String::chr(DIGITS[(byte >> 4) & 0xF]);
        out += godot::String::chr(DIGITS[byte & 0xF]);
    }
    return out;
}

inline void seed(int64_t value) {
#if defined(NETW_MODULE)
    VariantUtilityFunctions::seed(value);
#else
    godot::UtilityFunctions::seed(value);
#endif
}

inline int64_t randi() {
#if defined(NETW_MODULE)
    return VariantUtilityFunctions::randi();
#else
    return godot::UtilityFunctions::randi();
#endif
}

inline int64_t randi_range(int64_t from, int64_t to) {
#if defined(NETW_MODULE)
    return VariantUtilityFunctions::randi_range(from, to);
#else
    return godot::UtilityFunctions::randi_range(from, to);
#endif
}

} // namespace netw::gd

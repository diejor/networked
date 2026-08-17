#pragma once

#include <cstdlib>

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/error/error_macros.h"
#include "core/string/print_string.h"
#include "core/variant/variant_utility.h"
#elif defined(NETW_GDEXTENSION)
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

#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw {

namespace config_draft {

struct Guard {
    const char *verb = "";
    bool sealed = false;
    bool late_reported = false;
    godot::String scope;

    void seal(const godot::String &p_scope);

    bool takes(const char *p_field);
    bool takes_enum(const char *p_field, int64_t p_value, int64_t p_count);
    bool takes_count(const char *p_field, int64_t p_value);
    bool takes_positive_count(const char *p_field, int64_t p_value);
    bool takes_ratio(const char *p_field, double p_value);
    bool takes_positive(const char *p_field, double p_value);
};

} // namespace config_draft

} // namespace netw

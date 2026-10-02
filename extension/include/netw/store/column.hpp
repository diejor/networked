#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw::store {

enum class Port : uint8_t { BODY, ENGINE, SCRIPT, CUSTOM, ARRAY };

enum class Family : uint8_t { VOLATILE, RETAINED, WINDOW };

enum class Source : uint8_t { NONE, LOCAL, REMOTE };

enum class BodyKey : uint8_t {
    NONE,
    POSITION,
    TURN,
    LINEAR,
    ANGULAR,
    SLEEPING
};

struct Column {
    Port port = Port::ENGINE;
    BodyKey body = BodyKey::NONE;
    uint32_t value_bytes = 0;
};

uint32_t value_bytes_of(int p_type);

const char *port_name(Port p_port);

const char *family_name(Family p_family);

} // namespace netw::store

#pragma once

#include <cstdint>

namespace netw {

struct CarrierRow {
    int64_t route = 0;
    uint8_t comp = 0;
    int64_t bits = 0;
    uint64_t token = 0;
    uint64_t revision = 0;
    bool masked = false;
};

} // namespace netw

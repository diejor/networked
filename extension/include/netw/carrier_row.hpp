#pragma once

#include <cstdint>

#include "netw/wire/code_row.hpp"

namespace netw {

struct CarrierRow {
    int64_t route = 0;
    uint8_t comp = 0;
    int64_t bits = 0;
    uint64_t token = 0;
    uint64_t revision = 0;
    bool masked = false;
    wire::CodeRow row;

    void copy_from(const CarrierRow &p_other) {
        route = p_other.route;
        comp = p_other.comp;
        bits = p_other.bits;
        token = p_other.token;
        revision = p_other.revision;
        masked = p_other.masked;
        row.copy_from(p_other.row);
    }
};

} // namespace netw

#pragma once

#include <cstdint>

namespace netw::wire {

constexpr uint32_t SNAPSHOT_STAGE_DEPTH = 64;
constexpr uint64_t SNAPSHOT_DELTA_SPAN = 32;
constexpr uint64_t SNAPSHOT_FIRST_REVISION = 1;

enum class ReceiptVerdict : uint8_t {
    PROMOTED,
    IGNORED,
    EVICTED,
    UNEXPOSED,
};

} // namespace netw::wire

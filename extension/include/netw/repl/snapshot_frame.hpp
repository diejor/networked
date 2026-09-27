#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/repl/window_ring.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

constexpr int SNAPSHOT_DISTANCE_BITS = 6;
constexpr uint64_t SNAPSHOT_DISTANCE_CEILING = 32;

struct SnapshotHeader {
    uint64_t token = 0;
    uint64_t revision = 0;
    uint64_t distance = 0;
    int64_t tick = -1;
    int64_t reconcile_ack = -1;
    uint64_t mask = 0;

    bool absolute() const {
        return distance == 0;
    }

    uint64_t baseline_revision() const {
        return revision - distance;
    }
};

enum class SnapshotRefusal : uint8_t {
    NONE,
    MALFORMED,
    BASELINE_UNKNOWN,
};

godot::PackedByteArray write_snapshot_row(
    const SnapshotHeader &p_header,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    const wire::CodeRow &p_row,
    const wire::CodeRow *p_baseline
);

bool name_snapshot_row(
    const godot::PackedByteArray &p_bytes,
    SnapshotHeader &r_header
);

bool read_snapshot_row(
    const godot::PackedByteArray &p_bytes,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    SnapshotHeader &r_header,
    wire::CodeRow &r_row,
    const wire::CodeRow *p_baseline,
    SnapshotRefusal *r_refusal = nullptr
);

godot::PackedByteArray write_snapshot_window(
    const SnapshotHeader &p_header,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    const godot::LocalVector<WindowSample> &p_samples
);

bool name_snapshot_window(
    const godot::PackedByteArray &p_bytes,
    SnapshotHeader &r_header
);

bool read_snapshot_window(
    const godot::PackedByteArray &p_bytes,
    int64_t p_base_tick,
    const wire::WirePlan &p_plan,
    SnapshotHeader &r_header,
    godot::LocalVector<WindowSample> &r_samples
);

} // namespace netw::repl

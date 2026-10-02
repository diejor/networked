#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/predict/compare.hpp"
#include "netw/sim/select.hpp"

namespace netw::predict {

using godot::LocalVector;
using sim::Tenure;

enum class CellProvenance : uint8_t {
    COAST = 0,
    SUBSTITUTED = 1,
    RELAYED = 2,
    AUTHORED = 3,
};

struct JointFloorDecision {
    int64_t floor = -1;
    bool heal = false;
};

struct JointTrack {
    Tenure tenure;
    int64_t basis = -1;
    int64_t relay_floor = -1;
    int64_t epoch_floor = -1;
    int64_t state_floor = -1;

    int64_t history_floor(
        int64_t p_oldest_state,
        int64_t p_oldest_command
    ) const;
    bool moved() const;
    void clear_floors();
    void clear_cells(int64_t p_newest_state);
};

struct JointRestore {
    StateRow state;
    int64_t slot = 0;
    int64_t entry = -1;
};

struct JointStep {
    StateRow command;
    int64_t slot = 0;
    int64_t transition = -1;
    CellProvenance provenance = CellProvenance::COAST;
};

struct JointPassPlan {
    godot::LocalVector<JointRestore> restores;
    godot::LocalVector<JointStep> steps;
    int64_t floor = -1;
    int64_t present = -1;
    bool heal = false;
    bool valid = false;

    JointPassPlan() = default;
    JointPassPlan(const JointPassPlan &p_other);
    JointPassPlan &operator=(const JointPassPlan &p_other);
};

struct JointStats {
    int passes = 0;
    int members = 0;
    int cells_relayed = 0;
    int cells_substituted = 0;
    int heal_snaps = 0;
    int linger_held = 0;
    int64_t floor = -1;
    int64_t present = -1;
    int max_depth = 0;
    int floor_ack_moves = 0;
    int floor_state_moves = 0;
    int floor_relay_moves = 0;
    int floor_epoch_moves = 0;
};

JointFloorDecision joint_floor(
    const godot::LocalVector<int64_t> &p_bases,
    const godot::LocalVector<int64_t> &p_relay_floors,
    int64_t p_epoch_floor,
    int64_t p_history_floor,
    int64_t p_present
);

CellProvenance joint_cell(
    bool p_authored,
    bool p_relayed,
    bool p_predictor_valid
);

} // namespace netw::predict

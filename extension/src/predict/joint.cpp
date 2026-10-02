#include "netw/predict/joint.hpp"

#include <algorithm>

using namespace godot;

namespace netw::predict {

namespace {

template <typename T>
void copy_values(LocalVector<T> &r_target, const LocalVector<T> &p_source) {
    r_target.resize(p_source.size());
    for (uint32_t at = 0; at < p_source.size(); ++at) {
        r_target[at] = p_source[at];
    }
}

} // namespace

int64_t JointTrack::history_floor(
    int64_t p_oldest_state,
    int64_t p_oldest_command
) const {
    if (p_oldest_state < 0 || p_oldest_command < 0) {
        return -1;
    }
    return std::max(p_oldest_state, p_oldest_command - 1);
}

bool JointTrack::moved() const {
    return basis >= 0 || relay_floor >= 0 || epoch_floor >= 0;
}

void JointTrack::clear_floors() {
    basis = -1;
    relay_floor = -1;
    epoch_floor = -1;
}

void JointTrack::clear_cells(int64_t p_newest_state) {
    state_floor = std::max(state_floor, p_newest_state);
    clear_floors();
}

JointPassPlan::JointPassPlan(const JointPassPlan &p_other) {
    *this = p_other;
}

JointPassPlan &JointPassPlan::operator=(const JointPassPlan &p_other) {
    if (this == &p_other) {
        return *this;
    }
    copy_values(restores, p_other.restores);
    copy_values(steps, p_other.steps);
    floor = p_other.floor;
    present = p_other.present;
    heal = p_other.heal;
    valid = p_other.valid;
    return *this;
}

JointFloorDecision joint_floor(
    const LocalVector<int64_t> &p_bases,
    const LocalVector<int64_t> &p_relay_floors,
    int64_t p_epoch_floor,
    int64_t p_history_floor,
    int64_t p_present
) {
    JointFloorDecision out;
    out.floor = p_present;
    for (uint32_t at = 0; at < p_bases.size(); ++at) {
        if (p_bases[at] >= 0) {
            out.floor = std::min(out.floor, p_bases[at]);
        }
    }
    for (uint32_t at = 0; at < p_relay_floors.size(); ++at) {
        if (p_relay_floors[at] >= 0) {
            out.floor = std::min(out.floor, p_relay_floors[at]);
        }
    }
    if (p_epoch_floor >= 0) {
        out.floor = std::min(out.floor, p_epoch_floor);
    }
    if (out.floor < p_history_floor) {
        out.floor = p_present;
        out.heal = true;
    }
    return out;
}

CellProvenance joint_cell(
    bool p_authored,
    bool p_relayed,
    bool p_predictor_valid
) {
    if (p_authored) {
        return CellProvenance::AUTHORED;
    }
    if (p_relayed) {
        return CellProvenance::RELAYED;
    }
    if (p_predictor_valid) {
        return CellProvenance::SUBSTITUTED;
    }
    return CellProvenance::COAST;
}

} // namespace netw::predict

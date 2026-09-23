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

template <typename T> struct ByTransition {
    bool operator()(const T &p_left, const T &p_right) const {
        return p_left.transition < p_right.transition;
    }
};

template <typename T> void trim(LocalVector<T> &r_rows) {
    const uint32_t size = r_rows.size();
    if (size > 1 && ByTransition<T>()(r_rows[size - 1], r_rows[size - 2])) {
        r_rows.template sort_custom<ByTransition<T>>();
    }
    while (int(r_rows.size()) > 256) {
        r_rows.remove_at(0);
    }
}

} // namespace

void JointTrack::record(
    int64_t p_transition,
    const StateRow &p_state,
    const godot::Variant &p_command,
    CellProvenance p_provenance
) {
    record_state(p_transition, p_state);
    for (uint32_t at = 0; at < commands.size(); ++at) {
        JointCommandRecord &row = commands[at];
        if (row.transition != p_transition) {
            continue;
        }
        if (p_provenance > row.provenance) {
            row.command = p_command;
            row.provenance = p_provenance;
        }
        return;
    }
    JointCommandRecord command_row;
    command_row.transition = p_transition;
    command_row.command = p_command;
    command_row.provenance = p_provenance;
    commands.push_back(command_row);
    trim(commands);
}

void JointTrack::record_state(int64_t p_transition, const StateRow &p_state) {
    if (!p_state.any()) {
        return;
    }
    for (uint32_t at = 0; at < states.size(); ++at) {
        if (states[at].transition == p_transition) {
            states[at].state = p_state;
            return;
        }
    }
    JointStateRecord state_row;
    state_row.transition = p_transition;
    state_row.state = p_state;
    states.push_back(state_row);
    trim(states);
}

const JointStateRecord *JointTrack::newest_state_at_or_before(
    int64_t p_transition
) const {
    const JointStateRecord *out = nullptr;
    for (uint32_t at = 0; at < states.size(); ++at) {
        const JointStateRecord &row = states[at];
        if (row.transition <= p_transition
            && (out == nullptr || row.transition > out->transition)) {
            out = &row;
        }
    }
    return out;
}

const StateRow *JointTrack::state_at(int64_t p_transition) const {
    for (int at = int(states.size()) - 1; at >= 0; --at) {
        if (states[uint32_t(at)].transition == p_transition) {
            return &states[uint32_t(at)].state;
        }
    }
    return nullptr;
}

const JointCommandRecord *JointTrack::command_at(int64_t p_transition) const {
    for (int at = int(commands.size()) - 1; at >= 0; --at) {
        if (commands[uint32_t(at)].transition == p_transition) {
            return &commands[uint32_t(at)];
        }
    }
    return nullptr;
}

int64_t JointTrack::history_floor() const {
    if (states.is_empty() || commands.is_empty()) {
        return -1;
    }
    int64_t state_floor = states[0].transition;
    int64_t command_floor = commands[0].transition;
    for (uint32_t at = 1; at < states.size(); ++at) {
        state_floor = std::min(state_floor, states[at].transition);
    }
    for (uint32_t at = 1; at < commands.size(); ++at) {
        command_floor = std::min(command_floor, commands[at].transition);
    }
    return std::max(state_floor, command_floor - 1);
}

bool JointTrack::moved() const {
    return basis >= 0 || relay_floor >= 0 || epoch_floor >= 0;
}

void JointTrack::clear_floors() {
    basis = -1;
    relay_floor = -1;
    epoch_floor = -1;
}

void JointTrack::clear_cells() {
    states.clear();
    commands.clear();
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

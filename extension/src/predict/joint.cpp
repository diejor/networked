#include "netw/predict/joint.hpp"

#include <algorithm>

#include "netw/colors.hpp"
#include "netw/profile.hpp"

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
    r_rows.template sort_custom<ByTransition<T>>();
    while (int(r_rows.size()) > 256) {
        r_rows.remove_at(0);
    }
}

IslandMember *mutable_member(
    LocalVector<IslandMember> &r_members,
    int64_t p_slot
) {
    for (uint32_t at = 0; at < r_members.size(); ++at) {
        if (r_members[at].slot == p_slot) {
            return &r_members[at];
        }
    }
    return nullptr;
}

bool selected(const LocalVector<int64_t> &p_slots, int64_t p_slot) {
    for (uint32_t at = 0; at < p_slots.size(); ++at) {
        if (p_slots[at] == p_slot) {
            return true;
        }
    }
    return false;
}

void sort_candidates(LocalVector<IslandCandidate> &r_candidates) {
    struct ByDistance {
        bool operator()(
            const IslandCandidate &p_left,
            const IslandCandidate &p_right
        ) const {
            if (p_left.distance_squared == p_right.distance_squared) {
                if (p_left.order_key == p_right.order_key) {
                    return p_left.slot < p_right.slot;
                }
                return p_left.order_key < p_right.order_key;
            }
            return p_left.distance_squared < p_right.distance_squared;
        }
    };
    r_candidates.sort_custom<ByDistance>();
}

void sort_candidate_order(LocalVector<IslandCandidate> &r_candidates) {
    struct ByOrder {
        bool operator()(
            const IslandCandidate &p_left,
            const IslandCandidate &p_right
        ) const {
            if (p_left.order_key == p_right.order_key) {
                return p_left.slot < p_right.slot;
            }
            return p_left.order_key < p_right.order_key;
        }
    };
    r_candidates.sort_custom<ByOrder>();
}

struct RetainedCandidate {
    IslandCandidate candidate;
    bool held = false;
};

void deselect(LocalVector<int64_t> &r_slots, int64_t p_slot) {
    for (uint32_t at = 0; at < r_slots.size(); ++at) {
        if (r_slots[at] == p_slot) {
            r_slots.remove_at(at);
            return;
        }
    }
}

const IslandCandidate *candidate_of(
    const LocalVector<IslandCandidate> &p_candidates,
    int64_t p_slot
) {
    for (uint32_t at = 0; at < p_candidates.size(); ++at) {
        if (p_candidates[at].slot == p_slot) {
            return &p_candidates[at];
        }
    }
    return nullptr;
}

// A member the owner is touching keeps the promotion it already holds, so a
// handoff waits for the contact to end rather than landing inside it.
void defer_contacting(
    const Island &p_island,
    const LocalVector<IslandCandidate> &p_candidates,
    LocalVector<int64_t> &r_desired
) {
    for (uint32_t at = 0; at < p_candidates.size(); ++at) {
        const IslandCandidate &candidate = p_candidates[at];
        if (!candidate.contact) {
            continue;
        }
        const IslandMember *row = p_island.member(candidate.slot);
        const bool held = row != nullptr && row->promoted;
        if (held == selected(r_desired, candidate.slot)) {
            continue;
        }
        if (held) {
            r_desired.push_back(candidate.slot);
        } else {
            deselect(r_desired, candidate.slot);
        }
    }
}

// A deferred handoff can carry more automatic members than the nearest policy
// declared room for, so the budget is re-imposed over what survives it,
// retaining what is already promoted and then what is closest.
void enforce_promotion_count(
    const Island &p_island,
    const LocalVector<IslandCandidate> &p_candidates,
    LocalVector<int64_t> &r_desired
) {
    if (p_island.promotion != Promotion::NEAREST) {
        return;
    }
    LocalVector<RetainedCandidate> automatic;
    for (uint32_t at = 0; at < r_desired.size(); ++at) {
        const IslandCandidate *row = candidate_of(p_candidates, r_desired[at]);
        if (row == nullptr || row->fidelity == Fidelity::SIMULATED) {
            continue;
        }
        const IslandMember *held = p_island.member(row->slot);
        RetainedCandidate entry;
        entry.candidate = *row;
        entry.held = held != nullptr && held->promoted;
        automatic.push_back(entry);
    }
    if (int(automatic.size()) <= p_island.promotion_count) {
        return;
    }
    struct ByRetainedThenDistance {
        bool operator()(
            const RetainedCandidate &p_left,
            const RetainedCandidate &p_right
        ) const {
            if (p_left.held != p_right.held) {
                return p_left.held;
            }
            const IslandCandidate &left = p_left.candidate;
            const IslandCandidate &right = p_right.candidate;
            if (left.distance_squared == right.distance_squared) {
                if (left.order_key == right.order_key) {
                    return left.slot < right.slot;
                }
                return left.order_key < right.order_key;
            }
            return left.distance_squared < right.distance_squared;
        }
    };
    automatic.sort_custom<ByRetainedThenDistance>();
    for (uint32_t at = uint32_t(std::max(0, p_island.promotion_count));
         at < automatic.size();
         ++at) {
        deselect(r_desired, automatic[at].candidate.slot);
    }
}

} // namespace

bool Tenure::contains(int64_t p_transition) const {
    if (begin >= 0 && p_transition < begin) {
        return false;
    }
    return end < 0 || p_transition <= end;
}

void Island::commit(
    const LocalVector<IslandCandidate> &p_candidates,
    int64_t p_frontier
) {
    NETW_ZONE_NC("NetwPredict commit island", colors::PREDICTION);
    for (uint32_t at = 0; at < members.size(); ++at) {
        members[at].present = false;
    }

    LocalVector<IslandCandidate> automatic;
    LocalVector<int64_t> desired;
    for (uint32_t at = 0; at < p_candidates.size(); ++at) {
        const IslandCandidate &candidate = p_candidates[at];
        IslandMember *row = mutable_member(members, candidate.slot);
        if (row == nullptr) {
            IslandMember added;
            added.slot = candidate.slot;
            members.push_back(added);
            row = &members[members.size() - 1];
        }
        row->order_key = candidate.order_key;
        row->distance_squared = candidate.distance_squared;
        row->present = true;
        row->eligible = candidate.eligible;
        row->explicit_simulation = candidate.fidelity == Fidelity::SIMULATED;
        if (row->explicit_simulation) {
            desired.push_back(row->slot);
        } else if (
            candidate.fidelity == Fidelity::UNDECLARED && candidate.eligible
        ) {
            automatic.push_back(candidate);
        }
    }

    sort_candidates(automatic);
    if (promotion == Promotion::ALL) {
        for (uint32_t at = 0; at < automatic.size(); ++at) {
            desired.push_back(automatic[at].slot);
        }
    } else if (promotion == Promotion::WITHIN) {
        const double enter = promotion_meters * promotion_meters;
        const double exit = enter * ISLAND_EXIT_MARGIN_SQUARED;
        for (uint32_t at = 0; at < automatic.size(); ++at) {
            const IslandCandidate &candidate = automatic[at];
            const IslandMember *row = member(candidate.slot);
            const double limit = row != nullptr && row->promoted ? exit : enter;
            if (candidate.distance_squared <= limit) {
                desired.push_back(candidate.slot);
            }
        }
    } else if (
        promotion == Promotion::NEAREST && promotion_count > 0
        && !automatic.is_empty()
    ) {
        const int limit = std::min(promotion_count, int(automatic.size()));
        const double cutoff = automatic[uint32_t(limit - 1)].distance_squared
            * ISLAND_EXIT_MARGIN_SQUARED;
        LocalVector<IslandCandidate> retained;
        for (uint32_t at = 0; at < automatic.size(); ++at) {
            const IslandCandidate &candidate = automatic[at];
            const IslandMember *row = member(candidate.slot);
            if (row != nullptr && row->promoted
                && candidate.distance_squared <= cutoff) {
                retained.push_back(candidate);
            }
        }
        sort_candidate_order(retained);
        int automatic_selected = 0;
        for (uint32_t at = 0; at < retained.size(); ++at) {
            if (automatic_selected >= promotion_count) {
                break;
            }
            desired.push_back(retained[at].slot);
            automatic_selected += 1;
        }
        for (uint32_t at = 0; at < automatic.size(); ++at) {
            if (automatic_selected >= promotion_count) {
                break;
            }
            if (!selected(desired, automatic[at].slot)) {
                desired.push_back(automatic[at].slot);
                automatic_selected += 1;
            }
        }
    }

    defer_contacting(*this, p_candidates, desired);
    enforce_promotion_count(*this, p_candidates, desired);

    for (uint32_t at = 0; at < members.size(); ++at) {
        IslandMember &row = members[at];
        const bool promote = row.present && selected(desired, row.slot);
        if (promote && !row.promoted) {
            row.tenure.begin = p_frontier + 1;
            row.tenure.end = -1;
            row.lingering = false;
        } else if (!promote && row.promoted) {
            row.tenure.end = p_frontier;
            row.lingering = true;
        }
        row.promoted = promote;
    }

    for (uint32_t at = 0; at < members.size();) {
        const IslandMember &row = members[at];
        if (!row.present && !row.promoted && !row.lingering) {
            members.remove_at(at);
        } else {
            at += 1;
        }
    }

    struct ByOrder {
        bool operator()(
            const IslandMember &p_left,
            const IslandMember &p_right
        ) const {
            if (p_left.order_key == p_right.order_key) {
                return p_left.slot < p_right.slot;
            }
            return p_left.order_key < p_right.order_key;
        }
    };
    members.sort_custom<ByOrder>();
}

void Island::release_lingering(int64_t p_floor) {
    for (uint32_t at = 0; at < members.size();) {
        IslandMember &row = members[at];
        if (row.lingering && row.tenure.end < p_floor) {
            row.lingering = false;
        }
        if (!row.present && !row.promoted && !row.lingering) {
            members.remove_at(at);
        } else {
            at += 1;
        }
    }
}

const IslandMember *Island::member(int64_t p_slot) const {
    for (uint32_t at = 0; at < members.size(); ++at) {
        if (members[at].slot == p_slot) {
            return &members[at];
        }
    }
    return nullptr;
}

int Island::present_count() const {
    int out = 0;
    for (uint32_t at = 0; at < members.size(); ++at) {
        out += members[at].present ? 1 : 0;
    }
    return out;
}

int Island::promoted_count() const {
    int out = 0;
    for (uint32_t at = 0; at < members.size(); ++at) {
        out += members[at].promoted ? 1 : 0;
    }
    return out;
}

int Island::lingering_count() const {
    int out = 0;
    for (uint32_t at = 0; at < members.size(); ++at) {
        out += members[at].lingering ? 1 : 0;
    }
    return out;
}

void JointTrack::record(
    int64_t p_transition,
    const StateRow &p_state,
    const godot::Variant &p_command,
    CellProvenance p_provenance
) {
    // An empty row is the absence of an observation, so a record that carries
    // only a better command leaves the state this transition produced alone.
    if (p_state.any()) {
        bool found_state = false;
        for (uint32_t at = 0; at < states.size(); ++at) {
            if (states[at].transition == p_transition) {
                states[at].state = p_state;
                found_state = true;
                break;
            }
        }
        if (!found_state) {
            JointStateRecord state_row;
            state_row.transition = p_transition;
            state_row.state = p_state;
            states.push_back(state_row);
            trim(states);
        }
    }

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

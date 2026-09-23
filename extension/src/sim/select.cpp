#include "netw/sim/select.hpp"

#include <algorithm>

#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::sim {

namespace {

Selected *mutable_member(LocalVector<Selected> &r_members, int64_t p_key) {
    for (uint32_t at = 0; at < r_members.size(); ++at) {
        if (r_members[at].key == p_key) {
            return &r_members[at];
        }
    }
    return nullptr;
}

bool selected(const LocalVector<int64_t> &p_keys, int64_t p_key) {
    for (uint32_t at = 0; at < p_keys.size(); ++at) {
        if (p_keys[at] == p_key) {
            return true;
        }
    }
    return false;
}

void sort_candidates(LocalVector<Candidate> &r_candidates) {
    struct ByDistance {
        bool operator()(
            const Candidate &p_left,
            const Candidate &p_right
        ) const {
            if (p_left.distance_squared == p_right.distance_squared) {
                if (p_left.order_key == p_right.order_key) {
                    return p_left.key < p_right.key;
                }
                return p_left.order_key < p_right.order_key;
            }
            return p_left.distance_squared < p_right.distance_squared;
        }
    };
    r_candidates.sort_custom<ByDistance>();
}

void sort_candidate_order(LocalVector<Candidate> &r_candidates) {
    struct ByOrder {
        bool operator()(
            const Candidate &p_left,
            const Candidate &p_right
        ) const {
            if (p_left.order_key == p_right.order_key) {
                return p_left.key < p_right.key;
            }
            return p_left.order_key < p_right.order_key;
        }
    };
    r_candidates.sort_custom<ByOrder>();
}

struct RetainedCandidate {
    Candidate candidate;
    bool held = false;
};

void deselect(LocalVector<int64_t> &r_keys, int64_t p_key) {
    for (uint32_t at = 0; at < r_keys.size(); ++at) {
        if (r_keys[at] == p_key) {
            r_keys.remove_at(at);
            return;
        }
    }
}

const Candidate *candidate_of(
    const LocalVector<Candidate> &p_candidates,
    int64_t p_key
) {
    for (uint32_t at = 0; at < p_candidates.size(); ++at) {
        if (p_candidates[at].key == p_key) {
            return &p_candidates[at];
        }
    }
    return nullptr;
}

void defer_contacting(
    const Selection &p_selection,
    const LocalVector<Candidate> &p_candidates,
    LocalVector<int64_t> &r_desired
) {
    for (uint32_t at = 0; at < p_candidates.size(); ++at) {
        const Candidate &candidate = p_candidates[at];
        if (!candidate.contact) {
            continue;
        }
        const Selected *row = p_selection.member(candidate.key);
        const bool held = row != nullptr && row->promoted;
        if (held == selected(r_desired, candidate.key)) {
            continue;
        }
        if (held) {
            r_desired.push_back(candidate.key);
        } else {
            deselect(r_desired, candidate.key);
        }
    }
}

void enforce_count(
    const Selection &p_selection,
    const LocalVector<Candidate> &p_candidates,
    LocalVector<int64_t> &r_desired
) {
    if (p_selection.policy != Policy::NEAREST) {
        return;
    }
    LocalVector<RetainedCandidate> automatic;
    for (uint32_t at = 0; at < r_desired.size(); ++at) {
        const Candidate *row = candidate_of(p_candidates, r_desired[at]);
        if (row == nullptr || row->pick == Pick::CHOSEN) {
            continue;
        }
        const Selected *held = p_selection.member(row->key);
        RetainedCandidate entry;
        entry.candidate = *row;
        entry.held = held != nullptr && held->promoted;
        automatic.push_back(entry);
    }
    if (int(automatic.size()) <= p_selection.count) {
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
            const Candidate &left = p_left.candidate;
            const Candidate &right = p_right.candidate;
            if (left.distance_squared == right.distance_squared) {
                if (left.order_key == right.order_key) {
                    return left.key < right.key;
                }
                return left.order_key < right.order_key;
            }
            return left.distance_squared < right.distance_squared;
        }
    };
    automatic.sort_custom<ByRetainedThenDistance>();
    for (uint32_t at = uint32_t(std::max(0, p_selection.count));
         at < automatic.size();
         ++at) {
        deselect(r_desired, automatic[at].candidate.key);
    }
}

int named_index(const Choice &p_choice, const RID &p_entity) {
    for (uint32_t at = 0; at < p_choice.named.size(); ++at) {
        if (p_choice.named[at].entity == p_entity) {
            return int(at);
        }
    }
    return -1;
}

void automatic(Choice &r_choice, Policy p_policy, const StringName &p_layer) {
    r_choice.policy = p_policy;
    r_choice.layers.clear();
    r_choice.layers.push_back(p_layer);
}

} // namespace

bool Tenure::contains(int64_t p_transition) const {
    if (begin >= 0 && p_transition < begin) {
        return false;
    }
    return end < 0 || p_transition <= end;
}

void Selection::commit(
    const LocalVector<Candidate> &p_candidates,
    int64_t p_frontier
) {
    NETW_ZONE_NC("sim commit selection", colors::PREDICTION);
    for (uint32_t at = 0; at < members.size(); ++at) {
        members[at].present = false;
    }

    LocalVector<Candidate> automatic;
    LocalVector<int64_t> desired;
    for (uint32_t at = 0; at < p_candidates.size(); ++at) {
        const Candidate &candidate = p_candidates[at];
        Selected *row = mutable_member(members, candidate.key);
        if (row == nullptr) {
            Selected added;
            added.key = candidate.key;
            members.push_back(added);
            row = &members[members.size() - 1];
        }
        row->order_key = candidate.order_key;
        row->distance_squared = candidate.distance_squared;
        row->present = true;
        row->eligible = candidate.eligible;
        row->chosen = candidate.pick == Pick::CHOSEN;
        if (row->chosen) {
            desired.push_back(row->key);
        } else if (candidate.pick == Pick::AUTOMATIC && candidate.eligible) {
            automatic.push_back(candidate);
        }
    }

    sort_candidates(automatic);
    if (policy == Policy::ALL) {
        for (uint32_t at = 0; at < automatic.size(); ++at) {
            desired.push_back(automatic[at].key);
        }
    } else if (policy == Policy::WITHIN) {
        const double enter = meters * meters;
        const double exit = enter * EXIT_MARGIN_SQUARED;
        for (uint32_t at = 0; at < automatic.size(); ++at) {
            const Candidate &candidate = automatic[at];
            const Selected *row = member(candidate.key);
            const double limit = row != nullptr && row->promoted ? exit : enter;
            if (candidate.distance_squared <= limit) {
                desired.push_back(candidate.key);
            }
        }
    } else if (
        policy == Policy::NEAREST && count > 0 && !automatic.is_empty()
    ) {
        const int limit = std::min(count, int(automatic.size()));
        const double cutoff = automatic[uint32_t(limit - 1)].distance_squared
            * EXIT_MARGIN_SQUARED;
        LocalVector<Candidate> retained;
        for (uint32_t at = 0; at < automatic.size(); ++at) {
            const Candidate &candidate = automatic[at];
            const Selected *row = member(candidate.key);
            if (row != nullptr && row->promoted
                && candidate.distance_squared <= cutoff) {
                retained.push_back(candidate);
            }
        }
        sort_candidate_order(retained);
        int automatic_selected = 0;
        for (uint32_t at = 0; at < retained.size(); ++at) {
            if (automatic_selected >= count) {
                break;
            }
            desired.push_back(retained[at].key);
            automatic_selected += 1;
        }
        for (uint32_t at = 0; at < automatic.size(); ++at) {
            if (automatic_selected >= count) {
                break;
            }
            if (!selected(desired, automatic[at].key)) {
                desired.push_back(automatic[at].key);
                automatic_selected += 1;
            }
        }
    }

    defer_contacting(*this, p_candidates, desired);
    enforce_count(*this, p_candidates, desired);

    for (uint32_t at = 0; at < members.size(); ++at) {
        Selected &row = members[at];
        const bool promote = row.present && selected(desired, row.key);
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
        const Selected &row = members[at];
        if (!row.present && !row.promoted && !row.lingering) {
            members.remove_at(at);
        } else {
            at += 1;
        }
    }

    struct ByOrder {
        bool operator()(const Selected &p_left, const Selected &p_right) const {
            if (p_left.order_key == p_right.order_key) {
                return p_left.key < p_right.key;
            }
            return p_left.order_key < p_right.order_key;
        }
    };
    members.sort_custom<ByOrder>();
}

void Selection::release_lingering(int64_t p_floor) {
    for (uint32_t at = 0; at < members.size();) {
        Selected &row = members[at];
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

const Selected *Selection::member(int64_t p_key) const {
    for (uint32_t at = 0; at < members.size(); ++at) {
        if (members[at].key == p_key) {
            return &members[at];
        }
    }
    return nullptr;
}

int Selection::present_count() const {
    int out = 0;
    for (uint32_t at = 0; at < members.size(); ++at) {
        out += members[at].present ? 1 : 0;
    }
    return out;
}

int Selection::promoted_count() const {
    int out = 0;
    for (uint32_t at = 0; at < members.size(); ++at) {
        out += members[at].promoted ? 1 : 0;
    }
    return out;
}

int Selection::lingering_count() const {
    int out = 0;
    for (uint32_t at = 0; at < members.size(); ++at) {
        out += members[at].lingering ? 1 : 0;
    }
    return out;
}

bool Choice::declared() const {
    return !named.is_empty() || !layers.is_empty();
}

const Named *Choice::named_of(const RID &p_entity) const {
    const int at = named_index(*this, p_entity);
    return at < 0 ? nullptr : &named[uint32_t(at)];
}

bool claims_exact(const Choice &p_choice, bool p_stepped) {
    return p_stepped && p_choice.policy == Policy::NONE;
}

bool choose(Choice &r_choice, const RID &p_entity, Pick p_pick) {
    const int at = named_index(r_choice, p_entity);
    if (at >= 0) {
        if (r_choice.named[uint32_t(at)].pick == p_pick) {
            return false;
        }
        r_choice.named[uint32_t(at)].pick = p_pick;
        return true;
    }
    Named added;
    added.entity = p_entity;
    added.pick = p_pick;
    r_choice.named.push_back(added);
    return true;
}

bool forget(Choice &r_choice, const RID &p_entity) {
    const int at = named_index(r_choice, p_entity);
    if (at < 0) {
        return false;
    }
    r_choice.named.remove_at(uint32_t(at));
    return true;
}

void simulate_nearest(
    Choice &r_choice,
    int p_count,
    const StringName &p_layer
) {
    automatic(r_choice, p_count < 0 ? Policy::NONE : Policy::NEAREST, p_layer);
    r_choice.count = std::max(0, p_count);
}

void simulate_within(
    Choice &r_choice,
    double p_meters,
    const StringName &p_layer
) {
    automatic(
        r_choice,
        p_meters < 0.0 ? Policy::NONE : Policy::WITHIN,
        p_layer
    );
    r_choice.meters = std::max(0.0, p_meters);
}

void simulate_all(Choice &r_choice, const StringName &p_layer) {
    automatic(r_choice, Policy::ALL, p_layer);
}

void simulate_none(Choice &r_choice) {
    r_choice.policy = Policy::NONE;
    r_choice.layers.clear();
}

bool IdSet::note(uint64_t p_id) {
    for (uint32_t at = 0; at < ids.size(); ++at) {
        if (ids[at] == p_id) {
            return false;
        }
    }
    ids.push_back(p_id);
    return true;
}

bool IdSet::erase(uint64_t p_id) {
    for (uint32_t at = 0; at < ids.size(); ++at) {
        if (ids[at] == p_id) {
            ids.remove_at(at);
            return true;
        }
    }
    return false;
}

} // namespace netw::sim

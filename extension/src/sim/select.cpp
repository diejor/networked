#include "netw/sim/select.hpp"

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

int named_index(const Choice &p_choice, const RID &p_entity) {
    for (uint32_t at = 0; at < p_choice.named.size(); ++at) {
        if (p_choice.named[at].entity == p_entity) {
            return int(at);
        }
    }
    return -1;
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
        row->present = true;
        row->eligible = candidate.eligible;
        row->chosen = candidate.pick == Pick::CHOSEN;
        if (row->chosen
            || (all && candidate.pick == Pick::AUTOMATIC
                && candidate.eligible)) {
            desired.push_back(row->key);
        }
    }

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

bool Choice::selects_all(bool p_predicted) const {
    return stated ? !layers.is_empty() : p_predicted;
}

bool Choice::declared(bool p_predicted) const {
    return !named.is_empty() || selects_all(p_predicted);
}

const Named *Choice::named_of(const RID &p_entity) const {
    const int at = named_index(*this, p_entity);
    return at < 0 ? nullptr : &named[uint32_t(at)];
}

bool claims_exact(const Choice &p_choice, bool p_stepped) {
    return p_stepped && !p_choice.selects_all(true);
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

void simulate_all(Choice &r_choice, const StringName &p_layer) {
    r_choice.stated = true;
    r_choice.layers.clear();
    r_choice.layers.push_back(p_layer);
}

void simulate_none(Choice &r_choice) {
    r_choice.stated = true;
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

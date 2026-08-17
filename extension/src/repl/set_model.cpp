#include "netw/repl/set_model.hpp"

#include <algorithm>

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

namespace {

bool names(
    const SetRow &p_row,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) {
    return p_row.kind == p_kind && p_row.key == p_key
        && p_row.record == p_record;
}

} // namespace

bool row_authors(
    const SetRow &p_row,
    int64_t p_local_id,
    bool p_node_authority,
    int64_t p_controller
) {
    if (p_row.record == SET_RECORD_STATE) {
        return p_local_id == 1;
    }
    switch (WritePolicy(p_row.policy)) {
        case WritePolicy::AUTHORITY:
            return p_node_authority;
        case WritePolicy::CONTROLLER:
            return p_local_id == p_controller;
        case WritePolicy::ANY_PEER:
            return true;
    }
    return false;
}

PackedInt32Array row_recipients(
    const SetRow &p_row,
    int64_t p_local_id,
    const PackedInt32Array &p_live
) {
    PackedInt32Array out;
    if (p_row.audience == SET_AUDIENCE_SERVER_ONLY) {
        if (p_local_id != 1) {
            out.push_back(1);
        }
        return out;
    }
    for (int at = 0; at < p_live.size(); ++at) {
        const int32_t peer = p_live[at];
        if (int64_t(peer) != p_local_id) {
            out.push_back(peer);
        }
    }
    return out;
}

PackedInt32Array event_recipients(
    bool p_is_host,
    int64_t p_local_id,
    int64_t p_exclude,
    const PackedInt32Array &p_live
) {
    PackedInt32Array out;
    if (!p_is_host) {
        if (p_local_id != 1 && p_exclude != 1) {
            out.push_back(1);
        }
        return out;
    }
    for (int at = 0; at < p_live.size(); ++at) {
        const int32_t peer = p_live[at];
        if (int64_t(peer) != p_exclude) {
            out.push_back(peer);
        }
    }
    return out;
}

bool row_admits_sender(
    const SetRow &p_row,
    int64_t p_sender,
    int64_t p_node_authority,
    int64_t p_controller
) {
    if (p_sender == 1) {
        return true;
    }
    if (p_row.record == SET_RECORD_STATE) {
        return false;
    }
    return NetwEntityControl::policy_admits(
        int(p_row.policy),
        p_sender,
        p_node_authority,
        p_controller
    );
}

void SetModel::reindex(LocalVector<SetRow> &r_rows) {
    std::stable_sort(
        r_rows.ptr(),
        r_rows.ptr() + r_rows.size(),
        [](const SetRow &a, const SetRow &b) {
            if (a.kind != b.kind) {
                return a.kind < b.kind;
            }
            if (a.key != b.key) {
                return String(a.key) < String(b.key);
            }
            return a.record < b.record;
        }
    );
    for (uint32_t i = 0; i < r_rows.size(); ++i) {
        r_rows[i].ordinal = int64_t(i);
    }
}

int64_t SetModel::declare(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_comp,
    const RID &p_set,
    int64_t p_record,
    int64_t p_schema_hash,
    int64_t p_policy,
    int64_t p_audience
) {
    NETW_ZONE_NC("Set declare", colors::WIRE);
    if (p_route <= 0 || String(p_key).is_empty()) {
        NETW_WARN_ONCE(
            sys::WIRE,
            "A property set declared on route %d under key '%s' is not "
            "addressable, so nothing it holds will sync.",
            int(p_route),
            String(p_key).utf8().get_data()
        );
        return -1;
    }
    LocalVector<SetRow> &rows = routes[p_route];
    SetRow *found = nullptr;
    for (SetRow &row : rows) {
        if (names(row, p_kind, p_key, p_record)) {
            found = &row;
            break;
        }
    }
    if (found == nullptr) {
        SetRow fresh;
        fresh.route = p_route;
        fresh.kind = p_kind;
        fresh.key = p_key;
        fresh.record = p_record;
        rows.push_back(fresh);
        found = &rows[rows.size() - 1];
    }
    found->comp = p_comp;
    found->set = p_set;
    found->schema_hash = p_schema_hash;
    found->policy = p_policy;
    found->audience = p_audience;
    reindex(rows);

    const SetRow *reindexed = row_for(p_route, p_kind, p_key, p_record);
    return reindexed == nullptr ? -1 : reindexed->ordinal;
}

void SetModel::drop(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) {
    HashMap<int64_t, LocalVector<SetRow>>::Iterator held = routes.find(p_route);
    if (held == routes.end()) {
        return;
    }
    LocalVector<SetRow> &rows = held->value;
    for (uint32_t i = 0; i < rows.size(); ++i) {
        if (!names(rows[i], p_kind, p_key, p_record)) {
            continue;
        }
        rows.remove_at(i);
        if (rows.is_empty()) {
            routes.erase(p_route);
        } else {
            reindex(rows);
        }
        return;
    }
}

const SetRow *SetModel::row(int64_t p_route, int64_t p_ordinal) const {
    const LocalVector<SetRow> *rows = route_rows(p_route);
    if (rows == nullptr || p_ordinal < 0 || p_ordinal >= int64_t(rows->size())) {
        return nullptr;
    }
    return &(*rows)[uint32_t(p_ordinal)];
}

const SetRow *SetModel::row_for(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) const {
    const LocalVector<SetRow> *rows = route_rows(p_route);
    if (rows == nullptr) {
        return nullptr;
    }
    for (const SetRow &row : *rows) {
        if (names(row, p_kind, p_key, p_record)) {
            return &row;
        }
    }
    return nullptr;
}

const LocalVector<SetRow> *SetModel::route_rows(int64_t p_route) const {
    HashMap<int64_t, LocalVector<SetRow>>::ConstIterator held
        = routes.find(p_route);
    return held == routes.end() ? nullptr : &held->value;
}

void SetModel::note_descriptors(
    int64_t p_route,
    const HashMap<int64_t, int64_t> &p_descriptors
) {
    if (p_descriptors.is_empty()) {
        descriptors.erase(p_route);
        return;
    }
    descriptors[p_route] = p_descriptors;
}

bool SetModel::admits_schema(int64_t p_route, int64_t p_ordinal) const {
    HashMap<int64_t, HashMap<int64_t, int64_t>>::ConstIterator noted
        = descriptors.find(p_route);
    if (noted == descriptors.end()) {
        return true;
    }
    HashMap<int64_t, int64_t>::ConstIterator declared
        = noted->value.find(p_ordinal);
    if (declared == noted->value.end()) {
        return true;
    }
    const SetRow *found = row(p_route, p_ordinal);
    return found != nullptr && found->schema_hash == declared->value;
}

void SetModel::clear_route(int64_t p_route) {
    routes.erase(p_route);
    descriptors.erase(p_route);
}

void SetModel::clear() {
    routes.clear();
    descriptors.clear();
}

} // namespace netw::repl

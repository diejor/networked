#include "netw/lagcomp_core.hpp"

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *MODULE = "lagcomp";

} // namespace

NetwLagCompCore::Row *NetwLagCompCore::mutable_row_of(int64_t p_slot) {
    HashMap<int64_t, Row>::Iterator found = rows.find(p_slot);
    return found != rows.end() ? &found->value : nullptr;
}

const NetwLagCompCore::Row *NetwLagCompCore::row_of(int64_t p_slot) const {
    HashMap<int64_t, Row>::ConstIterator found = rows.find(p_slot);
    return found != rows.end() ? &found->value : nullptr;
}

int64_t NetwLagCompCore::timeline_open(int64_t p_history_limit) {
    const int64_t slot = next_slot;
    next_slot += 1;
    Row row;
    row.history = NetwTimeline::create(p_history_limit);
    rows.insert(slot, row);
    return slot;
}

Ref<NetwTimeline> NetwLagCompCore::timeline_history(int64_t p_slot) const {
    const Row *row = row_of(p_slot);
    return row != nullptr ? row->history : Ref<NetwTimeline>();
}

void NetwLagCompCore::timeline_close(int64_t p_slot) {
    rows.erase(p_slot);
}

bool NetwLagCompCore::timeline_is_open(int64_t p_slot) const {
    return row_of(p_slot) != nullptr;
}

bool NetwLagCompCore::timeline_bind_owner(int64_t p_slot, Object *p_owner) {
    Row *row = mutable_row_of(p_slot);
    return row != nullptr && row->port.bind(p_owner);
}

void NetwLagCompCore::timeline_unbind_owner(int64_t p_slot) {
    Row *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->port.unbind();
    }
}

bool NetwLagCompCore::timeline_owner_bound(int64_t p_slot) const {
    const Row *row = row_of(p_slot);
    return row != nullptr && row->port.bound();
}

void NetwLagCompCore::timeline_declare(int64_t p_slot, const Array &p_keys) {
    Row *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    row->declared.clear();
    for (int at = 0; at < p_keys.size(); ++at) {
        row->declared.push_back(StringName(p_keys[at]));
    }
}

Array NetwLagCompCore::timeline_declared(int64_t p_slot) const {
    Array out;
    const Row *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    for (uint32_t at = 0; at < row->declared.size(); ++at) {
        out.push_back(row->declared[at]);
    }
    return out;
}

Array NetwLagCompCore::touched_keys(const Row &p_row, const Dictionary &p_past) {
    if (p_row.declared.is_empty()) {
        return p_past.keys();
    }
    Array out;
    for (uint32_t at = 0; at < p_row.declared.size(); ++at) {
        if (p_past.has(p_row.declared[at])) {
            out.push_back(p_row.declared[at]);
        }
    }
    return out;
}

Dictionary NetwLagCompCore::declared_only(
    const Row &p_row,
    const Dictionary &p_payload
) {
    if (p_row.declared.is_empty()) {
        return p_payload;
    }
    Dictionary out;
    for (uint32_t at = 0; at < p_row.declared.size(); ++at) {
        const StringName &key = p_row.declared[at];
        if (p_payload.has(key)) {
            out[key] = p_payload[key];
        }
    }
    return out;
}

void NetwLagCompCore::timeline_record(
    int64_t p_slot,
    int64_t p_tick,
    const Dictionary &p_payload
) {
    Row *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->history->record_state(p_tick, p_payload);
    }
}

int64_t NetwLagCompCore::timeline_sample_tick(int64_t p_slot, int64_t p_tick)
    const {
    const Row *row = row_of(p_slot);
    return row != nullptr ? row->history->latest_state_tick_at_or_before(p_tick)
                          : -1;
}

Dictionary NetwLagCompCore::timeline_sample(int64_t p_slot, int64_t p_tick)
    const {
    const Row *row = row_of(p_slot);
    return row != nullptr ? row->history->latest_state_at_or_before(p_tick)
                          : Dictionary();
}

void NetwLagCompCore::timeline_trim_before(int64_t p_slot, int64_t p_tick) {
    Row *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->history->trim_before(p_tick);
    }
}

int NetwLagCompCore::rewind(
    const PackedInt64Array &p_slots,
    int64_t p_tick,
    const Callable &p_body
) {
    NETW_ZONE_NC("NetwLagComp rewind", colors::PREDICTION);
    LocalVector<int64_t> moved;
    LocalVector<Dictionary> live;
    for (int at = 0; at < p_slots.size(); ++at) {
        const int64_t slot = p_slots[at];
        Row *row = mutable_row_of(slot);
        if (row == nullptr || !row->port.bound()) {
            continue;
        }
        const Dictionary past = timeline_sample(slot, p_tick);
        if (past.is_empty()) {
            continue;
        }
        const Array keys = touched_keys(*row, past);
        if (keys.is_empty()) {
            continue;
        }
        const Dictionary held = port_capture(row->port, MODULE, keys);
        if (held.is_empty()) {
            continue;
        }
        port_apply(row->port, MODULE, declared_only(*row, past));
        port_sync_transform(row->port.resolve(MODULE));
        moved.push_back(slot);
        live.push_back(held);
    }

    if (p_body.is_valid()) {
        p_body.call();
    }

    for (uint32_t at = 0; at < moved.size(); ++at) {
        Row *row = mutable_row_of(moved[at]);
        if (row == nullptr) {
            continue;
        }
        port_apply(row->port, MODULE, live[at]);
        port_sync_transform(row->port.resolve(MODULE));
    }
    return int(moved.size());
}

void NetwLagCompCore::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("timeline_open", "history_limit"),
        &NetwLagCompCore::timeline_open
    );
    ClassDB::bind_method(
        D_METHOD("timeline_close", "slot"),
        &NetwLagCompCore::timeline_close
    );
    ClassDB::bind_method(
        D_METHOD("timeline_is_open", "slot"),
        &NetwLagCompCore::timeline_is_open
    );
    ClassDB::bind_method(
        D_METHOD("timeline_bind_owner", "slot", "owner"),
        &NetwLagCompCore::timeline_bind_owner
    );
    ClassDB::bind_method(
        D_METHOD("timeline_unbind_owner", "slot"),
        &NetwLagCompCore::timeline_unbind_owner
    );
    ClassDB::bind_method(
        D_METHOD("timeline_owner_bound", "slot"),
        &NetwLagCompCore::timeline_owner_bound
    );
    ClassDB::bind_method(
        D_METHOD("timeline_declare", "slot", "keys"),
        &NetwLagCompCore::timeline_declare
    );
    ClassDB::bind_method(
        D_METHOD("timeline_declared", "slot"),
        &NetwLagCompCore::timeline_declared
    );
    ClassDB::bind_method(
        D_METHOD("timeline_history", "slot"),
        &NetwLagCompCore::timeline_history
    );
    ClassDB::bind_method(
        D_METHOD("timeline_record", "slot", "tick", "payload"),
        &NetwLagCompCore::timeline_record
    );
    ClassDB::bind_method(
        D_METHOD("timeline_sample", "slot", "tick"),
        &NetwLagCompCore::timeline_sample
    );
    ClassDB::bind_method(
        D_METHOD("timeline_sample_tick", "slot", "tick"),
        &NetwLagCompCore::timeline_sample_tick
    );
    ClassDB::bind_method(
        D_METHOD("timeline_trim_before", "slot", "tick"),
        &NetwLagCompCore::timeline_trim_before
    );
    ClassDB::bind_method(
        D_METHOD("rewind", "slots", "tick", "body"),
        &NetwLagCompCore::rewind
    );
}

} // namespace netw

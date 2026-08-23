#include "netw/api/event_plane.hpp"

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

struct Named {
    int64_t value;
    const char *name;
    const char *group;
};

constexpr Named TAXONOMY[] = {
    { EventPlane::SPAWNING, "SPAWNING", "lifecycle" },
    { EventPlane::SPAWNED, "SPAWNED", "lifecycle" },
    { EventPlane::DESPAWNING, "DESPAWNING", "lifecycle" },
    { EventPlane::DESPAWNED, "DESPAWNED", "lifecycle" },
    { EventPlane::REPARENTED, "REPARENTED", "lifecycle" },
    { EventPlane::STAGE_TRANSITION, "STAGE_TRANSITION", "lifecycle" },
    { EventPlane::CONTROL_CHANGED, "CONTROL_CHANGED", "lifecycle" },
    { EventPlane::CONTROL_REQUESTED, "CONTROL_REQUESTED", "lifecycle" },

    { EventPlane::INTEREST_ENTER, "INTEREST_ENTER", "interest" },
    { EventPlane::INTEREST_EXIT, "INTEREST_EXIT", "interest" },
    { EventPlane::OBSERVER_ENTERED, "OBSERVER_ENTERED", "interest" },
    { EventPlane::OBSERVER_LEFT, "OBSERVER_LEFT", "interest" },
    { EventPlane::INTEREST_COMMIT, "INTEREST_COMMIT", "interest" },

    { EventPlane::EPISODE_OPEN, "EPISODE_OPEN", "prediction" },
    { EventPlane::EPISODE_CLOSE, "EPISODE_CLOSE", "prediction" },
    { EventPlane::EPISODE_FALLBACK, "EPISODE_FALLBACK", "prediction" },
    { EventPlane::PREDICT_DRIVE, "PREDICT_DRIVE", "prediction" },
    { EventPlane::PREDICT_CONSUME, "PREDICT_CONSUME", "prediction" },
    { EventPlane::PREDICT_EVALUATE, "PREDICT_EVALUATE", "prediction" },
    { EventPlane::PREDICT_RECOVER, "PREDICT_RECOVER", "prediction" },
    { EventPlane::DIVERGENCE, "DIVERGENCE", "prediction" },
    { EventPlane::RECOVERY, "RECOVERY", "prediction" },

    { EventPlane::GATE_SYNC, "GATE_SYNC", "gates" },
    { EventPlane::GATE_SPAWN, "GATE_SPAWN", "gates" },
    { EventPlane::GATE_TABLE, "GATE_TABLE", "gates" },
    { EventPlane::GATE_PREDICT, "GATE_PREDICT", "gates" },

    { EventPlane::SYNC_ENCODE, "SYNC_ENCODE", "stages" },
    { EventPlane::SYNC_DECODE, "SYNC_DECODE", "stages" },
    { EventPlane::GATHER, "GATHER", "stages" },
    { EventPlane::APPLY, "APPLY", "stages" },
    { EventPlane::SPAWN_DECLARE, "SPAWN_DECLARE", "stages" },
    { EventPlane::SPAWN_RECONCILE, "SPAWN_RECONCILE", "stages" },
    { EventPlane::SPAWN_CONSTRUCT, "SPAWN_CONSTRUCT", "stages" },
    { EventPlane::DISPLAY_RECORD, "DISPLAY_RECORD", "stages" },
    { EventPlane::DISPLAY_PUMP, "DISPLAY_PUMP", "stages" },
    { EventPlane::DISPLAY_WRITE, "DISPLAY_WRITE", "stages" },
    { EventPlane::TABLE_COMMIT, "TABLE_COMMIT", "stages" },

    { EventPlane::VERDICT, "VERDICT", "verdicts" },
    { EventPlane::SEAM_MISUSE, "SEAM_MISUSE", "verdicts" },

    { EventPlane::SESSION_STATE, "SESSION_STATE", "session" },
    { EventPlane::PEER_JOINED, "PEER_JOINED", "session" },
    { EventPlane::PEER_LEFT, "PEER_LEFT", "session" },
    { EventPlane::PEER_AUTH_FAILED, "PEER_AUTH_FAILED", "session" },
    { EventPlane::SCENE_LIVE, "SCENE_LIVE", "session" },

    { EventPlane::DATAGRAM_SENT, "DATAGRAM_SENT", "carrier" },
    { EventPlane::DATAGRAM_RECEIVED, "DATAGRAM_RECEIVED", "carrier" },
    { EventPlane::DATAGRAM_MALFORMED, "DATAGRAM_MALFORMED", "carrier" },
    { EventPlane::ACK_ADVANCED, "ACK_ADVANCED", "carrier" },
};

constexpr int TAXONOMY_SIZE = int(sizeof(TAXONOMY) / sizeof(TAXONOMY[0]));

const Named *named(int64_t event) {
    for (int index = 0; index < TAXONOMY_SIZE; index++) {
        if (TAXONOMY[index].value == event) {
            return &TAXONOMY[index];
        }
    }
    return nullptr;
}

struct Membership {
    const char *key;
    const char *detail_key;
};

constexpr Membership MEMBERSHIPS[] = {
    { "verdict_in", "" },        { "sender_in", "sender" },
    { "channel_in", "channel" }, { "comp_in", "comp" },
    { "stage_in", "stage" },     { "layer_in", "layer" },
};

constexpr int MEMBERSHIPS_SIZE
    = int(sizeof(MEMBERSHIPS) / sizeof(MEMBERSHIPS[0]));

bool is_predicate_key(const StringName &key) {
    for (int index = 0; index < MEMBERSHIPS_SIZE; index++) {
        if (key == StringName(MEMBERSHIPS[index].key)) {
            return true;
        }
    }
    return key == StringName("tick_min") || key == StringName("tick_max");
}

bool is_target_key(const StringName &key) {
    return key == StringName("route") || key == StringName("entity_id")
        || key == StringName("peer");
}

bool is_option_key(const StringName &key) {
    return key == StringName("phase") || key == StringName("enabled")
        || key == StringName("once") || key == StringName("dedupe")
        || key == StringName("note");
}

bool is_text(const Variant &value) {
    return value.get_type() == Variant::STRING
        || value.get_type() == Variant::STRING_NAME;
}

bool same_value_ignoring_string_kind(const Variant &a, const Variant &b) {
    if (is_text(a) && is_text(b)) {
        return String(a) == String(b);
    }
    return a == b;
}

bool holds(const Array &allowed, const Variant &value) {
    for (int index = 0; index < allowed.size(); index++) {
        if (same_value_ignoring_string_kind(allowed[index], value)) {
            return true;
        }
    }
    return false;
}

int64_t claim_key(int64_t event, int64_t verdict) {
    return event * 1024 + (verdict & 1023);
}

} // namespace

bool EventPlane::is_event(int64_t event) {
    return named(event) != nullptr;
}

const char *EventPlane::group_of(int64_t event) {
    const Named *found = named(event);
    return found == nullptr ? "" : found->group;
}

const char *EventPlane::name_of(int64_t event) {
    const Named *found = named(event);
    return found == nullptr ? "" : found->name;
}

bool EventPlane::is_terminal(int64_t event) {
    return event == DESPAWNED;
}

int EventPlane::taxonomy_size() {
    return TAXONOMY_SIZE;
}

int64_t EventPlane::value_at(int p_index) {
    if (p_index < 0 || p_index >= TAXONOMY_SIZE) {
        return -1;
    }
    return TAXONOMY[p_index].value;
}

int64_t EventPlane::watch(
    const PackedInt64Array &p_events,
    const Dictionary &p_target,
    const Dictionary &p_predicate,
    const Callable &p_sink,
    const Dictionary &p_opts
) {
    NETW_ZONE_NC("EventPlane watch", colors::SESSION);
    if (p_events.is_empty()) {
        NETW_ERROR(sys::EVENT, "a watch row names no events");
        return -1;
    }
    Watch row;
    for (int index = 0; index < p_events.size(); index++) {
        const int64_t event = p_events[index];
        if (!is_event(event)) {
            NETW_ERROR(sys::EVENT, "watch refused, unknown event %d", event);
            return -1;
        }
        row.events.push_back(event);
    }
    const Array target_keys = p_target.keys();
    for (int index = 0; index < target_keys.size(); index++) {
        const StringName key = target_keys[index];
        if (!is_target_key(key)) {
            NETW_ERROR(
                sys::EVENT,
                "watch refused, unknown target key %s",
                String(key)
            );
            return -1;
        }
    }
    const Array predicate_keys = p_predicate.keys();
    for (int index = 0; index < predicate_keys.size(); index++) {
        const StringName key = predicate_keys[index];
        if (!is_predicate_key(key)) {
            NETW_ERROR(
                sys::EVENT,
                "watch refused, unknown predicate key %s",
                String(key)
            );
            return -1;
        }
    }
    const Array option_keys = p_opts.keys();
    for (int index = 0; index < option_keys.size(); index++) {
        const StringName key = option_keys[index];
        if (!is_option_key(key)) {
            NETW_ERROR(
                sys::EVENT,
                "watch refused, unknown option key %s",
                String(key)
            );
            return -1;
        }
    }

    row.id = next_id++;
    row.route = p_target.get("route", 0);
    row.entity_id = p_target.get("entity_id", StringName());
    row.peer = p_target.get("peer", 0);
    row.predicate = p_predicate;
    row.sink = p_sink;
    row.phase = p_opts.get("phase", AFTER);
    row.enabled = p_opts.get("enabled", true);
    row.once = p_opts.get("once", false);
    row.note = p_opts.get("note", String());

    bool verdict_row = false;
    for (const int64_t event : row.events) {
        verdict_row = verdict_row || group_of(event) == group_of(VERDICT);
    }
    row.dedupe = p_opts.get("dedupe", verdict_row);

    rows.push_back(row);
    rebuild_hot();
    return row.id;
}

bool EventPlane::unwatch(int64_t p_id) {
    for (uint32_t index = 0; index < rows.size(); index++) {
        if (rows[index].id != p_id) {
            continue;
        }
        rows.remove_at(index);
        rebuild_hot();
        return true;
    }
    return false;
}

Array EventPlane::watches() const {
    Array listed;
    for (const Watch &row : rows) {
        Dictionary described;
        PackedInt64Array events;
        for (const int64_t event : row.events) {
            events.push_back(event);
        }
        described["id"] = row.id;
        described["events"] = events;
        described["phase"] = row.phase;
        described["route"] = row.route;
        described["entity_id"] = row.entity_id;
        described["peer"] = row.peer;
        described["predicate"] = row.predicate;
        described["enabled"] = row.enabled;
        described["once"] = row.once;
        described["dedupe"] = row.dedupe;
        described["note"] = row.note;
        described["hit_count"] = row.hit_count;
        listed.push_back(described);
    }
    return listed;
}

bool EventPlane::wants(int64_t p_event, int64_t) const {
    if (!is_event(p_event)) {
        return false;
    }
    if (armed_for_lane_threads.load(std::memory_order_relaxed)) {
        return true;
    }
    return hot_for_lane_threads[p_event].load(std::memory_order_relaxed) > 0;
}

void EventPlane::emit(const Emission &p_fact) {
    NETW_ZONE_NC("EventPlane emit", colors::SESSION);
    if (!is_event(p_fact.event)) {
        NETW_ERROR_ONCE(sys::EVENT, "emit refused, unknown event %d", p_fact.event);
        return;
    }
    Emission carried = p_fact;
    if (!carried.model.is_empty()) {
        snapshots[carried.route] = carried.model;
    } else if (is_terminal(carried.event)) {
        const Dictionary *found = snapshots.getptr(carried.route);
        if (found != nullptr) {
            carried.model = *found;
        }
    }
    if (!carried.entity_id.is_empty()) {
        identities[carried.route] = carried.entity_id;
    } else if (carried.route != 0) {
        const StringName *known = identities.getptr(carried.route);
        if (known != nullptr) {
            carried.entity_id = *known;
        }
    }
    if (armed_for_lane_threads.load(std::memory_order_relaxed)
        || hot_for_lane_threads[carried.event].load(std::memory_order_relaxed)
               > 0) {
        record(carried.route, carried);
    }
    deliver(carried);
    if (is_terminal(carried.event)) {
        rings.erase(carried.route);
        snapshots.erase(carried.route);
        identities.erase(carried.route);
    }
}

void EventPlane::stage(const Emission &p_fact) {
    std::lock_guard<std::mutex> guard(staged_lock);
    staged.push_back(p_fact);
}

void EventPlane::drain_staged() {
    LocalVector<Emission> batch;
    {
        std::lock_guard<std::mutex> guard(staged_lock);
        if (staged.is_empty()) {
            return;
        }
        for (const Emission &fact : staged) {
            batch.push_back(fact);
        }
        staged.clear();
    }
    NETW_ZONE_NC("EventPlane drain", colors::SESSION);
    for (const Emission &fact : batch) {
        Emission delivered = fact;
        delivered.phase = AFTER;
        emit(delivered);
    }
}

Array EventPlane::ring(int64_t p_route) {
    Array drained;
    Ring *found = rings.getptr(p_route);
    if (found == nullptr) {
        return drained;
    }
    const uint32_t size = found->rows.size();
    for (uint32_t offset = 0; offset < size; offset++) {
        const uint32_t at = (uint32_t(found->head) + offset) % size;
        Ref<NetwEvent> record;
        record.instantiate();
        const Emission &row = found->rows[at];
        record->event = row.event;
        record->phase = row.phase;
        record->tick = row.tick;
        record->route = row.route;
        record->entity_id = row.entity_id;
        record->peer = row.peer;
        record->verdict = row.verdict;
        record->detail = row.detail.duplicate();
        record->model = row.model.duplicate();
        drained.push_back(record);
    }
    rings.erase(p_route);
    return drained;
}

void EventPlane::ring_clear(int64_t p_route) {
    rings.erase(p_route);
}

void EventPlane::set_armed(bool p_enabled) {
    armed_for_lane_threads.store(p_enabled, std::memory_order_relaxed);
}

bool EventPlane::is_armed() const {
    return armed_for_lane_threads.load(std::memory_order_relaxed);
}

void EventPlane::clear() {
    rows.clear();
    rings.clear();
    snapshots.clear();
    identities.clear();
    {
        std::lock_guard<std::mutex> guard(staged_lock);
        staged.clear();
    }
    rebuild_hot();
}

void EventPlane::rebuild_hot() {
    for (int64_t event = 0; event < EVENT_LIMIT; event++) {
        hot_for_lane_threads[event].store(0, std::memory_order_relaxed);
    }
    for (const Watch &row : rows) {
        if (!row.enabled) {
            continue;
        }
        for (const int64_t event : row.events) {
            hot_for_lane_threads[event].fetch_add(1, std::memory_order_relaxed);
        }
    }
}

bool EventPlane::matches(const Watch &p_row, const Emission &p_fact) const {
    if (!p_row.enabled || p_row.phase != p_fact.phase) {
        return false;
    }
    bool named_event = false;
    for (const int64_t event : p_row.events) {
        named_event = named_event || event == p_fact.event;
    }
    if (!named_event) {
        return false;
    }
    if (p_row.route != 0 && p_row.route != p_fact.route) {
        return false;
    }
    if (!p_row.entity_id.is_empty() && p_row.entity_id != p_fact.entity_id) {
        return false;
    }
    if (p_row.peer != 0 && p_row.peer != p_fact.peer) {
        return false;
    }
    if (p_row.predicate.is_empty()) {
        return true;
    }
    for (int index = 0; index < MEMBERSHIPS_SIZE; index++) {
        const StringName key(MEMBERSHIPS[index].key);
        if (!p_row.predicate.has(key)) {
            continue;
        }
        const Array allowed = p_row.predicate[key];
        const String detail_key(MEMBERSHIPS[index].detail_key);
        const Variant value = detail_key.is_empty()
            ? Variant(p_fact.verdict)
            : p_fact.detail.get(detail_key, Variant());
        if (!holds(allowed, value)) {
            return false;
        }
    }
    if (p_row.predicate.has(StringName("tick_min"))
        && p_fact.tick < int64_t(p_row.predicate["tick_min"])) {
        return false;
    }
    if (p_row.predicate.has(StringName("tick_max"))
        && p_fact.tick > int64_t(p_row.predicate["tick_max"])) {
        return false;
    }
    return true;
}

void EventPlane::deliver(const Emission &p_fact) {
    LocalVector<int64_t> matched;
    for (const Watch &row : rows) {
        if (matches(row, p_fact)) {
            matched.push_back(row.id);
        }
    }
    if (matched.is_empty()) {
        return;
    }
    for (const int64_t id : matched) {
        Watch *row = nullptr;
        for (Watch &candidate : rows) {
            if (candidate.id == id) {
                row = &candidate;
                break;
            }
        }
        if (row == nullptr || !row->enabled) {
            continue;
        }
        if (row->dedupe) {
            HashSet<int64_t> &claimed = row->claimed[p_fact.route];
            const int64_t key = claim_key(p_fact.event, p_fact.verdict);
            if (claimed.has(key)) {
                continue;
            }
            claimed.insert(key);
        }
        row->hit_count++;
        if (row->once) {
            row->enabled = false;
            rebuild_hot();
        }
        if (!row->sink.is_valid()) {
            continue;
        }
        Ref<NetwEvent> record;
        record.instantiate();
        record->event = p_fact.event;
        record->phase = p_fact.phase;
        record->tick = p_fact.tick;
        record->route = p_fact.route;
        record->entity_id = p_fact.entity_id;
        record->peer = p_fact.peer;
        record->verdict = p_fact.verdict;
        record->detail = p_fact.detail.duplicate();
        record->model = p_fact.model.duplicate();
        const Callable sink = row->sink;
        sink.call(record);
    }
}

void EventPlane::record(int64_t p_route, const Emission &p_fact) {
    Ring &held = rings[p_route];
    if (held.rows.size() < uint32_t(RING_CAPACITY)) {
        held.rows.push_back(p_fact);
        return;
    }
    held.rows[uint32_t(held.head)] = p_fact;
    held.head = (held.head + 1) % RING_CAPACITY;
}

String NetwEvent::get_event_name() const {
    return String(EventPlane::name_of(event));
}

void NetwEvent::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_event"), &NetwEvent::get_event);
    ClassDB::bind_method(D_METHOD("get_phase"), &NetwEvent::get_phase);
    ClassDB::bind_method(D_METHOD("get_tick"), &NetwEvent::get_tick);
    ClassDB::bind_method(D_METHOD("get_route"), &NetwEvent::get_route);
    ClassDB::bind_method(D_METHOD("get_entity_id"), &NetwEvent::get_entity_id);
    ClassDB::bind_method(D_METHOD("get_peer"), &NetwEvent::get_peer);
    ClassDB::bind_method(D_METHOD("get_verdict"), &NetwEvent::get_verdict);
    ClassDB::bind_method(D_METHOD("get_detail"), &NetwEvent::get_detail);
    ClassDB::bind_method(D_METHOD("get_model"), &NetwEvent::get_model);
    ClassDB::bind_method(
        D_METHOD("get_event_name"),
        &NetwEvent::get_event_name
    );

    ADD_PROPERTY(PropertyInfo(Variant::INT, "event"), "", "get_event");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "phase"), "", "get_phase");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "tick"), "", "get_tick");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "route"), "", "get_route");
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "entity_id"),
        "",
        "get_entity_id"
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "peer"), "", "get_peer");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "verdict"), "", "get_verdict");
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "detail"),
        "",
        "get_detail"
    );
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "model"), "", "get_model");
}

} // namespace netw

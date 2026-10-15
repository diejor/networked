#include "netw/lagcomp_core.hpp"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/colliders.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr SubsystemName MODULE = sys::LAGCOMP;

bool is_suspended_call(const Variant &p_answered) {
    if (p_answered.get_type() != Variant::OBJECT) {
        return false;
    }
    const Object *answered = p_answered;
    return answered != nullptr
        && answered->is_class(StringName("GDScriptFunctionState"));
}

bool same_value(const Variant &p_now, const Variant &p_wrote) {
    if (p_now.get_type() == Variant::QUATERNION
        && p_wrote.get_type() == Variant::QUATERNION) {
        const Quaternion now = p_now;
        const Quaternion wrote = p_wrote;
        return now.is_equal_approx(wrote) || now.is_equal_approx(-wrote);
    }
    return p_now == p_wrote;
}

void report_write_inside(Object *p_owner, const Dictionary &p_wrote) {
    Node *node = Object::cast_to<Node>(p_owner);
    if (node == nullptr) {
        return;
    }
    const Array keys = p_wrote.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const StringName key = keys[at];
        if (same_value(port_get(p_owner, key), p_wrote[key])) {
            continue;
        }
        if (!colliders::first_report(
                p_owner,
                colliders::Fault::WRITE_INSIDE_REWIND
            )) {
            return;
        }
        NETW_WARN(
            MODULE,
            "'%s.%s' changed inside a rewind, and the rewind put it back. "
            "Change it after rewind returns.",
            String(node->get_name()),
            String(key)
        );
        return;
    }
}

} // namespace

NetwLagCompCore::~NetwLagCompCore() {
    for (Row *row : rows) {
        if (row != nullptr) {
            memdelete(row);
        }
    }
    rows.clear();
}

NetwLagCompCore::Row *NetwLagCompCore::mutable_row_of(int64_t p_slot) {
    const int64_t seat = ids.seat(p_slot);
    return seat >= 0 && seat < int64_t(rows.size()) ? rows[uint32_t(seat)]
                                                    : nullptr;
}

const NetwLagCompCore::Row *NetwLagCompCore::row_of(int64_t p_slot) const {
    const int64_t seat = ids.seat(p_slot);
    return seat >= 0 && seat < int64_t(rows.size()) ? rows[uint32_t(seat)]
                                                    : nullptr;
}

void NetwLagCompCore::bind_store(
    int64_t p_slot,
    const store::EntityRef &p_ref
) {
    Row *row = mutable_row_of(p_slot);
    if (row == nullptr || world == nullptr) {
        return;
    }
    if (row->store != p_ref) {
        unbind_store(p_slot);
    }
    if (!world->holds(p_ref)) {
        return;
    }
    row->store = p_ref;
    world->bind_lagcomp(p_ref.slot, p_slot);
}

void NetwLagCompCore::unbind_store(int64_t p_slot) {
    Row *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const store::EntityRef held = row->store;
    row->store = store::EntityRef();
    if (world != nullptr && world->holds(held)
        && world->lagcomp_of(held.slot) == p_slot) {
        world->bind_lagcomp(held.slot, -1);
    }
}

store::EntityRef NetwLagCompCore::store_ref_of(int64_t p_slot) const {
    const Row *row = row_of(p_slot);
    return row != nullptr ? row->store : store::EntityRef();
}

int64_t NetwLagCompCore::timeline_open(int64_t p_history_limit) {
    const int64_t slot = ids.acquire();
    const uint32_t seat = SlotIds::index_of(slot);
    Row *row = memnew(Row);
    row->book.set_window(p_history_limit);
    while (rows.size() <= seat) {
        rows.push_back(nullptr);
    }
    rows[seat] = row;
    return slot;
}

const LagBook *NetwLagCompCore::book_of(int64_t p_slot) const {
    const Row *row = row_of(p_slot);
    return row != nullptr ? &row->book : nullptr;
}

Ref<RefCounted> NetwLagCompCore::entity_at(int64_t p_slot) const {
    const Row *row = row_of(p_slot);
    return row != nullptr ? row->entity : Ref<RefCounted>();
}

uint64_t NetwLagCompCore::entity_key(const Ref<RefCounted> &p_entity) {
    return p_entity.is_valid() ? uint64_t(p_entity->get_instance_id())
                               : uint64_t(0);
}

int64_t NetwLagCompCore::timeline_register(
    const Ref<RefCounted> &p_entity,
    int64_t p_history_limit
) {
    const uint64_t key = entity_key(p_entity);
    if (key == 0) {
        NETW_ERR_V(
            -1,
            MODULE,
            "Lag compensation was asked to track a null entity."
        );
    }
    HashMap<uint64_t, int64_t>::ConstIterator seated = slot_by_entity.find(key);
    if (seated != slot_by_entity.end()) {
        NETW_TRACE(
            MODULE,
            "Entity %d already has lag compensation history %d.",
            int64_t(key),
            seated->value
        );
        return seated->value;
    }
    const int64_t slot = timeline_open(p_history_limit);
    Row *row = mutable_row_of(slot);
    row->entity = p_entity;
    slot_by_entity.insert(key, slot);
    NETW_TRACE(
        MODULE,
        "Entity %d got lag compensation history %d, %d in all.",
        int64_t(key),
        slot,
        int64_t(slot_by_entity.size())
    );
    return slot;
}

int64_t NetwLagCompCore::timeline_slot_of(
    const Ref<RefCounted> &p_entity
) const {
    HashMap<uint64_t, int64_t>::ConstIterator seated
        = slot_by_entity.find(entity_key(p_entity));
    return seated != slot_by_entity.end() ? seated->value : -1;
}

void NetwLagCompCore::timeline_unregister(const Ref<RefCounted> &p_entity) {
    const int64_t slot = timeline_slot_of(p_entity);
    if (slot < 0) {
        return;
    }
    NETW_TRACE(
        MODULE,
        "Lag compensation history %d was released, %d left.",
        slot,
        int64_t(slot_by_entity.size()) - 1
    );
    timeline_close(slot);
}

int64_t NetwLagCompCore::timeline_registered() const {
    return int64_t(slot_by_entity.size());
}

Dictionary NetwLagCompCore::timeline_sample_entity(
    const Ref<RefCounted> &p_entity,
    int64_t p_tick
) const {
    const int64_t slot = timeline_slot_of(p_entity);
    Dictionary sampled = timeline_sample(slot, p_tick);
    const Row *row = row_of(slot);
    if (row == nullptr || row->parts.is_empty()) {
        return sampled;
    }
    Node *root = Object::cast_to<Node>(gd::object_of(row->port.id));
    for (const int64_t part : row->parts) {
        const Row *held = row_of(part);
        Node *node = held != nullptr
            ? Object::cast_to<Node>(gd::object_of(held->port.id))
            : nullptr;
        if (root == nullptr || node == nullptr) {
            continue;
        }
        const String prefix = String(root->get_path_to(node)) + ":";
        const Dictionary past = timeline_sample(part, p_tick);
        const Array keys = past.keys();
        for (int at = 0; at < keys.size(); ++at) {
            sampled[StringName(prefix + String(keys[at]))] = past[keys[at]];
        }
    }
    return sampled;
}

int64_t NetwLagCompCore::timeline_part(
    int64_t p_slot,
    uint64_t p_binding,
    Object *p_node,
    const Array &p_keys
) {
    Row *row = mutable_row_of(p_slot);
    if (row == nullptr || p_binding == 0 || p_node == nullptr) {
        return -1;
    }
    for (const int64_t part : row->parts) {
        Row *held = mutable_row_of(part);
        if (held != nullptr && held->part_binding == p_binding) {
            held->part_kept = true;
            if (held->port.id != gd::instance_id(p_node)) {
                held->port.bind(p_node);
            }
            return part;
        }
    }
    const int64_t part = timeline_open(row->book.get_window());
    Row *held = mutable_row_of(part);
    held->part_binding = p_binding;
    held->part_kept = true;
    held->port.bind(p_node);
    timeline_declare(part, p_keys);
    mutable_row_of(p_slot)->parts.push_back(part);
    return part;
}

void NetwLagCompCore::timeline_prune_parts(int64_t p_slot) {
    Row *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    for (uint32_t at = row->parts.size(); at > 0; --at) {
        const int64_t part = row->parts[at - 1];
        Row *held = mutable_row_of(part);
        if (held != nullptr && held->part_kept) {
            held->part_kept = false;
            continue;
        }
        row->parts.remove_at(at - 1);
        timeline_close(part);
    }
}

void NetwLagCompCore::timeline_close(int64_t p_slot) {
    Row *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    while (!row->parts.is_empty()) {
        const int64_t part = row->parts[row->parts.size() - 1];
        row->parts.remove_at(row->parts.size() - 1);
        timeline_close(part);
    }
    if (row->entity.is_valid()) {
        slot_by_entity.erase(entity_key(row->entity));
    }
    unbind_store(p_slot);
    rows[SlotIds::index_of(p_slot)] = nullptr;
    ids.release(p_slot);
    memdelete(row);
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
    row->declared_changed = true;
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

Array NetwLagCompCore::touched_keys(
    const Row &p_row,
    const Dictionary &p_past
) {
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
        return p_payload.duplicate();
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
    if (row == nullptr) {
        return;
    }
    if (!row->book.is_compiled() || row->declared_changed) {
        Array keys;
        for (const StringName &key : row->declared) {
            keys.push_back(key);
        }
        row->book.compile_keys(keys.is_empty() ? p_payload.keys() : keys);
        row->declared_changed = false;
    }
    row->book.record(p_tick, p_payload);
}

void NetwLagCompCore::timeline_record_coded(
    int64_t p_slot,
    int64_t p_tick,
    const Dictionary &p_payload,
    const predict::FieldCodec &p_source
) {
    Row *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    if (!row->book.is_compiled() || row->declared_changed) {
        row->book.compile(p_source, row->declared);
        row->declared_changed = false;
    }
    row->book.record(p_tick, p_payload);
}

int64_t NetwLagCompCore::timeline_sample_tick(
    int64_t p_slot,
    int64_t p_tick
) const {
    const Row *row = row_of(p_slot);
    return row != nullptr ? row->book.sample_tick(p_tick) : -1;
}

Dictionary NetwLagCompCore::timeline_sample(
    int64_t p_slot,
    int64_t p_tick
) const {
    const Row *row = row_of(p_slot);
    return row != nullptr ? row->book.sample(p_tick) : Dictionary();
}

bool NetwLagCompCore::timeline_recorded_at(
    int64_t p_slot,
    int64_t p_tick
) const {
    const Row *row = row_of(p_slot);
    return row != nullptr && row->book.recorded_at(p_tick);
}

void NetwLagCompCore::timeline_trim_before(int64_t p_slot, int64_t p_tick) {
    Row *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->book.trim_before(p_tick);
    }
}

Variant NetwLagCompCore::rewind(
    const PackedInt64Array &p_slots,
    int64_t p_tick,
    const Callable &p_body,
    int *r_moved
) {
    NETW_ZONE_NC("NetwLagComp rewind", colors::PREDICTION);
    LocalVector<int64_t> moved;
    LocalVector<Dictionary> live;
    LocalVector<Dictionary> wrote;
    LocalVector<colliders::ServerPose> server_poses;
    LocalVector<int64_t> slots;
    for (int at = 0; at < p_slots.size(); ++at) {
        slots.push_back(p_slots[at]);
        const Row *row = row_of(p_slots[at]);
        if (row != nullptr) {
            for (const int64_t part : row->parts) {
                slots.push_back(part);
            }
        }
    }
    for (const int64_t slot : slots) {
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
        Object *owner = row->port.resolve(MODULE);
        colliders::hold_server_poses(owner, server_poses);
        Dictionary written = declared_only(*row, past);
        port_apply(row->port, MODULE, written);
        colliders::teleport(owner);
        colliders::report_unseen(owner);
        moved.push_back(slot);
        live.push_back(held);
        for (int at = 0; at < keys.size(); ++at) {
            const StringName key = keys[at];
            written[key] = port_get(owner, key);
        }
        wrote.push_back(written);
    }

    const Variant answered = p_body.is_valid() ? p_body.call() : Variant();
    if (is_suspended_call(answered)) {
        NETW_WARN_ONCE(
            MODULE,
            "The rewind body awaited, so the rest of it ran after the rewind "
            "ended. Remove the await from it."
        );
    }

    for (uint32_t at = 0; at < moved.size(); ++at) {
        Row *row = mutable_row_of(moved[at]);
        if (row == nullptr) {
            continue;
        }
        Object *owner = row->port.resolve(MODULE);
        report_write_inside(owner, wrote[at]);
        port_apply(row->port, MODULE, live[at]);
        colliders::restore_back(owner, server_poses);
    }
    if (r_moved != nullptr) {
        *r_moved = int(moved.size());
    }
    return answered;
}

} // namespace netw

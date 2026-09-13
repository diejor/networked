#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/timeline.hpp"
#include "netw/object_port.hpp"

namespace netw {

class NetwLagCompCore {
    struct Row {
        ObjectPort port;
        godot::Ref<NetwTimeline> history;
        godot::LocalVector<godot::StringName> declared;
        godot::Ref<godot::RefCounted> entity;
    };

    godot::HashMap<int64_t, Row> rows;
    godot::HashMap<uint64_t, int64_t> slot_by_entity;
    int64_t next_slot = 1;

    static uint64_t entity_key(const godot::Ref<godot::RefCounted> &p_entity);

    Row *mutable_row_of(int64_t p_slot);
    const Row *row_of(int64_t p_slot) const;

    static godot::Array touched_keys(
        const Row &p_row,
        const godot::Dictionary &p_past
    );
    static godot::Dictionary declared_only(
        const Row &p_row,
        const godot::Dictionary &p_payload
    );

public:
    int64_t timeline_open(int64_t p_history_limit);
    void timeline_close(int64_t p_slot);
    bool timeline_is_open(int64_t p_slot) const;
    bool timeline_bind_owner(int64_t p_slot, godot::Object *p_owner);
    void timeline_unbind_owner(int64_t p_slot);
    bool timeline_owner_bound(int64_t p_slot) const;

    void timeline_declare(int64_t p_slot, const godot::Array &p_keys);
    godot::Array timeline_declared(int64_t p_slot) const;

    godot::Ref<NetwTimeline> timeline_history(int64_t p_slot) const;

    int64_t timeline_register(
        const godot::Ref<godot::RefCounted> &p_entity,
        int64_t p_history_limit
    );
    int64_t timeline_slot_of(
        const godot::Ref<godot::RefCounted> &p_entity
    ) const;
    void timeline_unregister(const godot::Ref<godot::RefCounted> &p_entity);
    int64_t timeline_registered() const;
    godot::Dictionary timeline_entities() const;
    godot::Dictionary timeline_sample_entity(
        const godot::Ref<godot::RefCounted> &p_entity,
        int64_t p_tick
    ) const;

    void timeline_record(
        int64_t p_slot,
        int64_t p_tick,
        const godot::Dictionary &p_payload
    );
    godot::Dictionary timeline_sample(int64_t p_slot, int64_t p_tick) const;
    int64_t timeline_sample_tick(int64_t p_slot, int64_t p_tick) const;
    void timeline_trim_before(int64_t p_slot, int64_t p_tick);

    int rewind(
        const godot::PackedInt64Array &p_slots,
        int64_t p_tick,
        const godot::Callable &p_body
    );
};

} // namespace netw

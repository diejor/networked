#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/lagcomp_book.hpp"
#include "netw/object_port.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/slot_ids.hpp"
#include "netw/store/world.hpp"

namespace netw {

class NetwLagCompCore {
    struct Row {
        ObjectPort port;
        LagBook book;
        godot::LocalVector<godot::StringName> declared;
        godot::Ref<godot::RefCounted> entity;
        store::EntityRef store;
        bool declared_changed = false;
    };

    godot::LocalVector<Row *> rows;
    godot::HashMap<uint64_t, int64_t> slot_by_entity;
    SlotIds ids;
    store::World *world = nullptr;

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
    NetwLagCompCore() = default;
    NetwLagCompCore(const NetwLagCompCore &) = delete;
    NetwLagCompCore &operator=(const NetwLagCompCore &) = delete;
    ~NetwLagCompCore();

    void seat_world(store::World *p_world) {
        world = p_world;
    }

    void bind_store(int64_t p_slot, const store::EntityRef &p_ref);
    void unbind_store(int64_t p_slot);
    store::EntityRef store_ref_of(int64_t p_slot) const;

    int64_t timeline_open(int64_t p_history_limit);
    void timeline_close(int64_t p_slot);
    bool timeline_is_open(int64_t p_slot) const;
    bool timeline_bind_owner(int64_t p_slot, godot::Object *p_owner);
    void timeline_unbind_owner(int64_t p_slot);
    bool timeline_owner_bound(int64_t p_slot) const;

    void timeline_declare(int64_t p_slot, const godot::Array &p_keys);
    godot::Array timeline_declared(int64_t p_slot) const;

    const LagBook *book_of(int64_t p_slot) const;

    int64_t timeline_register(
        const godot::Ref<godot::RefCounted> &p_entity,
        int64_t p_history_limit
    );
    int64_t timeline_slot_of(
        const godot::Ref<godot::RefCounted> &p_entity
    ) const;
    void timeline_unregister(const godot::Ref<godot::RefCounted> &p_entity);
    int64_t timeline_registered() const;
    godot::Dictionary timeline_sample_entity(
        const godot::Ref<godot::RefCounted> &p_entity,
        int64_t p_tick
    ) const;

    uint32_t seat_capacity() const {
        return ids.capacity();
    }

    int64_t slot_at_seat(uint32_t p_seat) const {
        return ids.id_at(p_seat);
    }

    godot::Ref<godot::RefCounted> entity_at(int64_t p_slot) const;

    void timeline_record(
        int64_t p_slot,
        int64_t p_tick,
        const godot::Dictionary &p_payload
    );
    void timeline_record_coded(
        int64_t p_slot,
        int64_t p_tick,
        const godot::Dictionary &p_payload,
        const predict::FieldCodec &p_source
    );
    godot::Dictionary timeline_sample(int64_t p_slot, int64_t p_tick) const;
    int64_t timeline_sample_tick(int64_t p_slot, int64_t p_tick) const;
    bool timeline_recorded_at(int64_t p_slot, int64_t p_tick) const;
    void timeline_trim_before(int64_t p_slot, int64_t p_tick);

    int rewind(
        const godot::PackedInt64Array &p_slots,
        int64_t p_tick,
        const godot::Callable &p_body
    );
};

} // namespace netw

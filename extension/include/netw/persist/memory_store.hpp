#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/database_connection.hpp"

namespace netw::persist {

class MemoryStore {
    godot::
        HashMap<godot::String, godot::HashMap<godot::String, godot::Dictionary>>
            slots;

public:
    bool has_slot(const godot::String &p_slot) const;
    void open_slot(const godot::String &p_slot);
    bool erase_slot(const godot::String &p_slot);
    godot::PackedStringArray slot_names() const;

    bool read(
        const godot::String &p_slot,
        const godot::String &p_address,
        godot::Dictionary &r_envelope
    ) const;
    void replace(
        const godot::String &p_slot,
        const godot::String &p_address,
        const godot::Dictionary &p_envelope
    );
    void erase(const godot::String &p_slot, const godot::String &p_address);
    godot::Array page(
        const godot::String &p_slot,
        const godot::String &p_schema_name,
        int p_kind,
        const godot::String &p_cursor,
        int p_limit,
        godot::String &r_next
    ) const;
    void clear();
};

MemoryStore &store_named(const godot::String &p_root);
void forget_stores();

class MemoryConnection : public NetwDatabaseConnection {
    godot::String root;
    godot::String slot;
    bool deferring = false;
    godot::Error next_failure = godot::OK;
    bool doubting = false;
    godot::Error doubted = godot::OK;
    int closes = 0;
    godot::LocalVector<godot::Ref<NetwPromise>> withheld;
    godot::LocalVector<godot::Variant> withheld_values;

    godot::Ref<NetwPromise> answer(const godot::Variant &p_value);

public:
    static godot::Ref<MemoryConnection> opened(
        const godot::String &p_root,
        const godot::String &p_slot
    );

    void defer(bool p_value);
    void fail_next(godot::Error p_error);
    void doubt_next(godot::Error p_error);
    int close_count() const;
    int withheld_count() const;
    void release();

    godot::Ref<NetwPromise> read(const godot::Dictionary &p_address) override;
    godot::Ref<NetwPromise> scan(const godot::Dictionary &p_request) override;
    godot::Ref<NetwPromise> write_batch(
        const godot::Array &p_operations
    ) override;
    godot::Ref<NetwPromise> close() override;
};

} // namespace netw::persist

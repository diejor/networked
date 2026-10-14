#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/predict/feed.hpp"
#include "netw/repl/property_set.hpp"
#include "netw/slot_book.hpp"
#include "netw/table/schema_record.hpp"

namespace netw::store {
class Table;
} // namespace netw::store

namespace netw {
class NetwEntity;
class NetwMultiplayer;
class ReplicationSend;
} // namespace netw

namespace netw::repl {

class PropertyBinding;
class BindingBook;

using PropertyBindingPtr = PropertyBinding *;

enum class Pin : uint8_t {
    STATE = 0,
    INPUT = 1,
};

struct PropertyIndex {
    godot::HashMap<godot::StringName, int64_t> types;
    uint64_t digest = 0;
    uint32_t users = 0;
};

class PropertyIndexBook {
    godot::LocalVector<PropertyIndex *> held;

public:
    PropertyIndexBook() = default;
    PropertyIndexBook(const PropertyIndexBook &) = delete;
    PropertyIndexBook &operator=(const PropertyIndexBook &) = delete;
    ~PropertyIndexBook();

    const PropertyIndex *acquire(const godot::Array &p_infos);
    void release(const PropertyIndex *p_index);
    void clear();
    uint32_t count() const {
        return held.size();
    }

    int64_t held_bytes() const {
        int64_t bytes = netw::held::vector(held);
        for (const PropertyIndex *index : held) {
            bytes += int64_t(sizeof(PropertyIndex))
                + netw::held::map(index->types);
        }
        return bytes;
    }
};

class PropertyBinding {
    friend class BindingBook;

public:
    ~PropertyBinding();

    using Landed = void (*)(
        void *p_context,
        const store::Table &p_table,
        uint32_t p_row
    );

    uint64_t id = SlotBook<PropertyBinding>::NONE;
    uint64_t set_id = SetBook::NONE;
    int64_t route = 0;
    int64_t comp = 0;
    godot::StringName order_key;
    Landed on_landed = nullptr;
    void *landed_context = nullptr;
    bool write_gate = true;
    int64_t authored_tick = -1;
    int64_t reconcile_ack = -1;
    bool volatile_external = false;
    bool input_fed = false;

    struct InterpolatedColumns {
        uint64_t mask = 0;
        uint64_t epoch = 0;
        uint64_t script = 0;
        uint32_t table = 0;
    };
    InterpolatedColumns interpolated[2];

    PropertyBinding() = default;
    PropertyBinding(const PropertyBinding &) = delete;
    PropertyBinding &operator=(const PropertyBinding &) = delete;

    bool is_retired() const {
        return retired;
    }

    godot::Node *node() const;

    void apply_state_feed(const predict::Feed &p_feed);
    void apply_input_feed(const predict::Feed &p_feed);

    bool is_windowed() const;
    bool admits_arrival(
        const table::SchemaRecord &p_schema,
        int64_t p_ordinal,
        bool p_retained
    );
    void resolve_entity_row(
        ReplicationSend *p_send,
        int64_t p_ordinal,
        store::Table &r_table,
        uint32_t p_row,
        bool p_write
    );
    void settle_arrival();
    bool stream_is_reset() const;
    bool writes_node(bool p_retained) const;
    bool writes_node(bool p_retained, const NetwEntity *p_entity) const;
    void notify_landed(const store::Table &p_table, uint32_t p_row) const;

    godot::Error write_entity_column(int64_t p_column, int64_t p_route);
    godot::Dictionary snapshot_payload();
    void apply_payload(const godot::Dictionary &p_payload);
    bool read_field(
        godot::Node *p_node,
        const godot::StringName &p_key,
        godot::Variant &r_value
    ) const;
    bool write_field(
        godot::Node *p_node,
        const godot::StringName &p_key,
        const godot::Variant &p_value
    ) const;
    godot::Dictionary canonicalize_payload(const godot::Dictionary &p_payload);

    int64_t property_class_of(const godot::StringName &p_property) const;
    double converge_stiffness_of(const godot::StringName &p_property) const;
    godot::StringName carry_channel_of(
        const godot::StringName &p_property
    ) const;
    bool teleport_only_of(const godot::StringName &p_property) const;
    bool reconcile_only_of(const godot::StringName &p_property) const;
    double epsilon_override_of(const godot::StringName &p_property) const;
    double teleport_at_of(const godot::StringName &p_property) const;
    const PropertyColumn *field_of(const godot::StringName &p_property) const;

    void clear_peer(int64_t p_peer);

    bool retains_arrivals() const;
    bool retains_arrivals(const NetwEntity *p_entity) const;
    bool follows_tenure() const;
    godot::PackedByteArray write_image(const godot::Array &p_values);
    bool read_image(
        const godot::PackedByteArray &p_bytes,
        godot::Array &r_keys,
        godot::Array &r_values
    );

    PropertySet *get_set() const;
    int64_t get_route() const {
        return route;
    }
    void set_route(int64_t p_value) {
        route = p_value;
    }
    int64_t get_comp() const {
        return comp;
    }
    const godot::StringName &get_order_key() const {
        return order_key;
    }
    void set_order_key(const godot::StringName &p_value) {
        order_key = p_value;
    }
    int64_t get_authored_tick() const {
        return authored_tick;
    }
    void set_authored_tick(int64_t p_value) {
        authored_tick = p_value;
    }
    int64_t get_reconcile_ack() const {
        return reconcile_ack;
    }
    void set_reconcile_ack(int64_t p_value) {
        reconcile_ack = p_value;
    }

    static uint64_t script_id_of(godot::Node *p_node);

    const PropertyIndex *property_index() const {
        return staged_index;
    }

private:
    const SetBook *sets = nullptr;
    PropertyIndexBook *indexes = nullptr;
    godot::ObjectID node_id;
    bool stream_reset = false;
    bool retired = false;
    uint8_t state_pins = 0;
    uint8_t input_pins = 0;

    void reset_stream();

    NetwMultiplayer *core() const;
    void stage_properties(godot::Node *p_node) const;
    bool property_present(
        godot::Node *p_node,
        const godot::StringName &p_key
    ) const;
    int64_t property_type(
        godot::Node *p_node,
        const godot::StringName &p_key
    ) const;

    const gd::PropertyResolution &unlisted_column(
        godot::Node *p_node,
        const godot::StringName &p_key
    ) const;

    mutable const PropertyIndex *staged_index = nullptr;
    mutable bool staged_properties_valid = false;
    mutable uint64_t staged_script_id = 0;
    mutable uint64_t staged_property_revision = 0;
    mutable uint64_t stage_count = 0;
    mutable godot::HashMap<godot::StringName, gd::PropertyResolution>
        unlisted_columns;
    const table::SchemaRecord *admitted_schema = nullptr;
    int admitted_shape = 0;
    uint64_t admitted_stage = 0;
    godot::Array keys_of_set() const;
    godot::Array quantizers_of() const;
    godot::Array types_of(godot::Node *p_node) const;
};

class BindingBook {
    PropertyIndexBook indexes;
    SlotBook<PropertyBinding> held;
    const SetBook *sets = nullptr;
    uint32_t retired_held = 0;
    godot::HashMap<uint64_t, godot::LocalVector<uint64_t>> by_node;

public:
    static constexpr uint64_t NONE = SlotBook<PropertyBinding>::NONE;

    explicit BindingBook(const SetBook &p_sets) : sets(&p_sets) {
    }

    BindingBook(const BindingBook &) = delete;
    BindingBook &operator=(const BindingBook &) = delete;

    const SetBook &set_book() const {
        return *sets;
    }

    PropertyBinding *create(uint64_t p_set, godot::Node *p_node);
    PropertyBinding *get(uint64_t p_id) const;
    PropertyBinding *live(uint64_t p_id) const;
    void retire(uint64_t p_id);
    void pin(uint64_t p_id, Pin p_as);
    void unpin(uint64_t p_id, Pin p_as);
    void clear();
    uint32_t count() const;
    uint32_t retired_count() const;
    uint32_t property_index_count() const {
        return indexes.count();
    }

    void index_node(uint64_t p_node, uint64_t p_binding);
    void reindex_node(uint64_t p_node, uint64_t p_from, uint64_t p_to);
    void unindex_node(uint64_t p_node, uint64_t p_binding);
    const godot::LocalVector<uint64_t> *on_node(uint64_t p_node) const;
    void clear_nodes();

    int64_t held_bytes() const {
        return indexes.held_bytes()
            + held.held_bytes([](const PropertyBinding &) { return 0; })
            + netw::held::map_of_vectors(by_node);
    }

private:
    void free_if_released(PropertyBinding *p_binding);
};

} // namespace netw::repl

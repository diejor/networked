#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/property_set.hpp"
#include "netw/predict/feed.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/repl/session_send.hpp"

namespace netw {

class NetwMultiplayer;
class ReplicationSend;

class NetwPropertySetBinding : public godot::RefCounted {
    GDCLASS(NetwPropertySetBinding, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    godot::Ref<NetwPropertySet> set;
    int64_t route = 0;
    int64_t comp = 0;
    godot::StringName order_key;
    godot::Callable on_applied;
    bool write_gate = true;
    int64_t authored_tick = -1;
    int64_t reconcile_ack = -1;
    bool volatile_external = false;

    static godot::Ref<NetwPropertySetBinding> create(
        const godot::Ref<NetwPropertySet> &p_set,
        godot::Node *p_node
    );

    godot::Node *node() const;

    void apply_state_feed(const predict::Feed &p_feed);
    void apply_input_feed(const predict::Feed &p_feed);

    bool is_active() const;
    bool is_windowed() const;
    godot::Array volatile_row();
    godot::Array retained_row();
    void offer_rows(
        int64_t p_ordinal,
        const godot::PackedInt32Array &p_recipients,
        int64_t p_tick,
        int64_t p_life,
        godot::LocalVector<repl::RowOffer> &r_offers
    );
    godot::Dictionary apply_row_frame(
        ReplicationSend *p_send,
        const godot::PackedByteArray &p_frame,
        const repl::RowArrival &p_arrival
    );
    godot::Dictionary apply_window_frame(
        ReplicationSend *p_send,
        const godot::PackedByteArray &p_frame,
        const repl::RowArrival &p_arrival
    );
    godot::Dictionary apply_retained_row(
        ReplicationSend *p_send,
        const godot::PackedByteArray &p_frame,
        const repl::RowArrival &p_arrival
    );
    godot::Error write_entity_column(int64_t p_column, int64_t p_route);
    godot::Dictionary snapshot_payload();
    void apply_payload(const godot::Dictionary &p_payload);
    godot::Dictionary canonicalize_payload(const godot::Dictionary &p_payload);
    godot::PackedByteArray canonical_bytes(const godot::Dictionary &p_payload);

    int64_t property_class_of(const godot::StringName &p_property) const;
    double converge_stiffness_of(const godot::StringName &p_property) const;
    godot::StringName carry_channel_of(
        const godot::StringName &p_property
    ) const;
    bool teleport_only_of(const godot::StringName &p_property) const;
    bool reconcile_only_of(const godot::StringName &p_property) const;
    double epsilon_override_of(const godot::StringName &p_property) const;
    double teleport_at_of(const godot::StringName &p_property) const;
    godot::Ref<NetwPropertySetColumn> field_of(
        const godot::StringName &p_property
    ) const;

    void clear_peer(int64_t p_peer);

    godot::Array gather_row(const godot::Array &p_fields, bool p_allow_missing);
    godot::Error write_row(
        const godot::Array &p_values,
        const godot::Array &p_keys
    );

    godot::Ref<NetwPropertySet> get_set() const {
        return set;
    }
    void set_set(const godot::Ref<NetwPropertySet> &p_set) {
        set = p_set;
    }
    int64_t get_route() const {
        return route;
    }
    void set_route(int64_t p_value) {
        route = p_value;
    }
    int64_t get_comp() const {
        return comp;
    }
    void set_comp(int64_t p_value) {
        comp = p_value;
    }
    godot::StringName get_order_key() const {
        return order_key;
    }
    void set_order_key(const godot::StringName &p_value) {
        order_key = p_value;
    }
    godot::Callable get_on_applied() const {
        return on_applied;
    }
    void set_on_applied(const godot::Callable &p_value) {
        on_applied = p_value;
    }
    bool get_write_gate() const {
        return write_gate;
    }
    void set_write_gate(bool p_value) {
        write_gate = p_value;
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
    bool get_volatile_external() const {
        return volatile_external;
    }
    void set_volatile_external(bool p_value) {
        volatile_external = p_value;
    }

private:
    godot::ObjectID node_id;
    godot::PackedByteArray held_row;
    godot::PackedByteArray held_retained;
    repl::BaselineRing volatile_ring;

    NetwMultiplayer *core() const;
    godot::RID entity_rid() const;
    godot::Array lane_fields(int64_t p_lane) const;
    int set_index_of(const godot::Ref<NetwPropertySetColumn> &p_field) const;
    void resolve_entity_columns(
        ReplicationSend *p_send,
        const godot::Array &p_fields,
        int64_t p_ordinal,
        godot::Array &r_values
    );
    godot::Dictionary gather_fields(
        godot::Node *p_node,
        const godot::Array &p_fields,
        bool p_allow_missing
    );
    void stage_properties(godot::Node *p_node) const;
    bool property_present(
        godot::Node *p_node,
        const godot::StringName &p_key
    ) const;
    int64_t property_type(
        godot::Node *p_node,
        const godot::StringName &p_key
    ) const;
    static uint64_t script_generation_of(godot::Node *p_node);

    mutable godot::HashMap<godot::StringName, int64_t> staged_properties;
    mutable bool staged_properties_valid = false;
    mutable uint64_t staged_script_generation = 0;
    godot::Error apply_values(
        godot::Node *p_node,
        const godot::Array &p_keys,
        const godot::Array &p_values
    );
    godot::Dictionary canonical_plan(const godot::Dictionary &p_payload) const;
    static godot::PackedByteArray canonical_run(
        const godot::Dictionary &p_plan
    );
    static godot::Array keys_of(const godot::Array &p_fields);
};

} // namespace netw

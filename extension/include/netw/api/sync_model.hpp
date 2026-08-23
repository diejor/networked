#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/repl/set_model.hpp"

namespace netw {

class NetwSyncSetRow : public godot::RefCounted {
    GDCLASS(NetwSyncSetRow, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    repl::SetRow row;

    int64_t get_route() const { return row.route; }
    int64_t get_ordinal() const { return row.ordinal; }
    int64_t get_comp() const { return row.comp; }
    int64_t get_kind() const { return row.kind; }
    godot::StringName get_key() const { return row.key; }
    godot::RID get_set() const { return row.set; }
    int64_t get_record() const { return row.record; }
    int64_t get_schema_hash() const { return row.schema_hash; }
    int64_t get_policy() const { return row.policy; }
    int64_t get_audience() const { return row.audience; }
};

class NetwSyncModel : public godot::RefCounted {
    GDCLASS(NetwSyncModel, godot::RefCounted)

    repl::SetModel impl;
    godot::HashMap<godot::StringName, godot::Ref<godot::RefCounted>> bindings;

    static godot::StringName slot_of(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record
    );

protected:
    static void _bind_methods();

public:
    enum Kind { KIND_CONSUMED = 0, KIND_DERIVED = 1 };

    int64_t declare(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_comp,
        const godot::RID &p_set,
        int64_t p_record,
        int64_t p_schema_hash,
        int64_t p_policy,
        int64_t p_audience
    );

    void drop(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record
    );

    godot::Ref<NetwSyncSetRow> row(int64_t p_route, int64_t p_ordinal) const;

    godot::Ref<NetwSyncSetRow> row_for(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record
    ) const;

    godot::TypedArray<NetwSyncSetRow> route_rows(int64_t p_route) const;

    bool authors(
        int64_t p_route,
        int64_t p_ordinal,
        int64_t p_local_id,
        bool p_node_authority,
        int64_t p_controller
    ) const;

    godot::PackedInt32Array recipients(
        int64_t p_route,
        int64_t p_ordinal,
        int64_t p_local_id,
        const godot::PackedInt32Array &p_live
    ) const;

    static godot::PackedInt32Array event_recipients(
        bool p_is_host,
        int64_t p_local_id,
        int64_t p_exclude,
        const godot::PackedInt32Array &p_live
    );

    bool admits_sender(
        int64_t p_route,
        int64_t p_ordinal,
        int64_t p_sender,
        int64_t p_node_authority,
        int64_t p_controller
    ) const;

    void note_descriptors(int64_t p_route, const godot::Dictionary &p_noted);

    bool admits_schema(int64_t p_route, int64_t p_ordinal) const;

    void attach(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record,
        const godot::Ref<godot::RefCounted> &p_binding
    );

    void detach(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record
    );

    godot::Ref<godot::RefCounted> binding_of(
        int64_t p_route,
        int64_t p_ordinal
    ) const;

    godot::TypedArray<godot::RefCounted> route_bindings(
        int64_t p_route,
        int64_t p_kind
    ) const;

    void clear_route(int64_t p_route);
    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwSyncModel::Kind);

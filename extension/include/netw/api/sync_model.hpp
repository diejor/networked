#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/repl/set_model.hpp"

namespace netw {

class NetwSyncModel {
    repl::SetModel impl;
    godot::HashMap<godot::StringName, godot::Ref<NetwPropertySetBinding>>
        bindings;
    int64_t drops_no_set = 0;
    int64_t skips_not_author = 0;
    int64_t skips_no_recipients = 0;
    int64_t drops_bad_sender = 0;
    int64_t drops_schema = 0;
    int64_t rows_in = 0;

    static godot::StringName slot_of(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record
    );

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

    const repl::SetRow *row(int64_t p_route, int64_t p_ordinal) const;

    const repl::SetRow *row_for(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record
    ) const;

    const godot::LocalVector<repl::SetRow> *route_rows(int64_t p_route) const;

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
        const godot::Ref<NetwPropertySetBinding> &p_binding
    );

    void detach(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record
    );

    godot::Ref<NetwPropertySetBinding> binding_of(
        int64_t p_route,
        int64_t p_ordinal
    ) const;

    godot::Ref<NetwPropertySetBinding> admit_row(
        int64_t p_route,
        int64_t p_ordinal,
        int64_t p_sender,
        int64_t p_controller
    );

    void note_row_applied();

    godot::PackedInt32Array offer_row(
        int64_t p_route,
        int64_t p_ordinal,
        int64_t p_local_id,
        bool p_node_authority,
        int64_t p_controller,
        const godot::PackedInt32Array &p_live
    );

    godot::Dictionary stats() const;

    godot::TypedArray<NetwPropertySetBinding> route_bindings(
        int64_t p_route,
        int64_t p_kind
    ) const;

    void clear_route(int64_t p_route);
    void clear();
};

} // namespace netw

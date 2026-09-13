#include "netw/api/sync_model.hpp"

#include "godot/node.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw {

using namespace godot;

int64_t NetwSyncModel::declare(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_comp,
    const RID &p_set,
    int64_t p_record,
    int64_t p_schema_hash,
    int64_t p_policy,
    int64_t p_audience
) {
    return impl.declare(
        p_route,
        p_kind,
        p_key,
        p_comp,
        p_set,
        p_record,
        p_schema_hash,
        p_policy,
        p_audience
    );
}

void NetwSyncModel::drop(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) {
    impl.drop(p_route, p_kind, p_key, p_record);
}

const repl::SetRow *NetwSyncModel::row(
    int64_t p_route,
    int64_t p_ordinal
) const {
    return impl.row(p_route, p_ordinal);
}

const repl::SetRow *NetwSyncModel::row_for(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) const {
    return impl.row_for(p_route, p_kind, p_key, p_record);
}

const LocalVector<repl::SetRow> *NetwSyncModel::route_rows(
    int64_t p_route
) const {
    return impl.route_rows(p_route);
}

bool NetwSyncModel::authors(
    int64_t p_route,
    int64_t p_ordinal,
    int64_t p_local_id,
    bool p_node_authority,
    int64_t p_controller
) const {
    const repl::SetRow *found = impl.row(p_route, p_ordinal);
    if (found == nullptr) {
        return false;
    }
    return repl::row_authors(
        *found,
        p_local_id,
        p_node_authority,
        p_controller
    );
}

PackedInt32Array NetwSyncModel::recipients(
    int64_t p_route,
    int64_t p_ordinal,
    int64_t p_local_id,
    const PackedInt32Array &p_live
) const {
    const repl::SetRow *found = impl.row(p_route, p_ordinal);
    if (found == nullptr) {
        return PackedInt32Array();
    }
    return repl::row_recipients(*found, p_local_id, p_live);
}

PackedInt32Array NetwSyncModel::event_recipients(
    bool p_is_host,
    int64_t p_local_id,
    int64_t p_exclude,
    const PackedInt32Array &p_live
) {
    return repl::event_recipients(p_is_host, p_local_id, p_exclude, p_live);
}

bool NetwSyncModel::admits_sender(
    int64_t p_route,
    int64_t p_ordinal,
    int64_t p_sender,
    int64_t p_node_authority,
    int64_t p_controller
) const {
    const repl::SetRow *found = impl.row(p_route, p_ordinal);
    if (found == nullptr) {
        return false;
    }
    return repl::row_admits_sender(
        *found,
        p_sender,
        p_node_authority,
        p_controller
    );
}

void NetwSyncModel::note_descriptors(
    int64_t p_route,
    const Dictionary &p_noted
) {
    HashMap<int64_t, int64_t> noted;
    const Array ordinals = p_noted.keys();
    for (int at = 0; at < ordinals.size(); ++at) {
        noted[int64_t(ordinals[at])] = int64_t(p_noted[ordinals[at]]);
    }
    impl.note_descriptors(p_route, noted);
}

bool NetwSyncModel::admits_schema(int64_t p_route, int64_t p_ordinal) const {
    return impl.admits_schema(p_route, p_ordinal);
}

StringName NetwSyncModel::slot_of(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) {
    return StringName(
        String::num_int64(p_route) + "/" + String::num_int64(p_kind) + "/"
        + String(p_key) + "/" + String::num_int64(p_record)
    );
}

void NetwSyncModel::attach(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record,
    const Ref<NetwPropertySetBinding> &p_binding
) {
    const StringName slot = slot_of(p_route, p_kind, p_key, p_record);
    if (p_binding.is_null()) {
        bindings.erase(slot);
        return;
    }
    bindings[slot] = p_binding;
}

void NetwSyncModel::detach(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) {
    bindings.erase(slot_of(p_route, p_kind, p_key, p_record));
}

Ref<NetwPropertySetBinding> NetwSyncModel::binding_of(
    int64_t p_route,
    int64_t p_ordinal
) const {
    const repl::SetRow *found = impl.row(p_route, p_ordinal);
    if (found == nullptr) {
        return Ref<NetwPropertySetBinding>();
    }
    HashMap<StringName, Ref<NetwPropertySetBinding>>::ConstIterator held
        = bindings.find(
            slot_of(found->route, found->kind, found->key, found->record)
        );
    return held == bindings.end() ? Ref<NetwPropertySetBinding>() : held->value;
}

TypedArray<NetwPropertySetBinding> NetwSyncModel::route_bindings(
    int64_t p_route,
    int64_t p_kind
) const {
    TypedArray<NetwPropertySetBinding> out;
    const LocalVector<repl::SetRow> *rows = impl.route_rows(p_route);
    if (rows == nullptr) {
        return out;
    }
    for (const repl::SetRow &row : *rows) {
        if (row.kind != p_kind) {
            continue;
        }
        HashMap<StringName, Ref<NetwPropertySetBinding>>::ConstIterator held
            = bindings.find(slot_of(row.route, row.kind, row.key, row.record));
        if (held != bindings.end()) {
            out.push_back(held->value);
        }
    }
    return out;
}

void NetwSyncModel::clear_route(int64_t p_route) {
    const String prefix = String::num_int64(p_route) + "/";
    LocalVector<StringName> doomed;
    for (const KeyValue<StringName, Ref<NetwPropertySetBinding>> &row :
         bindings) {
        if (String(row.key).begins_with(prefix)) {
            doomed.push_back(row.key);
        }
    }
    for (const StringName &slot : doomed) {
        bindings.erase(slot);
    }
    impl.clear_route(p_route);
}

void NetwSyncModel::clear() {
    bindings.clear();
    impl.clear();
}

Ref<NetwPropertySetBinding> NetwSyncModel::admit_row(
    int64_t p_route,
    int64_t p_ordinal,
    int64_t p_sender,
    int64_t p_controller
) {
    if (p_ordinal < 0) {
        ++drops_no_set;
        return Ref<NetwPropertySetBinding>();
    }
    const Ref<NetwPropertySetBinding> held = binding_of(p_route, p_ordinal);
    if (held.is_null()) {
        ++drops_no_set;
        return Ref<NetwPropertySetBinding>();
    }
    Node *node = held->node();
    if (node == nullptr) {
        ++drops_no_set;
        return Ref<NetwPropertySetBinding>();
    }
    if (!admits_sender(
            p_route,
            p_ordinal,
            p_sender,
            node->get_multiplayer_authority(),
            p_controller
        )) {
        ++drops_bad_sender;
        return Ref<NetwPropertySetBinding>();
    }
    if (!admits_schema(p_route, p_ordinal)) {
        ++drops_schema;
        return Ref<NetwPropertySetBinding>();
    }
    return held;
}

PackedInt32Array NetwSyncModel::offer_row(
    int64_t p_route,
    int64_t p_ordinal,
    int64_t p_local_id,
    bool p_node_authority,
    int64_t p_controller,
    const PackedInt32Array &p_live
) {
    NETW_ZONE_NC("Sync model offer row", colors::WIRE);
    if (!authors(
            p_route,
            p_ordinal,
            p_local_id,
            p_node_authority,
            p_controller
        )) {
        ++skips_not_author;
        return PackedInt32Array();
    }
    const PackedInt32Array out
        = recipients(p_route, p_ordinal, p_local_id, p_live);
    if (out.is_empty()) {
        ++skips_no_recipients;
    }
    return out;
}

void NetwSyncModel::note_row_applied() {
    ++rows_in;
}

Dictionary NetwSyncModel::stats() const {
    Dictionary out;
    out[StringName("drops_no_set")] = drops_no_set;
    out[StringName("drops_bad_sender")] = drops_bad_sender;
    out[StringName("drops_schema")] = drops_schema;
    out[StringName("rows_in")] = rows_in;
    out[StringName("skips_not_author")] = skips_not_author;
    out[StringName("skips_no_recipients")] = skips_no_recipients;
    return out;
}

} // namespace netw

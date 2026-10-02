#include "netw/repl/property_binding.hpp"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/call_args.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/object_port.hpp"
#include "netw/profile.hpp"
#include "netw/repl/set_model.hpp"
#include "netw/replication_send.hpp"
#include "netw/store/table.hpp"
#include "netw/subsystems.hpp"
#include "netw/tree_hook.hpp"

using namespace godot;

namespace netw::repl {

namespace {

int64_t typed_as(
    const HashMap<StringName, int64_t> &p_types,
    const StringName &p_property
) {
    const HashMap<StringName, int64_t>::ConstIterator found
        = p_types.find(p_property);
    return found != p_types.end() ? found->value : int64_t(Variant::NIL);
}

uint64_t digest_of(const HashMap<StringName, int64_t> &p_types) {
    uint64_t digest = uint64_t(p_types.size());
    for (const KeyValue<StringName, int64_t> &entry : p_types) {
        uint64_t mixed = uint64_t(entry.key.hash()) * 0x9e3779b97f4a7c15ull;
        mixed ^= uint64_t(entry.value) + (mixed >> 29);
        digest += mixed * 0xbf58476d1ce4e5b9ull;
    }
    return digest;
}

bool same_types(
    const HashMap<StringName, int64_t> &p_held,
    const HashMap<StringName, int64_t> &p_built
) {
    if (p_held.size() != p_built.size()) {
        return false;
    }
    for (const KeyValue<StringName, int64_t> &entry : p_built) {
        const HashMap<StringName, int64_t>::ConstIterator found
            = p_held.find(entry.key);
        if (found == p_held.end() || found->value != entry.value) {
            return false;
        }
    }
    return true;
}

} // namespace

PropertyIndexBook::~PropertyIndexBook() {
    clear();
}

const PropertyIndex *PropertyIndexBook::acquire(const Array &p_infos) {
    HashMap<StringName, int64_t> built;
    for (int at = 0; at < p_infos.size(); at++) {
        const Dictionary info = p_infos[at];
        built.insert(
            StringName(info.get("name", StringName())),
            int64_t(info.get("type", int64_t(Variant::NIL)))
        );
    }
    const uint64_t digest = digest_of(built);
    for (PropertyIndex *index : held) {
        if (index->digest == digest && same_types(index->types, built)) {
            index->users += 1;
            return index;
        }
    }
    PropertyIndex *made = new PropertyIndex();
    made->types = built;
    made->digest = digest;
    made->users = 1;
    held.push_back(made);
    return made;
}

void PropertyIndexBook::release(const PropertyIndex *p_index) {
    for (uint32_t at = 0; at < held.size(); ++at) {
        PropertyIndex *index = held[at];
        if (index != p_index) {
            continue;
        }
        if (index->users > 1) {
            index->users -= 1;
            return;
        }
        held.remove_at_unordered(at);
        delete index;
        return;
    }
}

void PropertyIndexBook::clear() {
    for (PropertyIndex *index : held) {
        delete index;
    }
    held.clear();
}

PropertyBinding::~PropertyBinding() {
    if (indexes != nullptr && staged_index != nullptr) {
        indexes->release(staged_index);
    }
}

PropertyBinding *BindingBook::create(uint64_t p_set, Node *p_node) {
    const uint64_t minted = held.create();
    PropertyBinding *made = held.get(minted);
    made->id = minted;
    made->sets = sets;
    made->indexes = &indexes;
    made->set_id = p_set;
    made->node_id = gd::instance_id(p_node);
    return made;
}

PropertyBinding *BindingBook::get(uint64_t p_id) const {
    return held.get(p_id);
}

PropertyBinding *BindingBook::live(uint64_t p_id) const {
    PropertyBinding *found = held.get(p_id);
    return found != nullptr && !found->retired ? found : nullptr;
}

void BindingBook::retire(uint64_t p_id) {
    PropertyBinding *found = held.get(p_id);
    if (found == nullptr || found->retired) {
        return;
    }
    found->retired = true;
    retired_held += 1;
    free_if_released(found);
}

void BindingBook::pin(uint64_t p_id, Pin p_as) {
    PropertyBinding *found = held.get(p_id);
    if (found == nullptr) {
        return;
    }
    uint8_t &pins
        = p_as == Pin::STATE ? found->state_pins : found->input_pins;
    pins += 1;
}

void BindingBook::unpin(uint64_t p_id, Pin p_as) {
    PropertyBinding *found = held.get(p_id);
    if (found == nullptr) {
        return;
    }
    uint8_t &pins
        = p_as == Pin::STATE ? found->state_pins : found->input_pins;
    if (pins > 0) {
        pins -= 1;
    }
    free_if_released(found);
}

void BindingBook::free_if_released(PropertyBinding *p_binding) {
    if (!p_binding->retired || p_binding->state_pins != 0
        || p_binding->input_pins != 0) {
        return;
    }
    retired_held -= 1;
    held.free(p_binding->id);
}

void BindingBook::clear() {
    held.clear();
    indexes.clear();
    retired_held = 0;
    by_node.clear();
}

uint32_t BindingBook::count() const {
    return held.count();
}

uint32_t BindingBook::retired_count() const {
    return retired_held;
}

void BindingBook::index_node(uint64_t p_node, uint64_t p_binding) {
    if (!by_node.has(p_node)) {
        by_node.insert(p_node, LocalVector<uint64_t>());
        tree::mark_id(p_node, tree::MARK_HEARD);
    }
    by_node[p_node].push_back(p_binding);
}

void BindingBook::reindex_node(
    uint64_t p_node,
    uint64_t p_from,
    uint64_t p_to
) {
    LocalVector<uint64_t> *held_ids = by_node.getptr(p_node);
    if (held_ids == nullptr) {
        return;
    }
    for (uint64_t &entry : *held_ids) {
        if (entry == p_from) {
            entry = p_to;
            return;
        }
    }
}

void BindingBook::unindex_node(uint64_t p_node, uint64_t p_binding) {
    LocalVector<uint64_t> *held_ids = by_node.getptr(p_node);
    if (held_ids == nullptr) {
        return;
    }
    for (uint32_t at = 0; at < held_ids->size(); ++at) {
        if ((*held_ids)[at] == p_binding) {
            held_ids->remove_at(at);
            break;
        }
    }
    if (held_ids->is_empty()) {
        by_node.erase(p_node);
    }
}

const LocalVector<uint64_t> *BindingBook::on_node(uint64_t p_node) const {
    return by_node.getptr(p_node);
}

void BindingBook::clear_nodes() {
    by_node.clear();
}

PropertySet *PropertyBinding::get_set() const {
    return sets != nullptr ? sets->get(set_id) : nullptr;
}

Node *PropertyBinding::node() const {
    return Object::cast_to<Node>(gd::object_of(node_id));
}

NetwMultiplayer *PropertyBinding::core() const {
    Node *held = node();
    if (held == nullptr || !held->is_inside_tree()) {
        return nullptr;
    }
    return NetwEntity::session_core_for(held);
}

void PropertyBinding::apply_state_feed(const predict::Feed &p_feed) {
    write_gate = p_feed.state_write_gate;
}

void PropertyBinding::apply_input_feed(const predict::Feed &p_feed) {
    write_gate = p_feed.input_write_gate;
    volatile_external = p_feed.input_volatile_external;
    input_fed = true;
}

bool PropertyBinding::is_active() const {
    Node *held = node();
    return held != nullptr && held->is_inside_tree()
        && held->is_multiplayer_authority();
}

bool PropertyBinding::is_windowed() const {
    const PropertySet *set = get_set();
    return set != nullptr && set->window > 0
        && set->stamp == PropertySet::STAMP_TICK;
}

Array PropertyBinding::keys_of_set() const {
    const PropertySet *set = get_set();
    Array out;
    for (const PropertyColumn &column : set->columns) {
        out.push_back(column.get_key());
    }
    return out;
}

void PropertyBinding::stage_properties(Node *p_node) const {
    NETW_ZONE_LEAF(
        "Binding stage properties",
        colors::WIRE,
        profile::SUBSYSTEM_WIRE
    );
    const uint64_t generation = script_generation_of(p_node);
    if (staged_properties_valid && staged_script_generation == generation) {
        return;
    }
    if (indexes != nullptr && staged_index != nullptr) {
        indexes->release(staged_index);
    }
    staged_index = nullptr;
    staged_script_generation = generation;
    staged_properties_valid = false;
    if (p_node == nullptr || indexes == nullptr) {
        return;
    }
    Array infos;
    {
        NETW_ZONE_LEAF(
            "Binding engine property list",
            colors::WIRE,
            profile::SUBSYSTEM_WIRE
        );
        infos = gd::property_list(p_node);
    }
    NETW_ZONE_VALUE(infos.size());
    {
        NETW_ZONE_LEAF(
            "Binding index properties",
            colors::WIRE,
            profile::SUBSYSTEM_WIRE
        );
        staged_index = indexes->acquire(infos);
    }
    staged_properties_valid = true;
}

uint64_t PropertyBinding::script_generation_of(Node *p_node) {
    if (p_node == nullptr) {
        return 0;
    }
    const Ref<Script> held = p_node->get_script();
    return held.is_valid() ? uint64_t(held->get_instance_id()) : 1;
}

int64_t PropertyBinding::property_type(
    Node *p_node,
    const StringName &p_key
) const {
    stage_properties(p_node);
    return staged_index != nullptr ? typed_as(staged_index->types, p_key)
                                   : int64_t(Variant::NIL);
}

bool PropertyBinding::property_present(
    Node *p_node,
    const StringName &p_key
) const {
    stage_properties(p_node);
    if (staged_index != nullptr && staged_index->types.has(p_key)) {
        return true;
    }
    return gd::has_property(p_node, p_key);
}

void PropertyBinding::resolve_entity_row(
    ReplicationSend *p_send,
    int64_t p_ordinal,
    store::Table &r_table,
    uint32_t p_row,
    bool p_write
) {
    if (p_send == nullptr) {
        return;
    }
    const PropertySet *set = get_set();
    for (int at = 0; at < r_table.schema.column_count(); at++) {
        const table::SchemaColumn *declared = r_table.schema.at(at);
        if (declared->type != SchemaCore::ENTITY) {
            continue;
        }
        repl::EntitySlot slot;
        slot.route = get_route();
        slot.comp = uint8_t(p_ordinal);
        slot.column = uint32_t(
            set != nullptr ? set->index_of(field_of(declared->key)) : -1
        );
        const int64_t resolved = p_send->entity_resolve(
            slot,
            int64_t(r_table.read_value(p_row, uint32_t(at)))
        );
        if (p_write) {
            r_table.write_value(p_row, uint32_t(at), resolved);
        }
    }
}

bool PropertyBinding::admits_arrival(
    const table::SchemaRecord &p_schema,
    int64_t p_ordinal,
    bool p_retained
) {
    Node *held = node();
    if (held == nullptr) {
        return false;
    }
    if (!p_retained && !write_gate) {
        return true;
    }
    stage_properties(held);
    if (admitted_schema == &p_schema && admitted_shape == p_schema.shape_hash
        && admitted_generation == staged_script_generation) {
        return true;
    }
    bool indexed = staged_index != nullptr;
    for (int at = 0; at < p_schema.column_count(); at++) {
        const StringName &key = p_schema.at(at)->key;
        if (staged_index != nullptr && staged_index->types.has(key)) {
            continue;
        }
        indexed = false;
        if (gd::has_property(held, key)) {
            continue;
        }
        NETW_WARN_ONCE(
            sys::WIRE,
            "An update for route %d names property '%s', which the node on "
            "this peer does not have, so it was dropped. Give the node the "
            "same script on every peer.",
            int(route),
            String(key).utf8().get_data()
        );
        reset_stream();
        return false;
    }
    if (indexed) {
        admitted_schema = &p_schema;
        admitted_shape = p_schema.shape_hash;
        admitted_generation = staged_script_generation;
    }
    return true;
}

void PropertyBinding::settle_arrival() {
    stream_reset = false;
}

Error PropertyBinding::write_entity_column(int64_t p_column, int64_t p_route) {
    Node *held = node();
    const PropertySet *set = get_set();
    if (held == nullptr || set == nullptr || p_column < 0
        || p_column >= int64_t(set->columns.size())) {
        return ERR_DOES_NOT_EXIST;
    }
    const PropertyColumn &field = set->columns[uint32_t(p_column)];
    NetwMultiplayer *session = core();
    if (session == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    return session->store_write_entity(this, field.get_key(), p_route);
}

bool PropertyBinding::follows_tenure() const {
    const PropertySet *set = get_set();
    return set != nullptr && repl::record_follows_tenure(set->record);
}

bool PropertyBinding::retains_arrivals() const {
    if (!follows_tenure()) {
        return false;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node());
    return entity.is_valid() && entity->is_claim_running_ahead();
}

bool PropertyBinding::retains_arrivals(const NetwEntity *p_entity) const {
    return follows_tenure() && p_entity != nullptr
        && p_entity->is_claim_running_ahead();
}

Array PropertyBinding::quantizers_of() const {
    Array out;
    for (const PropertyColumn &column : get_set()->columns) {
        out.push_back(Variant(column.get_quantizer()));
    }
    return out;
}

Array PropertyBinding::types_of(Node *p_node) const {
    Array out;
    for (const PropertyColumn &column : get_set()->columns) {
        out.push_back(property_type(p_node, column.get_key()));
    }
    return out;
}

PackedByteArray PropertyBinding::write_image(const Array &p_values) {
    Node *held = node();
    const PropertySet *set = get_set();
    if (held == nullptr || set == nullptr || set->columns.is_empty()
        || p_values.size() != int64_t(set->columns.size())) {
        return PackedByteArray();
    }
    const Array &values = p_values;
    wire::WriteStream stream;
    if (!call_args::values_write(
            stream,
            values,
            quantizers_of(),
            types_of(held)
        )
        || !stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool PropertyBinding::read_image(
    const PackedByteArray &p_bytes,
    Array &r_keys,
    Array &r_values
) {
    Node *held = node();
    const PropertySet *set = get_set();
    if (held == nullptr || set == nullptr || p_bytes.is_empty()) {
        return false;
    }
    wire::ReadStream reader(p_bytes);
    Array values;
    if (!call_args::values_read(reader, quantizers_of(), types_of(held), values)
        || values.size() != int64_t(set->columns.size())) {
        return false;
    }
    r_keys = keys_of_set();
    r_values = values;
    return true;
}

void PropertyBinding::reset_stream() {
    stream_reset = true;
}

bool PropertyBinding::stream_is_reset() const {
    return stream_reset;
}

bool PropertyBinding::writes_node(bool p_retained) const {
    return (p_retained || write_gate) && !retains_arrivals();
}

bool PropertyBinding::writes_node(
    bool p_retained,
    const NetwEntity *p_entity
) const {
    return (p_retained || write_gate) && !retains_arrivals(p_entity);
}

void PropertyBinding::notify_landed(
    const store::Table &p_table,
    uint32_t p_row
) const {
    if (on_landed != nullptr) {
        on_landed(landed_context, p_table, p_row);
    }
}

Dictionary PropertyBinding::snapshot_payload() {
    NETW_ZONE_LEAF(
        "Binding snapshot payload",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    Node *held = node();
    const PropertySet *set = get_set();
    if (held == nullptr || set == nullptr) {
        return Dictionary();
    }
    Dictionary payload;
    for (const PropertyColumn &column : set->columns) {
        const StringName &key = column.get_key();
        if (property_present(held, key)) {
            payload[key] = port_get(held, key);
        }
    }
    return payload;
}

bool PropertyBinding::read_field(
    Node *p_node,
    const StringName &p_key,
    Variant &r_value
) const {
    if (!property_present(p_node, p_key)) {
        return false;
    }
    r_value = port_get(p_node, p_key);
    return true;
}

bool PropertyBinding::write_field(
    Node *p_node,
    const StringName &p_key,
    const Variant &p_value
) const {
    if (field_of(p_key) == nullptr || !property_present(p_node, p_key)) {
        return false;
    }
    port_set(p_node, p_key, p_value);
    return true;
}

void PropertyBinding::apply_payload(const Dictionary &p_payload) {
    Node *held = node();
    const PropertySet *set = get_set();
    if (held == nullptr || set == nullptr) {
        return;
    }
    for (const PropertyColumn &column : set->columns) {
        const StringName &key = column.get_key();
        if (p_payload.has(key) && gd::has_property(held, key)) {
            port_set(held, key, p_payload[key]);
        }
    }
}

Dictionary PropertyBinding::canonicalize_payload(const Dictionary &p_payload) {
    NETW_ZONE_LEAF(
        "Binding canonicalize payload",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    PropertySet *set = get_set();
    if (p_payload.is_empty() || node() == nullptr || set == nullptr) {
        return p_payload.duplicate();
    }
    return predict::canonicalize(set->get_codec(), p_payload);
}

const PropertyColumn *PropertyBinding::field_of(
    const StringName &p_property
) const {
    const PropertySet *set = get_set();
    return set != nullptr ? set->column_named(p_property) : nullptr;
}

int64_t PropertyBinding::property_class_of(const StringName &p_property) const {
    const PropertyColumn *field = field_of(p_property);
    return field != nullptr ? field->property_class
                            : int64_t(PropertySet::CAUSAL);
}

double PropertyBinding::converge_stiffness_of(
    const StringName &p_property
) const {
    const PropertyColumn *field = field_of(p_property);
    return field != nullptr ? field->converge_stiffness : 0.0;
}

StringName PropertyBinding::carry_channel_of(
    const StringName &p_property
) const {
    const PropertyColumn *field = field_of(p_property);
    return field != nullptr ? field->carry_channel : StringName();
}

bool PropertyBinding::teleport_only_of(const StringName &p_property) const {
    const PropertyColumn *field = field_of(p_property);
    return field != nullptr && field->explicit_teleport_only;
}

bool PropertyBinding::reconcile_only_of(const StringName &p_property) const {
    const PropertyColumn *field = field_of(p_property);
    return field != nullptr && field->explicit_reconcile_only;
}

double PropertyBinding::epsilon_override_of(
    const StringName &p_property
) const {
    const PropertyColumn *field = field_of(p_property);
    return field != nullptr ? field->epsilon_override : -1.0;
}

double PropertyBinding::teleport_at_of(const StringName &p_property) const {
    const PropertyColumn *field = field_of(p_property);
    return field != nullptr ? field->teleport_at_override : -1.0;
}

void PropertyBinding::clear_peer(int64_t p_peer) {
}

} // namespace netw::repl

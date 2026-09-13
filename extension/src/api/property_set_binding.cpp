#include "netw/api/property_set_binding.hpp"

#include "godot/class_db.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/call_args.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/replication_send.hpp"
#include "netw/staged_writes.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace netw {

namespace {

int64_t typed_as(
    const HashMap<StringName, int64_t> &p_types,
    const StringName &p_property
) {
    const HashMap<StringName, int64_t>::ConstIterator found
        = p_types.find(p_property);
    return found != p_types.end() ? found->value : int64_t(Variant::NIL);
}

} // namespace

Ref<NetwPropertySetBinding> NetwPropertySetBinding::create(
    const Ref<NetwPropertySet> &p_set,
    Node *p_node
) {
    Ref<NetwPropertySetBinding> made;
    made.instantiate();
    made->set = p_set;
    made->node_id = gd::instance_id(p_node);
    return made;
}

Node *NetwPropertySetBinding::node() const {
    return Object::cast_to<Node>(gd::object_of(node_id));
}

NetwMultiplayer *NetwPropertySetBinding::core() const {
    Node *held = node();
    if (held == nullptr || !held->is_inside_tree()) {
        return nullptr;
    }
    return NetwEntity::session_core_for(held);
}

RID NetwPropertySetBinding::entity_rid() const {
    NetwMultiplayer *session = core();
    if (session == nullptr || route <= 0) {
        return RID();
    }
    const Ref<NetwEntity> entity = Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(session->wrapper_for_route(route).ptr())
    );
    return entity.is_valid() ? entity->get_rid_handle() : RID();
}

void NetwPropertySetBinding::apply_state_feed(const predict::Feed &p_feed) {
    on_applied = p_feed.state_on_applied;
    write_gate = p_feed.state_write_gate;
}

void NetwPropertySetBinding::apply_input_feed(const predict::Feed &p_feed) {
    on_applied = p_feed.input_on_applied;
    write_gate = p_feed.input_write_gate;
    volatile_external = p_feed.input_volatile_external;
}

bool NetwPropertySetBinding::is_active() const {
    Node *held = node();
    return held != nullptr && held->is_inside_tree()
        && held->is_multiplayer_authority();
}

bool NetwPropertySetBinding::is_windowed() const {
    return set.is_valid() && set->window > 0
        && set->stamp == NetwPropertySet::STAMP_TICK;
}

Array NetwPropertySetBinding::lane_fields(int64_t p_lane) const {
    Array out;
    if (set.is_null()) {
        return out;
    }
    for (int at = 0; at < set->columns.size(); at++) {
        const Ref<NetwPropertySetColumn> field = set->columns[at];
        if (field.is_valid() && field->lane == p_lane) {
            out.push_back(field);
        }
    }
    return out;
}

Array NetwPropertySetBinding::keys_of(const Array &p_fields) {
    Array out;
    for (int at = 0; at < p_fields.size(); at++) {
        const Ref<NetwPropertySetColumn> field = p_fields[at];
        out.push_back(field.is_valid() ? field->get_key() : StringName());
    }
    return out;
}

void NetwPropertySetBinding::stage_properties(Node *p_node) const {
    NETW_ZONE_NC("Binding stage properties", colors::WIRE);
    const uint64_t generation = script_generation_of(p_node);
    if (staged_properties_valid && staged_script_generation == generation) {
        return;
    }
    staged_properties.clear();
    staged_script_generation = generation;
    if (p_node == nullptr) {
        staged_properties_valid = false;
        return;
    }
    Array infos;
    {
        NETW_ZONE_NC("Binding engine property list", colors::WIRE);
        infos = gd::property_list(p_node);
    }
    NETW_ZONE_VALUE(infos.size());
    {
        NETW_ZONE_NC("Binding index properties", colors::WIRE);
        for (int at = 0; at < infos.size(); at++) {
            const Dictionary info = infos[at];
            staged_properties.insert(
                StringName(info.get("name", StringName())),
                int64_t(info.get("type", int64_t(Variant::NIL)))
            );
        }
    }
    staged_properties_valid = true;
}

uint64_t NetwPropertySetBinding::script_generation_of(Node *p_node) {
    if (p_node == nullptr) {
        return 0;
    }
    const Ref<Script> held = p_node->get_script();
    return held.is_valid() ? uint64_t(held->get_instance_id()) : 1;
}

int64_t NetwPropertySetBinding::property_type(
    Node *p_node,
    const StringName &p_key
) const {
    stage_properties(p_node);
    return typed_as(staged_properties, p_key);
}

bool NetwPropertySetBinding::property_present(
    Node *p_node,
    const StringName &p_key
) const {
    stage_properties(p_node);
    if (staged_properties_valid && staged_properties.has(p_key)) {
        return true;
    }
    return gd::has_property(p_node, p_key);
}

Array NetwPropertySetBinding::gather_row(
    const Array &p_fields,
    bool p_allow_missing
) {
    NETW_ZONE_NC("Binding gather row", colors::WIRE);
    NETW_ZONE_VALUE(p_fields.size());
    Node *held = node();
    Array values;
    if (held == nullptr) {
        return values;
    }
    for (int at = 0; at < p_fields.size(); at++) {
        const Ref<NetwPropertySetColumn> field = p_fields[at];
        const StringName key
            = field.is_valid() ? field->get_key() : StringName();
        const bool readable = property_present(held, key);
        if (!readable && !p_allow_missing) {
            return Array();
        }
        values.push_back(readable ? held->get(key) : Variant());
    }
    return values;
}

Dictionary NetwPropertySetBinding::gather_fields(
    Node *p_node,
    const Array &p_fields,
    bool p_allow_missing
) {
    NETW_ZONE_NC("Binding gather fields", colors::WIRE);
    NETW_ZONE_VALUE(p_fields.size());
    Array readable;
    {
        NETW_ZONE_NC("Binding readable probe", colors::WIRE);
        for (int at = 0; at < p_fields.size(); at++) {
            const Ref<NetwPropertySetColumn> field = p_fields[at];
            const StringName key
                = field.is_valid() ? field->get_key() : StringName();
            readable.push_back(property_present(p_node, key));
        }
    }
    const Callable gatherer
        = callable_mp(this, &NetwPropertySetBinding::gather_row)
              .bind(p_fields, p_allow_missing);
    NetwMultiplayer *session = core();
    const Array values = session != nullptr
        ? session->run_gather_set(entity_rid(), comp, gatherer)
        : Array(gatherer.call());
    Dictionary out;
    out[StringName("ok")] = values.size() == p_fields.size();
    out[StringName("values")] = values;
    out[StringName("readable")] = readable;
    return out;
}

Error NetwPropertySetBinding::write_row(
    const Array &p_values,
    const Array &p_keys
) {
    NETW_ZONE_NC("Binding write row", colors::WIRE);
    NETW_ZONE_VALUE(p_keys.size());
    Node *held = node();
    if (held == nullptr || p_values.size() != p_keys.size()) {
        return ERR_INVALID_DATA;
    }
    for (int at = 0; at < p_keys.size(); at++) {
        held->set(StringName(p_keys[at]), p_values[at]);
    }
    return OK;
}

int NetwPropertySetBinding::set_index_of(
    const Ref<NetwPropertySetColumn> &p_field
) const {
    if (set.is_null() || p_field.is_null()) {
        return -1;
    }
    for (int at = 0; at < set->columns.size(); at++) {
        const Ref<NetwPropertySetColumn> held = set->columns[at];
        if (held == p_field) {
            return at;
        }
    }
    return -1;
}

void NetwPropertySetBinding::resolve_entity_columns(
    ReplicationSend *p_send,
    const Array &p_fields,
    int64_t p_ordinal,
    Array &r_values
) {
    if (p_send == nullptr) {
        return;
    }
    for (int at = 0; at < p_fields.size(); at++) {
        const Ref<NetwPropertySetColumn> field = p_fields[at];
        if (field.is_null() || field->get_type() != SchemaCore::ENTITY) {
            continue;
        }
        repl::EntitySlot slot;
        slot.route = get_route();
        slot.comp = uint8_t(p_ordinal);
        slot.column = uint32_t(set_index_of(field));
        r_values[at] = p_send->entity_resolve(slot, int64_t(r_values[at]));
    }
}

Error NetwPropertySetBinding::write_entity_column(
    int64_t p_column,
    int64_t p_route
) {
    Node *held = node();
    if (held == nullptr || set.is_null() || p_column < 0
        || p_column >= set->columns.size()) {
        return ERR_DOES_NOT_EXIST;
    }
    const Ref<NetwPropertySetColumn> field = set->columns[int(p_column)];
    if (field.is_null()) {
        return ERR_DOES_NOT_EXIST;
    }
    Array keys;
    keys.push_back(field->get_key());
    Array values;
    values.push_back(p_route);
    return apply_values(held, keys, values);
}

Error NetwPropertySetBinding::apply_values(
    Node *p_node,
    const Array &p_keys,
    const Array &p_values
) {
    const Callable applier
        = callable_mp(this, &NetwPropertySetBinding::write_row).bind(p_keys);
    NetwMultiplayer *session = core();
    if (session == nullptr) {
        return write_row(p_values, p_keys);
    }
    return session->run_apply_set(entity_rid(), comp, p_values, applier);
}

Array NetwPropertySetBinding::volatile_row() {
    NETW_ZONE_NC("Binding volatile row", colors::WIRE);
    Node *held = node();
    if (held == nullptr) {
        return Array();
    }
    const Array fields = lane_fields(NetwPropertySet::VOLATILE);
    if (fields.is_empty()) {
        return Array();
    }
    const Dictionary gathered = gather_fields(held, fields, false);
    return bool(gathered[StringName("ok")])
        ? Array(gathered[StringName("values")])
        : Array();
}

Array NetwPropertySetBinding::retained_row() {
    NETW_ZONE_NC("Binding retained row", colors::WIRE);
    Node *held = node();
    if (held == nullptr) {
        return Array();
    }
    const Array fields = lane_fields(NetwPropertySet::RETAINED);
    if (fields.is_empty()) {
        return Array();
    }
    const Dictionary gathered = gather_fields(held, fields, false);
    return bool(gathered[StringName("ok")])
        ? Array(gathered[StringName("values")])
        : Array();
}

namespace {

LocalVector<int> recipients_of(const PackedInt32Array &p_recipients) {
    LocalVector<int> out;
    for (int64_t at = 0; at < p_recipients.size(); ++at) {
        out.push_back(p_recipients[at]);
    }
    return out;
}

} // namespace

void NetwPropertySetBinding::offer_rows(
    int64_t p_ordinal,
    const PackedInt32Array &p_recipients,
    int64_t p_tick,
    int64_t p_life,
    LocalVector<repl::RowOffer> &r_offers
) {
    NETW_ZONE_NC("Binding offer rows", colors::WIRE);
    static const wire::WireRegistry registry
        = wire::WireRegistry::create_default();
    static const wire::ChannelDecl *row
        = registry.find_channel_by_name("SYNC_ROW");
    static const wire::ChannelDecl *row_delta
        = registry.find_channel_by_name("SYNC_ROW_DELTA");
    static const wire::ChannelDecl *row_window
        = registry.find_channel_by_name("SYNC_ROW_WINDOW");

    if (set.is_null() || row == nullptr || row_delta == nullptr
        || row_window == nullptr) {
        return;
    }
    const int64_t tick = authored_tick >= 0 ? authored_tick : p_tick;

    if (!volatile_external) {
        const Array values = volatile_row();
        if (!values.is_empty()) {
            const bool windowed = is_windowed() && !set->masked;
            repl::RowOffer offer;
            offer.route = route;
            offer.comp = uint8_t(p_ordinal);
            offer.channel = uint8_t(windowed ? row_window->id : row->id);
            offer.schema = &set->get_volatile_schema();
            offer.values = values;
            offer.recipients = recipients_of(p_recipients);
            offer.tick = tick;
            offer.ack = reconcile_ack;
            offer.masked = set->masked && !windowed;
            offer.windowed = windowed;
            offer.window = uint32_t(set->window);
            offer.life = p_life;
            offer.priority = 1.0f;
            r_offers.push_back(offer);
        }
    }

    const Array retained = retained_row();
    if (!retained.is_empty()) {
        repl::RowOffer offer;
        offer.route = route;
        offer.comp = uint8_t(p_ordinal);
        offer.channel = uint8_t(row_delta->id);
        offer.schema = &set->get_retained_schema();
        offer.values = retained;
        offer.recipients = recipients_of(p_recipients);
        offer.tick = -1;
        offer.reliable = true;
        offer.life = p_life;
        offer.priority = 1.0f;
        r_offers.push_back(offer);
    }
}

Dictionary NetwPropertySetBinding::apply_row_frame(
    ReplicationSend *p_send,
    const PackedByteArray &p_frame,
    const repl::RowArrival &p_arrival
) {
    Node *held = node();
    if (held == nullptr || p_send == nullptr || set.is_null()) {
        return Dictionary();
    }
    const Array fields = lane_fields(NetwPropertySet::VOLATILE);
    if (fields.is_empty()) {
        return Dictionary();
    }
    const Dictionary applied = p_send->apply(
        set->get_volatile_schema(),
        held_row,
        p_frame,
        p_arrival.base_tick,
        p_arrival.life,
        &volatile_ring,
        p_arrival.seq
    );
    if (!bool(applied.get("ok", false))) {
        return Dictionary();
    }
    Array values = applied["values"];
    if (values.size() != fields.size()) {
        return Dictionary();
    }
    resolve_entity_columns(p_send, fields, p_arrival.ordinal, values);
    held_row = applied["held"];

    StagedWrites staged;
    staged.ordinal = p_arrival.ordinal;
    staged.tick = int64_t(applied.get("tick", -1));
    staged.ack = int64_t(applied.get("ack", -1));
    staged.whole = bool(applied.get("whole", true));
    staged.values = values;
    const Array keys = keys_of(fields);
    staged.keys = keys;
    for (int at = 0; at < fields.size(); at++) {
        staged.row[keys[at]] = values[at];
    }
    if (write_gate && apply_values(held, keys, values) != OK) {
        return Dictionary();
    }
    const Dictionary header = staged.header();
    if (on_applied.is_valid()) {
        on_applied.call(header);
    }
    return header;
}

Dictionary NetwPropertySetBinding::apply_window_frame(
    ReplicationSend *p_send,
    const PackedByteArray &p_frame,
    const repl::RowArrival &p_arrival
) {
    Node *held = node();
    if (held == nullptr || p_send == nullptr || set.is_null()) {
        return Dictionary();
    }
    const Array fields = lane_fields(NetwPropertySet::VOLATILE);
    if (fields.is_empty()) {
        return Dictionary();
    }
    const Dictionary applied = p_send->apply_window(
        set->get_volatile_schema(),
        p_frame,
        p_arrival.base_tick,
        p_arrival.life
    );
    if (!bool(applied.get("ok", false))) {
        return Dictionary();
    }
    const Array samples = applied["samples"];
    if (samples.is_empty()) {
        return Dictionary();
    }
    const Array keys = keys_of(fields);
    Array rows;
    for (int at = 0; at < samples.size(); at++) {
        const Dictionary sample = samples[at];
        const Array values = sample["values"];
        if (values.size() != fields.size()) {
            return Dictionary();
        }
        Dictionary row;
        for (int index = 0; index < keys.size(); index++) {
            row[keys[index]] = values[index];
        }
        Dictionary entry;
        entry[StringName("tick")] = int64_t(sample["tick"]);
        entry[StringName("payload")] = row;
        rows.push_back(entry);
    }

    const Dictionary newest = rows[rows.size() - 1];
    StagedWrites staged;
    staged.ordinal = p_arrival.ordinal;
    staged.tick = int64_t(applied.get("tick", -1));
    staged.ack = int64_t(applied.get("ack", -1));
    staged.keys = keys;
    const Dictionary last = samples[samples.size() - 1];
    staged.values = last["values"];
    staged.row = newest[StringName("payload")];
    staged.samples = rows;
    if (write_gate && apply_values(held, keys, staged.values) != OK) {
        return Dictionary();
    }
    const Dictionary header = staged.header();
    if (on_applied.is_valid()) {
        on_applied.call(header);
    }
    return header;
}

Dictionary NetwPropertySetBinding::apply_retained_row(
    ReplicationSend *p_send,
    const PackedByteArray &p_frame,
    const repl::RowArrival &p_arrival
) {
    Node *held = node();
    if (held == nullptr || p_send == nullptr || set.is_null()) {
        return Dictionary();
    }
    const Array fields = lane_fields(NetwPropertySet::RETAINED);
    if (fields.is_empty()) {
        return Dictionary();
    }
    const Dictionary applied = p_send->apply(
        set->get_retained_schema(),
        held_retained,
        p_frame,
        p_arrival.base_tick,
        p_arrival.life,
        nullptr,
        -1,
        repl::BaselineNaming::BY_ORDER
    );
    if (!bool(applied.get("ok", false))) {
        return Dictionary();
    }
    Array values = applied["values"];
    if (values.size() != fields.size()) {
        return Dictionary();
    }
    resolve_entity_columns(p_send, fields, p_arrival.ordinal, values);
    held_retained = applied["held"];

    StagedWrites staged;
    staged.ordinal = p_arrival.ordinal;
    staged.tick = int64_t(applied.get("tick", -1));
    staged.ack = int64_t(applied.get("ack", -1));
    staged.whole = bool(applied.get("whole", true));
    staged.values = values;
    const Array keys = keys_of(fields);
    staged.keys = keys;
    for (int at = 0; at < fields.size(); at++) {
        staged.row[keys[at]] = values[at];
    }
    if (apply_values(held, keys, values) != OK) {
        return Dictionary();
    }
    return staged.header();
}

Dictionary NetwPropertySetBinding::snapshot_payload() {
    NETW_ZONE_NC("Binding snapshot payload", colors::PREDICTION);
    Node *held = node();
    if (held == nullptr || set.is_null()) {
        return Dictionary();
    }
    const Dictionary gathered = gather_fields(held, set->columns, true);
    if (!bool(gathered[StringName("ok")])) {
        return Dictionary();
    }
    const Array readable = gathered[StringName("readable")];
    const Array values = gathered[StringName("values")];
    Dictionary payload;
    for (int at = 0; at < set->columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = set->columns[at];
        if (column.is_valid() && at < readable.size() && bool(readable[at])) {
            payload[column->get_key()] = values[at];
        }
    }
    return payload;
}

void NetwPropertySetBinding::apply_payload(const Dictionary &p_payload) {
    Node *held = node();
    if (held == nullptr || set.is_null()) {
        return;
    }
    Array keys;
    Array values;
    for (int at = 0; at < set->columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = set->columns[at];
        if (column.is_null()) {
            continue;
        }
        const StringName key = column->get_key();
        if (p_payload.has(key) && gd::has_property(held, key)) {
            keys.push_back(key);
            values.push_back(p_payload[key]);
        }
    }
    apply_values(held, keys, values);
}

Dictionary NetwPropertySetBinding::canonical_plan(
    const Dictionary &p_payload
) const {
    NETW_ZONE_NC("Binding canonical plan", colors::PREDICTION);
    Node *held = node();
    if (p_payload.is_empty() || held == nullptr || set.is_null()) {
        return Dictionary();
    }
    Array keys;
    Array values;
    Array quantizers;
    Array types;
    for (int at = 0; at < set->columns.size(); at++) {
        const Ref<NetwPropertySetColumn> field = set->columns[at];
        if (field.is_null() || !p_payload.has(field->get_key())) {
            continue;
        }
        const StringName key = field->get_key();
        keys.push_back(key);
        values.push_back(p_payload[key]);
        quantizers.push_back(field->get_quantizer());
        types.push_back(property_type(held, key));
    }
    if (keys.is_empty()) {
        return Dictionary();
    }
    Dictionary plan;
    plan[StringName("keys")] = keys;
    plan[StringName("values")] = values;
    plan[StringName("quantizers")] = quantizers;
    plan[StringName("types")] = types;
    return plan;
}

PackedByteArray NetwPropertySetBinding::canonical_run(
    const Dictionary &p_plan
) {
    wire::WriteStream stream;
    if (!call_args::values_write(
            stream,
            p_plan["values"],
            p_plan["quantizers"],
            p_plan["types"]
        )
        || !stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

Dictionary NetwPropertySetBinding::canonicalize_payload(
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("Binding canonicalize payload", colors::PREDICTION);
    const Dictionary plan = canonical_plan(p_payload);
    if (plan.is_empty()) {
        return p_payload.duplicate();
    }
    const PackedByteArray bytes = canonical_run(plan);
    wire::ReadStream reader(bytes);
    Array canonical;
    if (bytes.is_empty()
        || !call_args::values_read(
            reader,
            plan["quantizers"],
            plan["types"],
            canonical
        )) {
        return p_payload.duplicate();
    }
    const Array keys = plan["keys"];
    Dictionary out = p_payload.duplicate();
    const int span
        = keys.size() < canonical.size() ? keys.size() : canonical.size();
    for (int at = 0; at < span; at++) {
        out[keys[at]] = canonical[at];
    }
    return out;
}

PackedByteArray NetwPropertySetBinding::canonical_bytes(
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("Binding canonical bytes", colors::PREDICTION);
    const Dictionary plan = canonical_plan(p_payload);
    if (plan.is_empty()) {
        return PackedByteArray();
    }
    return canonical_run(plan);
}

Ref<NetwPropertySetColumn> NetwPropertySetBinding::field_of(
    const StringName &p_property
) const {
    if (set.is_null()) {
        return Ref<NetwPropertySetColumn>();
    }
    for (int at = 0; at < set->columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = set->columns[at];
        if (column.is_valid() && column->get_key() == p_property) {
            return column;
        }
    }
    return Ref<NetwPropertySetColumn>();
}

int64_t NetwPropertySetBinding::property_class_of(
    const StringName &p_property
) const {
    const Ref<NetwPropertySetColumn> field = field_of(p_property);
    return field.is_valid() ? field->property_class
                            : int64_t(NetwPropertySet::CAUSAL);
}

double NetwPropertySetBinding::converge_stiffness_of(
    const StringName &p_property
) const {
    const Ref<NetwPropertySetColumn> field = field_of(p_property);
    return field.is_valid() ? field->converge_stiffness : 0.0;
}

StringName NetwPropertySetBinding::carry_channel_of(
    const StringName &p_property
) const {
    const Ref<NetwPropertySetColumn> field = field_of(p_property);
    return field.is_valid() ? field->carry_channel : StringName();
}

bool NetwPropertySetBinding::teleport_only_of(
    const StringName &p_property
) const {
    const Ref<NetwPropertySetColumn> field = field_of(p_property);
    return field.is_valid() && field->explicit_teleport_only;
}

bool NetwPropertySetBinding::reconcile_only_of(
    const StringName &p_property
) const {
    const Ref<NetwPropertySetColumn> field = field_of(p_property);
    return field.is_valid() && field->explicit_reconcile_only;
}

double NetwPropertySetBinding::epsilon_override_of(
    const StringName &p_property
) const {
    const Ref<NetwPropertySetColumn> field = field_of(p_property);
    return field.is_valid() ? field->epsilon_override : -1.0;
}

double NetwPropertySetBinding::teleport_at_of(
    const StringName &p_property
) const {
    const Ref<NetwPropertySetColumn> field = field_of(p_property);
    return field.is_valid() ? field->teleport_at_override : -1.0;
}

void NetwPropertySetBinding::clear_peer(int64_t p_peer) {
    held_row = PackedByteArray();
    held_retained = PackedByteArray();
    volatile_ring.clear();
}

void NetwPropertySetBinding::_bind_methods() {
    ClassDB::bind_method(D_METHOD("node"), &NetwPropertySetBinding::node);
    ClassDB::bind_method(D_METHOD("get_set"), &NetwPropertySetBinding::get_set);
    ClassDB::bind_method(
        D_METHOD("set_set", "value"),
        &NetwPropertySetBinding::set_set
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "set",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPropertySet"
        ),
        "set_set",
        "get_set"
    );
    ClassDB::bind_method(
        D_METHOD("get_route"),
        &NetwPropertySetBinding::get_route
    );
    ClassDB::bind_method(
        D_METHOD("set_route", "value"),
        &NetwPropertySetBinding::set_route
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "route"), "set_route", "get_route");
    ClassDB::bind_method(
        D_METHOD("get_comp"),
        &NetwPropertySetBinding::get_comp
    );
    ClassDB::bind_method(
        D_METHOD("set_comp", "value"),
        &NetwPropertySetBinding::set_comp
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "comp"), "set_comp", "get_comp");
    ClassDB::bind_method(
        D_METHOD("get_order_key"),
        &NetwPropertySetBinding::get_order_key
    );
    ClassDB::bind_method(
        D_METHOD("set_order_key", "value"),
        &NetwPropertySetBinding::set_order_key
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "order_key"),
        "set_order_key",
        "get_order_key"
    );
    ClassDB::bind_method(
        D_METHOD("get_on_applied"),
        &NetwPropertySetBinding::get_on_applied
    );
    ClassDB::bind_method(
        D_METHOD("set_on_applied", "value"),
        &NetwPropertySetBinding::set_on_applied
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::CALLABLE, "on_applied"),
        "set_on_applied",
        "get_on_applied"
    );
    ClassDB::bind_method(
        D_METHOD("is_active"),
        &NetwPropertySetBinding::is_active
    );
    ClassDB::bind_method(
        D_METHOD("is_windowed"),
        &NetwPropertySetBinding::is_windowed
    );
    ClassDB::bind_method(
        D_METHOD("property_class_of", "property"),
        &NetwPropertySetBinding::property_class_of
    );
    ClassDB::bind_method(
        D_METHOD("converge_stiffness_of", "property"),
        &NetwPropertySetBinding::converge_stiffness_of
    );
    ClassDB::bind_method(
        D_METHOD("carry_channel_of", "property"),
        &NetwPropertySetBinding::carry_channel_of
    );
    ClassDB::bind_method(
        D_METHOD("teleport_only_of", "property"),
        &NetwPropertySetBinding::teleport_only_of
    );
    ClassDB::bind_method(
        D_METHOD("reconcile_only_of", "property"),
        &NetwPropertySetBinding::reconcile_only_of
    );
    ClassDB::bind_method(
        D_METHOD("epsilon_override_of", "property"),
        &NetwPropertySetBinding::epsilon_override_of
    );
    ClassDB::bind_method(
        D_METHOD("teleport_at_of", "property"),
        &NetwPropertySetBinding::teleport_at_of
    );
    ClassDB::bind_method(
        D_METHOD("field_of", "property"),
        &NetwPropertySetBinding::field_of
    );
}

} // namespace netw

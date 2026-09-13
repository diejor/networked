#include "netw/api/property_set.hpp"

#include "godot/class_db.hpp"
#include "netw/api/schema_core.hpp"

using namespace godot;

namespace netw {

Ref<NetwPropertySetColumn> NetwPropertySetColumn::create(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    bool p_watch,
    int64_t p_type
) {
    Ref<NetwPropertySetColumn> made;
    made.instantiate();
    made->shape.key = p_key;
    made->shape.type = int(p_type);
    made->shape.quantizer = p_quantizer;
    made->watch = p_watch;
    made->lane
        = p_watch ? NetwPropertySet::RETAINED : NetwPropertySet::VOLATILE;
    return made;
}

StringName NetwPropertySetColumn::get_key() const {
    return shape.key;
}

int64_t NetwPropertySetColumn::get_type() const {
    return shape.type;
}

Ref<NetwQuantize> NetwPropertySetColumn::get_quantizer() const {
    return shape.quantizer;
}

void NetwPropertySetColumn::set_quantizer(const Ref<NetwQuantize> &p_value) {
    shape.quantizer = p_value;
}

table::DeltaMode NetwPropertySet::delta_of(
    const Ref<NetwPropertySetColumn> &p_column
) const {
    if (p_column.is_null()) {
        return table::DeltaMode::FULL;
    }
    if (p_column->delta_mode == NetwPropertySetColumn::DELTA_LADDER) {
        return table::DeltaMode::LADDER;
    }
    if (p_column->delta_mode == NetwPropertySetColumn::DELTA_FULL) {
        return table::DeltaMode::FULL;
    }
    const Ref<NetwQuantize> grid = p_column->shape.quantizer;
    if (record != RECORD_STATE || grid.is_null()
        || grid->get_class() == StringName("NetwQuantizeAngle")) {
        return table::DeltaMode::FULL;
    }
    const Variant::Type element = static_cast<Variant::Type>(
        SchemaCore::element_type(p_column->shape.type)
    );
    return grid->bit_width(element) > 8 ? table::DeltaMode::LADDER
                                        : table::DeltaMode::FULL;
}

void NetwPropertySet::project_lanes() {
    volatile_schema = SchemaRecord();
    retained_schema = SchemaRecord();
    for (int at = 0; at < columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        if (column.is_null()) {
            continue;
        }
        SchemaColumn shape = column->shape;
        shape.delta = delta_of(column);
        if (column->lane == VOLATILE) {
            volatile_schema.columns.push_back(shape);
        } else if (column->lane == RETAINED) {
            retained_schema.columns.push_back(shape);
        }
    }
    SchemaCore::fix(&volatile_schema);
    SchemaCore::fix(&retained_schema);
    lanes_projected = true;
}

const SchemaRecord &NetwPropertySet::get_volatile_schema() {
    if (!lanes_projected) {
        project_lanes();
    }
    return volatile_schema;
}

const SchemaRecord &NetwPropertySet::get_retained_schema() {
    if (!lanes_projected) {
        project_lanes();
    }
    return retained_schema;
}

void NetwPropertySet::reproject_lanes() {
    lanes_projected = false;
}

Ref<NetwPropertySetColumn> NetwPropertySet::bind_column(
    const Ref<NetwPropertySetColumn> &p_column
) {
    if (p_column.is_null()) {
        return p_column;
    }
    p_column->schema_column = int64_t(schema.columns.size());
    schema.columns.push_back(p_column->shape);
    columns.push_back(p_column);
    reproject_lanes();
    return p_column;
}

Ref<NetwPropertySetColumn> NetwPropertySet::member(
    int64_t p_schema_column
) const {
    for (int at = 0; at < columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        if (column.is_valid() && column->schema_column == p_schema_column) {
            return column;
        }
    }
    return Ref<NetwPropertySetColumn>();
}

TypedArray<StringName> NetwPropertySet::keys() const {
    TypedArray<StringName> out;
    for (int at = 0; at < columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        out.push_back(column.is_valid() ? column->get_key() : StringName());
    }
    return out;
}

Array NetwPropertySet::quantizers() const {
    Array out;
    for (int at = 0; at < columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        out.push_back(
            column.is_valid() ? column->get_quantizer() : Ref<NetwQuantize>()
        );
    }
    return out;
}

int64_t NetwPropertySet::wire_hash() const {
    if (sealed) {
        return sealed_wire_hash;
    }
    PackedStringArray parts;
    for (int at = 0; at < columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        if (column.is_null()) {
            continue;
        }
        const int64_t stride = column->shape.stride;
        parts.push_back(
            String::num_int64(at) + ":" + String(column->get_key()) + ":"
            + String::num_int64(column->get_type()) + ":"
            + String::num_int64(stride) + ":"
            + SchemaCore::quantizer_tag(&column->shape) + ":"
            + String::num_int64(column->lane)
        );
    }
    return int64_t(uint32_t(String("|").join(parts).hash()));
}

int64_t NetwPropertySet::identity_hash() const {
    PackedStringArray parts;
    parts.push_back(String::num_int64(record));
    for (int at = 0; at < columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        if (column.is_null()) {
            continue;
        }
        parts.push_back(
            String::num_int64(at) + ":" + String(column->get_key()) + ":"
            + String::num_int64(column->get_type()) + ":"
            + String::num_int64(column->shape.stride) + ":"
            + SchemaCore::quantizer_tag(&column->shape) + ":"
            + String::num_int64(column->lane) + ":"
            + String::num_int64(int64_t(delta_of(column)))
        );
    }
    const CharString text = String("|").join(parts).utf8();
    uint64_t fold = 14695981039346656037ULL;
    for (int at = 0; at < int(text.length()); ++at) {
        fold ^= uint64_t(uint8_t(text[at]));
        fold *= 1099511628211ULL;
    }
    return int64_t(fold);
}

void NetwPropertySet::seal() {
    if (sealed) {
        return;
    }
    SchemaCore::fix(&schema);
    sealed_wire_hash = wire_hash();
    sealed = true;
}

int64_t NetwPropertySet::column_type_for(
    const Ref<Script> &p_script,
    Node *p_node,
    const StringName &p_property
) {
    int64_t variant_type = Variant::NIL;
    if (p_script.is_valid()) {
        const Array declared = gd::script_property_list(p_script);
        for (int at = 0; at < declared.size(); at++) {
            const Dictionary entry = declared[at];
            const int64_t usage = entry.get("usage", 0);
            if (!(usage & PROPERTY_USAGE_SCRIPT_VARIABLE)) {
                continue;
            }
            if (StringName(entry.get("name", StringName())) == p_property) {
                variant_type = entry.get("type", int64_t(Variant::NIL));
                break;
            }
        }
    }
    if (variant_type == Variant::NIL && p_node != nullptr) {
        const Array live = gd::property_list(p_node);
        for (int at = 0; at < live.size(); at++) {
            const Dictionary entry = live[at];
            if (StringName(entry.get("name", StringName())) == p_property) {
                variant_type = entry.get("type", int64_t(Variant::NIL));
                break;
            }
        }
    }
    return SchemaCore::type_from_variant(int(variant_type));
}

void NetwPropertySet::stamp_column_types(
    const Ref<NetwPropertySet> &p_set,
    const Ref<Script> &p_script,
    Node *p_node
) {
    if (p_set.is_null()) {
        return;
    }
    for (int at = 0; at < p_set->columns.size(); at++) {
        const Ref<NetwPropertySetColumn> column = p_set->columns[at];
        if (column.is_null()) {
            continue;
        }
        column->shape.type
            = int(column_type_for(p_script, p_node, column->get_key()));
        SchemaColumn *declared = p_set->schema.at(int(column->schema_column));
        if (declared != nullptr) {
            declared->type = column->shape.type;
        }
        p_set->reproject_lanes();
    }
}

void NetwPropertySet::compile_against(Node *p_node) {
    if (sealed || p_node == nullptr) {
        return;
    }
    stamp_column_types(
        Ref<NetwPropertySet>(this),
        p_node->get_script(),
        p_node
    );
    seal();
}

void NetwPropertySetColumn::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPropertySetColumn",
        D_METHOD("create", "key", "quantizer", "watch", "type"),
        &NetwPropertySetColumn::create,
        DEFVAL(Ref<NetwQuantize>()),
        DEFVAL(false),
        DEFVAL(int64_t(SchemaCore::VARIANT))
    );
    ClassDB::bind_method(D_METHOD("get_key"), &NetwPropertySetColumn::get_key);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "key"), "", "get_key");
    ClassDB::bind_method(
        D_METHOD("get_type"),
        &NetwPropertySetColumn::get_type
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "type"), "", "get_type");
    ClassDB::bind_method(
        D_METHOD("get_quantizer"),
        &NetwPropertySetColumn::get_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("set_quantizer", "value"),
        &NetwPropertySetColumn::set_quantizer
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "quantizer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwQuantize"
        ),
        "set_quantizer",
        "get_quantizer"
    );
    ClassDB::bind_method(
        D_METHOD("get_schema_column"),
        &NetwPropertySetColumn::get_schema_column
    );
    ClassDB::bind_method(
        D_METHOD("set_schema_column", "value"),
        &NetwPropertySetColumn::set_schema_column
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "schema_column"),
        "set_schema_column",
        "get_schema_column"
    );
    ClassDB::bind_method(
        D_METHOD("get_delta_mode"),
        &NetwPropertySetColumn::get_delta_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_delta_mode", "value"),
        &NetwPropertySetColumn::set_delta_mode
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "delta_mode"),
        "set_delta_mode",
        "get_delta_mode"
    );
    BIND_ENUM_CONSTANT(DELTA_AUTO);
    BIND_ENUM_CONSTANT(DELTA_FULL);
    BIND_ENUM_CONSTANT(DELTA_LADDER);
    ClassDB::bind_method(
        D_METHOD("get_watch"),
        &NetwPropertySetColumn::get_watch
    );
    ClassDB::bind_method(
        D_METHOD("set_watch", "value"),
        &NetwPropertySetColumn::set_watch
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "watch"),
        "set_watch",
        "get_watch"
    );
    ClassDB::bind_method(
        D_METHOD("get_lane"),
        &NetwPropertySetColumn::get_lane
    );
    ClassDB::bind_method(
        D_METHOD("set_lane", "value"),
        &NetwPropertySetColumn::set_lane
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "lane"), "set_lane", "get_lane");
    ClassDB::bind_method(
        D_METHOD("get_property_class"),
        &NetwPropertySetColumn::get_property_class
    );
    ClassDB::bind_method(
        D_METHOD("set_property_class", "value"),
        &NetwPropertySetColumn::set_property_class
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "property_class"),
        "set_property_class",
        "get_property_class"
    );
    ClassDB::bind_method(
        D_METHOD("get_converge_stiffness"),
        &NetwPropertySetColumn::get_converge_stiffness
    );
    ClassDB::bind_method(
        D_METHOD("set_converge_stiffness", "value"),
        &NetwPropertySetColumn::set_converge_stiffness
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "converge_stiffness"),
        "set_converge_stiffness",
        "get_converge_stiffness"
    );
    ClassDB::bind_method(
        D_METHOD("get_carry_channel"),
        &NetwPropertySetColumn::get_carry_channel
    );
    ClassDB::bind_method(
        D_METHOD("set_carry_channel", "value"),
        &NetwPropertySetColumn::set_carry_channel
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "carry_channel"),
        "set_carry_channel",
        "get_carry_channel"
    );
    ClassDB::bind_method(
        D_METHOD("get_explicit_teleport_only"),
        &NetwPropertySetColumn::get_explicit_teleport_only
    );
    ClassDB::bind_method(
        D_METHOD("set_explicit_teleport_only", "value"),
        &NetwPropertySetColumn::set_explicit_teleport_only
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "explicit_teleport_only"),
        "set_explicit_teleport_only",
        "get_explicit_teleport_only"
    );
    ClassDB::bind_method(
        D_METHOD("get_explicit_reconcile_only"),
        &NetwPropertySetColumn::get_explicit_reconcile_only
    );
    ClassDB::bind_method(
        D_METHOD("set_explicit_reconcile_only", "value"),
        &NetwPropertySetColumn::set_explicit_reconcile_only
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "explicit_reconcile_only"),
        "set_explicit_reconcile_only",
        "get_explicit_reconcile_only"
    );
    ClassDB::bind_method(
        D_METHOD("get_epsilon_override"),
        &NetwPropertySetColumn::get_epsilon_override
    );
    ClassDB::bind_method(
        D_METHOD("set_epsilon_override", "value"),
        &NetwPropertySetColumn::set_epsilon_override
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "epsilon_override"),
        "set_epsilon_override",
        "get_epsilon_override"
    );
    ClassDB::bind_method(
        D_METHOD("get_teleport_at_override"),
        &NetwPropertySetColumn::get_teleport_at_override
    );
    ClassDB::bind_method(
        D_METHOD("set_teleport_at_override", "value"),
        &NetwPropertySetColumn::set_teleport_at_override
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "teleport_at_override"),
        "set_teleport_at_override",
        "get_teleport_at_override"
    );
}

void NetwPropertySet::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_cadence"),
        &NetwPropertySet::get_cadence
    );
    ClassDB::bind_method(
        D_METHOD("set_cadence", "value"),
        &NetwPropertySet::set_cadence
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "cadence"),
        "set_cadence",
        "get_cadence"
    );
    ClassDB::bind_method(
        D_METHOD("get_profile"),
        &NetwPropertySet::get_profile
    );
    ClassDB::bind_method(
        D_METHOD("set_profile", "value"),
        &NetwPropertySet::set_profile
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "profile"),
        "set_profile",
        "get_profile"
    );
    ClassDB::bind_method(
        D_METHOD("get_trigger"),
        &NetwPropertySet::get_trigger
    );
    ClassDB::bind_method(
        D_METHOD("set_trigger", "value"),
        &NetwPropertySet::set_trigger
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "trigger"),
        "set_trigger",
        "get_trigger"
    );
    ClassDB::bind_method(D_METHOD("get_stamp"), &NetwPropertySet::get_stamp);
    ClassDB::bind_method(
        D_METHOD("set_stamp", "value"),
        &NetwPropertySet::set_stamp
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "stamp"), "set_stamp", "get_stamp");
    ClassDB::bind_method(D_METHOD("get_record"), &NetwPropertySet::get_record);
    ClassDB::bind_method(
        D_METHOD("set_record", "value"),
        &NetwPropertySet::set_record
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "record"),
        "set_record",
        "get_record"
    );
    ClassDB::bind_method(D_METHOD("get_window"), &NetwPropertySet::get_window);
    ClassDB::bind_method(
        D_METHOD("set_window", "value"),
        &NetwPropertySet::set_window
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "window"),
        "set_window",
        "get_window"
    );
    ClassDB::bind_method(
        D_METHOD("get_audience"),
        &NetwPropertySet::get_audience
    );
    ClassDB::bind_method(
        D_METHOD("set_audience", "value"),
        &NetwPropertySet::set_audience
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "audience"),
        "set_audience",
        "get_audience"
    );
    ClassDB::bind_method(D_METHOD("get_masked"), &NetwPropertySet::get_masked);
    ClassDB::bind_method(
        D_METHOD("set_masked", "value"),
        &NetwPropertySet::set_masked
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "masked"),
        "set_masked",
        "get_masked"
    );
    ClassDB::bind_method(D_METHOD("get_policy"), &NetwPropertySet::get_policy);
    ClassDB::bind_method(
        D_METHOD("set_policy", "value"),
        &NetwPropertySet::set_policy
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "policy"),
        "set_policy",
        "get_policy"
    );
    ClassDB::bind_method(
        D_METHOD("get_channel"),
        &NetwPropertySet::get_channel
    );
    ClassDB::bind_method(
        D_METHOD("set_channel", "value"),
        &NetwPropertySet::set_channel
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "channel"),
        "set_channel",
        "get_channel"
    );
    ClassDB::bind_method(
        D_METHOD("get_reliable"),
        &NetwPropertySet::get_reliable
    );
    ClassDB::bind_method(
        D_METHOD("set_reliable", "value"),
        &NetwPropertySet::set_reliable
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "reliable"),
        "set_reliable",
        "get_reliable"
    );
    ClassDB::bind_method(D_METHOD("get_sealed"), &NetwPropertySet::get_sealed);
    ClassDB::bind_method(
        D_METHOD("set_sealed", "value"),
        &NetwPropertySet::set_sealed
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "sealed"),
        "set_sealed",
        "get_sealed"
    );
    ClassDB::bind_method(
        D_METHOD("get_columns"),
        &NetwPropertySet::get_columns
    );
    ClassDB::bind_method(
        D_METHOD("set_columns", "value"),
        &NetwPropertySet::set_columns
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "columns",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwPropertySetColumn"
        ),
        "set_columns",
        "get_columns"
    );
    ClassDB::bind_method(
        D_METHOD("get_rid_handle"),
        &NetwPropertySet::get_rid_handle
    );
    ClassDB::bind_method(
        D_METHOD("set_rid_handle", "value"),
        &NetwPropertySet::set_rid_handle
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::RID, "rid"),
        "set_rid_handle",
        "get_rid_handle"
    );

    ClassDB::bind_method(
        D_METHOD("compile_against", "node"),
        &NetwPropertySet::compile_against
    );
    ClassDB::bind_method(
        D_METHOD("bind", "column"),
        &NetwPropertySet::bind_column
    );
    ClassDB::bind_method(
        D_METHOD("reproject_lanes"),
        &NetwPropertySet::reproject_lanes
    );
    ClassDB::bind_method(
        D_METHOD("member", "schema_column"),
        &NetwPropertySet::member
    );
    ClassDB::bind_method(D_METHOD("keys"), &NetwPropertySet::keys);
    ClassDB::bind_method(D_METHOD("quantizers"), &NetwPropertySet::quantizers);
    ClassDB::bind_method(D_METHOD("wire_hash"), &NetwPropertySet::wire_hash);
    ClassDB::bind_method(D_METHOD("seal"), &NetwPropertySet::seal);
    ClassDB::bind_static_method(
        "NetwPropertySet",
        D_METHOD("column_type_for", "script", "node", "property"),
        &NetwPropertySet::column_type_for
    );
    ClassDB::bind_static_method(
        "NetwPropertySet",
        D_METHOD("stamp_column_types", "set", "script", "node"),
        &NetwPropertySet::stamp_column_types
    );

    BIND_ENUM_CONSTANT(TICK);
    BIND_ENUM_CONSTANT(ON_DEMAND);
    BIND_ENUM_CONSTANT(ON_CHANGE);
    BIND_ENUM_CONSTANT(PLAIN);
    BIND_ENUM_CONSTANT(STAMPED);
    BIND_ENUM_CONSTANT(TRIGGER_TICK);
    BIND_ENUM_CONSTANT(TRIGGER_ON_CHANGE);
    BIND_ENUM_CONSTANT(TRIGGER_ON_DEMAND);
    BIND_ENUM_CONSTANT(STAMP_NONE);
    BIND_ENUM_CONSTANT(STAMP_TICK);
    BIND_ENUM_CONSTANT(STAMP_TICK_ACK);
    BIND_ENUM_CONSTANT(RECORD_NONE);
    BIND_ENUM_CONSTANT(RECORD_STATE);
    BIND_ENUM_CONSTANT(RECORD_INPUT);
    BIND_ENUM_CONSTANT(RECORD_BROADCAST);
    BIND_ENUM_CONSTANT(VOLATILE);
    BIND_ENUM_CONSTANT(RETAINED);
    BIND_ENUM_CONSTANT(CAUSAL);
    BIND_ENUM_CONSTANT(DERIVED);
    BIND_ENUM_CONSTANT(COSMETIC);
    BIND_ENUM_CONSTANT(AUDIENCE_PUBLIC);
    BIND_ENUM_CONSTANT(AUDIENCE_SERVER_ONLY);
}

} // namespace netw

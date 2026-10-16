#include "netw/repl/property_set.hpp"

#include "netw/api/schema_core.hpp"
#include "netw/script/model.hpp"

using namespace godot;

namespace netw::repl {

namespace {

uint64_t next_set_id() {
    static uint64_t minted = 0;
    minted += 1;
    return minted;
}

struct PropertyTypeLists {
    Array declared;
    Array live;
    bool live_fetched = false;
};

PropertyTypeLists lists_for(const Ref<Script> &p_script) {
    PropertyTypeLists lists;
    if (p_script.is_valid()) {
        lists.declared = gd::script_property_list(p_script);
    }
    return lists;
}

int64_t column_type_in(
    PropertyTypeLists &p_lists,
    Node *p_node,
    const StringName &p_property
) {
    gd::PropertyResolution resolved
        = gd::listed_property(p_lists.declared, p_property);
    if (p_node != nullptr) {
        if (!p_lists.live_fetched) {
            p_lists.live = gd::property_list(p_node);
            p_lists.live_fetched = true;
        }
        resolved
            = script::model::resolve_property(p_node, p_property, p_lists.live);
    }
    return resolved.type >= 0 ? SchemaCore::type_from_variant(resolved.type)
                              : SchemaCore::VARIANT;
}

} // namespace

PropertyColumn PropertyColumn::create(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    bool p_watch,
    enums::NetwMultiplayer::ColumnType p_type
) {
    PropertyColumn made;
    made.shape.key = p_key;
    made.shape.type = int(p_type);
    made.shape.quantizer = p_quantizer;
    made.watch = p_watch;
    made.lane = p_watch ? PropertySet::RETAINED : PropertySet::VOLATILE;
    return made;
}

PropertySet::PropertySet() : id(next_set_id()) {
}

table::DeltaMode PropertySet::delta_of(const PropertyColumn &p_column) const {
    if (p_column.delta_mode == PropertyColumn::DELTA_LADDER) {
        return table::DeltaMode::LADDER;
    }
    if (p_column.delta_mode == PropertyColumn::DELTA_FULL) {
        return table::DeltaMode::FULL;
    }
    const Ref<NetwQuantize> grid = p_column.shape.quantizer;
    if (record != RECORD_STATE || grid.is_null()
        || grid->get_class() == StringName("NetwQuantizeAngle")) {
        return table::DeltaMode::FULL;
    }
    const Variant::Type element = static_cast<Variant::Type>(
        SchemaCore::element_type(p_column.shape.type)
    );
    return grid->bit_width(element) > 8 ? table::DeltaMode::LADDER
                                        : table::DeltaMode::FULL;
}

void PropertySet::project_lanes() {
    volatile_schema = table::SchemaRecord();
    retained_schema = table::SchemaRecord();
    codec.clear();
    for (const PropertyColumn &column : columns) {
        table::SchemaColumn shape = column.shape;
        shape.delta = delta_of(column);
        if (column.lane == VOLATILE) {
            volatile_schema.columns.push_back(shape);
        } else if (column.lane == RETAINED) {
            retained_schema.columns.push_back(shape);
        }
        codec.append(
            shape.key,
            shape.quantizer,
            SchemaCore::element_type(shape.type),
            shape.type
        );
    }
    SchemaCore::fix(&volatile_schema);
    SchemaCore::fix(&retained_schema);
    codec.seal();
    lanes_projected = true;
}

const predict::FieldCodec &PropertySet::get_codec() {
    if (!lanes_projected) {
        project_lanes();
    }
    return codec;
}

const table::SchemaRecord &PropertySet::get_volatile_schema() {
    if (!lanes_projected) {
        project_lanes();
    }
    return volatile_schema;
}

const table::SchemaRecord &PropertySet::get_retained_schema() {
    if (!lanes_projected) {
        project_lanes();
    }
    return retained_schema;
}

void PropertySet::reproject_lanes() {
    lanes_projected = false;
}

PropertyColumn &PropertySet::bind_column(const PropertyColumn &p_column) {
    columns.push_back(p_column);
    PropertyColumn &bound = columns[columns.size() - 1];
    bound.schema_column = int64_t(schema.columns.size());
    schema.columns.push_back(bound.shape);
    reproject_lanes();
    return bound;
}

const PropertyColumn *PropertySet::member(int64_t p_schema_column) const {
    for (const PropertyColumn &column : columns) {
        if (column.schema_column == p_schema_column) {
            return &column;
        }
    }
    return nullptr;
}

const PropertyColumn *PropertySet::column_named(const StringName &p_key) const {
    for (const PropertyColumn &column : columns) {
        if (column.get_key() == p_key) {
            return &column;
        }
    }
    return nullptr;
}

int64_t PropertySet::index_of(const PropertyColumn *p_column) const {
    if (p_column == nullptr || columns.is_empty() || p_column < columns.ptr()
        || p_column >= columns.ptr() + columns.size()) {
        return -1;
    }
    return int64_t(p_column - columns.ptr());
}

TypedArray<StringName> PropertySet::keys() const {
    TypedArray<StringName> out;
    for (const PropertyColumn &column : columns) {
        out.push_back(column.get_key());
    }
    return out;
}

Array PropertySet::quantizers() const {
    Array out;
    for (const PropertyColumn &column : columns) {
        out.push_back(column.get_quantizer());
    }
    return out;
}

int64_t PropertySet::wire_hash() const {
    if (sealed) {
        return sealed_wire_hash;
    }
    PackedStringArray parts;
    for (uint32_t at = 0; at < columns.size(); at++) {
        const PropertyColumn &column = columns[at];
        parts.push_back(
            String::num_int64(at) + ":" + String(column.get_key()) + ":"
            + String::num_int64(int64_t(column.get_type())) + ":"
            + String::num_int64(column.shape.stride) + ":"
            + SchemaCore::quantizer_tag(&column.shape) + ":"
            + String::num_int64(column.lane)
        );
    }
    return int64_t(uint32_t(String("|").join(parts).hash()));
}

int64_t PropertySet::identity_hash() const {
    PackedStringArray parts;
    parts.push_back(String::num_int64(record));
    for (uint32_t at = 0; at < columns.size(); at++) {
        const PropertyColumn &column = columns[at];
        parts.push_back(
            String::num_int64(at) + ":" + String(column.get_key()) + ":"
            + String::num_int64(int64_t(column.get_type())) + ":"
            + String::num_int64(column.shape.stride) + ":"
            + SchemaCore::quantizer_tag(&column.shape) + ":"
            + String::num_int64(column.lane) + ":"
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

void PropertySet::seal() {
    if (sealed) {
        return;
    }
    SchemaCore::fix(&schema);
    sealed_wire_hash = wire_hash();
    sealed = true;
}

enums::NetwMultiplayer::ColumnType PropertySet::column_type_for(
    const Ref<Script> &p_script,
    Node *p_node,
    const StringName &p_property
) {
    PropertyTypeLists lists = lists_for(p_script);
    return enums::NetwMultiplayer::ColumnType(
        column_type_in(lists, p_node, p_property)
    );
}

void PropertySet::stamp_column_types(
    const Ref<Script> &p_script,
    Node *p_node
) {
    PropertyTypeLists lists = lists_for(p_script);
    for (PropertyColumn &column : columns) {
        column.shape.type
            = int(column_type_in(lists, p_node, column.get_key()));
        table::SchemaColumn *declared = schema.at(int(column.schema_column));
        if (declared != nullptr) {
            declared->type = column.shape.type;
        }
        reproject_lanes();
    }
}

void PropertySet::compile_against(Node *p_node) {
    if (sealed || p_node == nullptr) {
        return;
    }
    stamp_column_types(p_node->get_script(), p_node);
    seal();
}

} // namespace netw::repl

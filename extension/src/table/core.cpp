#include "netw/table/core.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::table {

using netw::NetwQuantize;
using netw::SchemaCore;

namespace {

constexpr int64_t MEMO_AGE_TICKS = 256;

constexpr int64_t REORDER_WINDOW_TICKS = 8;

const int WIRE_BITS[SchemaCore::COLUMN_TYPE_COUNT]
    = {32, 64, 8, 8, 16, 16, 32, 64, 1, 64, 96, 128, 128, 128, 40, 0};

const int MEMCPY_BYTES[SchemaCore::COLUMN_TYPE_COUNT]
    = {4, 8, 0, 0, 0, 0, 4, 8, 0, 8, 12, 16, 16, 16, 0, 0};

int element_count(const Variant &data) {
    switch (data.get_type()) {
        case Variant::PACKED_FLOAT32_ARRAY:
            return PackedFloat32Array(data).size();
        case Variant::PACKED_FLOAT64_ARRAY:
            return PackedFloat64Array(data).size();
        case Variant::PACKED_INT32_ARRAY:
            return PackedInt32Array(data).size();
        case Variant::PACKED_INT64_ARRAY:
            return PackedInt64Array(data).size();
        case Variant::PACKED_BYTE_ARRAY:
            return PackedByteArray(data).size();
        case Variant::PACKED_VECTOR2_ARRAY:
            return PackedVector2Array(data).size();
        case Variant::PACKED_VECTOR3_ARRAY:
            return PackedVector3Array(data).size();
        case Variant::PACKED_VECTOR4_ARRAY:
            return PackedVector4Array(data).size();
        case Variant::PACKED_COLOR_ARRAY:
            return PackedColorArray(data).size();
        case Variant::ARRAY:
            return Array(data).size();
        default:
            return -1;
    }
}

template <typename T> Variant uniquify(const Variant &data) {
    T copy = data;
    if (copy.size() > 0) {
        copy.ptrw();
    }
    return copy;
}

Variant duplicate_storage(const Variant &data) {
    switch (data.get_type()) {
        case Variant::PACKED_FLOAT32_ARRAY:
            return uniquify<PackedFloat32Array>(data);
        case Variant::PACKED_FLOAT64_ARRAY:
            return uniquify<PackedFloat64Array>(data);
        case Variant::PACKED_INT32_ARRAY:
            return uniquify<PackedInt32Array>(data);
        case Variant::PACKED_INT64_ARRAY:
            return uniquify<PackedInt64Array>(data);
        case Variant::PACKED_BYTE_ARRAY:
            return uniquify<PackedByteArray>(data);
        case Variant::PACKED_VECTOR2_ARRAY:
            return uniquify<PackedVector2Array>(data);
        case Variant::PACKED_VECTOR3_ARRAY:
            return uniquify<PackedVector3Array>(data);
        case Variant::PACKED_VECTOR4_ARRAY:
            return uniquify<PackedVector4Array>(data);
        case Variant::PACKED_COLOR_ARRAY:
            return uniquify<PackedColorArray>(data);
        case Variant::ARRAY:
            return Array(data).duplicate();
        default:
            return data;
    }
}

template <typename T> void resize_storage(Variant &data, int size) {
    T typed = data;
    typed.resize(size);
    data = typed;
}

void storage_resize(Variant &data, int size) {
    switch (data.get_type()) {
        case Variant::PACKED_FLOAT32_ARRAY:
            resize_storage<PackedFloat32Array>(data, size);
            return;
        case Variant::PACKED_FLOAT64_ARRAY:
            resize_storage<PackedFloat64Array>(data, size);
            return;
        case Variant::PACKED_INT32_ARRAY:
            resize_storage<PackedInt32Array>(data, size);
            return;
        case Variant::PACKED_INT64_ARRAY:
            resize_storage<PackedInt64Array>(data, size);
            return;
        case Variant::PACKED_BYTE_ARRAY:
            resize_storage<PackedByteArray>(data, size);
            return;
        case Variant::PACKED_VECTOR2_ARRAY:
            resize_storage<PackedVector2Array>(data, size);
            return;
        case Variant::PACKED_VECTOR3_ARRAY:
            resize_storage<PackedVector3Array>(data, size);
            return;
        case Variant::PACKED_VECTOR4_ARRAY:
            resize_storage<PackedVector4Array>(data, size);
            return;
        case Variant::PACKED_COLOR_ARRAY:
            resize_storage<PackedColorArray>(data, size);
            return;
        case Variant::ARRAY: {
            Array typed = data;
            typed.resize(size);
            data = typed;
            return;
        }
        default:
            return;
    }
}

int storage_size(const Variant &data) {
    const int size = element_count(data);
    return size < 0 ? 0 : size;
}

template <typename T, typename E>
void copy_typed(Variant &dst, int at, const Variant &src, int from, int count) {
    T target = dst;
    if (target.size() <= 0) {
        return;
    }
    E *write = target.ptrw();
    const T source = src;
    const E *read = source.ptr();
    for (int i = 0; i < count; i++) {
        write[at + i] = read[from + i];
    }
    dst = target;
}

void copy_elements(
    Variant &dst,
    int at,
    const Variant &src,
    int from,
    int count
) {
    if (count <= 0) {
        return;
    }
    switch (dst.get_type()) {
        case Variant::PACKED_FLOAT32_ARRAY:
            copy_typed<PackedFloat32Array, float>(dst, at, src, from, count);
            return;
        case Variant::PACKED_FLOAT64_ARRAY:
            copy_typed<PackedFloat64Array, double>(dst, at, src, from, count);
            return;
        case Variant::PACKED_INT32_ARRAY:
            copy_typed<PackedInt32Array, int32_t>(dst, at, src, from, count);
            return;
        case Variant::PACKED_INT64_ARRAY:
            copy_typed<PackedInt64Array, int64_t>(dst, at, src, from, count);
            return;
        case Variant::PACKED_BYTE_ARRAY:
            copy_typed<PackedByteArray, uint8_t>(dst, at, src, from, count);
            return;
        case Variant::PACKED_VECTOR2_ARRAY:
            copy_typed<PackedVector2Array, Vector2>(dst, at, src, from, count);
            return;
        case Variant::PACKED_VECTOR3_ARRAY:
            copy_typed<PackedVector3Array, Vector3>(dst, at, src, from, count);
            return;
        case Variant::PACKED_VECTOR4_ARRAY:
            copy_typed<PackedVector4Array, Vector4>(dst, at, src, from, count);
            return;
        case Variant::PACKED_COLOR_ARRAY:
            copy_typed<PackedColorArray, Color>(dst, at, src, from, count);
            return;
        case Variant::ARRAY: {
            Array target = dst;
            const Array source = src;
            for (int i = 0; i < count; i++) {
                target[at + i] = source[from + i];
            }
            dst = target;
            return;
        }
        default:
            return;
    }
}

Variant element_value(const Variant &data, int at, int type) {
    if (type == SchemaCore::QUATERNION) {
        const PackedVector4Array typed = data;
        const Vector4 v = typed[at];
        return Quaternion(v.x, v.y, v.z, v.w);
    }
    switch (data.get_type()) {
        case Variant::PACKED_FLOAT32_ARRAY:
            return PackedFloat32Array(data)[at];
        case Variant::PACKED_FLOAT64_ARRAY:
            return PackedFloat64Array(data)[at];
        case Variant::PACKED_INT32_ARRAY:
            return PackedInt32Array(data)[at];
        case Variant::PACKED_INT64_ARRAY:
            return PackedInt64Array(data)[at];
        case Variant::PACKED_BYTE_ARRAY:
            return PackedByteArray(data)[at];
        case Variant::PACKED_VECTOR2_ARRAY:
            return PackedVector2Array(data)[at];
        case Variant::PACKED_VECTOR3_ARRAY:
            return PackedVector3Array(data)[at];
        case Variant::PACKED_VECTOR4_ARRAY:
            return PackedVector4Array(data)[at];
        case Variant::PACKED_COLOR_ARRAY:
            return PackedColorArray(data)[at];
        case Variant::ARRAY:
            return Array(data)[at];
        default:
            return Variant();
    }
}

Variant storage_value(const Variant &value, int type) {
    if (type == SchemaCore::QUATERNION) {
        const Quaternion q = value;
        return Vector4(q.x, q.y, q.z, q.w);
    }
    return value;
}

void store_element(Variant &data, int at, const Variant &value) {
    switch (data.get_type()) {
        case Variant::PACKED_FLOAT32_ARRAY: {
            PackedFloat32Array t = data;
            t.ptrw()[at] = static_cast<float>(static_cast<double>(value));
            data = t;
            return;
        }
        case Variant::PACKED_FLOAT64_ARRAY: {
            PackedFloat64Array t = data;
            t.ptrw()[at] = static_cast<double>(value);
            data = t;
            return;
        }
        case Variant::PACKED_INT32_ARRAY: {
            PackedInt32Array t = data;
            t.ptrw()[at] = static_cast<int32_t>(static_cast<int64_t>(value));
            data = t;
            return;
        }
        case Variant::PACKED_INT64_ARRAY: {
            PackedInt64Array t = data;
            t.ptrw()[at] = static_cast<int64_t>(value);
            data = t;
            return;
        }
        case Variant::PACKED_BYTE_ARRAY: {
            PackedByteArray t = data;
            t.ptrw()[at] = static_cast<uint8_t>(static_cast<int64_t>(value));
            data = t;
            return;
        }
        case Variant::PACKED_VECTOR2_ARRAY: {
            PackedVector2Array t = data;
            t.ptrw()[at] = value;
            data = t;
            return;
        }
        case Variant::PACKED_VECTOR3_ARRAY: {
            PackedVector3Array t = data;
            t.ptrw()[at] = value;
            data = t;
            return;
        }
        case Variant::PACKED_VECTOR4_ARRAY: {
            PackedVector4Array t = data;
            t.ptrw()[at] = value;
            data = t;
            return;
        }
        case Variant::PACKED_COLOR_ARRAY: {
            PackedColorArray t = data;
            t.ptrw()[at] = value;
            data = t;
            return;
        }
        case Variant::ARRAY: {
            Array t = data;
            t[at] = value;
            data = t;
            return;
        }
        default:
            return;
    }
}

int64_t narrow(int64_t value, int bits, bool is_signed) {
    if (!is_signed) {
        return value;
    }
    const int64_t sign_bit = int64_t(1) << (bits - 1);
    return (value & sign_bit) ? value - (int64_t(1) << bits) : value;
}

static_assert(sizeof(Vector2) == 8, "the wire packs Vector2 as two float32");
static_assert(sizeof(Vector3) == 12, "the wire packs Vector3 as three float32");
static_assert(sizeof(Vector4) == 16, "the wire packs Vector4 as four float32");
static_assert(sizeof(Color) == 16, "the wire packs Color as four float32");

template <typename T, typename E>
Variant bytes_into(const PackedByteArray &bytes, int count) {
    T out;
    out.resize(count);
    if (count > 0) {
        memcpy(out.ptrw(), bytes.ptr(), size_t(count) * sizeof(E));
    }
    return out;
}

Variant from_bytes(const PackedByteArray &bytes, int type, int count) {
    switch (type) {
        case SchemaCore::F32:
            return bytes_into<PackedFloat32Array, float>(bytes, count);
        case SchemaCore::F64:
            return bytes_into<PackedFloat64Array, double>(bytes, count);
        case SchemaCore::I32:
            return bytes_into<PackedInt32Array, int32_t>(bytes, count);
        case SchemaCore::I64:
            return bytes_into<PackedInt64Array, int64_t>(bytes, count);
        case SchemaCore::VECTOR2:
            return bytes_into<PackedVector2Array, Vector2>(bytes, count);
        case SchemaCore::VECTOR3:
            return bytes_into<PackedVector3Array, Vector3>(bytes, count);
        case SchemaCore::COLOR:
            return bytes_into<PackedColorArray, Color>(bytes, count);
        default:
            return bytes_into<PackedVector4Array, Vector4>(bytes, count);
    }
}

PackedByteArray slice_bytes(
    const Variant &data,
    int type,
    int start,
    int count
) {
    const int width = MEMCPY_BYTES[type];
    PackedByteArray out;
    out.resize(count * width);
    uint8_t *write = out.ptrw();
    switch (type) {
        case SchemaCore::F32: {
            const PackedFloat32Array typed = data;
            memcpy(write, typed.ptr() + start, size_t(count) * width);
            break;
        }
        case SchemaCore::F64: {
            const PackedFloat64Array typed = data;
            memcpy(write, typed.ptr() + start, size_t(count) * width);
            break;
        }
        case SchemaCore::I32: {
            const PackedInt32Array typed = data;
            memcpy(write, typed.ptr() + start, size_t(count) * width);
            break;
        }
        case SchemaCore::I64: {
            const PackedInt64Array typed = data;
            memcpy(write, typed.ptr() + start, size_t(count) * width);
            break;
        }
        case SchemaCore::VECTOR2: {
            const PackedVector2Array typed = data;
            memcpy(write, typed.ptr() + start, size_t(count) * width);
            break;
        }
        case SchemaCore::VECTOR3: {
            const PackedVector3Array typed = data;
            memcpy(write, typed.ptr() + start, size_t(count) * width);
            break;
        }
        case SchemaCore::VECTOR4:
        case SchemaCore::QUATERNION: {
            const PackedVector4Array typed = data;
            memcpy(write, typed.ptr() + start, size_t(count) * width);
            break;
        }
        case SchemaCore::COLOR: {
            const PackedColorArray typed = data;
            memcpy(write, typed.ptr() + start, size_t(count) * width);
            break;
        }
        default:
            break;
    }
    return out;
}

} // namespace

int Core::wire_bits(int type) {
    return (type >= 0 && type < SchemaCore::COLUMN_TYPE_COUNT) ? WIRE_BITS[type]
                                                               : 0;
}

Core::Record *Core::record_of(const RID &table) {
    return tables.getptr(table);
}

const Core::Record *Core::record_of(const RID &table) const {
    return tables.getptr(table);
}

const Core::ColumnShape *Core::shape_at(const RID &table, int column) const {
    const Record *record = record_of(table);
    if (record == nullptr || column < 0
        || column >= static_cast<int>(record->shapes.size())) {
        return nullptr;
    }
    return &record->shapes[column];
}

Error Core::declare(const RID &table, const SchemaRecord *schema) {
    if (schema == nullptr || !schema->sealed) {
        NETW_DEBUG(
            sys::TABLE,
            "A table is declared from a SEALED schema, and this one is %s.",
            schema == nullptr ? "null" : "still open"
        );
        return ERR_UNCONFIGURED;
    }
    for (int i = 0; i < schema->column_count(); i++) {
        const SchemaColumn *column = schema->at(i);
        if (column->type == SchemaCore::VARIANT) {
            NETW_DEBUG(
                sys::TABLE,
                "Column '%s' is a Variant, which no column layout can size.",
                String(column->key).utf8().get_data()
            );
            return ERR_INVALID_DATA;
        }
    }
    Record *existing = record_of(table);
    if (existing != nullptr) {
        if (existing->schema.name != schema->name
            || existing->schema.shape_hash != schema->shape_hash) {
            NETW_DEBUG(
                sys::TABLE,
                "Table '%s' is already declared under another schema.",
                String(existing->schema.name).utf8().get_data()
            );
            return ERR_ALREADY_EXISTS;
        }
        return OK;
    }

    Record record;
    record.schema = *schema;
    const int count = schema->column_count();
    record.shapes.resize(count);
    record.pending_columns.resize(count);
    record.pending_written.resize(count);
    for (int i = 0; i < count; i++) {
        const SchemaColumn *column = schema->at(i);
        ColumnShape &shape = record.shapes[i];
        shape.key = column->key;
        shape.type = column->type;
        shape.stride = column->stride;
        shape.quantizer = column->quantizer;
        shape.element_bits = shape.quantizer.is_valid()
            ? MAX(1,
                  shape.quantizer->bit_width(
                      static_cast<Variant::Type>(
                          SchemaCore::element_type(shape.type)
                      )
                  ))
            : wire_bits(shape.type);
        record.pending_written[i] = false;
    }
    tables.insert(table, record);
    clear_rows(*record_of(table));
    rebuild_wire_order();
    return OK;
}

void Core::set_reliable(const RID &table, bool value) {
    Record *record = record_of(table);
    if (record != nullptr) {
        record->reliable = value;
    }
}

bool Core::is_valid(const RID &table) const {
    return tables.has(table);
}

StringName Core::name_of(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->schema.name : StringName();
}

int Core::schema_hash(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->schema.shape_hash : 0;
}

int Core::column_count(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? static_cast<int>(record->shapes.size()) : 0;
}

StringName Core::column_key(const RID &table, int column) const {
    const ColumnShape *shape = shape_at(table, column);
    return shape != nullptr ? shape->key : StringName();
}

int Core::column_type(const RID &table, int column) const {
    const ColumnShape *shape = shape_at(table, column);
    return shape != nullptr ? shape->type : -1;
}

int Core::column_stride(const RID &table, int column) const {
    const ColumnShape *shape = shape_at(table, column);
    return shape != nullptr ? shape->stride : 0;
}

Ref<NetwQuantize> Core::column_quantizer(const RID &table, int column) const {
    const ColumnShape *shape = shape_at(table, column);
    return shape != nullptr ? shape->quantizer : Ref<NetwQuantize>();
}

bool Core::is_reliable(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr && record->reliable;
}

int Core::wire_id(const RID &table) const {
    const HashMap<RID, int>::ConstIterator found = wire_id_of.find(table);
    return found != wire_id_of.end() ? found->value : 0;
}

RID Core::table_from_wire_id(int id) const {
    if (id < 1 || id > static_cast<int>(wire_order.size())) {
        return RID();
    }
    return wire_order[id - 1];
}

void Core::rebuild_wire_order() {
    LocalVector<String> names;
    HashMap<String, RID> by_name;
    for (const KeyValue<RID, Record> &entry : tables) {
        if (entry.value.schema.sealed) {
            const String name = String(entry.value.schema.name);
            names.push_back(name);
            by_name[name] = entry.key;
        }
    }
    names.sort();
    wire_order.clear();
    wire_id_of.clear();
    for (uint32_t i = 0; i < names.size(); i++) {
        const RID table = by_name[names[i]];
        wire_order.push_back(table);
        wire_id_of[table] = static_cast<int>(i) + 1;
    }
}

Error Core::write_routes(const RID &table, const PackedInt64Array &routes) {
    Record *record = record_of(table);
    if (record == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!record->schema.sealed) {
        return ERR_UNCONFIGURED;
    }
    record->pending_routes = routes;
    record->pending_routes_written = true;
    return OK;
}

Error Core::write_column(const RID &table, int column, const Variant &data) {
    Record *record = record_of(table);
    if (record == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!record->schema.sealed) {
        return ERR_UNCONFIGURED;
    }
    if (column < 0 || column >= static_cast<int>(record->shapes.size())) {
        NETW_DEBUG(
            sys::TABLE,
            "Column %d is outside table '%s', which declares %d.",
            column,
            String(record->schema.name).utf8().get_data(),
            int(record->shapes.size())
        );
        return ERR_INVALID_DATA;
    }
    if (static_cast<int>(data.get_type())
        != SchemaCore::storage_type(record->shapes[column].type)) {
        NETW_DEBUG(
            sys::TABLE,
            "Column '%s' is declared as storage type %d and was written a %d.",
            String(record->shapes[column].key).utf8().get_data(),
            SchemaCore::storage_type(record->shapes[column].type),
            int(data.get_type())
        );
        return ERR_INVALID_DATA;
    }
    record->pending_columns[column] = data;
    record->pending_written[column] = true;
    return OK;
}

Error Core::commit(const RID &table, int64_t tick) {
    NETW_ZONE_NC("table::Core commit", colors::TABLE);
    Record *record = record_of(table);
    if (record == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!record->schema.sealed) {
        NETW_DEBUG(
            sys::TABLE,
            "Table '%s' commits from a schema that is still open.",
            String(record->schema.name).utf8().get_data()
        );
        return ERR_INVALID_DATA;
    }
    if (!record->pending_routes_written) {
        NETW_DEBUG(
            sys::TABLE,
            "Table '%s' commits with no route column written this wave.",
            String(record->schema.name).utf8().get_data()
        );
        return ERR_INVALID_DATA;
    }
    const int rows = record->pending_routes.size();
    const int count = static_cast<int>(record->shapes.size());
    for (int i = 0; i < count; i++) {
        if (!record->pending_written[i]) {
            NETW_DEBUG(
                sys::TABLE,
                "Column '%s' was not written this wave, and a wave writes "
                "every column or none.",
                String(record->shapes[i].key).utf8().get_data()
            );
            return ERR_INVALID_DATA;
        }
        if (element_count(record->pending_columns[i])
            != rows * record->shapes[i].stride) {
            NETW_DEBUG(
                sys::TABLE,
                "Column '%s' carries %d elements for %d rows of stride %d.",
                String(record->shapes[i].key).utf8().get_data(),
                int(element_count(record->pending_columns[i])),
                rows,
                record->shapes[i].stride
            );
            return ERR_INVALID_DATA;
        }
    }

    PackedInt64Array applied = record->pending_routes;
    if (applied.size() > 0) {
        applied.ptrw();
    }
    LocalVector<Variant> buffers;
    buffers.resize(count);
    for (int i = 0; i < count; i++) {
        buffers[i] = duplicate_storage(record->pending_columns[i]);
    }
    apply_wave(*record, applied, buffers, tick);
    record->dirty = true;
    return OK;
}

void Core::clear_dirty(const RID &table) {
    Record *record = record_of(table);
    if (record != nullptr) {
        record->dirty = false;
    }
}

TypedArray<RID> Core::dirty_tables() const {
    TypedArray<RID> out;
    for (uint32_t i = 0; i < wire_order.size(); i++) {
        const Record *record = record_of(wire_order[i]);
        if (record != nullptr && record->dirty) {
            out.push_back(wire_order[i]);
        }
    }
    return out;
}

TypedArray<RID> Core::published_tables() const {
    TypedArray<RID> out;
    for (uint32_t i = 0; i < wire_order.size(); i++) {
        const Record *record = record_of(wire_order[i]);
        if (record != nullptr && record->tick >= 0) {
            out.push_back(wire_order[i]);
        }
    }
    return out;
}

PackedInt64Array Core::take_pending_removals(const RID &table) {
    PackedInt64Array out;
    Record *record = record_of(table);
    if (record == nullptr) {
        return out;
    }
    for (const int64_t &route : record->published) {
        if (!record->row_of.has(route)) {
            out.push_back(route);
        }
    }
    record->published.clear();
    for (const KeyValue<int64_t, int> &entry : record->row_of) {
        record->published.insert(entry.key);
    }
    return out;
}

PackedInt64Array Core::read_routes(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->routes : PackedInt64Array();
}

Variant Core::read_column(const RID &table, int column) const {
    const Record *record = record_of(table);
    if (record == nullptr || column < 0
        || column >= static_cast<int>(record->columns_data.size())) {
        return Variant();
    }
    return record->columns_data[column];
}

PackedInt64Array Core::read_births(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->births : PackedInt64Array();
}

PackedInt64Array Core::read_deaths(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->deaths : PackedInt64Array();
}

int Core::row_of(const RID &table, int64_t route) const {
    const Record *record = record_of(table);
    if (record == nullptr) {
        return -1;
    }
    const HashMap<int64_t, int>::ConstIterator found
        = record->row_of.find(route);
    return found != record->row_of.end() ? found->value : -1;
}

PackedInt32Array Core::rows_of(
    const RID &table,
    const PackedInt64Array &routes
) const {
    PackedInt32Array out;
    out.resize(routes.size());
    int32_t *write = out.ptrw();
    const Record *record = record_of(table);
    for (int i = 0; i < routes.size(); i++) {
        if (record == nullptr) {
            write[i] = -1;
            continue;
        }
        const HashMap<int64_t, int>::ConstIterator found
            = record->row_of.find(routes[i]);
        write[i] = found != record->row_of.end() ? found->value : -1;
    }
    return out;
}

int64_t Core::tick_of(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->tick : -1;
}

int Core::rows_per_frame(const Record &record, int budget) const {
    int64_t bits = 40;
    for (uint32_t i = 0; i < record.shapes.size(); i++) {
        const ColumnShape &shape = record.shapes[i];
        bits += int64_t(shape.stride) * shape.element_bits + 7;
    }
    const int64_t room
        = MAX(int64_t(1), int64_t(budget) - FRAME_OVERHEAD_BYTES);
    return static_cast<int>(
        MAX(int64_t(1), (room * 8) / MAX(bits, int64_t(1)))
    );
}

bool Core::encode_column(
    wire::WriteStream &stream,
    const ColumnShape &shape,
    const Variant &data,
    int first,
    int rows
) const {
    if (!stream.align_verify()) {
        return false;
    }
    const int start = first * shape.stride;
    const int count = rows * shape.stride;
    if (count <= 0) {
        return true;
    }
    if (shape.quantizer.is_valid()) {
        for (int i = 0; i < count; i++) {
            const Variant value = element_value(data, start + i, shape.type);
            const Variant::Type held = value.get_type();
            const int width = shape.quantizer->bit_width(held);
            const int parts = shape.quantizer->stride(held);
            for (int part = 0; part < parts; part++) {
                uint64_t code = uint64_t(shape.quantizer->encode(value, part));
                if (!stream.bits(code, width)) {
                    return false;
                }
            }
        }
        return true;
    }
    switch (shape.type) {
        case SchemaCore::ENTITY: {
            const PackedInt64Array typed = data;
            const int64_t *read = typed.ptr();
            for (int i = 0; i < count; i++) {
                uint64_t route = uint64_t(MAX(read[start + i], int64_t(0)));
                if (!stream.varuint(route, 5)) {
                    return false;
                }
            }
            return true;
        }
        case SchemaCore::BOOL: {
            const PackedByteArray typed = data;
            const uint8_t *read = typed.ptr();
            for (int i = 0; i < count; i++) {
                bool set = read[start + i] != 0;
                if (!stream.bool1(set)) {
                    return false;
                }
            }
            return true;
        }
        case SchemaCore::I8:
        case SchemaCore::U8:
        case SchemaCore::I16:
        case SchemaCore::U16: {
            const int width
                = (shape.type == SchemaCore::I8 || shape.type == SchemaCore::U8)
                ? 8
                : 16;
            const PackedInt32Array typed = data;
            const int32_t *read = typed.ptr();
            const uint64_t mask = (uint64_t(1) << width) - 1;
            for (int i = 0; i < count; i++) {
                uint64_t packed = uint64_t(int64_t(read[start + i])) & mask;
                if (!stream.bits(packed, width)) {
                    return false;
                }
            }
            return true;
        }
        default: {
            PackedByteArray bytes = slice_bytes(data, shape.type, start, count);
            return stream.raw_bytes(bytes, bytes.size());
        }
    }
}

PackedByteArray Core::encode_frame(
    const Record &record,
    int id,
    int flags,
    const PackedInt64Array &routes,
    int first,
    int rows
) const {
    wire::WriteStream stream;
    FrameHead head;
    head.table_id = uint64_t(id);
    head.schema_hash = uint64_t(uint16_t(record.schema.shape_hash));
    head.tick = uint64_t(MAX(record.tick, int64_t(0)));
    head.flags = uint64_t(uint8_t(flags));
    head.rows = uint64_t(rows);
    if (!FrameHead::wire.run(stream, head)) {
        return PackedByteArray();
    }
    for (int i = 0; i < routes.size(); i++) {
        uint64_t route = uint64_t(MAX(routes[i], int64_t(0)));
        if (!stream.varuint(route, 5)) {
            return PackedByteArray();
        }
    }
    for (uint32_t i = 0; i < record.shapes.size(); i++) {
        if (!encode_column(
                stream,
                record.shapes[i],
                record.columns_data[i],
                first,
                rows
            )) {
            return PackedByteArray();
        }
    }
    if (!stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

TypedArray<PackedByteArray> Core::encode_frames(
    const RID &table,
    int budget,
    bool snapshot
) {
    NETW_ZONE_NC("table::Core encode frames", colors::TABLE);
    TypedArray<PackedByteArray> out;
    const Record *record = record_of(table);
    if (record == nullptr || !record->schema.sealed) {
        return out;
    }
    const int id = wire_id(table);
    if (id == LIFECYCLE_STREAM) {
        return out;
    }

    const int total = record->routes.size();
    if (total == 0) {
        if (snapshot) {
            out.push_back(encode_frame(
                *record,
                id,
                FLAG_SNAPSHOT,
                PackedInt64Array(),
                0,
                0
            ));
        }
        return out;
    }

    const int per_frame = rows_per_frame(*record, budget);
    int at = 0;
    while (at < total) {
        const int rows = MIN(per_frame, total - at);
        const int flags = (snapshot && at == 0) ? FLAG_SNAPSHOT : 0;
        out.push_back(encode_frame(
            *record,
            id,
            flags,
            record->routes.slice(at, at + rows),
            at,
            rows
        ));
        at += rows;
    }
    return out;
}

TypedArray<PackedByteArray> Core::encode_routes_only(
    int id,
    int hash,
    const PackedInt64Array &routes,
    int64_t tick,
    int budget
) {
    TypedArray<PackedByteArray> out;
    if (routes.is_empty()) {
        return out;
    }
    const int64_t room
        = MAX(int64_t(1), int64_t(budget) - FRAME_OVERHEAD_BYTES);
    const int per_frame = static_cast<int>(MAX(int64_t(1), room / 5));
    int at = 0;
    while (at < routes.size()) {
        const int count = MIN(per_frame, routes.size() - at);
        wire::WriteStream stream;
        FrameHead head;
        head.table_id = uint64_t(id);
        head.schema_hash = uint64_t(uint16_t(hash));
        head.tick = uint64_t(MAX(tick, int64_t(0)));
        head.flags = uint64_t(FLAG_REMOVE);
        head.rows = uint64_t(count);
        if (!FrameHead::wire.run(stream, head)) {
            return TypedArray<PackedByteArray>();
        }
        for (int i = 0; i < count; i++) {
            uint64_t route = uint64_t(MAX(routes[at + i], int64_t(0)));
            if (!stream.varuint(route, 5)) {
                return TypedArray<PackedByteArray>();
            }
        }
        if (!stream.align_verify()) {
            return TypedArray<PackedByteArray>();
        }
        out.push_back(stream.to_bytes());
        at += count;
    }
    return out;
}

TypedArray<PackedByteArray> Core::encode_removal(
    const RID &table,
    const PackedInt64Array &routes,
    int64_t tick,
    int budget
) {
    const Record *record = record_of(table);
    if (record == nullptr || !record->schema.sealed) {
        return TypedArray<PackedByteArray>();
    }
    return encode_routes_only(
        wire_id(table),
        record->schema.shape_hash,
        routes,
        tick,
        budget
    );
}

TypedArray<PackedByteArray> Core::encode_lifecycle(
    const PackedInt64Array &routes,
    int64_t tick,
    int budget
) {
    return encode_routes_only(LIFECYCLE_STREAM, 0, routes, tick, budget);
}

Dictionary Core::peek_header(const PackedByteArray &payload) {
    Dictionary out;
    wire::ReadStream stream(payload);
    FrameHead head;
    if (!FrameHead::wire.run(stream, head)) {
        return out;
    }
    out["table_id"] = int64_t(head.table_id);
    out["schema_hash"] = int64_t(head.schema_hash);
    out["tick"] = int64_t(head.tick);
    out["flags"] = int64_t(head.flags);
    out["rows"] = int64_t(head.rows);
    return out;
}

Dictionary Core::frame_spec_records() {
    Dictionary out;
    out["TableFrameHead"] = FrameHead::wire.spec_dump();
    return out;
}

Error Core::admit_header(const Dictionary &header) {
    if (header.is_empty()) {
        drops_truncated += 1;
        return ERR_INVALID_DATA;
    }
    const int flags = static_cast<int>(int64_t(header["flags"]));
    if (flags & ~int(FLAGS_IMPLEMENTED)) {
        drops_unknown_flag += 1;
        return ERR_INVALID_DATA;
    }
    const int id = static_cast<int>(int64_t(header["table_id"]));
    if (id == LIFECYCLE_STREAM) {
        if (!(flags & FLAG_REMOVE)) {
            drops_unknown_flag += 1;
            return ERR_INVALID_DATA;
        }
        return OK;
    }
    const RID table = table_from_wire_id(id);
    if (!table.is_valid()) {
        drops_unknown += 1;
        return ERR_DOES_NOT_EXIST;
    }
    if (int64_t(header["schema_hash"]) != schema_hash(table)) {
        drops_schema += 1;
        return ERR_INVALID_DATA;
    }
    return OK;
}

Variant Core::decode_column(
    wire::ReadStream &stream,
    const ColumnShape &shape,
    int rows
) const {
    if (!stream.align_verify()) {
        return Variant();
    }
    const int count = rows * shape.stride;
    Variant out = SchemaCore::make_storage(shape.type);
    if (count <= 0) {
        return out;
    }
    if (shape.quantizer.is_valid()) {
        const Variant::Type element
            = static_cast<Variant::Type>(SchemaCore::element_type(shape.type));
        const int width = shape.quantizer->bit_width(element);
        const int parts = shape.quantizer->stride(element);
        storage_resize(out, count);
        PackedInt64Array codes;
        codes.resize(parts);
        for (int i = 0; i < count; i++) {
            for (int part = 0; part < parts; part++) {
                uint64_t code = 0;
                if (!stream.bits(code, width)) {
                    return Variant();
                }
                codes.set(part, int64_t(code));
            }
            store_element(
                out,
                i,
                storage_value(
                    shape.quantizer->decode(codes, element),
                    shape.type
                )
            );
        }
        return out;
    }
    switch (shape.type) {
        case SchemaCore::ENTITY: {
            PackedInt64Array typed;
            typed.resize(count);
            int64_t *write = typed.ptrw();
            for (int i = 0; i < count; i++) {
                uint64_t route = 0;
                if (!stream.varuint(route, 5)) {
                    return Variant();
                }
                write[i] = int64_t(route);
            }
            return typed;
        }
        case SchemaCore::BOOL: {
            PackedByteArray typed;
            typed.resize(count);
            uint8_t *write = typed.ptrw();
            for (int i = 0; i < count; i++) {
                bool set = false;
                if (!stream.bool1(set)) {
                    return Variant();
                }
                write[i] = set ? 1 : 0;
            }
            return typed;
        }
        case SchemaCore::I8:
        case SchemaCore::U8:
        case SchemaCore::I16:
        case SchemaCore::U16: {
            const bool narrow_byte
                = shape.type == SchemaCore::I8 || shape.type == SchemaCore::U8;
            const int width = narrow_byte ? 8 : 16;
            const bool is_signed
                = shape.type == SchemaCore::I8 || shape.type == SchemaCore::I16;
            PackedInt32Array typed;
            typed.resize(count);
            int32_t *write = typed.ptrw();
            for (int i = 0; i < count; i++) {
                uint64_t packed = 0;
                if (!stream.bits(packed, width)) {
                    return Variant();
                }
                write[i] = static_cast<int32_t>(
                    narrow(int64_t(packed), width, is_signed)
                );
            }
            return typed;
        }
        default:
            break;
    }
    const int width = MEMCPY_BYTES[shape.type];
    PackedByteArray bytes;
    if (!stream.raw_bytes(bytes, int64_t(count) * width)) {
        return Variant();
    }
    return from_bytes(bytes, shape.type, count);
}

bool Core::read_frame_routes(
    wire::ReadStream &stream,
    int rows,
    PackedInt64Array &r_routes
) {
    r_routes = PackedInt64Array();
    if (rows <= 0) {
        return true;
    }
    r_routes.resize(rows);
    int64_t *write = r_routes.ptrw();
    for (int i = 0; i < rows; i++) {
        uint64_t route = 0;
        if (!stream.varuint(route, 5) || route == 0) {
            r_routes = PackedInt64Array();
            return false;
        }
        write[i] = int64_t(route);
    }
    return true;
}

Dictionary Core::apply_frame(const PackedByteArray &payload) {
    NETW_ZONE_NC("table::Core apply frame", colors::TABLE);
    NETW_ZONE_VALUE(payload.size());
    Dictionary result;
    result["verdict"] = ERR_INVALID_DATA;
    result["table"] = RID();
    result["tick"] = -1;
    result["bound"] = PackedInt64Array();
    result["retired"] = PackedInt64Array();

    wire::ReadStream stream(payload);
    FrameHead head;
    if (!FrameHead::wire.run(stream, head)) {
        drops_truncated += 1;
        return result;
    }

    const int id = static_cast<int>(head.table_id);
    const int64_t tick = int64_t(head.tick);
    const int flags = static_cast<int>(head.flags);
    const int rows = static_cast<int>(head.rows);
    result["tick"] = tick;

    PackedInt64Array routes;
    if (!read_frame_routes(stream, rows, routes)) {
        drops_truncated += 1;
        return result;
    }

    if (id == LIFECYCLE_STREAM) {
        retire_routes(routes, true);
        result["retired"] = routes;
        result["verdict"] = OK;
        return result;
    }

    const RID table = table_from_wire_id(id);
    Record *record = record_of(table);
    if (record == nullptr) {
        drops_unknown += 1;
        result["verdict"] = ERR_DOES_NOT_EXIST;
        return result;
    }
    result["table"] = table;

    if (record->tick >= 0 && tick < record->tick - REORDER_WINDOW_TICKS) {
        drops_stale += 1;
        result["verdict"] = ERR_SKIP;
        return result;
    }

    if (flags & FLAG_REMOVE) {
        open_wave(*record);
        apply_removal(*record, routes, tick);
        record->tick = MAX(record->tick, tick);
        result["verdict"] = OK;
        return result;
    }

    LocalVector<Variant> values;
    values.resize(record->shapes.size());
    for (uint32_t i = 0; i < record->shapes.size(); i++) {
        Variant decoded = decode_column(stream, record->shapes[i], rows);
        if (decoded.get_type() == Variant::NIL) {
            drops_truncated += 1;
            return result;
        }
        values[i] = decoded;
    }
    if (!stream.align_verify() || stream.bits_remaining() != 0) {
        drops_truncated += 1;
        return result;
    }

    open_wave(*record);
    if (flags & FLAG_SNAPSHOT) {
        clear_rows(*record);
    }
    result["bound"] = apply_upsert(*record, routes, values, tick);
    record->tick = MAX(record->tick, tick);
    prune_memos(*record, tick);
    result["verdict"] = OK;
    return result;
}

void Core::begin_intake() {
    for (KeyValue<RID, Record> &entry : tables) {
        entry.value.wave_touched = false;
    }
}

TypedArray<RID> Core::touched_tables() const {
    TypedArray<RID> out;
    for (uint32_t i = 0; i < wire_order.size(); i++) {
        const Record *record = record_of(wire_order[i]);
        if (record != nullptr && record->wave_touched) {
            out.push_back(wire_order[i]);
        }
    }
    return out;
}

void Core::count_bad_sender() {
    drops_bad_sender += 1;
}

void Core::retire_routes(const PackedInt64Array &routes, bool as_wave) {
    for (int i = 0; i < routes.size(); i++) {
        tombstones.insert(routes[i]);
    }
    for (KeyValue<RID, Record> &entry : tables) {
        Record &record = entry.value;
        PackedInt64Array present;
        for (int i = 0; i < routes.size(); i++) {
            if (record.row_of.has(routes[i])) {
                present.push_back(routes[i]);
            }
        }
        if (present.is_empty()) {
            continue;
        }
        if (as_wave) {
            open_wave(record);
        }
        apply_removal(record, present, MAX(record.tick, int64_t(0)));
        for (int i = 0; i < present.size(); i++) {
            record.published.erase(present[i]);
        }
    }
}

void Core::queue_lifecycle_removals(const PackedInt64Array &routes) {
    pending_lifecycle.append_array(routes);
}

PackedInt64Array Core::take_lifecycle_removals() {
    const PackedInt64Array out = pending_lifecycle;
    pending_lifecycle = PackedInt64Array();
    return out;
}

PackedInt64Array Core::lifecycle_removals() const {
    return pending_lifecycle;
}

bool Core::is_tombstoned(int64_t route) const {
    return tombstones.has(route);
}

void Core::clear_session() {
    pending_lifecycle = PackedInt64Array();
    tombstones.clear();
    for (KeyValue<RID, Record> &entry : tables) {
        Record &record = entry.value;
        record.wave_touched = false;
        clear_rows(record);
        record.births = PackedInt64Array();
        record.deaths = PackedInt64Array();
        record.tick = -1;
        record.dirty = false;
        record.published.clear();
        record.pending_routes = PackedInt64Array();
        record.pending_routes_written = false;
        record.pending_columns.clear();
        record.pending_columns.resize(record.shapes.size());
        record.pending_written.clear();
        record.pending_written.resize(record.shapes.size());
        for (uint32_t i = 0; i < record.pending_written.size(); i++) {
            record.pending_written[i] = false;
        }
    }
}

Dictionary Core::counters() const {
    Dictionary out;
    out["drops_table_unknown"] = drops_unknown;
    out["drops_table_schema"] = drops_schema;
    out["drops_table_unknown_flag"] = drops_unknown_flag;
    out["drops_table_truncated"] = drops_truncated;
    out["drops_table_bad_sender"] = drops_bad_sender;
    out["table_drops_stale"] = drops_stale;
    out["table_drops_tombstone"] = drops_tombstone;
    out["table_drops_stale_row"] = drops_stale_row;
    return out;
}

void Core::clear_rows(Record &record) {
    record.routes = PackedInt64Array();
    record.row_of.clear();
    record.row_ticks.clear();
    record.removal_memos.clear();
    record.columns_data.clear();
    record.columns_data.resize(record.shapes.size());
    for (uint32_t i = 0; i < record.shapes.size(); i++) {
        record.columns_data[i]
            = SchemaCore::make_storage(record.shapes[i].type);
    }
}

void Core::apply_wave(
    Record &record,
    const PackedInt64Array &routes,
    LocalVector<Variant> &buffers,
    int64_t tick
) {
    HashMap<int64_t, int> next;
    PackedInt64Array births;
    for (int i = 0; i < routes.size(); i++) {
        const int64_t route = routes[i];
        next[route] = i;
        if (!record.row_of.has(route)) {
            births.push_back(route);
        }
    }
    PackedInt64Array deaths;
    for (const KeyValue<int64_t, int> &entry : record.row_of) {
        if (!next.has(entry.key)) {
            deaths.push_back(entry.key);
        }
    }

    record.routes = routes;
    record.columns_data = buffers;
    record.row_of = next;
    record.births = births;
    record.deaths = deaths;
    record.tick = tick;
    record.row_ticks.clear();
    record.row_ticks.resize(routes.size());
    for (int i = 0; i < routes.size(); i++) {
        record.row_ticks[i] = tick;
    }
}

void Core::open_wave(Record &record) {
    if (record.wave_touched) {
        return;
    }
    record.wave_touched = true;
    record.births = PackedInt64Array();
    record.deaths = PackedInt64Array();
}

void Core::apply_removal(
    Record &record,
    const PackedInt64Array &routes,
    int64_t tick
) {
    for (int i = 0; i < routes.size(); i++) {
        const int64_t route = routes[i];
        record.removal_memos[route] = tick;
        const HashMap<int64_t, int>::ConstIterator found
            = record.row_of.find(route);
        if (found == record.row_of.end()) {
            continue;
        }
        swap_remove_row(record, route, found->value);
        record.deaths.push_back(route);
    }
}

void Core::swap_remove_row(Record &record, int64_t route, int row) {
    NETW_ASSERT(
        row >= 0 && row < record.routes.size(),
        sys::TABLE,
        "Swap-remove row is outside the route store."
    );
    NETW_ASSERT(
        record.row_ticks.size() == record.routes.size(),
        sys::TABLE,
        "Route and row-tick stores have different lengths."
    );
    NETW_ASSERT(
        record.columns_data.size() == record.shapes.size(),
        sys::TABLE,
        "Column stores and shapes have different lengths."
    );
    const int last = record.routes.size() - 1;
    const int64_t moved = record.routes[last];
    record.routes.set(row, moved);
    record.routes.resize(last);
    record.row_ticks[row] = record.row_ticks[last];
    record.row_ticks.resize(last);
    record.row_of.erase(route);
    if (moved != route) {
        record.row_of[moved] = row;
    }
    for (uint32_t c = 0; c < record.shapes.size(); c++) {
        const int stride = record.shapes[c].stride;
        Variant &storage = record.columns_data[c];
        NETW_ASSERT(
            storage_size(storage) == (last + 1) * stride,
            sys::TABLE,
            "Column storage does not match the route store."
        );
        copy_elements(storage, row * stride, storage, last * stride, stride);
        storage_resize(storage, last * stride);
    }
}

PackedInt64Array Core::apply_upsert(
    Record &record,
    const PackedInt64Array &routes,
    const LocalVector<Variant> &values,
    int64_t tick
) {
    PackedInt64Array bound;
    for (int i = 0; i < routes.size(); i++) {
        const int64_t route = routes[i];
        if (tombstones.has(route)) {
            drops_tombstone += 1;
            continue;
        }
        const HashMap<int64_t, int64_t>::ConstIterator memo
            = record.removal_memos.find(route);
        if (memo != record.removal_memos.end() && memo->value > tick) {
            drops_stale_row += 1;
            continue;
        }
        const HashMap<int64_t, int>::ConstIterator found
            = record.row_of.find(route);
        int row = found != record.row_of.end() ? found->value : -1;
        if (row < 0) {
            row = record.routes.size();
            record.routes.push_back(route);
            record.row_of[route] = row;
            record.row_ticks.push_back(tick);
            for (uint32_t c = 0; c < record.shapes.size(); c++) {
                Variant &storage = record.columns_data[c];
                storage_resize(
                    storage,
                    storage_size(storage) + record.shapes[c].stride
                );
            }
            record.births.push_back(route);
            bound.push_back(route);
        } else if (record.row_ticks[row] > tick) {
            drops_stale_row += 1;
            continue;
        } else {
            record.row_ticks[row] = tick;
        }
        for (uint32_t c = 0; c < record.shapes.size(); c++) {
            const int stride = record.shapes[c].stride;
            copy_elements(
                record.columns_data[c],
                row * stride,
                values[c],
                i * stride,
                stride
            );
        }
    }
    return bound;
}

void Core::prune_memos(Record &record, int64_t tick) {
    if (record.removal_memos.size() < 64) {
        return;
    }
    const int64_t cutoff = tick - MEMO_AGE_TICKS;
    LocalVector<int64_t> expired;
    for (const KeyValue<int64_t, int64_t> &entry : record.removal_memos) {
        if (entry.value < cutoff) {
            expired.push_back(entry.key);
        }
    }
    for (uint32_t i = 0; i < expired.size(); i++) {
        record.removal_memos.erase(expired[i]);
    }
}

} // namespace netw::table

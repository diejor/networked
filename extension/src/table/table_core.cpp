#include "netw/table/table_core.hpp"

#include "godot/class_db.hpp"
#include "netw/codec.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

// How many ticks a removal memo is kept before it is pruned. An implementation
// constant, never wire, so it may be measured and changed freely.
constexpr int64_t MEMO_AGE_TICKS = 256;

// How far behind the freshest applied tick a frame may still be applied, which
// absorbs ordinary datagram reordering without letting genuinely old state win.
constexpr int64_t REORDER_WINDOW_TICKS = 8;

const int WIRE_BITS[SchemaCore::COLUMN_TYPE_COUNT]
    = {32, 64, 8, 8, 16, 16, 32, 64, 1, 64, 96, 128, 128, 128, 40, 0};

// Bytes one element of a memcpy column occupies. Zero marks a type whose
// storage width does not equal its wire width, which is what decides between
// one memcpy for the whole column and a loop over its elements.
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

// A real copy rather than a shared reference. The commit is the one edge where
// atomicity is bought, so it severs the aliasing there and nowhere else.
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

// Moves a run of elements within one storage array, or from one to another of
// the same type. This is the per-element Variant loop the port exists to
// remove: each arm is a typed pointer copy.
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

// Reads one element as the Variant a quantizer expects, which is the one place
// a quaternion column stops being the Vector4 it stores as.
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

// The inverse of element_value.
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

// Sign-extends a narrow integer read back out of its wire width.
int64_t narrow(int64_t value, int bits, bool is_signed) {
    if (!is_signed) {
        return value;
    }
    const int64_t sign_bit = int64_t(1) << (bits - 1);
    return (value & sign_bit) ? value - (int64_t(1) << bits) : value;
}

// The wire carries the vector shapes as raw little-endian floats, so the byte
// width of one element is what makes the column a memcpy in both directions.
// A double-precision build would widen these and silently double every column,
// so it fails here instead.
static_assert(sizeof(Vector2) == 8, "the wire packs Vector2 as two float32");
static_assert(sizeof(Vector3) == 12, "the wire packs Vector3 as three float32");
static_assert(sizeof(Vector4) == 16, "the wire packs Vector4 as four float32");
static_assert(sizeof(Color) == 16, "the wire packs Color as four float32");

// Rebuilds a memcpy column's storage from its bytes, the inverse of
// slice_bytes. Straight into the typed array: the engine's PackedByteArray is a
// bare byte vector with no converters on it, and going through an intermediate
// float array would cost a copy the wire layout does not need.
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

// The bytes of one slice of a memcpy column, which is the raw little-endian
// copy the wire carries.
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

int TableCore::wire_bits(int type) {
    return (type >= 0 && type < SchemaCore::COLUMN_TYPE_COUNT) ? WIRE_BITS[type]
                                                               : 0;
}

TableCore::Record *TableCore::record_of(const RID &table) {
    return tables.getptr(table);
}

const TableCore::Record *TableCore::record_of(const RID &table) const {
    return tables.getptr(table);
}

const TableCore::ColumnShape *TableCore::shape_at(
    const RID &table,
    int column
) const {
    const Record *record = record_of(table);
    if (record == nullptr || column < 0
        || column >= static_cast<int>(record->shapes.size())) {
        return nullptr;
    }
    return &record->shapes[column];
}

/* Declaration */

Error TableCore::declare(const RID &table, const Ref<SchemaRecord> &schema) {
    if (schema.is_null() || !schema->sealed) {
        return ERR_UNCONFIGURED;
    }
    for (int i = 0; i < schema->column_count(); i++) {
        const Ref<SchemaColumn> column = schema->at(i);
        if (column.is_valid() && column->type == SchemaCore::VARIANT) {
            return ERR_INVALID_DATA;
        }
    }
    Record *existing = record_of(table);
    if (existing != nullptr) {
        return existing->schema == schema ? OK : ERR_ALREADY_EXISTS;
    }

    Record record;
    record.schema = schema;
    const int count = schema->column_count();
    record.shapes.resize(count);
    record.pending_columns.resize(count);
    record.pending_written.resize(count);
    for (int i = 0; i < count; i++) {
        const Ref<SchemaColumn> column = schema->at(i);
        ColumnShape &shape = record.shapes[i];
        if (column.is_valid()) {
            shape.key = column->key;
            shape.type = column->type;
            shape.stride = column->stride;
            shape.quantizer = column->quantizer;
        }
        shape.element_bits = shape.quantizer.is_valid()
            ? MAX(1,
                  shape.quantizer->bit_width(
                      SchemaCore::element_type(shape.type)
                  ))
            : wire_bits(shape.type);
        record.pending_written[i] = false;
    }
    tables.insert(table, record);
    clear_rows(*record_of(table));
    rebuild_wire_order();
    return OK;
}

void TableCore::set_reliable(const RID &table, bool value) {
    Record *record = record_of(table);
    if (record != nullptr) {
        record->reliable = value;
    }
}

bool TableCore::is_valid(const RID &table) const {
    return tables.has(table);
}

StringName TableCore::name_of(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->schema->name : StringName();
}

int TableCore::schema_hash(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->schema->shape_hash : 0;
}

int TableCore::column_count(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? static_cast<int>(record->shapes.size()) : 0;
}

StringName TableCore::column_key(const RID &table, int column) const {
    const ColumnShape *shape = shape_at(table, column);
    return shape != nullptr ? shape->key : StringName();
}

int TableCore::column_type(const RID &table, int column) const {
    const ColumnShape *shape = shape_at(table, column);
    return shape != nullptr ? shape->type : -1;
}

int TableCore::column_stride(const RID &table, int column) const {
    const ColumnShape *shape = shape_at(table, column);
    return shape != nullptr ? shape->stride : 0;
}

Ref<NetwQuantize> TableCore::column_quantizer(
    const RID &table,
    int column
) const {
    const ColumnShape *shape = shape_at(table, column);
    return shape != nullptr ? shape->quantizer : Ref<NetwQuantize>();
}

bool TableCore::is_reliable(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr && record->reliable;
}

int TableCore::wire_id(const RID &table) const {
    const HashMap<RID, int>::ConstIterator found = wire_id_of.find(table);
    return found != wire_id_of.end() ? found->value : 0;
}

RID TableCore::table_from_wire_id(int id) const {
    if (id < 1 || id > static_cast<int>(wire_order.size())) {
        return RID();
    }
    return wire_order[id - 1];
}

// Sorted as String rather than StringName, because StringName ordering is by
// internal pointer and two peers would disagree about it. The whole point of a
// name-sorted id is that both ends compute the same one without negotiating.
void TableCore::rebuild_wire_order() {
    LocalVector<String> names;
    HashMap<String, RID> by_name;
    for (const KeyValue<RID, Record> &entry : tables) {
        if (entry.value.schema.is_valid() && entry.value.schema->sealed) {
            const String name = String(entry.value.schema->name);
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

/* Publish */

Error TableCore::write_routes(
    const RID &table,
    const PackedInt64Array &routes
) {
    Record *record = record_of(table);
    if (record == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!record->schema->sealed) {
        return ERR_UNCONFIGURED;
    }
    record->pending_routes = routes;
    record->pending_routes_written = true;
    return OK;
}

Error TableCore::write_column(
    const RID &table,
    int column,
    const Variant &data
) {
    Record *record = record_of(table);
    if (record == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!record->schema->sealed) {
        return ERR_UNCONFIGURED;
    }
    if (column < 0 || column >= static_cast<int>(record->shapes.size())) {
        return ERR_INVALID_DATA;
    }
    if (static_cast<int>(data.get_type())
        != SchemaCore::storage_type(record->shapes[column].type)) {
        return ERR_INVALID_DATA;
    }
    record->pending_columns[column] = data;
    record->pending_written[column] = true;
    return OK;
}

Error TableCore::commit(const RID &table, int64_t tick) {
    NETW_ZONE_NC("TableCore commit", colors::TABLE);
    Record *record = record_of(table);
    if (record == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!record->schema->sealed || !record->pending_routes_written) {
        return ERR_INVALID_DATA;
    }
    const int rows = record->pending_routes.size();
    const int count = static_cast<int>(record->shapes.size());
    for (int i = 0; i < count; i++) {
        if (!record->pending_written[i]) {
            return ERR_INVALID_DATA;
        }
        if (element_count(record->pending_columns[i])
            != rows * record->shapes[i].stride) {
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

void TableCore::clear_dirty(const RID &table) {
    Record *record = record_of(table);
    if (record != nullptr) {
        record->dirty = false;
    }
}

TypedArray<RID> TableCore::dirty_tables() const {
    TypedArray<RID> out;
    for (uint32_t i = 0; i < wire_order.size(); i++) {
        const Record *record = record_of(wire_order[i]);
        if (record != nullptr && record->dirty) {
            out.push_back(wire_order[i]);
        }
    }
    return out;
}

TypedArray<RID> TableCore::published_tables() const {
    TypedArray<RID> out;
    for (uint32_t i = 0; i < wire_order.size(); i++) {
        const Record *record = record_of(wire_order[i]);
        if (record != nullptr && record->tick >= 0) {
            out.push_back(wire_order[i]);
        }
    }
    return out;
}

PackedInt64Array TableCore::take_pending_removals(const RID &table) {
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

/* Consume */

PackedInt64Array TableCore::read_routes(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->routes : PackedInt64Array();
}

Variant TableCore::read_column(const RID &table, int column) const {
    const Record *record = record_of(table);
    if (record == nullptr || column < 0
        || column >= static_cast<int>(record->columns_data.size())) {
        return Variant();
    }
    return record->columns_data[column];
}

PackedInt64Array TableCore::read_births(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->births : PackedInt64Array();
}

PackedInt64Array TableCore::read_deaths(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->deaths : PackedInt64Array();
}

int TableCore::row_of(const RID &table, int64_t route) const {
    const Record *record = record_of(table);
    if (record == nullptr) {
        return -1;
    }
    const HashMap<int64_t, int>::ConstIterator found
        = record->row_of.find(route);
    return found != record->row_of.end() ? found->value : -1;
}

PackedInt32Array TableCore::rows_of(
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

int64_t TableCore::tick_of(const RID &table) const {
    const Record *record = record_of(table);
    return record != nullptr ? record->tick : -1;
}

/* Wire — encode */

// Plans how many rows fit one frame, counting each column's worst case plus the
// byte alignment every column starts on.
int TableCore::rows_per_frame(const Record &record, int budget) const {
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

// Writes one column's slice. Every column starts byte aligned, so a bit-packed
// column can never leave the next one's memcpy straddling a byte.
void TableCore::encode_column(
    const Ref<NetwBitBufferWriter> &writer,
    const ColumnShape &shape,
    const Variant &data,
    int first,
    int rows
) const {
    writer->align();
    const int start = first * shape.stride;
    const int count = rows * shape.stride;
    if (count <= 0) {
        return;
    }
    if (shape.quantizer.is_valid()) {
        for (int i = 0; i < count; i++) {
            shape.quantizer->write(
                writer,
                element_value(data, start + i, shape.type)
            );
        }
        return;
    }
    switch (shape.type) {
        case SchemaCore::ENTITY: {
            const PackedInt64Array typed = data;
            const int64_t *read = typed.ptr();
            for (int i = 0; i < count; i++) {
                NetwCodec::put_varint(writer, read[start + i]);
            }
            return;
        }
        case SchemaCore::BOOL: {
            const PackedByteArray typed = data;
            const uint8_t *read = typed.ptr();
            for (int i = 0; i < count; i++) {
                writer->put_bits(read[start + i] != 0 ? 1 : 0, 1);
            }
            return;
        }
        case SchemaCore::I8:
        case SchemaCore::U8: {
            const PackedInt32Array typed = data;
            const int32_t *read = typed.ptr();
            for (int i = 0; i < count; i++) {
                writer->put_bits(read[start + i], 8);
            }
            return;
        }
        case SchemaCore::I16:
        case SchemaCore::U16: {
            const PackedInt32Array typed = data;
            const int32_t *read = typed.ptr();
            for (int i = 0; i < count; i++) {
                writer->put_bits(read[start + i], 16);
            }
            return;
        }
        default:
            writer->put_aligned_bytes(
                slice_bytes(data, shape.type, start, count)
            );
            return;
    }
}

// Writes one upsert frame carrying rows first through first + rows of the
// record's applied store.
PackedByteArray TableCore::encode_frame(
    const Record &record,
    int id,
    int flags,
    const PackedInt64Array &routes,
    int first,
    int rows
) const {
    Ref<NetwBitBufferWriter> writer;
    writer.instantiate();
    NetwCodec::put_varint(writer, id);
    writer->put_aligned_u16(record.schema->shape_hash);
    NetwCodec::put_varint(writer, MAX(record.tick, int64_t(0)));
    writer->put_aligned_u8(flags);
    NetwCodec::put_varint(writer, rows);
    for (int i = 0; i < routes.size(); i++) {
        NetwCodec::put_varint(writer, routes[i]);
    }
    for (uint32_t i = 0; i < record.shapes.size(); i++) {
        encode_column(
            writer,
            record.shapes[i],
            record.columns_data[i],
            first,
            rows
        );
    }
    return writer->to_bytes();
}

TypedArray<PackedByteArray> TableCore::encode_frames(
    const RID &table,
    int budget,
    bool snapshot
) {
    NETW_ZONE_NC("TableCore encode frames", colors::TABLE);
    TypedArray<PackedByteArray> out;
    const Record *record = record_of(table);
    if (record == nullptr || !record->schema->sealed) {
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

// Writes routes-only frames, the shape both a table removal and a lifecycle
// tombstone take.
TypedArray<PackedByteArray> TableCore::encode_routes_only(
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
        Ref<NetwBitBufferWriter> writer;
        writer.instantiate();
        NetwCodec::put_varint(writer, id);
        writer->put_aligned_u16(hash);
        NetwCodec::put_varint(writer, MAX(tick, int64_t(0)));
        writer->put_aligned_u8(FLAG_REMOVE);
        NetwCodec::put_varint(writer, count);
        for (int i = 0; i < count; i++) {
            NetwCodec::put_varint(writer, routes[at + i]);
        }
        out.push_back(writer->to_bytes());
        at += count;
    }
    return out;
}

TypedArray<PackedByteArray> TableCore::encode_removal(
    const RID &table,
    const PackedInt64Array &routes,
    int64_t tick,
    int budget
) {
    const Record *record = record_of(table);
    if (record == nullptr || !record->schema->sealed) {
        return TypedArray<PackedByteArray>();
    }
    return encode_routes_only(
        wire_id(table),
        record->schema->shape_hash,
        routes,
        tick,
        budget
    );
}

TypedArray<PackedByteArray> TableCore::encode_lifecycle(
    const PackedInt64Array &routes,
    int64_t tick,
    int budget
) {
    return encode_routes_only(LIFECYCLE_STREAM, 0, routes, tick, budget);
}

/* Wire — decode */

Dictionary TableCore::peek_header(const PackedByteArray &payload) {
    Dictionary out;
    if (payload.size() < 5) {
        return out;
    }
    Ref<NetwBitBufferReader> reader = NetwBitBufferReader::create(payload);
    const int64_t id = NetwCodec::get_safe_varint(reader);
    if (id < 0) {
        return out;
    }
    const int64_t hash = reader->get_aligned_u16();
    const int64_t tick = NetwCodec::get_safe_varint(reader);
    if (tick < 0) {
        return out;
    }
    const int64_t flags = reader->get_aligned_u8();
    const int64_t rows = NetwCodec::get_safe_varint(reader);
    if (rows < 0) {
        return out;
    }
    out["table_id"] = id;
    out["schema_hash"] = hash;
    out["tick"] = tick;
    out["flags"] = flags;
    out["rows"] = rows;
    return out;
}

Error TableCore::admit_header(const Dictionary &header) {
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

// Reads one column's slice into a fresh storage array, or nil when the frame
// ran out of bytes.
Variant TableCore::decode_column(
    const Ref<NetwBitBufferReader> &reader,
    const ColumnShape &shape,
    int rows
) const {
    reader->align();
    const int count = rows * shape.stride;
    Variant out = SchemaCore::make_storage(shape.type);
    if (count <= 0) {
        return out;
    }
    if (shape.quantizer.is_valid()) {
        const int element = SchemaCore::element_type(shape.type);
        const int64_t needed = (int64_t(count) * shape.element_bits + 7) / 8;
        if (reader->remaining_bytes() < needed) {
            return Variant();
        }
        storage_resize(out, count);
        for (int i = 0; i < count; i++) {
            store_element(
                out,
                i,
                storage_value(
                    shape.quantizer->read(reader, element),
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
                if (reader->remaining_bytes() <= 0) {
                    return Variant();
                }
                const int64_t value = NetwCodec::get_safe_varint(reader);
                if (value < 0) {
                    return Variant();
                }
                write[i] = value;
            }
            return typed;
        }
        case SchemaCore::BOOL: {
            if (reader->remaining_bytes() < (count + 7) / 8) {
                return Variant();
            }
            PackedByteArray typed;
            typed.resize(count);
            uint8_t *write = typed.ptrw();
            for (int i = 0; i < count; i++) {
                write[i] = static_cast<uint8_t>(reader->get_bits(1));
            }
            return typed;
        }
        case SchemaCore::I8:
        case SchemaCore::U8: {
            if (reader->remaining_bytes() < count) {
                return Variant();
            }
            PackedInt32Array typed;
            typed.resize(count);
            int32_t *write = typed.ptrw();
            for (int i = 0; i < count; i++) {
                write[i] = static_cast<int32_t>(
                    narrow(reader->get_bits(8), 8, shape.type == SchemaCore::I8)
                );
            }
            return typed;
        }
        case SchemaCore::I16:
        case SchemaCore::U16: {
            if (reader->remaining_bytes() < count * 2) {
                return Variant();
            }
            PackedInt32Array typed;
            typed.resize(count);
            int32_t *write = typed.ptrw();
            for (int i = 0; i < count; i++) {
                write[i] = static_cast<int32_t>(narrow(
                    reader->get_bits(16),
                    16,
                    shape.type == SchemaCore::I16
                ));
            }
            return typed;
        }
        default:
            break;
    }
    const int width = MEMCPY_BYTES[shape.type];
    const PackedByteArray bytes = reader->get_aligned_bytes(count * width);
    if (bytes.size() != count * width) {
        return Variant();
    }
    return from_bytes(bytes, shape.type, count);
}

// Reads the routes section, returning fewer routes than asked when the frame is
// truncated or carries a route no sender could have issued.
PackedInt64Array TableCore::read_frame_routes(
    const Ref<NetwBitBufferReader> &reader,
    int rows
) {
    PackedInt64Array out;
    if (rows <= 0) {
        return out;
    }
    out.resize(rows);
    int64_t *write = out.ptrw();
    for (int i = 0; i < rows; i++) {
        if (reader->remaining_bytes() <= 0) {
            return PackedInt64Array();
        }
        const int64_t route = NetwCodec::get_safe_varint(reader);
        if (route <= 0) {
            return PackedInt64Array();
        }
        write[i] = route;
    }
    return out;
}

Dictionary TableCore::apply_frame(const PackedByteArray &payload) {
    NETW_ZONE_NC("TableCore apply frame", colors::TABLE);
    NETW_ZONE_VALUE(payload.size());
    Dictionary result;
    result["verdict"] = ERR_INVALID_DATA;
    result["table"] = RID();
    result["tick"] = -1;
    result["bound"] = PackedInt64Array();
    result["retired"] = PackedInt64Array();

    const Dictionary header = peek_header(payload);
    if (header.is_empty()) {
        drops_truncated += 1;
        return result;
    }

    const int id = static_cast<int>(int64_t(header["table_id"]));
    const int64_t tick = int64_t(header["tick"]);
    const int flags = static_cast<int>(int64_t(header["flags"]));
    const int rows = static_cast<int>(int64_t(header["rows"]));
    result["tick"] = tick;

    Ref<NetwBitBufferReader> reader = NetwBitBufferReader::create(payload);
    NetwCodec::get_safe_varint(reader);
    reader->get_aligned_u16();
    NetwCodec::get_safe_varint(reader);
    reader->get_aligned_u8();
    NetwCodec::get_safe_varint(reader);

    const PackedInt64Array routes = read_frame_routes(reader, rows);
    if (routes.size() != rows) {
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
        Variant decoded = decode_column(reader, record->shapes[i], rows);
        if (decoded.get_type() == Variant::NIL) {
            drops_truncated += 1;
            return result;
        }
        values[i] = decoded;
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

/* Intake and session */

void TableCore::begin_intake() {
    for (KeyValue<RID, Record> &entry : tables) {
        entry.value.wave_touched = false;
    }
}

TypedArray<RID> TableCore::touched_tables() const {
    TypedArray<RID> out;
    for (uint32_t i = 0; i < wire_order.size(); i++) {
        const Record *record = record_of(wire_order[i]);
        if (record != nullptr && record->wave_touched) {
            out.push_back(wire_order[i]);
        }
    }
    return out;
}

void TableCore::count_bad_sender() {
    drops_bad_sender += 1;
}

// Retires routes here: they become tombstones and their rows leave every table
// that held one. The authority runs this the moment it releases an identity and
// every other peer runs it when the lifecycle frame lands.
void TableCore::retire_routes(const PackedInt64Array &routes, bool as_wave) {
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

void TableCore::queue_lifecycle_removals(const PackedInt64Array &routes) {
    pending_lifecycle.append_array(routes);
}

PackedInt64Array TableCore::take_lifecycle_removals() {
    const PackedInt64Array out = pending_lifecycle;
    pending_lifecycle = PackedInt64Array();
    return out;
}

PackedInt64Array TableCore::lifecycle_removals() const {
    return pending_lifecycle;
}

bool TableCore::is_tombstoned(int64_t route) const {
    return tombstones.has(route);
}

void TableCore::clear_session() {
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

Dictionary TableCore::counters() const {
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

/* Internals */

// Clears the rows without disturbing the declaration, the first half of a
// snapshot's clear-then-apply.
void TableCore::clear_rows(Record &record) {
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

// Replaces a record's applied state wholesale, deriving both cohorts against
// what it held before.
void TableCore::apply_wave(
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

// Starts this table's wave on the first frame of an intake that reaches it, so
// cohorts describe the wave rather than the last frame of it.
void TableCore::open_wave(Record &record) {
    if (record.wave_touched) {
        return;
    }
    record.wave_touched = true;
    record.births = PackedInt64Array();
    record.deaths = PackedInt64Array();
}

// Erases rows by swap-remove and records why, so a reordered upsert that lost
// the race to its own removal cannot put the row back.
void TableCore::apply_removal(
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

void TableCore::swap_remove_row(Record &record, int64_t route, int row) {
    NETW_ASSERT(
        row >= 0 && row < record.routes.size(),
        "table",
        "Swap-remove row is outside the route store."
    );
    NETW_ASSERT(
        record.row_ticks.size() == record.routes.size(),
        "table",
        "Route and row-tick stores have different lengths."
    );
    NETW_ASSERT(
        record.columns_data.size() == record.shapes.size(),
        "table",
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
            "table",
            "Column storage does not match the route store."
        );
        copy_elements(storage, row * stride, storage, last * stride, stride);
        storage_resize(storage, last * stride);
    }
}

// Applies upsert rows, returning the routes this frame introduced.
PackedInt64Array TableCore::apply_upsert(
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

void TableCore::prune_memos(Record &record, int64_t tick) {
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

void TableCore::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("declare", "table", "schema"),
        &TableCore::declare
    );
    ClassDB::bind_method(
        D_METHOD("set_reliable", "table", "value"),
        &TableCore::set_reliable
    );
    ClassDB::bind_method(D_METHOD("is_valid", "table"), &TableCore::is_valid);
    ClassDB::bind_method(D_METHOD("name_of", "table"), &TableCore::name_of);
    ClassDB::bind_method(
        D_METHOD("schema_hash", "table"),
        &TableCore::schema_hash
    );
    ClassDB::bind_method(
        D_METHOD("column_count", "table"),
        &TableCore::column_count
    );
    ClassDB::bind_method(
        D_METHOD("column_key", "table", "column"),
        &TableCore::column_key
    );
    ClassDB::bind_method(
        D_METHOD("column_type", "table", "column"),
        &TableCore::column_type
    );
    ClassDB::bind_method(
        D_METHOD("column_stride", "table", "column"),
        &TableCore::column_stride
    );
    ClassDB::bind_method(
        D_METHOD("column_quantizer", "table", "column"),
        &TableCore::column_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("is_reliable", "table"),
        &TableCore::is_reliable
    );
    ClassDB::bind_method(D_METHOD("wire_id", "table"), &TableCore::wire_id);
    ClassDB::bind_method(
        D_METHOD("table_from_wire_id", "id"),
        &TableCore::table_from_wire_id
    );

    ClassDB::bind_method(
        D_METHOD("write_routes", "table", "routes"),
        &TableCore::write_routes
    );
    ClassDB::bind_method(
        D_METHOD("write_column", "table", "column", "data"),
        &TableCore::write_column
    );
    ClassDB::bind_method(
        D_METHOD("commit", "table", "tick"),
        &TableCore::commit
    );
    ClassDB::bind_method(
        D_METHOD("clear_dirty", "table"),
        &TableCore::clear_dirty
    );
    ClassDB::bind_method(D_METHOD("dirty_tables"), &TableCore::dirty_tables);
    ClassDB::bind_method(
        D_METHOD("published_tables"),
        &TableCore::published_tables
    );
    ClassDB::bind_method(
        D_METHOD("take_pending_removals", "table"),
        &TableCore::take_pending_removals
    );

    ClassDB::bind_method(
        D_METHOD("read_routes", "table"),
        &TableCore::read_routes
    );
    ClassDB::bind_method(
        D_METHOD("read_column", "table", "column"),
        &TableCore::read_column
    );
    ClassDB::bind_method(
        D_METHOD("read_births", "table"),
        &TableCore::read_births
    );
    ClassDB::bind_method(
        D_METHOD("read_deaths", "table"),
        &TableCore::read_deaths
    );
    ClassDB::bind_method(
        D_METHOD("row_of", "table", "route"),
        &TableCore::row_of
    );
    ClassDB::bind_method(
        D_METHOD("rows_of", "table", "routes"),
        &TableCore::rows_of
    );
    ClassDB::bind_method(D_METHOD("tick_of", "table"), &TableCore::tick_of);

    ClassDB::bind_method(
        D_METHOD("encode_frames", "table", "budget", "snapshot"),
        &TableCore::encode_frames,
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("encode_removal", "table", "routes", "tick", "budget"),
        &TableCore::encode_removal
    );
    ClassDB::bind_static_method(
        "TableCore",
        D_METHOD("encode_lifecycle", "routes", "tick", "budget"),
        &TableCore::encode_lifecycle
    );
    ClassDB::bind_static_method(
        "TableCore",
        D_METHOD("peek_header", "payload"),
        &TableCore::peek_header
    );
    ClassDB::bind_method(
        D_METHOD("admit_header", "header"),
        &TableCore::admit_header
    );
    ClassDB::bind_method(
        D_METHOD("apply_frame", "payload"),
        &TableCore::apply_frame
    );
    ClassDB::bind_method(
        D_METHOD("retire_routes", "routes", "as_wave"),
        &TableCore::retire_routes,
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("count_bad_sender"),
        &TableCore::count_bad_sender
    );
    ClassDB::bind_method(D_METHOD("begin_intake"), &TableCore::begin_intake);
    ClassDB::bind_method(
        D_METHOD("touched_tables"),
        &TableCore::touched_tables
    );

    ClassDB::bind_method(
        D_METHOD("queue_lifecycle_removals", "routes"),
        &TableCore::queue_lifecycle_removals
    );
    ClassDB::bind_method(
        D_METHOD("take_lifecycle_removals"),
        &TableCore::take_lifecycle_removals
    );
    ClassDB::bind_method(
        D_METHOD("lifecycle_removals"),
        &TableCore::lifecycle_removals
    );
    ClassDB::bind_method(
        D_METHOD("is_tombstoned", "route"),
        &TableCore::is_tombstoned
    );

    ClassDB::bind_method(D_METHOD("clear_session"), &TableCore::clear_session);
    ClassDB::bind_method(D_METHOD("counters"), &TableCore::counters);
    ClassDB::bind_static_method(
        "TableCore",
        D_METHOD("wire_bits", "type"),
        &TableCore::wire_bits
    );

    BIND_CONSTANT(FLAG_SNAPSHOT);
    BIND_CONSTANT(FLAG_REMOVE);
    BIND_CONSTANT(FLAG_PAIR_KEY);
    BIND_CONSTANT(FLAG_NO_KEY);
    BIND_CONSTANT(FLAGS_IMPLEMENTED);
    BIND_CONSTANT(LIFECYCLE_STREAM);
    BIND_CONSTANT(FRAME_OVERHEAD_BYTES);
}

} // namespace netw

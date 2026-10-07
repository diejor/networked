#include "netw/store/array.hpp"

#include <cstring>

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/stage.hpp"
#include "netw/store/world.hpp"

using namespace godot;

namespace netw::store {

namespace {

constexpr int64_t MEMO_AGE_TICKS = 256;
constexpr int64_t REORDER_WINDOW_TICKS = 8;
constexpr uint32_t REFRESH_SENDS = 240;
constexpr uint32_t NO_ROW = 0xffffffffu;

const int WIRE_BITS[SchemaCore::COLUMN_TYPE_COUNT]
    = {32, 64, 8, 8, 16, 16, 32, 64, 1, 64, 96, 128, 128, 128, 40, 0, 0};

template <typename T>
const uint8_t *span_of(const Variant &p_data, int64_t &r_count) {
    const T held = p_data;
    r_count = held.size();
    return reinterpret_cast<const uint8_t *>(held.ptr());
}

const uint8_t *packed_span(const Variant &p_data, int64_t &r_count) {
    switch (p_data.get_type()) {
        case Variant::PACKED_FLOAT32_ARRAY:
            return span_of<PackedFloat32Array>(p_data, r_count);
        case Variant::PACKED_FLOAT64_ARRAY:
            return span_of<PackedFloat64Array>(p_data, r_count);
        case Variant::PACKED_INT32_ARRAY:
            return span_of<PackedInt32Array>(p_data, r_count);
        case Variant::PACKED_INT64_ARRAY:
            return span_of<PackedInt64Array>(p_data, r_count);
        case Variant::PACKED_BYTE_ARRAY:
            return span_of<PackedByteArray>(p_data, r_count);
        case Variant::PACKED_VECTOR2_ARRAY:
            return span_of<PackedVector2Array>(p_data, r_count);
        case Variant::PACKED_VECTOR3_ARRAY:
            return span_of<PackedVector3Array>(p_data, r_count);
        case Variant::PACKED_VECTOR4_ARRAY:
            return span_of<PackedVector4Array>(p_data, r_count);
        case Variant::PACKED_COLOR_ARRAY:
            return span_of<PackedColorArray>(p_data, r_count);
        default:
            r_count = -1;
            return nullptr;
    }
}

int64_t element_count(const Variant &p_data) {
    int64_t count = -1;
    packed_span(p_data, count);
    return count;
}

template <typename T, typename E>
Variant filled(const uint8_t *p_bytes, int64_t p_count) {
    T out;
    out.resize(p_count);
    if (p_count > 0) {
        memcpy(out.ptrw(), p_bytes, size_t(p_count) * sizeof(E));
    }
    return out;
}

Variant view_of(const ArrayColumn &p_column, uint32_t p_rows) {
    const int64_t count = int64_t(p_rows) * p_column.stride;
    const uint8_t *at = p_column.bytes.ptr();
    switch (SchemaCore::storage_type(p_column.type)) {
        case Variant::PACKED_FLOAT32_ARRAY:
            return filled<PackedFloat32Array, float>(at, count);
        case Variant::PACKED_FLOAT64_ARRAY:
            return filled<PackedFloat64Array, double>(at, count);
        case Variant::PACKED_INT32_ARRAY:
            return filled<PackedInt32Array, int32_t>(at, count);
        case Variant::PACKED_INT64_ARRAY:
            return filled<PackedInt64Array, int64_t>(at, count);
        case Variant::PACKED_BYTE_ARRAY:
            return filled<PackedByteArray, uint8_t>(at, count);
        case Variant::PACKED_VECTOR2_ARRAY:
            return filled<PackedVector2Array, Vector2>(at, count);
        case Variant::PACKED_VECTOR3_ARRAY:
            return filled<PackedVector3Array, Vector3>(at, count);
        case Variant::PACKED_VECTOR4_ARRAY:
            return filled<PackedVector4Array, Vector4>(at, count);
        case Variant::PACKED_COLOR_ARRAY:
            return filled<PackedColorArray, Color>(at, count);
        default:
            return SchemaCore::make_storage(p_column.type);
    }
}

template <typename T> T take(const uint8_t *p_at) {
    T value;
    memcpy(&value, p_at, sizeof(T));
    return value;
}

template <typename T> void put(uint8_t *p_at, const T &p_value) {
    memcpy(p_at, &p_value, sizeof(T));
}

Variant element_value(int p_type, const uint8_t *p_at) {
    switch (p_type) {
        case SchemaCore::F32:
            return take<float>(p_at);
        case SchemaCore::F64:
            return take<double>(p_at);
        case SchemaCore::I8:
        case SchemaCore::U8:
        case SchemaCore::I16:
        case SchemaCore::U16:
        case SchemaCore::I32:
            return take<int32_t>(p_at);
        case SchemaCore::I64:
        case SchemaCore::ENTITY:
            return take<int64_t>(p_at);
        case SchemaCore::BOOL:
            return int64_t(*p_at);
        case SchemaCore::VECTOR2:
            return take<Vector2>(p_at);
        case SchemaCore::VECTOR3:
            return take<Vector3>(p_at);
        case SchemaCore::VECTOR4:
            return take<Vector4>(p_at);
        case SchemaCore::COLOR:
            return take<Color>(p_at);
        case SchemaCore::QUATERNION: {
            const Vector4 held = take<Vector4>(p_at);
            return Quaternion(held.x, held.y, held.z, held.w);
        }
        default:
            return Variant();
    }
}

void store_value(int p_type, uint8_t *p_at, const Variant &p_value) {
    switch (p_type) {
        case SchemaCore::F32:
            put(p_at, static_cast<float>(static_cast<double>(p_value)));
            return;
        case SchemaCore::F64:
            put(p_at, static_cast<double>(p_value));
            return;
        case SchemaCore::I8:
        case SchemaCore::U8:
        case SchemaCore::I16:
        case SchemaCore::U16:
        case SchemaCore::I32:
            put(p_at, static_cast<int32_t>(static_cast<int64_t>(p_value)));
            return;
        case SchemaCore::I64:
        case SchemaCore::ENTITY:
            put(p_at, static_cast<int64_t>(p_value));
            return;
        case SchemaCore::BOOL:
            *p_at = static_cast<uint8_t>(static_cast<int64_t>(p_value));
            return;
        case SchemaCore::VECTOR2:
            put(p_at, Vector2(p_value));
            return;
        case SchemaCore::VECTOR3:
            put(p_at, Vector3(p_value));
            return;
        case SchemaCore::VECTOR4:
            put(p_at, Vector4(p_value));
            return;
        case SchemaCore::COLOR:
            put(p_at, Color(p_value));
            return;
        case SchemaCore::QUATERNION: {
            const Quaternion held = p_value;
            put(p_at, Vector4(held.x, held.y, held.z, held.w));
            return;
        }
        default:
            return;
    }
}

int64_t narrow(int64_t p_value, int p_bits, bool p_signed) {
    if (!p_signed) {
        return p_value;
    }
    const int64_t sign_bit = int64_t(1) << (p_bits - 1);
    return (p_value & sign_bit) ? p_value - (int64_t(1) << p_bits) : p_value;
}

bool same_routes(const ArrayTable &p_table) {
    const int64_t count = p_table.pending_routes.size();
    if (count != int64_t(p_table.routes.size())) {
        return false;
    }
    return count == 0
        || memcmp(
               p_table.pending_routes.ptr(),
               p_table.routes.ptr(),
               size_t(count) * sizeof(int64_t)
           )
        == 0;
}

void touch_columns(ArrayTable &r_table) {
    for (ArrayColumn &column : r_table.columns) {
        column.fresh = true;
        column.viewed = false;
        column.view = Variant();
    }
    r_table.routes_viewed = false;
}

} // namespace

static_assert(sizeof(Vector2) == 8, "the wire packs Vector2 as two float32");
static_assert(sizeof(Vector3) == 12, "the wire packs Vector3 as three float32");
static_assert(sizeof(Vector4) == 16, "the wire packs Vector4 as four float32");
static_assert(sizeof(Color) == 16, "the wire packs Color as four float32");

int Arrays::wire_bits(int p_type) {
    return (p_type >= 0 && p_type < SchemaCore::COLUMN_TYPE_COUNT)
        ? WIRE_BITS[p_type]
        : 0;
}

uint32_t Arrays::element_bytes(int p_type) {
    switch (p_type) {
        case SchemaCore::F32:
        case SchemaCore::I8:
        case SchemaCore::U8:
        case SchemaCore::I16:
        case SchemaCore::U16:
        case SchemaCore::I32:
            return 4;
        case SchemaCore::F64:
        case SchemaCore::I64:
        case SchemaCore::ENTITY:
            return 8;
        case SchemaCore::BOOL:
            return 1;
        case SchemaCore::VECTOR2:
            return sizeof(Vector2);
        case SchemaCore::VECTOR3:
            return sizeof(Vector3);
        case SchemaCore::VECTOR4:
        case SchemaCore::QUATERNION:
            return sizeof(Vector4);
        case SchemaCore::COLOR:
            return sizeof(Color);
        default:
            return 0;
    }
}

Arrays::~Arrays() {
    for (ArrayTable *held : tables) {
        memdelete(held);
    }
}

ArrayTable *Arrays::table_of(const RID &p_table) {
    const uint32_t *at = by_rid.getptr(p_table);
    return at != nullptr ? tables[*at] : nullptr;
}

const ArrayTable *Arrays::table_of(const RID &p_table) const {
    const uint32_t *at = by_rid.getptr(p_table);
    return at != nullptr ? tables[*at] : nullptr;
}

const ArrayColumn *Arrays::column_at(const RID &p_table, int p_column) const {
    const ArrayTable *held = table_of(p_table);
    if (held == nullptr || p_column < 0
        || p_column >= int(held->columns.size())) {
        return nullptr;
    }
    return &held->columns[p_column];
}

Error Arrays::declare(const RID &p_table, const table::SchemaRecord *p_schema) {
    if (p_schema == nullptr || !p_schema->sealed) {
        return ERR_UNCONFIGURED;
    }
    for (int at = 0; at < p_schema->column_count(); ++at) {
        if (!SchemaCore::is_sized(p_schema->at(at)->type)) {
            return ERR_INVALID_DATA;
        }
    }
    if (const ArrayTable *existing = table_of(p_table)) {
        if (existing->schema.name != p_schema->name
            || existing->schema.shape_hash != p_schema->shape_hash) {
            return ERR_ALREADY_EXISTS;
        }
        return OK;
    }
    ArrayTable *made = memnew(ArrayTable);
    made->schema = *p_schema;
    made->rid = p_table;
    made->columns.resize(uint32_t(p_schema->column_count()));
    for (int at = 0; at < p_schema->column_count(); ++at) {
        const table::SchemaColumn *declared = p_schema->at(at);
        ArrayColumn &column = made->columns[at];
        column.form.port = Port::ARRAY;
        column.key = declared->key;
        column.type = declared->type;
        column.stride = declared->stride;
        column.quantizer = declared->quantizer;
        column.element_bits = column.quantizer.is_valid()
            ? MAX(1,
                  column.quantizer->bit_width(
                      static_cast<Variant::Type>(
                          SchemaCore::element_type(column.type)
                      )
                  ))
            : wire_bits(column.type);
        column.element_bytes = element_bytes(column.type);
        column.row_bytes = column.element_bytes * uint32_t(column.stride);
        column.form.value_bytes = column.row_bytes;
        made->row_bits += int64_t(column.stride) * column.element_bits;
    }
    made->row_bits += 24;
    by_rid[p_table] = tables.size();
    tables.push_back(made);
    clear_rows(*made);
    rebuild_wire_order();
    return OK;
}

void Arrays::rebuild_wire_order() {
    LocalVector<String> names;
    HashMap<String, uint32_t> by_name;
    for (uint32_t at = 0; at < tables.size(); ++at) {
        if (tables[at]->schema.sealed) {
            const String name = String(tables[at]->schema.name);
            names.push_back(name);
            by_name[name] = at;
        }
    }
    names.sort();
    wire_order.clear();
    for (uint32_t at = 0; at < names.size(); ++at) {
        wire_order.push_back(by_name[names[at]]);
    }
}

void Arrays::set_reliable(const RID &p_table, bool p_value) {
    if (ArrayTable *held = table_of(p_table)) {
        held->reliable = p_value;
    }
}

bool Arrays::is_valid(const RID &p_table) const {
    return by_rid.has(p_table);
}

bool Arrays::is_reliable(const RID &p_table) const {
    const ArrayTable *held = table_of(p_table);
    return held != nullptr && held->reliable;
}

StringName Arrays::name_of(const RID &p_table) const {
    const ArrayTable *held = table_of(p_table);
    return held != nullptr ? held->schema.name : StringName();
}

int Arrays::schema_hash(const RID &p_table) const {
    const ArrayTable *held = table_of(p_table);
    return held != nullptr ? held->schema.shape_hash : 0;
}

int Arrays::column_count(const RID &p_table) const {
    const ArrayTable *held = table_of(p_table);
    return held != nullptr ? int(held->columns.size()) : 0;
}

int Arrays::wire_id(const RID &p_table) const {
    const uint32_t *at = by_rid.getptr(p_table);
    if (at == nullptr) {
        return 0;
    }
    for (uint32_t order = 0; order < wire_order.size(); ++order) {
        if (wire_order[order] == *at) {
            return int(order) + 1;
        }
    }
    return 0;
}

RID Arrays::table_from_wire_id(int p_id) const {
    if (p_id < 1 || p_id > int(wire_order.size())) {
        return RID();
    }
    return tables[wire_order[p_id - 1]]->rid;
}

Error Arrays::write_routes(
    const RID &p_table,
    const PackedInt64Array &p_routes
) {
    ArrayTable *held = table_of(p_table);
    if (held == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!held->schema.sealed) {
        return ERR_UNCONFIGURED;
    }
    held->pending_routes = p_routes;
    held->routes_written = true;
    return OK;
}

Error Arrays::write_column(
    const RID &p_table,
    int p_column,
    const Variant &p_data
) {
    ArrayTable *held = table_of(p_table);
    if (held == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!held->schema.sealed) {
        return ERR_UNCONFIGURED;
    }
    if (p_column < 0 || p_column >= int(held->columns.size())) {
        return ERR_INVALID_DATA;
    }
    ArrayColumn &column = held->columns[p_column];
    if (int(p_data.get_type()) != SchemaCore::storage_type(column.type)) {
        return ERR_INVALID_DATA;
    }
    column.pending = p_data;
    column.written = true;
    column.fresh = true;
    return OK;
}

uint32_t Arrays::enter_slot(int64_t p_route) {
    return world != nullptr ? world->enter(p_route, 0).slot : NO_ROW;
}

void Arrays::leave_slot(uint32_t p_slot) {
    if (world != nullptr && p_slot < world->slot_capacity()) {
        world->leave(world->ref_of(p_slot));
    }
}

void Arrays::forget_slots() {
    for (ArrayTable *held : tables) {
        for (uint32_t &slot : held->slots) {
            slot = NO_ROW;
        }
    }
}

bool Arrays::holds_route(int64_t p_route) const {
    for (const ArrayTable *held : tables) {
        if (held->row_of.has(p_route)) {
            return true;
        }
    }
    return false;
}

void Arrays::resize_rows(ArrayTable &r_table, uint32_t p_rows) {
    r_table.routes.resize(p_rows);
    r_table.slots.resize(p_rows);
    r_table.row_ticks.resize(p_rows);
    r_table.changed_ticks.resize(p_rows);
    r_table.born_sends.resize(p_rows);
    for (ArrayColumn &column : r_table.columns) {
        column.bytes.resize(p_rows * column.row_bytes);
    }
    for (ArrayPeer &peer : r_table.peers) {
        peer.owed.resize(p_rows);
        const uint32_t had = peer.acc.size();
        peer.acc.resize(p_rows);
        for (uint32_t at = had; at < p_rows; ++at) {
            peer.acc[at] = 0.0f;
        }
    }
}

void Arrays::mark_changed(ArrayTable &r_table, uint32_t p_row, int64_t p_tick) {
    r_table.changed_ticks[p_row] = p_tick;
    for (ArrayPeer &peer : r_table.peers) {
        peer.owed.set(p_row, true);
    }
}

Error Arrays::diff_same_routes(ArrayTable &r_table, int64_t p_tick) {
    const uint32_t rows = r_table.row_count();
    for (ArrayColumn &column : r_table.columns) {
        if (!column.fresh) {
            continue;
        }
        int64_t count = 0;
        const uint8_t *source = packed_span(column.pending, count);
        const uint32_t width = column.row_bytes;
        bool moved = false;
        for (uint32_t row = 0; row < rows; ++row) {
            const uint8_t *next = source + uint64_t(row) * width;
            uint8_t *held = column.row(row);
            if (memcmp(next, held, width) != 0) {
                memcpy(held, next, width);
                mark_changed(r_table, row, p_tick);
                moved = true;
            }
        }
        if (moved) {
            column.viewed = false;
            column.view = Variant();
        }
    }
    return OK;
}

void Arrays::relayout(ArrayTable &r_table, int64_t p_tick) {
    const uint32_t rows = uint32_t(r_table.pending_routes.size());
    const int64_t *next = r_table.pending_routes.ptr();
    remap.resize(rows);
    PackedInt64Array births;
    HashMap<int64_t, uint32_t> placed;
    for (uint32_t at = 0; at < rows; ++at) {
        const HashMap<int64_t, uint32_t>::ConstIterator found
            = r_table.row_of.find(next[at]);
        remap[at] = found != r_table.row_of.end() ? found->value : NO_ROW;
        if (remap[at] == NO_ROW) {
            births.push_back(next[at]);
        }
        placed[next[at]] = at;
    }
    PackedInt64Array deaths;
    LocalVector<uint32_t> old_slots;
    old_slots = r_table.slots;
    for (const KeyValue<int64_t, uint32_t> &entry : r_table.row_of) {
        if (!placed.has(entry.key)) {
            deaths.push_back(entry.key);
            leave_slot(old_slots[entry.value]);
            if (r_table.born_sends[entry.value] < r_table.sends) {
                r_table.unpublished.push_back(entry.key);
            }
        }
    }

    LocalVector<uint8_t> changed;
    changed.resize(rows);
    for (uint32_t at = 0; at < rows; ++at) {
        changed[at] = remap[at] == NO_ROW ? 1 : 0;
    }
    for (ArrayColumn &column : r_table.columns) {
        int64_t count = 0;
        const uint8_t *source = packed_span(column.pending, count);
        const uint32_t width = column.row_bytes;
        for (uint32_t at = 0; at < rows; ++at) {
            if (changed[at] == 0
                && memcmp(
                       source + uint64_t(at) * width,
                       column.row(remap[at]),
                       width
                   ) != 0) {
                changed[at] = 1;
            }
        }
    }

    LocalVector<uint32_t> old_born;
    old_born = r_table.born_sends;
    LocalVector<int64_t> old_changed;
    old_changed = r_table.changed_ticks;
    LocalVector<int64_t> old_ticks;
    old_ticks = r_table.row_ticks;
    LocalVector<Bits> old_owed;
    LocalVector<LocalVector<float>> old_acc;
    old_owed.resize(r_table.peers.size());
    old_acc.resize(r_table.peers.size());
    for (uint32_t peer = 0; peer < r_table.peers.size(); ++peer) {
        old_owed[peer] = r_table.peers[peer].owed;
        old_acc[peer] = r_table.peers[peer].acc;
    }

    resize_rows(r_table, rows);
    for (ArrayColumn &column : r_table.columns) {
        int64_t count = 0;
        const uint8_t *source = packed_span(column.pending, count);
        if (rows > 0) {
            memcpy(column.bytes.ptr(), source, size_t(rows) * column.row_bytes);
        }
        column.viewed = false;
        column.view = Variant();
    }
    r_table.row_of.clear();
    for (uint32_t at = 0; at < rows; ++at) {
        r_table.routes[at] = next[at];
        r_table.row_of[next[at]] = at;
        const uint32_t from = remap[at];
        r_table.slots[at]
            = from == NO_ROW ? enter_slot(next[at]) : old_slots[from];
        r_table.born_sends[at]
            = from == NO_ROW ? r_table.sends : old_born[from];
        r_table.changed_ticks[at] = from == NO_ROW ? p_tick : old_changed[from];
        r_table.row_ticks[at] = from == NO_ROW ? p_tick : old_ticks[from];
        for (uint32_t peer = 0; peer < r_table.peers.size(); ++peer) {
            const bool owed = from != NO_ROW && old_owed[peer].get(from);
            r_table.peers[peer].owed.set(at, owed);
            r_table.peers[peer].acc[at]
                = from != NO_ROW ? old_acc[peer][from] : 0.0f;
        }
    }
    for (uint32_t at = 0; at < rows; ++at) {
        if (changed[at] != 0) {
            mark_changed(r_table, at, p_tick);
        }
    }
    r_table.routes_viewed = false;
    r_table.births = births;
    r_table.deaths = deaths;
}

Error Arrays::commit(const RID &p_table, int64_t p_tick) {
    NETW_ZONE_NC("Table commit", colors::TABLE);
    ArrayTable *held = table_of(p_table);
    if (held == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!held->schema.sealed || !held->routes_written) {
        return ERR_INVALID_DATA;
    }
    const int64_t rows = held->pending_routes.size();
    for (const ArrayColumn &column : held->columns) {
        if (!column.written
            || element_count(column.pending) != rows * column.stride) {
            return ERR_INVALID_DATA;
        }
    }
    held->revision += 1;
    if (same_routes(*held)) {
        held->births = PackedInt64Array();
        held->deaths = PackedInt64Array();
        diff_same_routes(*held, p_tick);
    } else {
        relayout(*held, p_tick);
    }
    for (ArrayColumn &column : held->columns) {
        column.fresh = false;
    }
    held->tick = p_tick;
    held->dirty = true;
    return OK;
}

Error Arrays::replace_rows(
    const RID &p_table,
    const PackedInt64Array &p_routes,
    const LocalVector<Variant> &p_columns,
    int64_t p_tick
) {
    ArrayTable *held = table_of(p_table);
    if (held == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    if (!held->schema.sealed) {
        return ERR_UNCONFIGURED;
    }
    if (p_columns.size() != held->columns.size()) {
        return ERR_INVALID_DATA;
    }
    HashMap<int64_t, uint32_t> placed;
    for (int at = 0; at < p_routes.size(); ++at) {
        if (p_routes[at] <= 0 || placed.has(p_routes[at])) {
            return ERR_INVALID_DATA;
        }
        placed[p_routes[at]] = uint32_t(at);
    }
    for (uint32_t at = 0; at < p_columns.size(); ++at) {
        const ArrayColumn &column = held->columns[at];
        if (int(p_columns[at].get_type())
            != SchemaCore::storage_type(column.type)) {
            return ERR_INVALID_DATA;
        }
        if (element_count(p_columns[at])
            != int64_t(p_routes.size()) * column.stride) {
            return ERR_INVALID_DATA;
        }
    }
    held->revision += 1;
    for (const KeyValue<int64_t, uint32_t> &entry : held->row_of) {
        if (held->born_sends[entry.value] < held->sends) {
            held->unpublished.push_back(entry.key);
        }
        leave_slot(held->slots[entry.value]);
    }
    const uint32_t rows = uint32_t(p_routes.size());
    resize_rows(*held, rows);
    for (uint32_t at = 0; at < held->columns.size(); ++at) {
        ArrayColumn &column = held->columns[at];
        int64_t count = 0;
        const uint8_t *source = packed_span(p_columns[at], count);
        if (rows > 0) {
            memcpy(column.bytes.ptr(), source, size_t(rows) * column.row_bytes);
        }
    }
    held->row_of = placed;
    for (uint32_t at = 0; at < rows; ++at) {
        held->routes[at] = p_routes[at];
        held->slots[at] = enter_slot(p_routes[at]);
        held->row_ticks[at] = p_tick;
        held->born_sends[at] = held->sends;
        mark_changed(*held, at, p_tick);
    }
    touch_columns(*held);
    held->removal_memos.clear();
    held->births = p_routes;
    held->deaths = PackedInt64Array();
    held->tick = p_tick;
    held->dirty = true;
    return OK;
}

void Arrays::clear_dirty(const RID &p_table) {
    if (ArrayTable *held = table_of(p_table)) {
        held->dirty = false;
    }
}

TypedArray<RID> Arrays::dirty_tables() const {
    TypedArray<RID> out;
    for (uint32_t order = 0; order < wire_order.size(); ++order) {
        const ArrayTable *held = tables[wire_order[order]];
        if (held->dirty) {
            out.push_back(held->rid);
        }
    }
    return out;
}

TypedArray<RID> Arrays::published_tables() const {
    TypedArray<RID> out;
    for (uint32_t order = 0; order < wire_order.size(); ++order) {
        const ArrayTable *held = tables[wire_order[order]];
        if (held->tick >= 0) {
            out.push_back(held->rid);
        }
    }
    return out;
}

PackedInt64Array Arrays::take_pending_removals(const RID &p_table) {
    PackedInt64Array out;
    ArrayTable *held = table_of(p_table);
    if (held == nullptr) {
        return out;
    }
    HashSet<int64_t> seen;
    for (const int64_t route : held->unpublished) {
        if (!held->row_of.has(route) && !seen.has(route)) {
            seen.insert(route);
            out.push_back(route);
        }
    }
    held->unpublished.clear();
    held->sends += 1;
    return out;
}

bool Arrays::owes(const RID &p_table, int p_peer, int64_t p_route) const {
    const ArrayTable *held = table_of(p_table);
    if (held == nullptr) {
        return false;
    }
    const uint32_t *row = held->row_of.getptr(p_route);
    for (const ArrayPeer &peer : held->peers) {
        if (peer.id == p_peer) {
            return row != nullptr && peer.owed.get(*row);
        }
    }
    return row != nullptr;
}

PackedInt64Array Arrays::read_routes(const RID &p_table) const {
    const ArrayTable *held = table_of(p_table);
    if (held == nullptr) {
        return PackedInt64Array();
    }
    if (!held->routes_viewed) {
        held->routes_view = filled<PackedInt64Array, int64_t>(
            reinterpret_cast<const uint8_t *>(held->routes.ptr()),
            held->routes.size()
        );
        held->routes_viewed = true;
    }
    return held->routes_view;
}

Variant Arrays::read_column(const RID &p_table, int p_column) const {
    const ArrayTable *held = table_of(p_table);
    if (held == nullptr || p_column < 0
        || p_column >= int(held->columns.size())) {
        return Variant();
    }
    const ArrayColumn &column = held->columns[p_column];
    if (!column.viewed) {
        column.view = view_of(column, held->row_count());
        column.viewed = true;
    }
    return column.view;
}

PackedInt64Array Arrays::read_births(const RID &p_table) const {
    const ArrayTable *held = table_of(p_table);
    return held != nullptr ? held->births : PackedInt64Array();
}

PackedInt64Array Arrays::read_deaths(const RID &p_table) const {
    const ArrayTable *held = table_of(p_table);
    return held != nullptr ? held->deaths : PackedInt64Array();
}

int64_t Arrays::revision(const RID &p_table) const {
    const ArrayTable *held = table_of(p_table);
    return held != nullptr ? held->revision : -1;
}

bool Arrays::holds_elsewhere(const RID &p_table, int64_t p_route) const {
    for (const ArrayTable *held : tables) {
        if (held->rid != p_table && held->row_of.has(p_route)) {
            return true;
        }
    }
    return false;
}

int Arrays::row_of(const RID &p_table, int64_t p_route) const {
    const ArrayTable *held = table_of(p_table);
    if (held == nullptr) {
        return -1;
    }
    const uint32_t *row = held->row_of.getptr(p_route);
    return row != nullptr ? int(*row) : -1;
}

PackedInt32Array Arrays::rows_of(
    const RID &p_table,
    const PackedInt64Array &p_routes
) const {
    PackedInt32Array out;
    out.resize(p_routes.size());
    int32_t *write = out.ptrw();
    for (int at = 0; at < p_routes.size(); ++at) {
        write[at] = row_of(p_table, p_routes[at]);
    }
    return out;
}

int64_t Arrays::tick_of(const RID &p_table) const {
    const ArrayTable *held = table_of(p_table);
    return held != nullptr ? held->tick : -1;
}

ArrayPeer &Arrays::peer_of(ArrayTable &r_table, int p_peer) {
    for (ArrayPeer &peer : r_table.peers) {
        if (peer.id == p_peer) {
            return peer;
        }
    }
    r_table.peers.resize(r_table.peers.size() + 1);
    ArrayPeer &made = r_table.peers[r_table.peers.size() - 1];
    made.id = p_peer;
    made.refresh_at = 0;
    made.owed.resize(r_table.row_count());
    made.owed.fill();
    made.acc.resize(r_table.row_count());
    for (uint32_t at = 0; at < made.acc.size(); ++at) {
        made.acc[at] = 0.0f;
    }
    return made;
}

void Arrays::refresh_rows(
    ArrayTable &r_table,
    ArrayPeer &r_peer,
    LocalVector<uint32_t> &r_rows
) const {
    r_rows.clear();
    const uint32_t rows = r_table.row_count();
    if (r_table.reliable || rows == 0) {
        return;
    }
    const uint32_t share = (rows + REFRESH_SENDS - 1) / REFRESH_SENDS;
    uint32_t looked = 0;
    while (r_rows.size() < share && looked < rows) {
        const uint32_t row = r_peer.refresh_at % rows;
        r_peer.refresh_at = (r_peer.refresh_at + 1) % rows;
        looked += 1;
        if (!r_peer.owed.get(row)) {
            r_rows.push_back(row);
        }
    }
}

void Arrays::encode_picked(
    ArrayTable &r_table,
    ArrayPeer &r_peer,
    const LocalVector<uint32_t> &p_rows,
    int p_budget,
    TypedArray<PackedByteArray> &r_frames
) {
    for (const uint32_t row : p_rows) {
        r_peer.owed.set(row, false);
        r_peer.acc[row] = 0.0f;
    }
    encode_rows(r_table, p_budget, 0, p_rows, r_frames);
}

void Arrays::forget_peers_except(const PackedInt32Array &p_peers) {
    for (ArrayTable *held : tables) {
        for (uint32_t at = held->peers.size(); at > 0; --at) {
            if (!p_peers.has(held->peers[at - 1].id)) {
                held->peers.remove_at(at - 1);
            }
        }
    }
}

int Arrays::rows_per_frame(const ArrayTable &p_table, int p_budget) const {
    int64_t bits = 40;
    for (const ArrayColumn &column : p_table.columns) {
        bits += int64_t(column.stride) * column.element_bits + 7;
    }
    const int64_t room
        = MAX(int64_t(1), int64_t(p_budget) - FRAME_OVERHEAD_BYTES);
    return int(MAX(int64_t(1), (room * 8) / MAX(bits, int64_t(1))));
}

bool Arrays::encode_column(
    wire::WriteStream &r_stream,
    const ArrayColumn &p_column,
    const uint32_t *p_rows,
    int p_count
) const {
    if (!r_stream.align_verify()) {
        return false;
    }
    if (p_count <= 0) {
        return true;
    }
    const int stride = p_column.stride;
    const uint32_t step = p_column.element_bytes;
    if (p_column.quantizer.is_valid()) {
        const NetwQuantize *quantizer = p_column.quantizer.ptr();
        for (int at = 0; at < p_count; ++at) {
            const uint8_t *row = p_column.row(p_rows[at]);
            for (int element = 0; element < stride; ++element) {
                const Variant value
                    = element_value(p_column.type, row + element * step);
                const Variant::Type held = value.get_type();
                const int width = quantizer->bit_width(held);
                const int parts = quantizer->stride(held);
                for (int part = 0; part < parts; ++part) {
                    uint64_t code = uint64_t(quantizer->encode(value, part));
                    if (!r_stream.bits(code, width)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }
    switch (p_column.type) {
        case SchemaCore::ENTITY: {
            for (int at = 0; at < p_count; ++at) {
                const uint8_t *row = p_column.row(p_rows[at]);
                for (int element = 0; element < stride; ++element) {
                    const int64_t held = take<int64_t>(row + element * step);
                    uint64_t route = uint64_t(MAX(held, int64_t(0)));
                    if (!r_stream.varuint(route, 5)) {
                        return false;
                    }
                }
            }
            return true;
        }
        case SchemaCore::BOOL: {
            for (int at = 0; at < p_count; ++at) {
                const uint8_t *row = p_column.row(p_rows[at]);
                for (int element = 0; element < stride; ++element) {
                    bool set = row[element] != 0;
                    if (!r_stream.bool1(set)) {
                        return false;
                    }
                }
            }
            return true;
        }
        case SchemaCore::I8:
        case SchemaCore::U8:
        case SchemaCore::I16:
        case SchemaCore::U16: {
            const int width = (p_column.type == SchemaCore::I8
                               || p_column.type == SchemaCore::U8)
                ? 8
                : 16;
            const uint64_t mask = (uint64_t(1) << width) - 1;
            for (int at = 0; at < p_count; ++at) {
                const uint8_t *row = p_column.row(p_rows[at]);
                for (int element = 0; element < stride; ++element) {
                    const int32_t held = take<int32_t>(row + element * step);
                    uint64_t packed = uint64_t(int64_t(held)) & mask;
                    if (!r_stream.bits(packed, width)) {
                        return false;
                    }
                }
            }
            return true;
        }
        default: {
            int at = 0;
            while (at < p_count) {
                int run = 1;
                while (at + run < p_count
                       && p_rows[at + run] == p_rows[at] + uint32_t(run)) {
                    run += 1;
                }
                if (!r_stream.raw_span(
                        p_column.row(p_rows[at]),
                        int64_t(run) * p_column.row_bytes
                    )) {
                    return false;
                }
                at += run;
            }
            return true;
        }
    }
}

PackedByteArray Arrays::encode_frame(
    const ArrayTable &p_table,
    int p_flags,
    const uint32_t *p_rows,
    int p_count
) {
    wire::WriteStream stream;
    ArrayFrameHead head;
    head.table_id = uint64_t(wire_id(p_table.rid));
    head.schema_hash = uint64_t(uint16_t(p_table.schema.shape_hash));
    head.tick = uint64_t(MAX(p_table.tick, int64_t(0)));
    head.flags = uint64_t(uint8_t(p_flags));
    head.rows = uint64_t(p_count);
    if (!ArrayFrameHead::wire.run(stream, head)) {
        return PackedByteArray();
    }
    for (int at = 0; at < p_count; ++at) {
        uint64_t route = uint64_t(MAX(p_table.routes[p_rows[at]], int64_t(0)));
        if (!stream.varuint(route, 5)) {
            return PackedByteArray();
        }
    }
    for (const ArrayColumn &column : p_table.columns) {
        if (!encode_column(stream, column, p_rows, p_count)) {
            return PackedByteArray();
        }
    }
    if (!stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

void Arrays::encode_rows(
    const ArrayTable &p_table,
    int p_budget,
    int p_flags,
    const LocalVector<uint32_t> &p_rows,
    TypedArray<PackedByteArray> &r_frames
) {
    const int total = int(p_rows.size());
    if (total == 0) {
        r_frames.push_back(encode_frame(p_table, p_flags, nullptr, 0));
        return;
    }
    const int per_frame = rows_per_frame(p_table, p_budget);
    int at = 0;
    while (at < total) {
        const int count = MIN(per_frame, total - at);
        r_frames.push_back(encode_frame(
            p_table,
            at == 0 ? p_flags : 0,
            p_rows.ptr() + at,
            count
        ));
        at += count;
    }
}

TypedArray<PackedByteArray> Arrays::encode_owed(
    const RID &p_table,
    int p_peer,
    int p_budget
) {
    NETW_STAGE(SEND, "Table encode owed", colors::TABLE);
    TypedArray<PackedByteArray> out;
    ArrayTable *held = table_of(p_table);
    if (held == nullptr || !held->schema.sealed || wire_id(p_table) == 0) {
        return out;
    }
    const uint32_t rows = held->row_count();
    if (rows == 0) {
        return out;
    }
    ArrayPeer &peer = peer_of(*held, p_peer);
    picked.clear();
    for (uint32_t word = 0; word < peer.owed.words.size(); ++word) {
        uint64_t bits = peer.owed.words[word];
        while (bits != 0) {
            picked.push_back(word * 64 + Bits::lowest(bits));
            bits &= bits - 1;
        }
    }
    if (!held->reliable) {
        const uint32_t share = (rows + REFRESH_SENDS - 1) / REFRESH_SENDS;
        uint32_t refreshed = 0;
        uint32_t looked = 0;
        while (refreshed < share && looked < rows) {
            const uint32_t row = peer.refresh_at % rows;
            peer.refresh_at = (peer.refresh_at + 1) % rows;
            looked += 1;
            if (peer.owed.get(row)) {
                continue;
            }
            picked.push_back(row);
            refreshed += 1;
        }
    }
    peer.owed.clear();
    encode_rows(*held, p_budget, 0, picked, out);
    return out;
}

TypedArray<PackedByteArray> Arrays::encode_snapshot(
    const RID &p_table,
    int p_peer,
    int p_budget
) {
    TypedArray<PackedByteArray> out;
    ArrayTable *held = table_of(p_table);
    if (held == nullptr || !held->schema.sealed || wire_id(p_table) == 0) {
        return out;
    }
    ArrayPeer &peer = peer_of(*held, p_peer);
    peer.owed.clear();
    picked.resize(held->row_count());
    for (uint32_t at = 0; at < picked.size(); ++at) {
        picked[at] = at;
    }
    encode_rows(*held, p_budget, FLAG_SNAPSHOT, picked, out);
    return out;
}

TypedArray<PackedByteArray> Arrays::encode_routes_only(
    int p_id,
    int p_hash,
    const PackedInt64Array &p_routes,
    int64_t p_tick,
    int p_budget
) {
    TypedArray<PackedByteArray> out;
    if (p_routes.is_empty()) {
        return out;
    }
    const int64_t room
        = MAX(int64_t(1), int64_t(p_budget) - FRAME_OVERHEAD_BYTES);
    const int per_frame = int(MAX(int64_t(1), room / 5));
    int at = 0;
    while (at < p_routes.size()) {
        const int count = MIN(per_frame, int(p_routes.size()) - at);
        wire::WriteStream stream;
        ArrayFrameHead head;
        head.table_id = uint64_t(p_id);
        head.schema_hash = uint64_t(uint16_t(p_hash));
        head.tick = uint64_t(MAX(p_tick, int64_t(0)));
        head.flags = uint64_t(FLAG_REMOVE);
        head.rows = uint64_t(count);
        if (!ArrayFrameHead::wire.run(stream, head)) {
            return TypedArray<PackedByteArray>();
        }
        for (int row = 0; row < count; ++row) {
            uint64_t route = uint64_t(MAX(p_routes[at + row], int64_t(0)));
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

TypedArray<PackedByteArray> Arrays::encode_removal(
    const RID &p_table,
    const PackedInt64Array &p_routes,
    int64_t p_tick,
    int p_budget
) {
    const ArrayTable *held = table_of(p_table);
    if (held == nullptr || !held->schema.sealed) {
        return TypedArray<PackedByteArray>();
    }
    return encode_routes_only(
        wire_id(p_table),
        held->schema.shape_hash,
        p_routes,
        p_tick,
        p_budget
    );
}

TypedArray<PackedByteArray> Arrays::encode_lifecycle(
    const PackedInt64Array &p_routes,
    int64_t p_tick,
    int p_budget
) {
    return encode_routes_only(LIFECYCLE_STREAM, 0, p_routes, p_tick, p_budget);
}

bool Arrays::peek_header(
    const PackedByteArray &p_payload,
    ArrayFrameHead &r_head
) {
    wire::ReadStream stream(p_payload);
    return ArrayFrameHead::wire.run(stream, r_head);
}

Error Arrays::admit_header(const PackedByteArray &p_payload) {
    ArrayFrameHead head;
    if (!peek_header(p_payload, head)) {
        drops_truncated += 1;
        return ERR_INVALID_DATA;
    }
    const int flags = int(head.flags);
    if (flags & ~int(FLAGS_IMPLEMENTED)) {
        drops_unknown_flag += 1;
        return ERR_INVALID_DATA;
    }
    const int id = int(head.table_id);
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
    if (int64_t(head.schema_hash) != schema_hash(table)) {
        drops_schema += 1;
        return ERR_INVALID_DATA;
    }
    return OK;
}

bool Arrays::decode_column(
    wire::ReadStream &r_stream,
    ArrayColumn &r_column,
    int p_rows
) const {
    if (!r_stream.align_verify()) {
        return false;
    }
    const int count = p_rows * r_column.stride;
    r_column.landing.resize(uint32_t(p_rows) * r_column.row_bytes);
    if (count <= 0) {
        return true;
    }
    uint8_t *write = r_column.landing.ptr();
    const uint32_t step = r_column.element_bytes;
    if (r_column.quantizer.is_valid()) {
        const Variant::Type element = static_cast<Variant::Type>(
            SchemaCore::element_type(r_column.type)
        );
        const int width = r_column.quantizer->bit_width(element);
        const int parts = r_column.quantizer->stride(element);
        PackedInt64Array codes;
        codes.resize(parts);
        for (int at = 0; at < count; ++at) {
            for (int part = 0; part < parts; ++part) {
                uint64_t code = 0;
                if (!r_stream.bits(code, width)) {
                    return false;
                }
                codes.set(part, int64_t(code));
            }
            store_value(
                r_column.type,
                write + uint64_t(at) * step,
                r_column.quantizer->decode(codes, element)
            );
        }
        return true;
    }
    switch (r_column.type) {
        case SchemaCore::ENTITY: {
            for (int at = 0; at < count; ++at) {
                uint64_t route = 0;
                if (!r_stream.varuint(route, 5)) {
                    return false;
                }
                put(write + uint64_t(at) * step, int64_t(route));
            }
            return true;
        }
        case SchemaCore::BOOL: {
            for (int at = 0; at < count; ++at) {
                bool set = false;
                if (!r_stream.bool1(set)) {
                    return false;
                }
                write[at] = set ? 1 : 0;
            }
            return true;
        }
        case SchemaCore::I8:
        case SchemaCore::U8:
        case SchemaCore::I16:
        case SchemaCore::U16: {
            const bool byte = r_column.type == SchemaCore::I8
                || r_column.type == SchemaCore::U8;
            const int width = byte ? 8 : 16;
            const bool is_signed = r_column.type == SchemaCore::I8
                || r_column.type == SchemaCore::I16;
            for (int at = 0; at < count; ++at) {
                uint64_t packed = 0;
                if (!r_stream.bits(packed, width)) {
                    return false;
                }
                put(write + uint64_t(at) * step,
                    int32_t(narrow(int64_t(packed), width, is_signed)));
            }
            return true;
        }
        default:
            return r_stream.raw_span(write, int64_t(count) * step);
    }
}

ArrayLanding Arrays::apply_frame(const PackedByteArray &p_payload) {
    NETW_STAGE(APPLY, "Table apply frame", colors::TABLE);
    ArrayLanding out;
    wire::ReadStream stream(p_payload);
    ArrayFrameHead head;
    if (!ArrayFrameHead::wire.run(stream, head)) {
        drops_truncated += 1;
        return out;
    }
    const int id = int(head.table_id);
    const int64_t tick = int64_t(head.tick);
    const int flags = int(head.flags);
    const int rows = int(head.rows);
    out.tick = tick;
    frame_routes.resize(MAX(rows, 0));
    for (int at = 0; at < rows; ++at) {
        uint64_t route = 0;
        if (!stream.varuint(route, 5) || route == 0) {
            drops_truncated += 1;
            return out;
        }
        frame_routes.set(at, int64_t(route));
    }
    if (id == LIFECYCLE_STREAM) {
        retire_routes(frame_routes, true);
        out.retired = frame_routes;
        out.verdict = OK;
        return out;
    }
    const RID rid = table_from_wire_id(id);
    ArrayTable *held = table_of(rid);
    if (held == nullptr) {
        drops_unknown += 1;
        out.verdict = ERR_DOES_NOT_EXIST;
        return out;
    }
    out.table = rid;
    if (held->tick >= 0 && tick < held->tick - REORDER_WINDOW_TICKS) {
        drops_stale += 1;
        out.verdict = ERR_SKIP;
        return out;
    }
    if (flags & FLAG_REMOVE) {
        open_wave(*held);
        apply_removal(*held, frame_routes.ptr(), rows, tick, false);
        held->tick = MAX(held->tick, tick);
        out.verdict = OK;
        return out;
    }
    for (ArrayColumn &column : held->columns) {
        if (!decode_column(stream, column, rows)) {
            drops_truncated += 1;
            return out;
        }
    }
    if (!stream.align_verify() || stream.bits_remaining() != 0) {
        drops_truncated += 1;
        return out;
    }
    open_wave(*held);
    if (flags & FLAG_SNAPSHOT) {
        clear_rows(*held);
    }
    apply_upsert(*held, frame_routes, tick, out.bound);
    held->tick = MAX(held->tick, tick);
    prune_memos(*held, tick);
    out.verdict = OK;
    return out;
}

void Arrays::open_wave(ArrayTable &r_table) {
    if (r_table.wave_touched) {
        return;
    }
    r_table.wave_touched = true;
    r_table.births = PackedInt64Array();
    r_table.deaths = PackedInt64Array();
}

void Arrays::clear_rows(ArrayTable &r_table) {
    r_table.revision += 1;
    for (const uint32_t slot : r_table.slots) {
        leave_slot(slot);
    }
    r_table.row_of.clear();
    r_table.removal_memos.clear();
    resize_rows(r_table, 0);
    touch_columns(r_table);
}

void Arrays::swap_remove_row(
    ArrayTable &r_table,
    int64_t p_route,
    uint32_t p_row
) {
    r_table.revision += 1;
    const uint32_t last = r_table.row_count() - 1;
    const int64_t moved = r_table.routes[last];
    leave_slot(r_table.slots[p_row]);
    r_table.slots[p_row] = r_table.slots[last];
    r_table.routes[p_row] = moved;
    r_table.row_ticks[p_row] = r_table.row_ticks[last];
    r_table.changed_ticks[p_row] = r_table.changed_ticks[last];
    r_table.born_sends[p_row] = r_table.born_sends[last];
    for (ArrayColumn &column : r_table.columns) {
        if (p_row != last) {
            memcpy(column.row(p_row), column.row(last), column.row_bytes);
        }
    }
    for (ArrayPeer &peer : r_table.peers) {
        peer.owed.set(p_row, peer.owed.get(last));
        peer.acc[p_row] = peer.acc[last];
    }
    r_table.row_of.erase(p_route);
    if (moved != p_route) {
        r_table.row_of[moved] = p_row;
    }
    resize_rows(r_table, last);
    touch_columns(r_table);
}

void Arrays::apply_removal(
    ArrayTable &r_table,
    const int64_t *p_routes,
    int p_count,
    int64_t p_tick,
    bool p_publish
) {
    for (int at = 0; at < p_count; ++at) {
        const int64_t route = p_routes[at];
        r_table.removal_memos[route] = p_tick;
        const uint32_t *row = r_table.row_of.getptr(route);
        if (row == nullptr) {
            continue;
        }
        if (p_publish && r_table.born_sends[*row] < r_table.sends) {
            r_table.unpublished.push_back(route);
        }
        swap_remove_row(r_table, route, *row);
        r_table.deaths.push_back(route);
    }
}

void Arrays::apply_upsert(
    ArrayTable &r_table,
    const PackedInt64Array &p_routes,
    int64_t p_tick,
    PackedInt64Array &r_bound
) {
    r_table.revision += 1;
    bool touched = false;
    for (int at = 0; at < p_routes.size(); ++at) {
        const int64_t route = p_routes[at];
        if (tombstones.has(route)) {
            drops_tombstone += 1;
            continue;
        }
        const int64_t *memo = r_table.removal_memos.getptr(route);
        if (memo != nullptr && *memo > p_tick) {
            drops_stale_row += 1;
            continue;
        }
        const uint32_t *found = r_table.row_of.getptr(route);
        uint32_t row = found != nullptr ? *found : NO_ROW;
        if (row == NO_ROW) {
            row = r_table.row_count();
            resize_rows(r_table, row + 1);
            r_table.routes[row] = route;
            r_table.slots[row] = enter_slot(route);
            r_table.row_of[route] = row;
            r_table.row_ticks[row] = p_tick;
            r_table.changed_ticks[row] = p_tick;
            r_table.born_sends[row] = r_table.sends;
            r_table.births.push_back(route);
            r_bound.push_back(route);
            r_table.routes_viewed = false;
        } else if (r_table.row_ticks[row] > p_tick) {
            drops_stale_row += 1;
            continue;
        } else {
            r_table.row_ticks[row] = p_tick;
        }
        for (ArrayColumn &column : r_table.columns) {
            memcpy(
                column.row(row),
                column.landing.ptr() + uint64_t(at) * column.row_bytes,
                column.row_bytes
            );
        }
        touched = true;
    }
    if (touched) {
        touch_columns(r_table);
    }
}

void Arrays::prune_memos(ArrayTable &r_table, int64_t p_tick) {
    if (r_table.removal_memos.size() < 64) {
        return;
    }
    const int64_t cutoff = p_tick - MEMO_AGE_TICKS;
    LocalVector<int64_t> expired;
    for (const KeyValue<int64_t, int64_t> &entry : r_table.removal_memos) {
        if (entry.value < cutoff) {
            expired.push_back(entry.key);
        }
    }
    for (const int64_t route : expired) {
        r_table.removal_memos.erase(route);
    }
}

void Arrays::begin_intake() {
    for (ArrayTable *held : tables) {
        held->wave_touched = false;
    }
}

TypedArray<RID> Arrays::touched_tables() const {
    TypedArray<RID> out;
    for (uint32_t order = 0; order < wire_order.size(); ++order) {
        const ArrayTable *held = tables[wire_order[order]];
        if (held->wave_touched) {
            out.push_back(held->rid);
        }
    }
    return out;
}

void Arrays::count_bad_sender() {
    drops_bad_sender += 1;
}

void Arrays::retire_routes(const PackedInt64Array &p_routes, bool p_as_wave) {
    for (int at = 0; at < p_routes.size(); ++at) {
        tombstones.insert(p_routes[at]);
    }
    for (ArrayTable *held : tables) {
        LocalVector<int64_t> present;
        for (int at = 0; at < p_routes.size(); ++at) {
            if (held->row_of.has(p_routes[at])) {
                present.push_back(p_routes[at]);
            }
        }
        if (present.is_empty()) {
            continue;
        }
        if (p_as_wave) {
            open_wave(*held);
        }
        apply_removal(
            *held,
            present.ptr(),
            int(present.size()),
            MAX(held->tick, int64_t(0)),
            false
        );
        for (const int64_t route : present) {
            for (uint32_t at = held->unpublished.size(); at > 0; --at) {
                if (held->unpublished[at - 1] == route) {
                    held->unpublished.remove_at(at - 1);
                }
            }
        }
    }
}

void Arrays::queue_lifecycle_removals(const PackedInt64Array &p_routes) {
    pending_lifecycle.append_array(p_routes);
}

PackedInt64Array Arrays::take_lifecycle_removals() {
    const PackedInt64Array out = pending_lifecycle;
    pending_lifecycle = PackedInt64Array();
    return out;
}

PackedInt64Array Arrays::lifecycle_removals() const {
    return pending_lifecycle;
}

bool Arrays::is_tombstoned(int64_t p_route) const {
    return tombstones.has(p_route);
}

void Arrays::clear_session() {
    pending_lifecycle = PackedInt64Array();
    tombstones.clear();
    for (ArrayTable *held : tables) {
        held->wave_touched = false;
        clear_rows(*held);
        held->births = PackedInt64Array();
        held->deaths = PackedInt64Array();
        held->tick = -1;
        held->dirty = false;
        held->unpublished.clear();
        held->peers.clear();
        held->sends = 1;
        held->pending_routes = PackedInt64Array();
        held->routes_written = false;
        for (ArrayColumn &column : held->columns) {
            column.pending = Variant();
            column.written = false;
            column.fresh = false;
        }
    }
}

Dictionary Arrays::counters() const {
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

Dictionary Arrays::frame_spec_records() {
    Dictionary out;
    out["TableFrameHead"] = ArrayFrameHead::wire.spec_dump();
    return out;
}

} // namespace netw::store

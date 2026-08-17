#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/bit_buffer.hpp"
#include "netw/table/schema_core.hpp"

namespace netw {

class TableCore : public godot::RefCounted {
    GDCLASS(TableCore, godot::RefCounted)

public:
    enum {
        FLAG_SNAPSHOT = 1 << 0,
        FLAG_REMOVE = 1 << 1,
        FLAG_PAIR_KEY = 1 << 2,
        FLAG_NO_KEY = 1 << 3,
        FLAGS_IMPLEMENTED = FLAG_SNAPSHOT | FLAG_REMOVE,
        LIFECYCLE_STREAM = 0,
        FRAME_OVERHEAD_BYTES = 32,
    };

    static int wire_bits(int type);

private:
    struct ColumnShape {
        godot::StringName key;
        int type = SchemaCore::VARIANT;
        int stride = 1;
        godot::Ref<NetwQuantize> quantizer;
        int element_bits = 0;
    };

    struct Record {
        godot::Ref<SchemaRecord> schema;
        godot::LocalVector<ColumnShape> shapes;
        bool reliable = false;

        godot::PackedInt64Array routes;
        godot::LocalVector<godot::Variant> columns_data;
        int64_t tick = -1;
        godot::PackedInt64Array births;
        godot::PackedInt64Array deaths;
        bool dirty = false;

        godot::HashMap<int64_t, int> row_of;
        godot::LocalVector<int64_t> row_ticks;
        godot::HashMap<int64_t, int64_t> removal_memos;

        godot::PackedInt64Array pending_routes;
        godot::LocalVector<godot::Variant> pending_columns;
        godot::LocalVector<bool> pending_written;
        bool pending_routes_written = false;

        bool wave_touched = false;
        godot::HashSet<int64_t> published;
    };

    godot::HashMap<godot::RID, Record> tables;
    godot::PackedInt64Array pending_lifecycle;
    godot::HashSet<int64_t> tombstones;

    godot::LocalVector<godot::RID> wire_order;
    godot::HashMap<godot::RID, int> wire_id_of;

    int64_t drops_unknown = 0;
    int64_t drops_schema = 0;
    int64_t drops_unknown_flag = 0;
    int64_t drops_truncated = 0;
    int64_t drops_bad_sender = 0;
    int64_t drops_stale = 0;
    int64_t drops_tombstone = 0;
    int64_t drops_stale_row = 0;

    Record *record_of(const godot::RID &table);
    const Record *record_of(const godot::RID &table) const;
    const ColumnShape *shape_at(const godot::RID &table, int column) const;

    void rebuild_wire_order();
    void clear_rows(Record &record);
    void apply_wave(
        Record &record,
        const godot::PackedInt64Array &routes,
        godot::LocalVector<godot::Variant> &buffers,
        int64_t tick
    );
    void open_wave(Record &record);
    void apply_removal(
        Record &record,
        const godot::PackedInt64Array &routes,
        int64_t tick
    );
    godot::PackedInt64Array apply_upsert(
        Record &record,
        const godot::PackedInt64Array &routes,
        const godot::LocalVector<godot::Variant> &values,
        int64_t tick
    );
    void swap_remove_row(Record &record, int64_t route, int row);
    void prune_memos(Record &record, int64_t tick);
    int rows_per_frame(const Record &record, int budget) const;
    godot::PackedByteArray encode_frame(
        const Record &record,
        int id,
        int flags,
        const godot::PackedInt64Array &routes,
        int first,
        int rows
    ) const;
    void encode_column(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const ColumnShape &shape,
        const godot::Variant &data,
        int first,
        int rows
    ) const;
    godot::Variant decode_column(
        const godot::Ref<NetwBitBufferReader> &reader,
        const ColumnShape &shape,
        int rows
    ) const;
    godot::PackedInt64Array read_frame_routes(
        const godot::Ref<NetwBitBufferReader> &reader,
        int rows
    );
    static godot::TypedArray<godot::PackedByteArray> encode_routes_only(
        int id,
        int hash,
        const godot::PackedInt64Array &routes,
        int64_t tick,
        int budget
    );

protected:
    static void _bind_methods();

public:
    godot::Error declare(
        const godot::RID &table,
        const godot::Ref<SchemaRecord> &schema
    );
    void set_reliable(const godot::RID &table, bool value);
    bool is_valid(const godot::RID &table) const;
    godot::StringName name_of(const godot::RID &table) const;
    int schema_hash(const godot::RID &table) const;
    int column_count(const godot::RID &table) const;
    godot::StringName column_key(const godot::RID &table, int column) const;
    int column_type(const godot::RID &table, int column) const;
    int column_stride(const godot::RID &table, int column) const;
    godot::Ref<NetwQuantize> column_quantizer(
        const godot::RID &table,
        int column
    ) const;
    bool is_reliable(const godot::RID &table) const;
    int wire_id(const godot::RID &table) const;
    godot::RID table_from_wire_id(int id) const;

    godot::Error write_routes(
        const godot::RID &table,
        const godot::PackedInt64Array &routes
    );
    godot::Error write_column(
        const godot::RID &table,
        int column,
        const godot::Variant &data
    );
    godot::Error commit(const godot::RID &table, int64_t tick);
    void clear_dirty(const godot::RID &table);
    godot::TypedArray<godot::RID> dirty_tables() const;
    godot::TypedArray<godot::RID> published_tables() const;
    godot::PackedInt64Array take_pending_removals(const godot::RID &table);

    godot::PackedInt64Array read_routes(const godot::RID &table) const;
    godot::Variant read_column(const godot::RID &table, int column) const;
    godot::PackedInt64Array read_births(const godot::RID &table) const;
    godot::PackedInt64Array read_deaths(const godot::RID &table) const;
    int row_of(const godot::RID &table, int64_t route) const;
    godot::PackedInt32Array rows_of(
        const godot::RID &table,
        const godot::PackedInt64Array &routes
    ) const;
    int64_t tick_of(const godot::RID &table) const;

    godot::TypedArray<godot::PackedByteArray> encode_frames(
        const godot::RID &table,
        int budget,
        bool snapshot
    );
    godot::TypedArray<godot::PackedByteArray> encode_removal(
        const godot::RID &table,
        const godot::PackedInt64Array &routes,
        int64_t tick,
        int budget
    );
    static godot::TypedArray<godot::PackedByteArray> encode_lifecycle(
        const godot::PackedInt64Array &routes,
        int64_t tick,
        int budget
    );
    static godot::Dictionary peek_header(const godot::PackedByteArray &payload);
    godot::Error admit_header(const godot::Dictionary &header);
    godot::Dictionary apply_frame(const godot::PackedByteArray &payload);
    void retire_routes(const godot::PackedInt64Array &routes, bool as_wave);
    void count_bad_sender();
    void begin_intake();
    godot::TypedArray<godot::RID> touched_tables() const;

    void queue_lifecycle_removals(const godot::PackedInt64Array &routes);
    godot::PackedInt64Array take_lifecycle_removals();
    godot::PackedInt64Array lifecycle_removals() const;

    bool is_tombstoned(int64_t route) const;

    void clear_session();
    godot::Dictionary counters() const;
};

} // namespace netw

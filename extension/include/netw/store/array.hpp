#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/store/column.hpp"
#include "netw/store/peers.hpp"
#include "netw/wire/describe.hpp"
#include "netw/wire/stream.hpp"

namespace netw::store {

class World;

struct ArrayFrameHead {
    uint64_t table_id = 0;
    uint64_t schema_hash = 0;
    uint64_t tick = 0;
    uint64_t flags = 0;
    uint64_t rows = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ArrayFrameHead::table_id>(
            "table_id",
            netw::wire::varuint(2)
        ),
        netw::wire::field<&ArrayFrameHead::schema_hash>(
            "schema_hash",
            netw::wire::bits(16)
        ),
        netw::wire::field<&ArrayFrameHead::tick>(
            "tick",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ArrayFrameHead::flags>(
            "flags",
            netw::wire::bits(8)
        ),
        netw::wire::field<&ArrayFrameHead::rows>(
            "rows",
            netw::wire::varuint(2)
        )
    );
};

struct ArrayColumn {
    Column form;
    godot::StringName key;
    int type = SchemaCore::VARIANT;
    int stride = 1;
    godot::Ref<NetwQuantize> quantizer;
    int element_bits = 0;
    uint32_t element_bytes = 0;
    uint32_t row_bytes = 0;
    godot::LocalVector<uint8_t> bytes;
    godot::LocalVector<uint8_t> landing;
    godot::Variant pending;
    bool written = false;
    bool fresh = false;
    mutable godot::Variant view;
    mutable bool viewed = false;

    const uint8_t *row(uint32_t p_row) const {
        return bytes.ptr() + uint64_t(p_row) * row_bytes;
    }

    uint8_t *row(uint32_t p_row) {
        return bytes.ptr() + uint64_t(p_row) * row_bytes;
    }
};

struct ArrayPeer {
    int id = 0;
    Bits owed;
    godot::LocalVector<float> acc;
    uint32_t refresh_at = 0;
};

struct ArrayLanding {
    godot::Error verdict = godot::ERR_INVALID_DATA;
    godot::RID table;
    int64_t tick = -1;
    godot::PackedInt64Array bound;
    godot::PackedInt64Array retired;
};

struct ArrayTable {
    table::SchemaRecord schema;
    godot::RID rid;
    bool reliable = false;
    int64_t row_bits = 0;
    godot::LocalVector<ArrayColumn> columns;

    godot::LocalVector<int64_t> routes;
    godot::LocalVector<uint32_t> slots;
    godot::LocalVector<int64_t> row_ticks;
    godot::LocalVector<int64_t> changed_ticks;
    godot::LocalVector<uint32_t> born_sends;
    godot::HashMap<int64_t, uint32_t> row_of;
    godot::HashMap<int64_t, int64_t> removal_memos;
    godot::PackedInt64Array births;
    godot::PackedInt64Array deaths;
    mutable godot::PackedInt64Array routes_view;
    mutable bool routes_viewed = false;

    godot::PackedInt64Array pending_routes;
    bool routes_written = false;

    int64_t tick = -1;
    int64_t revision = 0;
    uint32_t sends = 1;
    bool dirty = false;
    bool wave_touched = false;
    godot::LocalVector<int64_t> unpublished;
    godot::LocalVector<ArrayPeer> peers;

    uint32_t row_count() const {
        return routes.size();
    }
};

class Arrays {
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

    static int wire_bits(int p_type);

    static uint32_t element_bytes(int p_type);

private:
    World *world = nullptr;
    godot::LocalVector<ArrayTable *> tables;
    godot::HashMap<godot::RID, uint32_t> by_rid;
    godot::LocalVector<uint32_t> wire_order;
    godot::PackedInt64Array pending_lifecycle;
    godot::HashSet<int64_t> tombstones;
    godot::LocalVector<uint32_t> picked;
    godot::LocalVector<uint32_t> remap;
    godot::PackedInt64Array frame_routes;

    int64_t drops_unknown = 0;
    int64_t drops_schema = 0;
    int64_t drops_unknown_flag = 0;
    int64_t drops_truncated = 0;
    int64_t drops_bad_sender = 0;
    int64_t drops_stale = 0;
    int64_t drops_tombstone = 0;
    int64_t drops_stale_row = 0;

    ArrayTable *table_of(const godot::RID &p_table);
    const ArrayTable *table_of(const godot::RID &p_table) const;
    const ArrayColumn *column_at(const godot::RID &p_table, int p_column)
        const;

    void rebuild_wire_order();
    void clear_rows(ArrayTable &r_table);
    void open_wave(ArrayTable &r_table);
    void resize_rows(ArrayTable &r_table, uint32_t p_rows);
    uint32_t enter_slot(int64_t p_route);
    void leave_slot(uint32_t p_slot);
    void mark_changed(ArrayTable &r_table, uint32_t p_row, int64_t p_tick);
    void swap_remove_row(ArrayTable &r_table, int64_t p_route, uint32_t p_row);
    void apply_removal(
        ArrayTable &r_table,
        const int64_t *p_routes,
        int p_count,
        int64_t p_tick,
        bool p_publish
    );
    void apply_upsert(
        ArrayTable &r_table,
        const godot::PackedInt64Array &p_routes,
        int64_t p_tick,
        godot::PackedInt64Array &r_bound
    );
    void prune_memos(ArrayTable &r_table, int64_t p_tick);
    godot::Error diff_same_routes(ArrayTable &r_table, int64_t p_tick);
    void relayout(ArrayTable &r_table, int64_t p_tick);
    int rows_per_frame(const ArrayTable &p_table, int p_budget) const;
    bool encode_column(
        wire::WriteStream &r_stream,
        const ArrayColumn &p_column,
        const uint32_t *p_rows,
        int p_count
    ) const;
    bool decode_column(
        wire::ReadStream &r_stream,
        ArrayColumn &r_column,
        int p_rows
    ) const;
    godot::PackedByteArray encode_frame(
        const ArrayTable &p_table,
        int p_flags,
        const uint32_t *p_rows,
        int p_count
    );
    void encode_rows(
        const ArrayTable &p_table,
        int p_budget,
        int p_flags,
        const godot::LocalVector<uint32_t> &p_rows,
        godot::TypedArray<godot::PackedByteArray> &r_frames
    );
    static godot::TypedArray<godot::PackedByteArray> encode_routes_only(
        int p_id,
        int p_hash,
        const godot::PackedInt64Array &p_routes,
        int64_t p_tick,
        int p_budget
    );

public:
    Arrays() = default;
    Arrays(const Arrays &) = delete;
    Arrays &operator=(const Arrays &) = delete;
    ~Arrays();

    void seat(World *p_world) {
        world = p_world;
    }

    void forget_slots();
    bool holds_route(int64_t p_route) const;

    godot::Error declare(
        const godot::RID &p_table,
        const table::SchemaRecord *p_schema
    );
    void set_reliable(const godot::RID &p_table, bool p_value);
    bool is_valid(const godot::RID &p_table) const;
    bool is_reliable(const godot::RID &p_table) const;
    godot::StringName name_of(const godot::RID &p_table) const;
    int schema_hash(const godot::RID &p_table) const;
    int column_count(const godot::RID &p_table) const;
    int wire_id(const godot::RID &p_table) const;
    godot::RID table_from_wire_id(int p_id) const;

    godot::Error write_routes(
        const godot::RID &p_table,
        const godot::PackedInt64Array &p_routes
    );
    godot::Error write_column(
        const godot::RID &p_table,
        int p_column,
        const godot::Variant &p_data
    );
    godot::Error commit(const godot::RID &p_table, int64_t p_tick);
    godot::Error replace_rows(
        const godot::RID &p_table,
        const godot::PackedInt64Array &p_routes,
        const godot::LocalVector<godot::Variant> &p_columns,
        int64_t p_tick
    );

    void clear_dirty(const godot::RID &p_table);
    godot::TypedArray<godot::RID> dirty_tables() const;
    godot::TypedArray<godot::RID> published_tables() const;
    godot::PackedInt64Array take_pending_removals(const godot::RID &p_table);
    bool owes(const godot::RID &p_table, int p_peer, int64_t p_route) const;

    godot::PackedInt64Array read_routes(const godot::RID &p_table) const;
    godot::Variant read_column(const godot::RID &p_table, int p_column) const;
    godot::PackedInt64Array read_births(const godot::RID &p_table) const;
    godot::PackedInt64Array read_deaths(const godot::RID &p_table) const;
    int64_t revision(const godot::RID &p_table) const;
    bool holds_elsewhere(const godot::RID &p_table, int64_t p_route) const;
    int row_of(const godot::RID &p_table, int64_t p_route) const;
    godot::PackedInt32Array rows_of(
        const godot::RID &p_table,
        const godot::PackedInt64Array &p_routes
    ) const;
    int64_t tick_of(const godot::RID &p_table) const;

    godot::TypedArray<godot::PackedByteArray> encode_owed(
        const godot::RID &p_table,
        int p_peer,
        int p_budget
    );
    godot::TypedArray<godot::PackedByteArray> encode_snapshot(
        const godot::RID &p_table,
        int p_peer,
        int p_budget
    );
    godot::TypedArray<godot::PackedByteArray> encode_removal(
        const godot::RID &p_table,
        const godot::PackedInt64Array &p_routes,
        int64_t p_tick,
        int p_budget
    );
    static godot::TypedArray<godot::PackedByteArray> encode_lifecycle(
        const godot::PackedInt64Array &p_routes,
        int64_t p_tick,
        int p_budget
    );
    void forget_peers_except(const godot::PackedInt32Array &p_peers);

    uint32_t table_count() const {
        return tables.size();
    }

    ArrayTable &table_at(uint32_t p_index) {
        return *tables[p_index];
    }

    const ArrayTable &table_at(uint32_t p_index) const {
        return *tables[p_index];
    }

    ArrayPeer &peer_of(ArrayTable &r_table, int p_peer);
    void refresh_rows(
        ArrayTable &r_table,
        ArrayPeer &r_peer,
        godot::LocalVector<uint32_t> &r_rows
    ) const;
    int64_t frame_head_bytes() const {
        return 8;
    }
    void encode_picked(
        ArrayTable &r_table,
        ArrayPeer &r_peer,
        const godot::LocalVector<uint32_t> &p_rows,
        int p_budget,
        godot::TypedArray<godot::PackedByteArray> &r_frames
    );

    static bool peek_header(
        const godot::PackedByteArray &p_payload,
        ArrayFrameHead &r_head
    );
    godot::Error admit_header(const godot::PackedByteArray &p_payload);
    ArrayLanding apply_frame(const godot::PackedByteArray &p_payload);
    void retire_routes(const godot::PackedInt64Array &p_routes, bool p_as_wave);
    void count_bad_sender();
    void begin_intake();
    godot::TypedArray<godot::RID> touched_tables() const;

    void queue_lifecycle_removals(const godot::PackedInt64Array &p_routes);
    godot::PackedInt64Array take_lifecycle_removals();
    godot::PackedInt64Array lifecycle_removals() const;
    bool is_tombstoned(int64_t p_route) const;

    void clear_session();
    godot::Dictionary counters() const;
    static godot::Dictionary frame_spec_records();
};

} // namespace netw::store

#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/store/array.hpp"
#include "netw/store/column.hpp"
#include "netw/store/entity_ref.hpp"
#include "netw/store/peers.hpp"
#include "netw/store/port.hpp"
#include "netw/store/resident.hpp"
#include "netw/store/table.hpp"
#include "netw/store/validate.hpp"
#include "netw/wire/stream_book.hpp"

namespace netw::store {

static_assert(EntityRef::NONE == Table::NO_SLOT);

enum class Landing : uint8_t {
    ACCEPTED,
    DUPLICATE,
    STALE,
    BASELINE_UNKNOWN,
    MALFORMED,
};

struct RowAddress {
    uint32_t table = Table::NO_SLOT;
    uint32_t row = Table::NO_SLOT;

    bool valid() const {
        return table != Table::NO_SLOT && row != Table::NO_SLOT;
    }
};

class World {
    struct TableName {
        uint32_t index = 0;
    };

    struct SetTables {
        uint32_t family[3] = {Table::NO_SLOT, Table::NO_SLOT, Table::NO_SLOT};
    };

    struct OwnerRows {
        RowAddress lane[2];
    };

    godot::LocalVector<int64_t> slot_route;
    godot::LocalVector<int64_t> slot_epoch;
    godot::LocalVector<uint32_t> slot_gen;
    godot::LocalVector<uint32_t> slot_rows;
    godot::LocalVector<float> slot_weight;
    godot::LocalVector<uint8_t> slot_marks;
    godot::LocalVector<uint64_t> slot_tenure;
    godot::LocalVector<int64_t> slot_tenure_tick;
    godot::LocalVector<int64_t> slot_predict;
    godot::LocalVector<int64_t> slot_lagcomp;
    godot::LocalVector<uint32_t> free_slots;
    godot::HashMap<int64_t, uint32_t> route_slot;

    godot::LocalVector<Table *> tables;
    godot::HashMap<uint64_t, SetTables> tables_by_set;
    godot::HashMap<uint64_t, OwnerRows> owner_rows;
    godot::HashMap<uint64_t, uint32_t> body_rows;
    mutable godot::RID_Owner<TableName> table_names;

    godot::LocalVector<PeerSide> sides;
    godot::HashMap<int, uint32_t> peer_index;
    godot::LocalVector<uint32_t> free_peers;
    godot::HashMap<uint64_t, RowAddress> lane_rows;

    Bits live;
    Bits stale;
    godot::LocalVector<uint32_t> stale_slots;
    godot::LocalVector<uint64_t> slot_stamps;
    uint64_t next_stamp = 0;

    static constexpr uint32_t MISSES_KEPT = 64;

    wire::CodeRow probe;
    godot::LocalVector<wire::CodeRow> spare_images;

    using Stamp = void (*)(void *p_context, uint32_t p_table, uint32_t p_row);
    Stamp stamper = nullptr;
    void *stamp_context = nullptr;

    uint32_t acquire_slot(int64_t p_route, int64_t p_epoch);
    uint32_t claim_slot(int64_t p_route, int64_t p_epoch);
    void release_slot(uint32_t p_slot);
    void leave_slot(uint32_t p_slot);
    void grow_slot_bits(uint32_t p_count);
    void fresh_slot_bits(uint32_t p_slot);
    void write_bit(Bits &r_bits, uint32_t p_at, bool p_on);

    uint64_t lane_key(const Table &p_table, uint32_t p_row) const;
    void forget_lane(const RowAddress &p_at);
    void forget_side(PeerSide &r_side);
    void trim_departed();
    void owe_changed(Table &p_table, uint32_t p_row);
    void count_probe(const Table &p_table, uint32_t p_row, bool p_reads);
    void close_row(const RowAddress &p_at, uint32_t p_peer, Stream p_stream);
    void close_life(const RowAddress &p_at);
    bool stream_row(
        int p_peer,
        const wire::StreamLane &p_lane,
        uint32_t &r_peer,
        RowAddress &r_at
    ) const;
    godot::LocalVector<wire::ClosedLane> close_where(
        int64_t p_route,
        bool (*p_dooms)(const wire::StreamTenure &, const wire::StreamTenure &),
        const wire::StreamTenure &p_bar
    );

    const NodeCell &resident(Table &p_table, uint32_t p_row);
    bool readable(Table &p_table, uint32_t p_row, godot::Object *p_node);
    void note_rest(Table &p_table, uint32_t p_row, const NodeCell &p_cell);
    void follow_authority(
        Table &p_table,
        uint32_t p_row,
        godot::Object *p_node
    );
    void follow_rest(Table &p_table, uint32_t p_row, const BodyState &p_body);
    bool read_row(
        Table &p_table,
        uint32_t p_row,
        godot::Object *p_node,
        BodyState &p_body,
        bool p_body_columns,
        wire::CodeRow &r_codes,
        bool p_keep
    );
    bool read_exact(
        Table &p_table,
        godot::Object *p_node,
        BodyState &p_body,
        bool p_body_columns
    );
    bool pull_exact(
        Table &p_table,
        uint32_t p_row,
        godot::Object *p_node,
        BodyState &p_body,
        bool p_body_columns,
        int64_t p_tick
    );
    void pull_rows(
        Table &p_table,
        int64_t p_tick,
        bool p_custom,
        bool p_validating
    );
    void pull_row(
        Table &p_table,
        uint32_t p_row,
        int64_t p_tick,
        bool p_custom,
        bool p_validating,
        bool p_gated
    );
    void sweep(Table &p_table);
    void audit_quiet(Table &p_table, int64_t p_tick);
    void check_codes(
        Table &p_table,
        uint32_t p_row,
        godot::Object *p_node,
        BodyState &p_body,
        int64_t p_tick,
        const char *p_gate
    );
    void note_miss(
        const Table &p_table,
        uint32_t p_row,
        int p_column,
        const char *p_gate,
        int64_t p_tick
    );
    Landing land_frame(
        const RowAddress &p_at,
        int p_sender,
        const godot::PackedByteArray &p_frame,
        int64_t p_base_tick,
        bool p_windowed
    );

    godot::LocalVector<uint32_t> visiting;

public:
    static constexpr uint32_t SWEEP_TICKS = 30;

    Arrays arrays;
    Residents residents;
    int64_t validate_every = validation_interval();
    godot::LocalVector<Miss> misses;
    int64_t miss_count = 0;
    godot::HashSet<uint64_t> dozing;
    godot::LocalVector<uint64_t> woken;
    godot::LocalVector<uint64_t> reprobed;
    godot::HashSet<uint64_t> watched;

    void mark_working(const RowAddress &p_at) {
        if (p_at.valid()) {
            tables[p_at.table]->working.set(p_at.row, true);
        }
    }

#if defined(NETW_TESTS)
    struct PullWork {
        int64_t pulls = 0;
        int64_t visits = 0;
        int64_t swept = 0;
        int64_t rows = 0;
        int64_t rested = 0;
        int64_t body = 0;
        int64_t engine = 0;
        int64_t script = 0;
        int64_t custom = 0;
        int64_t validated = 0;
        int64_t changed = 0;
        int64_t coded = 0;
        int64_t allocations = 0;
    };
    PullWork work;

    struct InterestWork {
        int64_t evaluations = 0;
        int64_t writes = 0;
    };
    InterestWork interest_work;

    struct ReceiveWork {
        int64_t frames = 0;
        int64_t landed = 0;
        int64_t allocations = 0;
    };
    ReceiveWork receive_work;

    struct PushWork {
        int64_t pushes = 0;
        int64_t allocations = 0;
    };
    PushWork push_work;
#endif

    Landing admits(
        const RowAddress &p_at,
        int p_sender,
        const godot::PackedByteArray &p_frame,
        bool p_windowed,
        uint64_t &r_accepted,
        const wire::CodeRow *&r_baseline
    );

    Landing receive(
        const RowAddress &p_at,
        int p_sender,
        const godot::PackedByteArray &p_frame,
        int64_t p_base_tick,
        bool p_windowed
    );

    void forget_sender(int p_id);

    void pull(uint32_t p_table, int64_t p_tick, bool p_custom);

    bool read_live(
        const RowAddress &p_at,
        godot::LocalVector<godot::Variant> &r_values
    );

    godot::Array gather_rows(
        uint32_t p_table,
        const godot::PackedInt64Array &p_routes
    );

    void absorb(uint32_t p_table, const godot::Array &p_rows, int64_t p_tick);

    World() {
        arrays.seat(this);
    }
    World(const World &) = delete;
    World &operator=(const World &) = delete;
    ~World();

    void clear();

    void stamp_with(Stamp p_stamper, void *p_context) {
        stamper = p_stamper;
        stamp_context = p_context;
    }

    void stamp(uint32_t p_table, uint32_t p_row) {
        if (stamper != nullptr) {
            stamper(stamp_context, p_table, p_row);
        }
    }

    void unsource_all();

    void unsource_owner(uint64_t p_owner);

    EntityRef enter(int64_t p_route, int64_t p_epoch);

    void leave(const EntityRef &p_ref);

    EntityRef find(int64_t p_route, int64_t p_epoch) const;

    bool holds(const EntityRef &p_ref) const;

    void bind_predict(uint32_t p_slot, int64_t p_predict);

    void bind_lagcomp(uint32_t p_slot, int64_t p_lagcomp);

    int64_t predict_of(uint32_t p_slot) const {
        return p_slot < slot_predict.size() ? slot_predict[p_slot] : -1;
    }

    int64_t lagcomp_of(uint32_t p_slot) const {
        return p_slot < slot_lagcomp.size() ? slot_lagcomp[p_slot] : -1;
    }

    EntityRef ref_of(uint32_t p_slot) const;

    int64_t route_of(uint32_t p_slot) const;

    int64_t epoch_of(uint32_t p_slot) const;

    uint32_t live_slot_count() const;

    uint32_t slot_of_route(int64_t p_route) const;

    float weight_of(uint32_t p_slot) const {
        return p_slot < slot_weight.size() ? slot_weight[p_slot] : 1.0f;
    }

    void set_weight(uint32_t p_slot, float p_weight) {
        if (p_slot < slot_weight.size()) {
            slot_weight[p_slot] = p_weight;
        }
    }

    uint8_t marks_of(uint32_t p_slot) const {
        return p_slot < slot_marks.size() ? slot_marks[p_slot] : 0;
    }

    void set_marks(uint32_t p_slot, uint8_t p_marks) {
        if (p_slot < slot_marks.size()) {
            slot_marks[p_slot] = p_marks;
        }
    }

    int64_t note_tenure(uint32_t p_slot, uint64_t p_tenure, int64_t p_tick);

    void stale_slot(uint32_t p_slot);

    void stale_route(int64_t p_route);

    void stale_all();

    bool slot_stale(uint32_t p_slot) const {
        return p_slot < stale.count && stale.get(p_slot);
    }

    void take_stale(godot::LocalVector<uint32_t> &r_slots);

    void keep_stale(uint32_t p_slot) {
        stale_slots.push_back(p_slot);
    }

    uint64_t slot_stamp(uint32_t p_slot) const {
        return p_slot < slot_stamps.size() ? slot_stamps[p_slot] : 0;
    }

    void set_live(uint32_t p_slot, bool p_live);

    void see(uint32_t p_slot, uint32_t p_peer, bool p_visible);

    void hold(uint32_t p_slot, uint32_t p_peer, bool p_held);

    void settle_slot(uint32_t p_slot);

    bool is_live(uint32_t p_slot) const {
        return p_slot < live.count && live.get(p_slot);
    }

    bool reaches(uint32_t p_slot, uint32_t p_peer) const;

    bool holds_copy(uint32_t p_slot, uint32_t p_peer) const;

    bool set_reachable(uint32_t p_peer, bool p_reachable);

    bool peer_reachable(uint32_t p_peer) const {
        return p_peer < sides.size() && sides[p_peer].reachable;
    }

    uint32_t slot_capacity() const {
        return slot_route.size();
    }

    uint32_t table_for(
        uint64_t p_set,
        Family p_family,
        const table::SchemaRecord &p_schema
    );

    uint32_t table_count() const {
        return tables.size();
    }

#if defined(NETW_PROFILING)
    int64_t row_bytes() const;
    int64_t peer_bytes() const;
    int64_t flight_bytes() const;
#endif

    Table &table(uint32_t p_table) {
        return *tables[p_table];
    }

    const Table &table(uint32_t p_table) const {
        return *tables[p_table];
    }

    uint32_t table_of(const godot::RID &p_rid) const;

    RowAddress add_row(
        uint32_t p_table,
        uint64_t p_owner,
        uint32_t p_lane,
        uint64_t p_node,
        uint8_t p_comp
    );

    RowAddress row_of(uint64_t p_owner, uint32_t p_lane) const;

    bool probes(uint64_t p_node) const;

    void place(const RowAddress &p_at, int64_t p_route, int64_t p_epoch);

    void remove_row(const RowAddress &p_at);

    void remove_owner(uint64_t p_owner);

    ReceiptCounts receipts;

    uint32_t peer_join(int p_id);

    void peer_leave(int p_id);

    void clear_peers();

    uint32_t peer_of(int p_id) const;

    int peer_id(uint32_t p_peer) const {
        return p_peer < sides.size() ? sides[p_peer].id : 0;
    }

    bool peer_joined(uint32_t p_peer) const {
        return p_peer < sides.size() && sides[p_peer].joined;
    }

    uint32_t peer_capacity() const {
        return sides.size();
    }

    uint32_t flight_count(uint32_t p_peer) const;

    uint32_t lanes_in_flight(uint32_t p_peer) const {
        return p_peer < sides.size() ? sides[p_peer].flights.size() : 0;
    }

    uint32_t spare_image_count() const {
        return spare_images.size();
    }

    const wire::CodeRow *spare_image(uint32_t p_at) const {
        return p_at < spare_images.size() ? &spare_images[p_at] : nullptr;
    }

    void name_lane(const RowAddress &p_at, uint8_t p_ordinal);

    RowAddress lane_row(const wire::StreamLane &p_lane) const;

    RowAddress lane_row(uint64_t p_address) const;

    void admit(const RowAddress &p_at, uint32_t p_peer, bool p_admitted);

    void hide(const RowAddress &p_at, uint32_t p_peer);

    void set_source(const RowAddress &p_at, Source p_source);

    void note_changed(const RowAddress &p_at);

    uint64_t stream_open(
        int p_peer,
        const wire::StreamLane &p_lane,
        const wire::StreamTenure &p_tenure
    );

    wire::ReadyVerdict stream_ready(
        int p_peer,
        uint64_t p_request,
        uint64_t p_token
    );

    bool stream_reset(int p_peer, uint64_t p_request, uint64_t p_token);

    bool stream_close(
        int p_peer,
        const wire::StreamLane &p_lane,
        uint64_t &r_token
    );

    bool stream_hide(int p_peer, const wire::StreamLane &p_lane);

    uint64_t token_of(int p_peer, const wire::StreamLane &p_lane) const;

    uint64_t request_of(int p_peer, const wire::StreamLane &p_lane) const;

    bool names_request(
        int p_peer,
        uint64_t p_request,
        wire::StreamLane &r_lane
    ) const;

    godot::LocalVector<uint64_t> opening_requests(int p_peer) const;

    godot::LocalVector<wire::ClosedLane> close_tenures_other_than(
        int64_t p_route,
        uint64_t p_tenure
    );

    godot::LocalVector<wire::ClosedLane> close_anchors_other_than(
        int64_t p_route,
        uint64_t p_anchor,
        uint64_t p_author
    );

    void route_closed(int64_t p_route);

    uint64_t mint(
        const RowAddress &p_at,
        uint32_t p_peer,
        const wire::CodeRow &p_image,
        bool p_pinned
    );

    const Flight *flight_of(
        uint32_t p_peer,
        uint64_t p_token,
        uint64_t p_revision
    ) const;

    const Flight *pinned_of(uint32_t p_peer, uint64_t p_token) const;

    void arm_refresh(const RowAddress &p_at, uint32_t p_peer, int64_t p_tick);

    void come_due(uint32_t p_peer, int64_t p_tick);

    void settle_due(uint32_t p_peer);

    const godot::LocalVector<uint64_t> &due_lanes(uint32_t p_peer) const {
        return sides[p_peer].due;
    }

    void stream_withdrawn(int p_peer, uint64_t p_token, uint64_t p_revision);

    wire::ReceiptVerdict stream_receipt(
        int p_peer,
        uint64_t p_token,
        uint64_t p_revision
    );
};

} // namespace netw::store

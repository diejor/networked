#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/window_ring.hpp"
#include "netw/store/coder.hpp"
#include "netw/store/column.hpp"
#include "netw/store/peers.hpp"
#include "netw/store/resident.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::store {

using netw::table::SchemaRecord;

struct Received {
    uint64_t token = 0;
    uint64_t accepted = 0;
    uint32_t next = 0;
    uint32_t held = 0;
    godot::LocalVector<uint64_t> revisions;
    godot::LocalVector<uint64_t> images;
    godot::LocalVector<godot::Variant> carried;
};

struct Sender {
    int id = 0;
    godot::LocalVector<Received> rows;
};

class Table {
public:
    static constexpr uint32_t NO_SLOT = 0xffffffffu;
    static constexpr uint32_t RECEIVED_DEPTH = 32;
    static constexpr uint32_t HISTORY_DEPTH = 8;
    static constexpr uint32_t PENDING_DEPTH = 8;

    enum Flag : uint8_t {
        DIRTY = 1,
        RESTED = 2,
        READABLE = 4,
        UNGATHERED = 8,
        PULLED = 16,
        UNENCODED = 32,
        DECLARED = 64,
        SERVER_AUDIENCE = 128,
    };

    SchemaRecord schema;
    wire::WirePlan plan;
    godot::LocalVector<Column> columns;
    Family family = Family::VOLATILE;
    uint64_t set_id = 0;
    godot::RID rid;
    bool ports_resolved = false;
    bool scalar = false;
    uint32_t body_columns = 0;
    uint32_t words = 0;
    uint32_t carried = 0;
    int64_t pulled_tick = -1;
    int64_t heartbeat = 0;
    uint32_t window = 1;
    uint8_t channel = 0;

    godot::LocalVector<uint32_t> slots;
    godot::LocalVector<int64_t> routes;
    godot::LocalVector<uint64_t> owners;
    godot::LocalVector<uint64_t> nodes;
    godot::LocalVector<NodeCell> cells;
    godot::LocalVector<uint64_t> scripts;
    godot::LocalVector<int32_t> authorities;
    godot::LocalVector<uint8_t> comps;
    godot::LocalVector<uint8_t> lanes;
    godot::LocalVector<uint8_t> ordinals;
    godot::LocalVector<uint8_t> sources;
    godot::LocalVector<uint8_t> flags;
    godot::LocalVector<int64_t> changed_ticks;
    godot::LocalVector<int64_t> frame_ticks;
    godot::LocalVector<int64_t> received_ticks;
    godot::LocalVector<int64_t> reconcile_acks;
    godot::LocalVector<wire::StreamTenure> tenures;
    godot::LocalVector<uint64_t> admit_stamps;
    godot::LocalVector<godot::LocalVector<uint8_t>> values;
    godot::LocalVector<godot::Variant> carried_values;
    godot::LocalVector<uint64_t> codes;
    godot::LocalVector<PeerRows> peers;
    godot::LocalVector<Sender> senders;

    uint32_t row_bytes = 0;
    godot::LocalVector<uint32_t> value_offsets;
    godot::LocalVector<int64_t> history_ticks;
    godot::LocalVector<uint8_t> history_values;
    godot::LocalVector<godot::Variant> history_carried;
    godot::LocalVector<uint8_t> history_next;
    godot::LocalVector<uint8_t> history_held;
    godot::LocalVector<uint32_t> history_appends;

    enum Offer : uint8_t {
        OFFER_NONE = 0,
        OFFER_UNAUTHORED = 1,
        OFFER_UNREACHED = 2,
        OFFER_MADE = 3,
    };

    Bits unsourced;
    Bits working;
    uint32_t sweep_at = 0;
    godot::LocalVector<int64_t> touched_routes;
    bool touched_overflow = false;
    godot::HashMap<uint64_t, int64_t> lagging;
    godot::LocalVector<uint8_t> offers;
    uint32_t offer_counts[4] = {0, 0, 0, 0};
    int64_t stamp_tick = -1;

    Bits due;
    godot::LocalVector<int64_t> due_ticks;
    godot::LocalVector<uint8_t> due_held;
    godot::LocalVector<int64_t> due_senders;
    godot::LocalVector<uint64_t> due_tenures;

    int64_t arrived_tick = -1;
    int64_t arrived_ack = -1;
    bool arrived_whole = true;
    godot::LocalVector<repl::WindowSample> arrived_window;

    godot::LocalVector<ColumnCoder> coders;
    bool typed = false;
    godot::LocalVector<uint8_t> exact;
    godot::LocalVector<uint64_t> pulled_codes;
    godot::LocalVector<uint32_t> code_offsets;

    wire::CodeRow scratch;
    wire::CodeRow landing;
    wire::CodeRow base;
    godot::LocalVector<uint8_t> landed;
    godot::LocalVector<godot::PackedInt64Array> decode_codes;
    godot::LocalVector<uint32_t> selected;

    void compile(const SchemaRecord &p_schema, Family p_family);

    void resolve_ports(godot::Object *p_node);

    uint32_t row_count() const {
        return slots.size();
    }

    uint32_t add_row();

    uint32_t swap_remove(uint32_t p_row);

    void set_offer(uint32_t p_row, uint8_t p_offer);

    const uint64_t *codes_of(uint32_t p_row) const {
        return codes.ptr() + uint64_t(p_row) * words;
    }

    const godot::Variant *carried_of(uint32_t p_row) const {
        return carried == 0 ? nullptr
                            : carried_values.ptr() + uint64_t(p_row) * carried;
    }

    void write_value(
        uint32_t p_row,
        uint32_t p_column,
        const godot::Variant &p_value
    );

    godot::Variant read_value(uint32_t p_row, uint32_t p_column) const;

    bool encode(
        uint32_t p_column,
        const godot::Variant &p_value,
        wire::CodeRow &r_row
    ) const;

    bool settle(uint32_t p_row, const wire::CodeRow &p_codes, int64_t p_tick);

    int first_difference(uint32_t p_row, const wire::CodeRow &p_codes) const;

    uint32_t dirty_count() const;

    int64_t byte_count() const;

#if defined(NETW_PROFILING)
    int64_t held_bytes() const;
    int64_t peer_bytes() const;
#endif

    void size_peers(uint32_t p_count);

    void open_peer(uint32_t p_peer);
    void trim_peers(uint32_t p_count) {
        if (peers.size() > p_count) {
            peers.resize(p_count);
        }
    }

    void reset_stream(uint32_t p_row, uint32_t p_peer, Stream p_stream);

    bool differs(uint32_t p_row, uint32_t p_peer) const;

    bool owes(uint32_t p_row, uint32_t p_peer) const;

    void refresh_owed(uint32_t p_row, uint32_t p_peer);

    void refresh_sendable(uint32_t p_row, uint32_t p_peer);

    void promote(
        uint32_t p_row,
        uint32_t p_peer,
        uint64_t p_revision,
        const wire::CodeRow &p_image
    );

    bool holds_image(uint32_t p_row, const wire::CodeRow &p_image) const;

    uint32_t sender_index(int p_id);

    uint32_t find_sender(int p_id) const;

    void forget_sender(int p_id);

    const uint64_t *received_image(
        const Received &p_received,
        uint64_t p_revision,
        const godot::Variant *&r_carried
    ) const;

    void keep_received(
        Received &r_received,
        uint64_t p_revision,
        const wire::CodeRow &p_codes
    );

    bool decode_row(const wire::CodeRow &p_codes);

    bool land(uint32_t p_row, const wire::CodeRow &p_codes, int64_t p_tick);

    void record(uint32_t p_row, int64_t p_tick, const uint8_t *p_image);

    void touch(uint32_t p_row);

    bool record_codes(
        uint32_t p_row,
        const wire::CodeRow &p_codes,
        int64_t p_tick
    );

    int settle_exact(uint32_t p_row, bool p_body_columns, int64_t p_tick);

    void record_held(uint32_t p_row, int64_t p_tick);

    void record_rest(uint32_t p_row, int64_t p_tick);

    uint32_t history_count(uint32_t p_row) const {
        return p_row < history_held.size() ? history_held[p_row] : 0;
    }

    int64_t history_tick(uint32_t p_row, uint32_t p_age) const;

    int history_at(uint32_t p_row, int64_t p_tick) const;

    godot::Variant history_value(
        uint32_t p_row,
        uint32_t p_age,
        uint32_t p_column
    ) const;

    uint32_t owe_push(
        uint32_t p_row,
        int64_t p_tick,
        int64_t p_sender,
        uint64_t p_tenure
    );

    bool take_due(uint32_t p_row, int64_t p_display, int64_t &r_stamp);

    void drop_due(uint32_t p_row);

    bool holds_values(uint32_t p_row) const {
        return received_ticks[p_row] >= 0;
    }

private:
    void compile_coders();
    uint32_t history_slot(uint32_t p_row, uint32_t p_age) const;
    godot::Variant read_bytes(const uint8_t *p_at, uint32_t p_column) const;
    void write_bytes(
        uint8_t *p_at,
        uint32_t p_column,
        const godot::Variant &p_value
    ) const;
    void fresh_peer_row(PeerRows &r_peer, uint32_t p_row);
    void push_peer_rows();
    void move_peer_rows(uint32_t p_to, uint32_t p_from);
    void shrink_peer_rows(uint32_t p_count);
};

} // namespace netw::store

#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/carrier_row.hpp"
#include "netw/repl/lane_set.hpp"
#include "netw/repl/link_governor.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/repl/send_pass.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/repl/window_ring.hpp"
#include "netw/wire/control_scheduler.hpp"
#include "netw/wire/registry.hpp"
#include "netw/wire/stream_book.hpp"

namespace netw::store {
class World;
class Table;
} // namespace netw::store

namespace netw::repl {

using netw::table::SchemaRecord;

enum class RowForm : uint8_t { FRESH, REPAIR, REFRESH, WINDOW };

struct RowSend {
    int64_t route = 0;
    uint8_t comp = 0;
    uint8_t channel = 0;
    int peer = 0;
    uint32_t table = 0;
    uint32_t row = 0;
    RowForm form = RowForm::FRESH;
    uint64_t mask = 0;
    uint64_t token = 0;
    uint64_t revision = 0;
    int64_t bits = 0;
    int64_t framed = 0;
    uint32_t sample_count = 0;
    bool masked = false;
    bool reliable = false;
    bool windowed = false;
    bool whole = false;
    bool resend = false;
    godot::PackedByteArray bytes;
};

struct TableFrame {
    int peer = 0;
    bool reliable = false;
    godot::PackedByteArray bytes;
};

struct SessionResult {
    godot::LocalVector<RowSend> sends;
    uint32_t ungathered = 0;
    uint32_t caught_up = 0;
    uint32_t deferred = 0;
    uint32_t staged_out = 0;
    int64_t sent_bits = 0;
    godot::LocalVector<TableFrame> tables;
};

struct AckReport {
    uint32_t delivered = 0;
    uint32_t lost = 0;
    int64_t delivered_bits = 0;
};

enum class RowVerdict : uint8_t {
    UNOFFERED,
    SENT,
    DEFERRED,
    CAUGHT_UP,
    UNGATHERED,
};

struct RowExplain {
    RowVerdict verdict = RowVerdict::UNOFFERED;
    int64_t tick = -1;
    uint64_t confirmed = 0;
    uint64_t exposed = 0;
    bool has_baseline = false;
    float priority = 0.0f;
    int64_t age_ticks = -1;
    const char *row_class = "resting";
};

struct PeerAccount {
    double balance = 0.0;
    double rate = 0.0;
    double fill = 0.0;
    int64_t filled_tick = 0;
    bool filled = false;
    int64_t sent_bytes = 0;
    uint32_t deferred = 0;
};

class SessionSend {
    struct Candidate {
        uint32_t table = 0;
        uint32_t row = 0;
        uint32_t order = 0;
        float acc = 0.0f;
        RowForm form = RowForm::FRESH;
        bool nodeless = false;
    };

    LaneSet lanes;
    SendPass pass;
    LinkGovernor link;
    wire::StreamReaderBook readers;
    wire::ControlScheduler control;
    godot::HashMap<int, PeerAccount> accounts;
    double session_rate = 0.0;
    int64_t tickrate = 60;
    int64_t row_budget_bits = 0;
    int64_t pass_tick = -1;
    godot::Callable stage;
    bool stage_wanted = true;
    bool tables_published = true;
    store::World *world = nullptr;
    godot::LocalVector<Candidate> candidates;
    godot::LocalVector<godot::LocalVector<uint32_t>> table_picks;
    godot::LocalVector<uint32_t> refreshed;
    wire::CodeRow image;
    wire::CodeRow baseline;
    wire::WriteStream stream;
    SessionResult result;

    void queue_close(int p_peer, uint64_t p_token);

    void queue_closes(const godot::LocalVector<wire::ClosedLane> &p_closed);

    void queue_open(
        int p_peer,
        const store::Table &p_table,
        uint32_t p_row,
        uint64_t p_request
    );

    bool stream_seated(
        uint32_t p_peer,
        uint32_t p_table,
        uint32_t p_row,
        int64_t p_now_ms
    );

    bool classify(
        uint32_t p_peer,
        uint32_t p_table,
        uint32_t p_row,
        bool p_due,
        int64_t p_now_ms,
        RowForm &r_form,
        SessionResult &r_out
    );

    bool unfitted(
        const wire::WireRegistry &p_registry,
        const store::Table &p_table
    ) const;

    void record_windows(int64_t p_base_tick);

    bool encode(
        uint32_t p_peer,
        uint32_t p_table,
        uint32_t p_row,
        RowForm p_form,
        int64_t p_base_tick,
        RowSend &r_send,
        SessionResult &r_out
    );

    void commit_send(
        uint32_t p_peer,
        const RowSend &p_send,
        int64_t p_base_tick,
        int64_t p_now_ms
    );

    void pass_peer(
        const wire::WireRegistry &p_registry,
        uint32_t p_peer,
        int64_t p_max_bits,
        int64_t p_base_tick,
        int64_t p_now_ms,
        SessionResult &r_out
    );

    void offer_nodeless(
        uint32_t p_index,
        int p_peer,
        int64_t p_tick,
        uint32_t &r_order
    );

    bool take_nodeless(
        const Candidate &p_held,
        int p_peer,
        PeerAccount &r_account
    );

    void emit_nodeless(
        int p_peer,
        int64_t p_max_bits,
        PeerAccount &r_account,
        SessionResult &r_out
    );

    PeerAccount &refill(int p_peer, int64_t p_max_bits, int64_t p_tick);

    godot::PackedByteArray price(
        const RowSend &p_send,
        int64_t p_base_tick
    ) const;

public:
    static constexpr uint32_t MISSES_PER_TICK = 4;
    static constexpr double BANKED_TICKS = 2.0;
    static constexpr double DEBT_TICKS = 2.0;
    static constexpr double ROW_FLOOR_SHARE = 0.25;

    void set_rate(double p_bytes_per_second) {
        session_rate = p_bytes_per_second;
    }

    void set_peer_rate(int p_peer, double p_bytes_per_second);

    void set_tickrate(int64_t p_tickrate) {
        tickrate = p_tickrate > 0 ? p_tickrate : 60;
    }

    double configured_rate(int p_peer, int64_t p_max_bits) const;

    double fill_of(int p_peer, int64_t p_max_bits) const {
        return configured_rate(p_peer, p_max_bits) * double(link.share(p_peer))
            / double(tickrate);
    }

    void charge(int p_peer, int64_t p_bytes);

    static void bound_debt(PeerAccount &r_account, double p_fill);

    void note_backlog(int p_peer, int64_t p_buffered, int64_t p_now_ms) {
        link.note_backlog(p_peer, p_buffered, p_now_ms);
    }

    const PeerAccount *account(int p_peer) const {
        return accounts.getptr(p_peer);
    }

#if defined(NETW_TESTS)
    int64_t rows_touched = 0;
#endif

    void set_encode_stage(const godot::Callable &p_stage) {
        stage = p_stage;
    }

    void want_encode_stage(bool p_wanted) {
        stage_wanted = p_wanted;
    }

    void publish_tables(bool p_publishes) {
        tables_published = p_publishes;
    }

    bool has_encode_stage() const {
        return stage.is_valid();
    }

    void watch_store(store::World *p_world) {
        world = p_world;
    }

    store::World *store() const {
        return world;
    }

    uint64_t stream_open(
        int p_peer,
        const wire::StreamLane &p_lane,
        const wire::StreamTenure &p_tenure = wire::StreamTenure()
    );

    wire::ReadyVerdict stream_ready(
        int p_peer,
        uint64_t p_request,
        uint64_t p_token
    );

    wire::ReceiptVerdict stream_receipt(
        int p_peer,
        uint64_t p_token,
        uint64_t p_revision
    );

    bool stream_reset(int p_peer, uint64_t p_request, uint64_t p_token);

    uint64_t token_of(int p_peer, const wire::StreamLane &p_lane) const;

    const SessionResult &run(
        const wire::WireRegistry &p_registry,
        int64_t p_max_bits,
        int64_t p_base_tick,
        int64_t p_now_ms = 0
    );

    bool describe(const RowSend &p_send, CarrierRow &r_row);

    bool commit(
        int p_peer,
        uint16_t p_seq,
        const godot::LocalVector<CarrierRow> &p_rows,
        int64_t p_frames,
        int64_t p_bits
    );

    void cancel(
        int p_peer,
        const godot::LocalVector<CarrierRow> &p_rows,
        int64_t p_tick = 0
    );

    void forget_peer(int p_peer);

    RowExplain explain(int64_t p_route, uint8_t p_comp, int p_peer) const;

    AckReport acknowledge(
        int p_peer,
        uint16_t p_acked_seq,
        uint32_t p_history,
        double p_rtt_ms = 0.0,
        double p_jitter_ms = 0.0,
        int64_t p_now_ms = 0
    );

    LinkGovernor::Mode link_mode(int p_peer) const {
        return link.mode(p_peer);
    }

    float link_loss(int p_peer) const {
        return link.loss(p_peer);
    }

    int64_t link_budget_bits(int p_peer, int64_t p_full_bits) const {
        return link.budget_bits(p_peer, p_full_bits);
    }

    int64_t control_budget_bytes(int p_peer, int64_t p_reserved_bytes) const;

    wire::StreamReaderBook &reader_book() {
        return readers;
    }

    wire::ControlScheduler &control_scheduler() {
        return control;
    }

    void hide(int p_peer, const wire::StreamLane &p_lane);

    void close_route(int64_t p_route);

    void park_tenures(int64_t p_route, uint64_t p_tenure);

    void close_anchors(int64_t p_route, uint64_t p_anchor, uint64_t p_author);

    godot::LocalVector<wire::ParkedOpen> expire_parks(int64_t p_now_ms);

    uint32_t window_lane_count() const {
        return lanes.window_size();
    }

    int outstanding() const {
        return pass.outstanding();
    }
};

} // namespace netw::repl

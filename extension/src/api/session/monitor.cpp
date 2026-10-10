#include "netw/api/netw_multiplayer.hpp"

#if defined(DEBUG_ENABLED)

#include <algorithm>

#include "netw/api/predict_stats.hpp"
#include "netw/api/sync_compat.hpp"
#include "netw/display/book.hpp"

using namespace godot;

namespace netw {

void NetwMultiplayer::monitor_sample(monitor::Sample &r_sample) {
    const ClockEngine &ticking = clock_engine();
    const PackedInt32Array peers = NETW_API_VIRTUAL(get_peer_ids)();
    ReplicationSend *send = row_send_of(get_replication_plane());
    const int64_t budget_bits = datagram_budget() * 8;
    const double tickrate = double(ticking.get_tickrate());
    r_sample.peers = peers.size();
    for (int at = 0; at < peers.size(); ++at) {
        const int64_t peer = peers[at];
        const double measured_ms = link_round_trip_ms(peer);
        const double round_trip
            = measured_ms > 0.0 ? measured_ms / 1000.0 : ticking.rtt_avg();
        const double jitter = measured_ms > 0.0 ? link_jitter_ms(peer) / 1000.0
                                                : ticking.rtt_jitter();
        r_sample.round_trip = std::max(r_sample.round_trip, round_trip);
        r_sample.jitter = std::max(r_sample.jitter, jitter);
        if (send == nullptr) {
            continue;
        }
        r_sample.loss = std::max(r_sample.loss, double(send->link_loss(peer)));
        const repl::PeerAccount *account = send->account(peer);
        if (account == nullptr) {
            continue;
        }
        r_sample.rows_waiting += int64_t(account->deferred);
        monitor::Upload upload;
        upload.peer = peer;
        upload.sent_bytes = account->sent_bytes;
        upload.budget_per_second = send->fill_of(peer, budget_bits) * tickrate;
        r_sample.uploads.push_back(upload);
    }

    if (ticking.get_configured()) {
        const double lag = display_book.is_valid()
            ? display_book->get_stats().max_display_lag
            : 0.0;
        r_sample.display_delay
            = (double(ticking.get_display_offset()) + lag) * ticking.ticktime();
    }

    r_sample.late_inputs
        = prediction_engine.held_fact_total(NetwPredictStats::FACT_MISSING);
    r_sample.corrections
        = prediction_engine.held_fact_total(NetwPredictStats::FACT_CORRECTIONS);
    r_sample.replayed_ticks = prediction_engine.replayed_tick_count();

    r_sample.entities = int64_t(entity_book.size());
    for (uint32_t at = 0; at < store_world.table_count(); ++at) {
        const store::Table &table = store_world.table(at);
        if (table.body_columns > 0) {
            r_sample.awake_rows += int64_t(table.working.population());
        }
    }

    const SyncCompat *compat = sync_adapter();
    r_sample.memory_replication = store_world.row_bytes()
        + store_world.peer_bytes() + store_world.flight_bytes()
        + binding_book.held_bytes() + set_book_bytes() + entity_bytes()
        + (compat != nullptr ? compat->held_bytes() : int64_t(0));
    int64_t transitions = 0;
    int64_t inputs = 0;
    int64_t journal = 0;
    int64_t slots = 0;
    prediction_engine.held_bytes(transitions, inputs, journal, slots);
    r_sample.memory_prediction = transitions + inputs + journal + slots;
    r_sample.memory_display
        = display_book.is_valid() ? display_book->held_bytes() : 0;
}

} // namespace netw

#endif

#pragma once

#include "offer_codes.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/replication_send.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/registry.hpp"

namespace netw_test {

inline netw::store::Family store_family(netw::wire::StreamFamily p_family) {
    return netw::store::Family(uint8_t(p_family));
}

inline netw::store::RowAddress stage_offer(
    netw::store::World &r_world,
    const RowOffer &p_offer,
    int64_t p_tick
) {
    const netw::wire::StreamLane lane = p_offer.lane();
    const uint64_t owner = lane.address();
    netw::store::RowAddress at = r_world.row_of(owner, 0);
    if (!at.valid()) {
        const uint32_t table = r_world.table_for(
            uint64_t(uintptr_t(p_offer.schema)),
            store_family(lane.family),
            p_offer.declared()
        );
        at = r_world.add_row(table, owner, 0, 0, p_offer.comp);
    }
    netw::store::Table &table = r_world.table(at.table);
    r_world.place(at, p_offer.route, p_offer.life);
    r_world.name_lane(at, p_offer.comp);
    r_world.set_source(at, netw::store::Source::LOCAL);
    table.frame_ticks[at.row] = p_offer.tick;
    table.reconcile_acks[at.row] = p_offer.ack;
    table.tenures[at.row] = p_offer.tenure;
    table.heartbeat = p_offer.heartbeat;
    table.window = p_offer.window > 0 ? p_offer.window : 1;
    table.channel = p_offer.channel;
    if (p_offer.codes == nullptr) {
        table.flags[at.row] |= netw::store::Table::UNENCODED;
    } else {
        netw::wire::CodeRow held;
        held.assign(table.plan, p_offer.codes, p_offer.carried);
        const bool fresh
            = (table.flags[at.row] & netw::store::Table::PULLED) == 0;
        if (table.settle(at.row, held, p_tick) || fresh) {
            r_world.note_changed(at);
        }
    }
    r_world.set_weight(table.slots[at.row], p_offer.priority);
    r_world.set_marks(table.slots[at.row], p_offer.marks);
    for (uint32_t which = 0; which < p_offer.recipients.size(); ++which) {
        r_world.peer_join(p_offer.recipients[which]);
    }
    for (uint32_t peer = 0; peer < r_world.peer_capacity(); ++peer) {
        if (!r_world.peer_joined(peer)) {
            continue;
        }
        bool named = false;
        for (uint32_t which = 0; which < p_offer.recipients.size(); ++which) {
            named = named || p_offer.recipients[which] == r_world.peer_id(peer);
        }
        r_world.admit(at, peer, named);
    }
    return at;
}

inline void stage_offers(
    netw::store::World &r_world,
    const godot::LocalVector<RowOffer> &p_offers,
    int64_t p_tick
) {
    for (uint32_t index = 0; index < r_world.table_count(); ++index) {
        netw::store::Table &table = r_world.table(index);
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            r_world.set_source({index, row}, netw::store::Source::NONE);
        }
    }
    for (uint32_t at = 0; at < p_offers.size(); ++at) {
        stage_offer(r_world, p_offers[at], p_tick);
    }
}

inline bool row_owed(
    const netw::store::World &p_world,
    const netw::wire::StreamLane &p_lane,
    int p_peer
) {
    const netw::store::RowAddress at = p_world.lane_row(p_lane);
    const uint32_t peer = p_world.peer_of(p_peer);
    if (!at.valid() || peer == netw::store::Table::NO_SLOT) {
        return false;
    }
    return p_world.table(at.table).peers[peer].owed.get(at.row);
}

inline uint64_t row_acked(
    const netw::store::World &p_world,
    const netw::wire::StreamLane &p_lane,
    int p_peer
) {
    const netw::store::RowAddress at = p_world.lane_row(p_lane);
    const uint32_t peer = p_world.peer_of(p_peer);
    if (!at.valid() || peer == netw::store::Table::NO_SLOT) {
        return 0;
    }
    return p_world.table(at.table).peers[peer].acked_rev[at.row];
}

inline uint64_t row_exposed(
    const netw::store::World &p_world,
    const netw::wire::StreamLane &p_lane,
    int p_peer
) {
    const netw::store::RowAddress at = p_world.lane_row(p_lane);
    const uint32_t peer = p_world.peer_of(p_peer);
    if (!at.valid() || peer == netw::store::Table::NO_SLOT) {
        return 0;
    }
    return p_world.table(at.table).peers[peer].exposed_rev[at.row];
}

inline bool row_baseline(
    const netw::store::World &p_world,
    const netw::wire::StreamLane &p_lane,
    int p_peer,
    netw::wire::CodeRow &r_row
) {
    const netw::store::RowAddress at = p_world.lane_row(p_lane);
    const uint32_t peer = p_world.peer_of(p_peer);
    if (!at.valid() || peer == netw::store::Table::NO_SLOT) {
        return false;
    }
    const netw::store::Table &table = p_world.table(at.table);
    const netw::store::PeerRows &rows = table.peers[peer];
    if (rows.acked_rev[at.row] == 0) {
        return false;
    }
    return r_row.assign(
        table.plan,
        rows.baseline.ptr() + uint64_t(at.row) * table.words,
        table.carried != 0
            ? rows.baseline_carried.ptr() + uint64_t(at.row) * table.carried
            : nullptr
    );
}

inline netw::wire::CodeRow sent_row(
    const netw::store::World &p_world,
    const netw::repl::RowSend &p_send
) {
    netw::wire::CodeRow out;
    const uint32_t peer = p_world.peer_of(p_send.peer);
    if (peer == netw::store::Table::NO_SLOT) {
        return out;
    }
    const netw::store::Flight *flight
        = p_world.flight_of(peer, p_send.token, p_send.revision);
    if (flight != nullptr) {
        out.copy_from(flight->image);
    }
    return out;
}

class SessionSend : public netw::repl::SessionSend {
public:
    netw::store::World world;

    SessionSend() {
        watch_store(&world);
    }

    SessionSend(const SessionSend &) = delete;
    SessionSend &operator=(const SessionSend &) = delete;

    using netw::repl::SessionSend::run;

    netw::repl::SessionResult run(
        const netw::wire::WireRegistry &p_registry,
        const godot::LocalVector<RowOffer> &p_offers,
        int64_t p_max_bits,
        int64_t p_base_tick,
        int64_t p_now_ms = 0
    ) {
        stage_offers(world, p_offers, p_base_tick);
        return netw::repl::SessionSend::run(
            p_registry,
            p_max_bits,
            p_base_tick,
            p_now_ms
        );
    }
};

class ReplicationSend : public netw::ReplicationSend {
public:
    netw::store::World world;

    ReplicationSend() {
        watch_store(&world);
    }

    ReplicationSend(const ReplicationSend &) = delete;
    ReplicationSend &operator=(const ReplicationSend &) = delete;

    using netw::ReplicationSend::run;

    netw::repl::SessionResult run(
        const godot::LocalVector<RowOffer> &p_offers,
        int64_t p_max_bits,
        int64_t p_base_tick,
        int64_t p_now_ms = 0
    ) {
        stage_offers(world, p_offers, p_base_tick);
        return netw::ReplicationSend::run(p_max_bits, p_base_tick, p_now_ms);
    }
};

} // namespace netw_test

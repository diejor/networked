#include "support/netw_test.h"

#include "support/send_drive.h"
#include "support/stream_seat.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/session_send.hpp"

using namespace godot;

namespace TestNetwReplSessionSend {

using godot::Array;
using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::repl::RowOffer;
using netw::repl::SessionResult;
using netw::repl::SessionSend;
using netw::table::SchemaRecord;
using netw::wire::ChannelDecl;
using netw::wire::Delivery;
using netw::wire::WireRegistry;
using netw_test::drive_send;

const uint8_t CHANNEL = 43;
const int PEER = 7;
const int OTHER = 9;

WireRegistry registry() {
    WireRegistry out;
    ChannelDecl decl;
    decl.id = CHANNEL;
    decl.name = godot::StringName("session_probe");
    decl.delivery = Delivery::FITTED;
    out.register_channel(decl);
    return out;
}

const SchemaRecord &body() {
    static SchemaRecord record = [] {
        SchemaRecord made;
        made.name = godot::StringName("Body");
        SchemaCore::append_column(
            &made,
            godot::StringName("x"),
            SchemaCore::I16,
            1
        );
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

int64_t oldest_sample_tick(const netw::repl::RowSend &p_send) {
    const netw::wire::WirePlan plan = netw::wire::WirePlan::compile(body());
    netw::repl::SnapshotHeader header;
    LocalVector<netw::repl::WindowSample> samples;
    if (!netw::repl::read_snapshot_window(
            p_send.bytes,
            0,
            plan,
            header,
            samples
        )
        || samples.is_empty()) {
        return -1;
    }
    return samples[0].tick;
}

SessionResult uncarried(
    SessionSend &session,
    const LocalVector<RowOffer> &p_offers
) {
    static const WireRegistry reg = registry();
    netw_test::seat_streams(session, p_offers);
    return session.run(reg, p_offers, 1 << 20, 0);
}

int64_t one_frame_bits(const LocalVector<RowOffer> &p_offers) {
    SessionSend probe;
    const SessionResult out = uncarried(probe, p_offers);
    return out.sends.is_empty() ? 0 : out.sends[0].bits;
}

class Carrier {
    godot::HashMap<int, netw::CarrierBatch> open;

public:
    SessionResult carry(
        SessionSend &p_session,
        const LocalVector<RowOffer> &p_offers
    ) {
        SessionResult out = uncarried(p_session, p_offers);
        for (uint32_t at = 0; at < out.sends.size(); ++at) {
            attach(p_session, out.sends[at]);
        }
        return out;
    }

    void attach(SessionSend &p_session, const netw::repl::RowSend &p_send) {
        netw::CarrierRow row;
        if (!p_session.describe(p_send, row)) {
            return;
        }
        godot::PackedByteArray frame;
        frame.resize(1);
        netw::CarrierBatch &batch = open[p_send.peer];
        batch.take_frame(frame);
        batch.attach(row);
    }

    void commit(SessionSend &p_session, int p_peer, uint16_t p_seq) {
        netw::CarrierBatch *batch = open.getptr(p_peer);
        if (batch == nullptr) {
            return;
        }
        p_session.commit(
            p_peer,
            p_seq,
            batch->rows(),
            batch->frame_count(),
            batch->bit_count()
        );
        open.erase(p_peer);
    }

    void cancel(SessionSend &p_session, int p_peer) {
        netw::CarrierBatch *batch = open.getptr(p_peer);
        if (batch == nullptr) {
            return;
        }
        p_session.cancel(p_peer, batch->rows());
        open.erase(p_peer);
    }

    uint32_t pending_count(int p_peer) const {
        const netw::CarrierBatch *batch = open.getptr(p_peer);
        return batch == nullptr ? 0 : batch->rows().size();
    }
};

RowOffer offer(int64_t route, int64_t value, const LocalVector<int> &peers) {
    RowOffer out;
    out.route = route;
    out.comp = 0;
    out.channel = CHANNEL;
    out.schema = &body();
    out.values.push_back(value);
    out.recipients = peers;
    out.masked = true;
    return out;
}

const SchemaRecord &banner() {
    static SchemaRecord record = [] {
        SchemaRecord made;
        made.name = godot::StringName("Banner");
        SchemaCore::append_column(
            &made,
            godot::StringName("hp"),
            SchemaCore::I32,
            1
        );
        SchemaCore::append_column(
            &made,
            godot::StringName("mp"),
            SchemaCore::I32,
            1
        );
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

RowOffer retained_offer(
    int64_t route,
    int64_t hp,
    int64_t mp,
    const LocalVector<int> &peers
) {
    RowOffer out;
    out.route = route;
    out.comp = 0;
    out.channel = CHANNEL;
    out.schema = &banner();
    out.values.push_back(hp);
    out.values.push_back(mp);
    out.recipients = peers;
    out.reliable = true;
    return out;
}

RowOffer window_offer(
    int64_t route,
    int64_t tick,
    int64_t value,
    const LocalVector<int> &peers
) {
    RowOffer out;
    out.route = route;
    out.comp = 0;
    out.channel = CHANNEL;
    out.schema = &body();
    out.values.push_back(value);
    out.recipients = peers;
    out.windowed = true;
    out.window = 3;
    out.tick = tick;
    return out;
}

LocalVector<int> peers(int a, int b = -1) {
    LocalVector<int> out;
    out.push_back(a);
    if (b >= 0) {
        out.push_back(b);
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a first pass owes every recipient the whole row"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER, OTHER)));

    const SessionResult result = drive_send(session, reg, offers, 10000, 1);
    NETW_CHECK_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.caught_up, 0);
    NETW_CHECK_EQ(result.ungathered, 0);
    NETW_CHECK_EQ(session.lane_count(), 1);
    NETW_CHECK_EQ(result.deferred, 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] an unchanged row after a receipt costs the "
    "pass nothing and says so"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> first;
    first.push_back(offer(1, 10, peers(PEER)));
    NETW_REQUIRE_EQ(drive_send(session, reg, first, 10000, 1).sends.size(), 1);
    netw_test::accept_streams(session, first, PEER);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    const SessionResult result = drive_send(session, reg, again, 10000, 2);

    NETW_CHECK_EQ(result.sends.size(), 0);
    NETW_CHECK_EQ(result.caught_up, 1);
    NETW_CHECK_EQ(result.ungathered, 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a row that will not gather is counted, not "
    "silently skipped"
) {
    const WireRegistry reg = registry();
    SessionSend session;

    RowOffer bad = offer(1, 10, peers(PEER));
    bad.values = Array();

    LocalVector<RowOffer> offers;
    offers.push_back(bad);
    offers.push_back(offer(2, 20, peers(PEER)));

    ERR_PRINT_OFF;
    const SessionResult result = drive_send(session, reg, offers, 10000, 1);
    ERR_PRINT_ON;

    NETW_CHECK_EQ(result.ungathered, 1);
    NETW_CHECK_EQ(result.sends.size(), 1);
    NETW_CHECK_EQ(result.sends[0].route, 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a row the budget deferred is not staged as "
    "though it were sent"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(offer(2, 20, peers(PEER)));
    offers.push_back(offer(3, 30, peers(PEER)));

    const SessionResult first
        = drive_send(session, reg, offers, one_frame_bits(offers), 1);
    NETW_REQUIRE_EQ(first.sends.size(), 1);
    const int64_t rode = first.sends[0].route;

    netw_test::accept_streams(session, offers, PEER);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(offer(2, 20, peers(PEER)));
    again.push_back(offer(3, 30, peers(PEER)));
    const SessionResult second = drive_send(session, reg, again, 10000, 2);

    NETW_CHECK_EQ(second.sends.size(), 2);
    NETW_CHECK_EQ(second.caught_up, 1);
    for (uint32_t at = 0; at < second.sends.size(); ++at) {
        const bool a_route_the_budget_held_back_rode
            = second.sends[at].route != rode;
        CHECK(a_route_the_budget_held_back_rode);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] one receipt per lane settles every lane the "
    "datagram carried"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(offer(2, 20, peers(PEER)));
    NETW_REQUIRE_EQ(drive_send(session, reg, offers, 10000, 1).sends.size(), 2);

    netw_test::accept_streams(session, offers, PEER);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(offer(2, 20, peers(PEER)));
    const SessionResult result = drive_send(session, reg, again, 10000, 2);
    NETW_CHECK_EQ(result.sends.size(), 0);
    NETW_CHECK_EQ(result.caught_up, 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that leaves is owed everything when it "
    "returns, across every lane"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(offer(2, 20, peers(PEER)));
    NETW_REQUIRE_EQ(drive_send(session, reg, offers, 10000, 1).sends.size(), 2);
    session.acknowledge(PEER, 1, 0);

    LocalVector<int> nobody;
    session.retain(nobody);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(offer(2, 20, peers(PEER)));
    const SessionResult result = drive_send(session, reg, again, 10000, 2);
    NETW_CHECK_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.caught_up, 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a row an open batch still owns is exposed, so "
    "the lane awaits its receipt and a cancel is what returns the debt"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    NETW_REQUIRE_EQ(carrier.carry(session, offers).sends.size(), 1);
    NETW_CHECK_EQ(carrier.pending_count(PEER), 1);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    const SessionResult held = carrier.carry(session, again);
    NETW_CHECK_EQ(held.sends.size(), 0);
    NETW_CHECK_EQ(held.caught_up, 1);

    carrier.cancel(session, PEER);
    LocalVector<RowOffer> owed;
    owed.push_back(offer(1, 10, peers(PEER)));
    const SessionResult result = carrier.carry(session, owed);
    NETW_CHECK_EQ(result.sends.size(), 1);
    NETW_CHECK_EQ(result.caught_up, 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] an unmasked lane opens absolute and settles on "
    "its own receipt like every other lane"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> first;
    first.push_back(offer(1, 10, peers(PEER)));
    first[0].masked = false;
    const SessionResult opened = carrier.carry(session, first);
    NETW_REQUIRE_EQ(opened.sends.size(), 1);
    const bool the_first_row_is_absolute = !opened.sends[0].masked;
    CHECK(the_first_row_is_absolute);
    const bool the_first_row_names_every_column = opened.sends[0].mask != 0;
    CHECK(the_first_row_names_every_column);

    carrier.commit(session, PEER, 1);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again[0].masked = false;
    const SessionResult awaited = carrier.carry(session, again);
    NETW_CHECK_EQ(awaited.sends.size(), 0);
    NETW_CHECK_EQ(awaited.caught_up, 1);

    netw_test::accept_streams(session, first, PEER);
    LocalVector<RowOffer> settled;
    settled.push_back(offer(1, 10, peers(PEER)));
    settled[0].masked = false;
    const SessionResult quiet = carrier.carry(session, settled);
    NETW_CHECK_EQ(quiet.sends.size(), 0);
    NETW_CHECK_EQ(quiet.caught_up, 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a receipt settles the stream of the peer that "
    "sent it and leaves the other peer's baseline where it was"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER, OTHER)));
    NETW_REQUIRE_EQ(carrier.carry(session, offers).sends.size(), 2);

    carrier.commit(session, PEER, 5);
    netw_test::accept_streams(session, offers, OTHER);

    LocalVector<RowOffer> moved;
    moved.push_back(offer(1, 11, peers(PEER, OTHER)));
    const SessionResult result = carrier.carry(session, moved);
    NETW_CHECK_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.caught_up, 0);
    for (uint32_t at = 0; at < result.sends.size(); ++at) {
        const bool a_confirmed_peer_takes_a_delta
            = result.sends[at].masked == (result.sends[at].peer == OTHER);
        CHECK(a_confirmed_peer_takes_a_delta);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a delivery acknowledgement is accounting and "
    "settles no stream, so the next row still composes on nothing"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    NETW_REQUIRE_EQ(carrier.carry(session, offers).sends.size(), 1);

    carrier.commit(session, PEER, 5);
    session.acknowledge(PEER, 5, 0);

    LocalVector<RowOffer> moved;
    moved.push_back(offer(1, 11, peers(PEER)));
    const SessionResult result = carrier.carry(session, moved);
    NETW_REQUIRE_EQ(result.sends.size(), 1);
    const bool delivery_promoted_no_baseline = !result.sends[0].masked;
    CHECK(delivery_promoted_no_baseline);
}

TEST_CASE("[Networked][Repl][Hosted] a committed pass is committed once") {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    NETW_REQUIRE_EQ(carrier.carry(session, offers).sends.size(), 1);

    carrier.commit(session, PEER, 5);
    NETW_CHECK_EQ(carrier.pending_count(PEER), 0);

    carrier.commit(session, PEER, 6);
    session.acknowledge(PEER, 5, 0);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    NETW_CHECK_EQ(carrier.carry(session, again).caught_up, 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that leaves before its commit is "
    "forgotten rather than staged later"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    NETW_REQUIRE_EQ(carrier.carry(session, offers).sends.size(), 1);

    LocalVector<int> nobody;
    session.retain(nobody);
    carrier.cancel(session, PEER);
    NETW_CHECK_EQ(carrier.pending_count(PEER), 0);

    carrier.commit(session, PEER, 5);
    session.acknowledge(PEER, 5, 0);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    NETW_CHECK_EQ(carrier.carry(session, again).sends.size(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a reliable row diffs against the snapshot its "
    "receipt confirmed, not against the one it last sent"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(retained_offer(1, 10, 20, peers(PEER)));
    const SessionResult first = carrier.carry(session, offers);
    NETW_CHECK_EQ(first.sends.size(), 1);
    CHECK(first.sends[0].reliable);
    NETW_CHECK_EQ(session.retained_lane_count(), 1);

    NETW_CHECK_EQ(carrier.pending_count(PEER), 0);

    LocalVector<RowOffer> again;
    again.push_back(retained_offer(1, 10, 20, peers(PEER)));
    NETW_CHECK_EQ(carrier.carry(session, again).caught_up, 1);

    LocalVector<RowOffer> unconfirmed;
    unconfirmed.push_back(retained_offer(1, 10, 21, peers(PEER)));
    const SessionResult absolute = carrier.carry(session, unconfirmed);
    NETW_REQUIRE_EQ(absolute.sends.size(), 1);
    NETW_CHECK_EQ(int64_t(absolute.sends[0].mask), int64_t(3));
    CHECK_FALSE(absolute.sends[0].masked);

    netw_test::accept_streams(session, unconfirmed, PEER);
    LocalVector<RowOffer> moved;
    moved.push_back(retained_offer(1, 10, 22, peers(PEER)));
    const SessionResult third = carrier.carry(session, moved);
    NETW_REQUIRE_EQ(third.sends.size(), 1);
    NETW_CHECK_EQ(int64_t(third.sends[0].mask), int64_t(2));
    CHECK(third.sends[0].masked);
}

TEST_CASE(
    "[Networked][Repl][Hosted] the two halves of one set share an address "
    "without colliding"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(retained_offer(1, 30, 40, peers(PEER)));

    const SessionResult result = carrier.carry(session, offers);
    NETW_CHECK_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.ungathered, 0);
    NETW_CHECK_EQ(session.lane_count(), 1);
    NETW_CHECK_EQ(session.retained_lane_count(), 1);

    NETW_CHECK_EQ(carrier.pending_count(PEER), 1);
}

TEST_CASE("[Networked][Repl][Hosted] the fitter never defers a reliable row") {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(retained_offer(2, 30, 40, peers(PEER)));

    const SessionResult result = drive_send(session, reg, offers, 1, 1);
    NETW_REQUIRE_EQ(result.sends.size(), 1);
    CHECK(result.sends[0].reliable);
    NETW_CHECK_EQ(result.sends[0].route, int64_t(2));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a departed peer is forgotten by both halves"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(retained_offer(1, 30, 40, peers(PEER)));
    NETW_REQUIRE_EQ(carrier.carry(session, offers).sends.size(), 2);
    carrier.commit(session, PEER, 5);
    session.acknowledge(PEER, 5, 0);

    LocalVector<int> nobody;
    session.retain(nobody);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(retained_offer(1, 30, 40, peers(PEER)));
    const SessionResult healed = carrier.carry(session, again);
    NETW_CHECK_EQ(healed.sends.size(), 2);
    NETW_CHECK_EQ(healed.caught_up, 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a dead route takes its retained lane with it"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(retained_offer(1, 30, 40, peers(PEER)));
    NETW_REQUIRE_EQ(carrier.carry(session, offers).sends.size(), 1);

    session.close_route(1);
    NETW_CHECK_EQ(session.retained_lane_count(), 0);

    LocalVector<RowOffer> reused;
    reused.push_back(retained_offer(1, 30, 40, peers(PEER)));
    NETW_CHECK_EQ(carrier.carry(session, reused).sends.size(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] one row's audience change leaves the other "
    "rows alone in both halves"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(retained_offer(1, 30, 40, peers(PEER, OTHER)));
    NETW_REQUIRE_EQ(carrier.carry(session, offers).sends.size(), 2);

    session.retain_row(1, 0, peers(PEER));

    LocalVector<RowOffer> again;
    again.push_back(retained_offer(1, 30, 40, peers(PEER, OTHER)));
    const SessionResult result = carrier.carry(session, again);
    const bool only_the_dropped_peer_is_owed_again
        = result.sends.size() == 1 && result.sends[0].peer == OTHER;
    CHECK(only_the_dropped_peer_is_owed_again);
    NETW_CHECK_EQ(result.caught_up, 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a windowed pass repeats every tick still in "
    "flight, up to the ring's depth"
) {
    SessionSend session;
    Carrier carrier;
    for (int64_t tick = 40; tick <= 42; ++tick) {
        LocalVector<RowOffer> offers;
        offers.push_back(window_offer(1, tick, 10 + tick, peers(PEER)));
        const SessionResult result = carrier.carry(session, offers);
        NETW_REQUIRE_EQ(result.sends.size(), 1);
        CHECK(result.sends[0].windowed);
        NETW_CHECK_EQ(result.sends[0].sample_count, uint32_t(tick - 39));
    }
    NETW_CHECK_EQ(session.window_lane_count(), 1);

    LocalVector<RowOffer> fourth;
    fourth.push_back(window_offer(1, 43, 99, peers(PEER)));
    const SessionResult result = carrier.carry(session, fourth);
    NETW_REQUIRE_EQ(result.sends.size(), 1);
    NETW_CHECK_EQ(result.sends[0].sample_count, 3);
    NETW_CHECK_EQ(oldest_sample_tick(result.sends[0]), int64_t(41));
}

TEST_CASE(
    "[Networked][Repl][Hosted] every recipient of a windowed row is sent the "
    "same window"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(window_offer(1, 40, 10, peers(PEER, OTHER)));
    const SessionResult result = carrier.carry(session, offers);

    NETW_REQUIRE_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.sends[0].sample_count, 1);
    NETW_CHECK_EQ(result.sends[1].sample_count, 1);
    NETW_CHECK_EQ(carrier.pending_count(PEER), 1);
    NETW_CHECK_EQ(carrier.pending_count(OTHER), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a windowed row the fitter defers is repeated "
    "rather than lost"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> deferred;
    deferred.push_back(window_offer(1, 40, 10, peers(PEER)));

    NETW_CHECK_EQ(drive_send(session, reg, deferred, 1, 1).sends.size(), 0);

    LocalVector<RowOffer> next;
    next.push_back(window_offer(1, 41, 11, peers(PEER)));
    const SessionResult result = drive_send(session, reg, next, 10000, 2);
    NETW_REQUIRE_EQ(result.sends.size(), 1);
    NETW_CHECK_EQ(result.sends[0].sample_count, 2);
    NETW_CHECK_EQ(oldest_sample_tick(result.sends[0]), int64_t(40));
}

TEST_CASE("[Networked][Repl][Hosted] all three lane shapes share one address") {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(retained_offer(1, 30, 40, peers(PEER)));
    offers.push_back(window_offer(1, 40, 50, peers(PEER)));

    const SessionResult result = carrier.carry(session, offers);
    NETW_CHECK_EQ(result.sends.size(), 3);
    NETW_CHECK_EQ(result.ungathered, 0);
    NETW_CHECK_EQ(session.lane_count(), 1);
    NETW_CHECK_EQ(session.retained_lane_count(), 1);
    NETW_CHECK_EQ(session.window_lane_count(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a dead route takes its windowed lane with it"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(window_offer(1, 40, 10, peers(PEER)));
    offers.push_back(window_offer(1, 41, 11, peers(PEER)));
    NETW_REQUIRE_EQ(carrier.carry(session, offers).sends.size(), 2);

    session.close_route(1);
    NETW_CHECK_EQ(session.window_lane_count(), 0);

    LocalVector<RowOffer> reused;
    reused.push_back(window_offer(1, 40, 10, peers(PEER)));
    const SessionResult result = carrier.carry(session, reused);
    NETW_REQUIRE_EQ(result.sends.size(), 1);
    NETW_CHECK_EQ(result.sends[0].sample_count, 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a run that split mid-pass binds each row to the "
    "datagram that carried it"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(offer(2, 20, peers(PEER)));
    const SessionResult pass = uncarried(session, offers);
    NETW_CHECK_EQ(pass.sends.size(), 2);
    if (pass.sends.size() != 2) {
        return;
    }

    carrier.attach(session, pass.sends[0]);
    carrier.commit(session, PEER, 5);
    carrier.attach(session, pass.sends[1]);
    carrier.commit(session, PEER, 6);

    LocalVector<RowOffer> settled;
    settled.push_back(offer(1, 10, peers(PEER)));
    netw_test::accept_streams(session, settled, PEER);

    LocalVector<RowOffer> moved;
    moved.push_back(offer(1, 11, peers(PEER)));
    moved.push_back(offer(2, 21, peers(PEER)));
    const SessionResult healed = uncarried(session, moved);
    NETW_CHECK_EQ(healed.sends.size(), 2);
    for (uint32_t at = 0; at < healed.sends.size(); ++at) {
        const bool only_the_confirmed_lane_takes_a_delta
            = healed.sends[at].masked == (healed.sends[at].route == 1);
        CHECK(only_the_confirmed_lane_takes_a_delta);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a send that never reached the carrier is never "
    "staged"
) {
    SessionSend session;
    Carrier carrier;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    NETW_CHECK_EQ(uncarried(session, offers).sends.size(), 1);

    NETW_CHECK_EQ(carrier.pending_count(PEER), 0);
    carrier.commit(session, PEER, 5);
    session.acknowledge(PEER, 5, 0);
    NETW_CHECK_EQ(session.explain(1, 0, PEER).exposed, 0);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    const SessionResult result = uncarried(session, again);
    NETW_CHECK_EQ(result.caught_up, 0);
    NETW_CHECK_EQ(result.sends.size(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a route never offered to a peer explains "
    "itself as unoffered rather than as caught up"
) {
    SessionSend session;
    const netw::repl::RowExplain held = session.explain(1, 0, PEER);

    const bool it_is_unoffered
        = held.verdict == netw::repl::RowVerdict::UNOFFERED;
    CHECK(it_is_unoffered);
    CHECK_FALSE(held.has_baseline);
}

TEST_CASE(
    "[Networked][Repl][Hosted] the pass records why each row did or did not "
    "reach its peer"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));

    drive_send(session, reg, offers, 1 << 20, 1);
    const bool the_sent_row_says_sent
        = session.explain(1, 0, PEER).verdict == netw::repl::RowVerdict::SENT;
    CHECK(the_sent_row_says_sent);

    netw_test::accept_streams(session, offers, PEER);
    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    drive_send(session, reg, again, 1 << 20, 2);
    const netw::repl::RowExplain settled = session.explain(1, 0, PEER);
    const bool the_settled_row_says_caught_up
        = settled.verdict == netw::repl::RowVerdict::CAUGHT_UP;
    CHECK(the_settled_row_says_caught_up);
    CHECK(settled.has_baseline);

    LocalVector<RowOffer> moved;
    moved.push_back(offer(1, 11, peers(PEER)));
    drive_send(session, reg, moved, 1, 3);
    const netw::repl::RowExplain starved = session.explain(1, 0, PEER);
    const bool the_budget_says_deferred
        = starved.verdict == netw::repl::RowVerdict::DEFERRED;
    CHECK(the_budget_says_deferred);
    const SessionResult repaid = drive_send(session, reg, moved, 1 << 20, 4);
    NETW_CHECK_EQ(repaid.sends.size(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a row that will not gather explains itself as "
    "ungathered"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    RowOffer bad = offer(1, 10, peers(PEER));
    bad.values = Array();

    LocalVector<RowOffer> offers;
    offers.push_back(bad);
    ERR_PRINT_OFF;
    drive_send(session, reg, offers, 1 << 20, 1);
    ERR_PRINT_ON;

    const bool it_says_ungathered = session.explain(1, 0, PEER).verdict
        == netw::repl::RowVerdict::UNGATHERED;
    CHECK(it_says_ungathered);
}

TEST_CASE(
    "[Networked][Repl][Hosted] control spends what the rows left of a peer's "
    "budget, and never less than its reservation"
) {
    const WireRegistry reg = registry();
    const int64_t reserved = 256;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(offer(2, 20, peers(PEER)));

    SessionSend roomy;
    const int64_t budget = 1 << 14;
    const SessionResult light = drive_send(roomy, reg, offers, budget, 1);
    int64_t rows_bits = 0;
    for (uint32_t at = 0; at < light.sends.size(); ++at) {
        rows_bits += light.sends[at].bits;
    }
    NETW_CHECK_EQ(
        roomy.control_budget_bytes(PEER, reserved),
        (budget - rows_bits) / 8
    );
    NETW_CHECK_EQ(roomy.control_budget_bytes(OTHER, reserved), budget / 8);

    SessionSend full;
    drive_send(full, reg, offers, one_frame_bits(offers), 1);
    NETW_CHECK_EQ(full.control_budget_bytes(PEER, reserved), reserved);
}

} // namespace TestNetwReplSessionSend

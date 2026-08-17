// One pass through the whole send side, and the sequencing it makes
// impossible.
//
// Every component under this is already law-covered. What only the sequence
// can be held to is the set of mistakes a plausible caller makes while every
// component it calls behaves perfectly: staging a row for a peer that was not
// sent one, staging a row the fitter deferred, and acking one lane when the
// datagram carried several. Each leaves the books internally consistent and
// the session wrong.

#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/table/schema_core.hpp"

namespace TestNetwReplSessionSend {

using godot::Array;
using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::repl::RowOffer;
using netw::repl::SessionResult;
using netw::repl::SessionSend;
using netw::wire::ChannelDecl;
using netw::wire::Delivery;
using netw::wire::WireRegistry;

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

Ref<SchemaRecord> body() {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Body");
    SchemaCore::append_column(record, godot::StringName("x"), SchemaCore::I16, 1);
    SchemaCore::fix(record);
    return record;
}

// One deferred pass whose every frame reached the carrier, which is the
// ordinary shape: nothing was dropped between the pass and the wire, and the
// peer's run did not split. The cases that are about the other shapes call
// `run_deferred` and `defer` themselves.
SessionResult carried(
    SessionSend &session,
    const LocalVector<RowOffer> &p_offers
) {
    SessionResult out = session.run_deferred(p_offers);
    for (uint32_t at = 0; at < out.sends.size(); ++at) {
        session.defer(out.sends[at]);
    }
    return out;
}

RowOffer offer(int64_t route, int64_t value, const LocalVector<int> &peers) {
    RowOffer out;
    out.route = route;
    out.comp = 0;
    out.channel = CHANNEL;
    out.schema = body();
    out.values.push_back(value);
    out.recipients = peers;
    out.masked = true;
    return out;
}

Ref<SchemaRecord> banner() {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Banner");
    SchemaCore::append_column(record, godot::StringName("hp"), SchemaCore::I32, 1);
    SchemaCore::append_column(record, godot::StringName("mp"), SchemaCore::I32, 1);
    SchemaCore::fix(record);
    return record;
}

// The retained half of the SAME address: one route, one ordinal, its own
// schema. A collision with the volatile half would refuse the gather, because
// neither row is valid for the other's plan.
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
    out.schema = banner();
    out.values.push_back(hp);
    out.values.push_back(mp);
    out.recipients = peers;
    out.reliable = true;
    return out;
}

// A windowed offer at its own address. The offer hands over ONE tick and the
// lane answers with the range still in flight, which is the whole difference
// between this shape and the other three.
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
    out.schema = body();
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

    const SessionResult result = session.run(reg, offers, 10000, 1, 500);
    NETW_CHECK_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.caught_up, 0);
    NETW_CHECK_EQ(result.ungathered, 0);
    NETW_CHECK_EQ(session.lane_count(), 1);
    CHECK_FALSE(result.untrackable);
}

TEST_CASE(
    "[Networked][Repl][Hosted] an unchanged row after an ack costs the pass "
    "nothing and says so"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> first;
    first.push_back(offer(1, 10, peers(PEER)));
    REQUIRE(session.run(reg, first, 10000, 1, 500).sends.size() == 1);
    session.acknowledge(PEER, 1);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    const SessionResult result = session.run(reg, again, 10000, 2, 501);

    // Nothing sent, and the result says WHY. A pass that reported only an
    // empty send list reads the same whether the session was caught up or
    // whether every gather refused.
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
    const SessionResult result = session.run(reg, offers, 10000, 1, 500);
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

    // A budget that admits one row of the three.
    const SessionResult first = session.run(reg, offers, 16, 1, 500);
    REQUIRE(first.sends.size() == 1);
    const int64_t rode = first.sends[0].route;

    session.acknowledge(PEER, 1);

    // Every row is offered again, unchanged. The two the fitter deferred were
    // never sent, so the peer still owes them in full; staging them last pass
    // would have told the book the peer already holds them and this pass would
    // send nothing at all.
    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(offer(2, 20, peers(PEER)));
    again.push_back(offer(3, 30, peers(PEER)));
    const SessionResult second = session.run(reg, again, 10000, 2, 501);

    NETW_CHECK_EQ(second.sends.size(), 2);
    NETW_CHECK_EQ(second.caught_up, 1);
    for (uint32_t at = 0; at < second.sends.size(); ++at) {
        CHECK(second.sends[at].route != rode);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] one ack settles every lane the datagram carried"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(offer(2, 20, peers(PEER)));
    REQUIRE(session.run(reg, offers, 10000, 1, 500).sends.size() == 2);

    session.acknowledge(PEER, 1);

    // A datagram carries frames from many lanes and an ack names the datagram,
    // so an ack that reached one lane would leave the others diffing against a
    // row the peer has already superseded, and every pass after would resend
    // columns that never moved.
    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(offer(2, 20, peers(PEER)));
    const SessionResult result = session.run(reg, again, 10000, 2, 501);
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
    REQUIRE(session.run(reg, offers, 10000, 1, 500).sends.size() == 2);
    session.acknowledge(PEER, 1);

    LocalVector<int> nobody;
    session.retain(nobody);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(offer(2, 20, peers(PEER)));
    const SessionResult result = session.run(reg, again, 10000, 2, 501);
    NETW_CHECK_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.caught_up, 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a deferred pass owes the row again until the "
    "seq that carried it is named"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    REQUIRE(carried(session, offers).sends.size() == 1);
    NETW_CHECK_EQ(session.pending_count(PEER), 1);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    const SessionResult result = carried(session, again);
    NETW_CHECK_EQ(result.sends.size(), 1);
    NETW_CHECK_EQ(result.caught_up, 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a plain lane sends an unchanged row anyway, and "
    "keeps no baseline to be caught up against"
) {
    SessionSend session;
    LocalVector<RowOffer> first;
    first.push_back(offer(1, 10, peers(PEER)));
    first[0].masked = false;
    REQUIRE(carried(session, first).sends.size() == 1);

    session.commit(PEER, 1);
    session.acknowledge(PEER, 1);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again[0].masked = false;
    const SessionResult plain = carried(session, again);
    NETW_CHECK_EQ(plain.sends.size(), 1);
    NETW_CHECK_EQ(plain.caught_up, 0);
    CHECK(plain.sends[0].mask != 0);
    CHECK_FALSE(plain.sends[0].masked);

    LocalVector<RowOffer> diffed;
    diffed.push_back(offer(1, 10, peers(PEER)));
    REQUIRE(carried(session, diffed).sends.size() == 1);
    session.commit(PEER, 2);
    session.acknowledge(PEER, 2);
    LocalVector<RowOffer> settled;
    settled.push_back(offer(1, 10, peers(PEER)));
    NETW_CHECK_EQ(carried(session, settled).sends.size(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a commit binds the pass to one peer's own seq"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER, OTHER)));
    REQUIRE(carried(session, offers).sends.size() == 2);

    session.commit(PEER, 5);
    session.acknowledge(OTHER, 5);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER, OTHER)));
    const SessionResult result = carried(session, again);
    NETW_CHECK_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.caught_up, 0);

    session.acknowledge(PEER, 5);
    LocalVector<RowOffer> third;
    third.push_back(offer(1, 10, peers(PEER)));
    NETW_CHECK_EQ(carried(session, third).caught_up, 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a committed pass is committed once"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    REQUIRE(carried(session, offers).sends.size() == 1);

    session.commit(PEER, 5);
    NETW_CHECK_EQ(session.pending_count(PEER), 0);

    session.commit(PEER, 6);
    session.acknowledge(PEER, 5);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    NETW_CHECK_EQ(carried(session, again).caught_up, 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that leaves before its commit is "
    "forgotten rather than staged later"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    REQUIRE(carried(session, offers).sends.size() == 1);

    LocalVector<int> nobody;
    session.retain(nobody);
    NETW_CHECK_EQ(session.pending_count(PEER), 0);

    session.commit(PEER, 5);
    session.acknowledge(PEER, 5);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    NETW_CHECK_EQ(carried(session, again).sends.size(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a reliable row advances on send, with no ack "
    "and no commit"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(retained_offer(1, 10, 20, peers(PEER)));
    const SessionResult first = carried(session, offers);
    NETW_CHECK_EQ(first.sends.size(), 1);
    CHECK(first.sends[0].reliable);
    NETW_CHECK_EQ(session.retained_lane_count(), 1);

    // Nothing waits on a seq, so there is nothing a commit could bind.
    NETW_CHECK_EQ(session.pending_count(PEER), 0);

    LocalVector<RowOffer> again;
    again.push_back(retained_offer(1, 10, 20, peers(PEER)));
    NETW_CHECK_EQ(carried(session, again).caught_up, 1);

    LocalVector<RowOffer> moved;
    moved.push_back(retained_offer(1, 10, 21, peers(PEER)));
    const SessionResult third = carried(session, moved);
    REQUIRE(third.sends.size() == 1);
    NETW_CHECK_EQ(int64_t(third.sends[0].mask), int64_t(2));
}

TEST_CASE(
    "[Networked][Repl][Hosted] the two halves of one set share an address "
    "without colliding"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(retained_offer(1, 30, 40, peers(PEER)));

    const SessionResult result = carried(session, offers);
    NETW_CHECK_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.ungathered, 0);
    NETW_CHECK_EQ(session.lane_count(), 1);
    NETW_CHECK_EQ(session.retained_lane_count(), 1);

    // Only the volatile half is waiting on the seq it rides.
    NETW_CHECK_EQ(session.pending_count(PEER), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] the fitter never defers a reliable row"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(retained_offer(2, 30, 40, peers(PEER)));

    // A budget too small for either row. The volatile one waits for the next
    // pass, which costs nothing because its baseline did not move. The
    // reliable one already advanced when its mask was taken, so a deferral
    // would drop those columns with no later frame to carry them.
    const SessionResult result = session.run(reg, offers, 1, 1, 500);
    REQUIRE(result.sends.size() == 1);
    CHECK(result.sends[0].reliable);
    NETW_CHECK_EQ(result.sends[0].route, int64_t(2));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a departed peer is forgotten by both halves"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(retained_offer(1, 30, 40, peers(PEER)));
    REQUIRE(carried(session, offers).sends.size() == 2);
    session.commit(PEER, 5);
    session.acknowledge(PEER, 5);

    LocalVector<int> nobody;
    session.retain(nobody);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(retained_offer(1, 30, 40, peers(PEER)));
    const SessionResult healed = carried(session, again);
    NETW_CHECK_EQ(healed.sends.size(), 2);
    NETW_CHECK_EQ(healed.caught_up, 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a dead route takes its retained lane with it"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(retained_offer(1, 30, 40, peers(PEER)));
    REQUIRE(carried(session, offers).sends.size() == 1);

    session.close_route(1);
    NETW_CHECK_EQ(session.retained_lane_count(), 0);

    LocalVector<RowOffer> reused;
    reused.push_back(retained_offer(1, 30, 40, peers(PEER)));
    NETW_CHECK_EQ(carried(session, reused).sends.size(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] one row's audience change leaves the other "
    "rows alone in both halves"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(retained_offer(1, 30, 40, peers(PEER, OTHER)));
    REQUIRE(carried(session, offers).sends.size() == 2);

    session.retain_row(1, 0, peers(PEER));

    LocalVector<RowOffer> again;
    again.push_back(retained_offer(1, 30, 40, peers(PEER, OTHER)));
    const SessionResult result = carried(session, again);
    REQUIRE(result.sends.size() == 1);
    NETW_CHECK_EQ(result.sends[0].peer, OTHER);
    NETW_CHECK_EQ(result.caught_up, 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a windowed pass repeats every tick still in "
    "flight, up to the ring's depth"
) {
    SessionSend session;
    for (int64_t tick = 40; tick <= 42; ++tick) {
        LocalVector<RowOffer> offers;
        offers.push_back(window_offer(1, tick, 10 + tick, peers(PEER)));
        const SessionResult result = carried(session, offers);
        REQUIRE(result.sends.size() == 1);
        CHECK(result.sends[0].windowed);
        NETW_CHECK_EQ(result.sends[0].samples.size(), uint32_t(tick - 39));
    }
    NETW_CHECK_EQ(session.window_lane_count(), 1);

    // The ring is bounded, so a fourth tick does not make a fourth sample.
    LocalVector<RowOffer> fourth;
    fourth.push_back(window_offer(1, 43, 99, peers(PEER)));
    const SessionResult result = carried(session, fourth);
    REQUIRE(result.sends.size() == 1);
    NETW_CHECK_EQ(result.sends[0].samples.size(), 3);
    NETW_CHECK_EQ(result.sends[0].samples[0].tick, int64_t(41));
}

TEST_CASE(
    "[Networked][Repl][Hosted] every recipient of a windowed row is sent the "
    "same window"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(window_offer(1, 40, 10, peers(PEER, OTHER)));
    const SessionResult result = carried(session, offers);

    REQUIRE(result.sends.size() == 2);
    NETW_CHECK_EQ(result.sends[0].samples.size(), 1);
    NETW_CHECK_EQ(result.sends[1].samples.size(), 1);
    // Nothing waits on a seq: a windowed lane holds no per-peer baseline, so
    // there is no book a commit could advance.
    NETW_CHECK_EQ(session.pending_count(PEER), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a windowed row the fitter defers is repeated "
    "rather than lost"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> deferred;
    deferred.push_back(window_offer(1, 40, 10, peers(PEER)));

    // Unlike a reliable row, a deferred windowed row costs nothing: the ring
    // still holds tick 40, so the next pass carries it beside tick 41. This is
    // why this lane may be fitted and the retained one may not.
    NETW_CHECK_EQ(session.run(reg, deferred, 1, 1, 500).sends.size(), 0);

    LocalVector<RowOffer> next;
    next.push_back(window_offer(1, 41, 11, peers(PEER)));
    const SessionResult result = session.run(reg, next, 10000, 2, 501);
    REQUIRE(result.sends.size() == 1);
    NETW_CHECK_EQ(result.sends[0].samples.size(), 2);
    NETW_CHECK_EQ(result.sends[0].samples[0].tick, int64_t(40));
}

TEST_CASE(
    "[Networked][Repl][Hosted] all three lane shapes share one address"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(retained_offer(1, 30, 40, peers(PEER)));
    offers.push_back(window_offer(1, 40, 50, peers(PEER)));

    const SessionResult result = carried(session, offers);
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
    LocalVector<RowOffer> offers;
    offers.push_back(window_offer(1, 40, 10, peers(PEER)));
    offers.push_back(window_offer(1, 41, 11, peers(PEER)));
    REQUIRE(carried(session, offers).sends.size() == 2);

    session.close_route(1);
    NETW_CHECK_EQ(session.window_lane_count(), 0);

    LocalVector<RowOffer> reused;
    reused.push_back(window_offer(1, 40, 10, peers(PEER)));
    const SessionResult result = carried(session, reused);
    REQUIRE(result.sends.size() == 1);
    // The next entity at this address starts with an empty ring rather than
    // repeating the last one's ticks at it.
    NETW_CHECK_EQ(result.sends[0].samples.size(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a run that split mid-pass binds each row to the "
    "datagram that carried it"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(offer(2, 20, peers(PEER)));
    const SessionResult pass = session.run_deferred(offers);
    NETW_CHECK_EQ(pass.sends.size(), 2);
    if (pass.sends.size() != 2) {
        return;
    }

    // The carrier took the first frame, overflowed on the second, and sent
    // what it held as datagram 5. Only the first row rode in it.
    session.defer(pass.sends[0]);
    session.commit(PEER, 5);
    session.defer(pass.sends[1]);
    session.commit(PEER, 6);

    // Datagram 6 is lost and 5 is acknowledged. Binding both rows to 5 would
    // advance the second row's baseline to a value the peer never received,
    // and every later pass would diff against it with no frame left to
    // correct it.
    session.acknowledge(PEER, 5);
    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(offer(2, 20, peers(PEER)));
    const SessionResult healed = session.run_deferred(again);
    NETW_CHECK_EQ(healed.caught_up, 1);
    NETW_CHECK_EQ(healed.sends.size(), 1);
    if (healed.sends.size() == 1) {
        NETW_CHECK_EQ(healed.sends[0].route, 2);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a send that never reached the carrier is never "
    "staged"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    NETW_CHECK_EQ(session.run_deferred(offers).sends.size(), 1);

    // The frame was dropped between the pass and the wire, so nothing is owed
    // a sequence and the commit that follows has nothing of it to bind.
    NETW_CHECK_EQ(session.pending_count(PEER), 0);
    session.commit(PEER, 5);
    session.acknowledge(PEER, 5);

    // The peer holds nothing, so the row is owed again whole.
    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    const SessionResult result = session.run_deferred(again);
    NETW_CHECK_EQ(result.caught_up, 0);
    NETW_CHECK_EQ(result.sends.size(), 1);
}

} // namespace TestNetwReplSessionSend

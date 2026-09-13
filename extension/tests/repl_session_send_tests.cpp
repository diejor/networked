#include "support/netw_test.h"
#include "support/send_drive.h"

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
    netw::repl::RowFrameHeader header;
    LocalVector<netw::repl::WindowSample> samples;
    if (!netw::repl::read_window_frame(
            p_send.bytes,
            0,
            -1,
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
    return session.run(reg, p_offers, 1 << 20, 0);
}

int64_t one_frame_bits(const LocalVector<RowOffer> &p_offers) {
    SessionSend probe;
    const SessionResult out = uncarried(probe, p_offers);
    return out.sends.is_empty() ? 0 : out.sends[0].bits;
}

SessionResult carried(
    SessionSend &session,
    const LocalVector<RowOffer> &p_offers
) {
    SessionResult out = uncarried(session, p_offers);
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
    "[Networked][Repl][Hosted] an unchanged row after an ack costs the pass "
    "nothing and says so"
) {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> first;
    first.push_back(offer(1, 10, peers(PEER)));
    REQUIRE(drive_send(session, reg, first, 10000, 1).sends.size() == 1);
    session.acknowledge(PEER, 1, 0);

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
    REQUIRE(first.sends.size() == 1);
    const int64_t rode = first.sends[0].route;

    session.acknowledge(PEER, 1, 0);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(offer(2, 20, peers(PEER)));
    again.push_back(offer(3, 30, peers(PEER)));
    const SessionResult second = drive_send(session, reg, again, 10000, 2);

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
    REQUIRE(drive_send(session, reg, offers, 10000, 1).sends.size() == 2);

    session.acknowledge(PEER, 1, 0);

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
    REQUIRE(drive_send(session, reg, offers, 10000, 1).sends.size() == 2);
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
    session.acknowledge(PEER, 1, 0);

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
    session.acknowledge(PEER, 2, 0);
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
    session.acknowledge(OTHER, 5, 0);

    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER, OTHER)));
    const SessionResult result = carried(session, again);
    NETW_CHECK_EQ(result.sends.size(), 2);
    NETW_CHECK_EQ(result.caught_up, 0);

    session.acknowledge(PEER, 5, 0);
    LocalVector<RowOffer> third;
    third.push_back(offer(1, 10, peers(PEER)));
    NETW_CHECK_EQ(carried(session, third).caught_up, 1);
}

TEST_CASE("[Networked][Repl][Hosted] a committed pass is committed once") {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    REQUIRE(carried(session, offers).sends.size() == 1);

    session.commit(PEER, 5);
    NETW_CHECK_EQ(session.pending_count(PEER), 0);

    session.commit(PEER, 6);
    session.acknowledge(PEER, 5, 0);

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
    session.acknowledge(PEER, 5, 0);

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

    NETW_CHECK_EQ(session.pending_count(PEER), 1);
}

TEST_CASE("[Networked][Repl][Hosted] the fitter never defers a reliable row") {
    const WireRegistry reg = registry();
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(retained_offer(2, 30, 40, peers(PEER)));

    const SessionResult result = drive_send(session, reg, offers, 1, 1);
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
    session.acknowledge(PEER, 5, 0);

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
        NETW_CHECK_EQ(result.sends[0].sample_count, uint32_t(tick - 39));
    }
    NETW_CHECK_EQ(session.window_lane_count(), 1);

    LocalVector<RowOffer> fourth;
    fourth.push_back(window_offer(1, 43, 99, peers(PEER)));
    const SessionResult result = carried(session, fourth);
    REQUIRE(result.sends.size() == 1);
    NETW_CHECK_EQ(result.sends[0].sample_count, 3);
    NETW_CHECK_EQ(oldest_sample_tick(result.sends[0]), int64_t(41));
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
    NETW_CHECK_EQ(result.sends[0].sample_count, 1);
    NETW_CHECK_EQ(result.sends[1].sample_count, 1);
    NETW_CHECK_EQ(session.pending_count(PEER), 1);
    NETW_CHECK_EQ(session.pending_count(OTHER), 1);
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
    REQUIRE(result.sends.size() == 1);
    NETW_CHECK_EQ(result.sends[0].sample_count, 2);
    NETW_CHECK_EQ(oldest_sample_tick(result.sends[0]), int64_t(40));
}

TEST_CASE("[Networked][Repl][Hosted] all three lane shapes share one address") {
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
    NETW_CHECK_EQ(result.sends[0].sample_count, 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a run that split mid-pass binds each row to the "
    "datagram that carried it"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 10, peers(PEER)));
    offers.push_back(offer(2, 20, peers(PEER)));
    const SessionResult pass = uncarried(session, offers);
    NETW_CHECK_EQ(pass.sends.size(), 2);
    if (pass.sends.size() != 2) {
        return;
    }

    session.defer(pass.sends[0]);
    session.commit(PEER, 5);
    session.defer(pass.sends[1]);
    session.commit(PEER, 6);

    session.acknowledge(PEER, 5, 0);
    LocalVector<RowOffer> again;
    again.push_back(offer(1, 10, peers(PEER)));
    again.push_back(offer(2, 20, peers(PEER)));
    const SessionResult healed = uncarried(session, again);
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
    NETW_CHECK_EQ(uncarried(session, offers).sends.size(), 1);

    NETW_CHECK_EQ(session.pending_count(PEER), 0);
    session.commit(PEER, 5);
    session.acknowledge(PEER, 5, 0);

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

    session.acknowledge(PEER, 1, 0);
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
    CHECK(starved.sticky != 0);
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

} // namespace TestNetwReplSessionSend

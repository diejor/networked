#include "support/netw_test.h"

#include "support/send_rig.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/wire/control_record.hpp"
#include "netw/wire/registry.hpp"
#include "netw/wire/stream_book.hpp"

using namespace godot;

namespace TestRowStreamGate {

using netw::SchemaCore;
using netw::repl::RowVerdict;
using netw::repl::SessionResult;
using netw::table::SchemaRecord;
using netw::wire::ChannelDecl;
using netw::wire::ControlRecord;
using netw::wire::ControlTag;
using netw::wire::Delivery;
using netw::wire::next_stream_token;
using netw::wire::StreamFamily;
using netw::wire::StreamLane;
using netw::wire::WireRegistry;
using netw_test::RowOffer;
using netw_test::SessionSend;

constexpr uint8_t CHANNEL = 45;
constexpr int PEER = 7;
constexpr int64_t ROUTE = 3;
constexpr int64_t EPOCH = 2;
constexpr int64_t WIDE = 1 << 20;

const WireRegistry &registry() {
    static const WireRegistry made = [] {
        WireRegistry out;
        ChannelDecl decl;
        decl.id = CHANNEL;
        decl.name = godot::StringName("gate_probe");
        decl.delivery = Delivery::FITTED;
        out.register_channel(decl);
        return out;
    }();
    return made;
}

const SchemaRecord &body() {
    static SchemaRecord record = [] {
        SchemaRecord made;
        made.name = godot::StringName("GateBody");
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

RowOffer offer(int64_t p_value) {
    RowOffer out;
    out.route = ROUTE;
    out.comp = 0;
    out.channel = CHANNEL;
    out.schema = &body();
    out.life = EPOCH;
    netw_test::offer_value(out, p_value);
    out.recipients.push_back(PEER);
    return out;
}

LocalVector<RowOffer> one(int64_t p_value) {
    LocalVector<RowOffer> out;
    out.push_back(offer(p_value));
    return out;
}

StreamLane volatile_lane() {
    StreamLane lane;
    lane.route = ROUTE;
    lane.ordinal = 0;
    lane.family = StreamFamily::VOLATILE;
    return lane;
}

LocalVector<ControlRecord> drain(SessionSend &p_send, int64_t p_now_ms) {
    return p_send.control_scheduler().flush(PEER, p_now_ms, 4096);
}

LocalVector<RowOffer> bound(int64_t p_value, uint64_t p_tenure) {
    LocalVector<RowOffer> out;
    out.push_back(offer(p_value));
    out[0].tenure.bound = true;
    out[0].tenure.tenure = p_tenure;
    out[0].heartbeat = 30;
    return out;
}

uint64_t seat(SessionSend &r_send, int64_t p_value, uint64_t p_tenure) {
    r_send.run(registry(), bound(p_value, p_tenure), WIDE, 0, 0);
    const LocalVector<ControlRecord> asked = drain(r_send, 0);
    const bool asked_once = asked.size() == 1;
    REQUIRE(asked_once);
    const uint64_t token = next_stream_token();
    r_send.stream_ready(PEER, asked[0].request, token);
    return token;
}

TEST_CASE(
    "[Networked][Wire][Hosted] G1 a lane the receiver has not opened sends no "
    "row and asks once, so no state can precede the stream that names it"
) {
    SessionSend send;
    const SessionResult first = send.run(registry(), one(10), WIDE, 0);
    NETW_CHECK_EQ(int(first.sends.size()), 0);
    NETW_CHECK_EQ(
        int(send.explain(ROUTE, 0, PEER).verdict),
        int(RowVerdict::DEFERRED)
    );

    const LocalVector<ControlRecord> asked = drain(send, 0);
    NETW_CHECK_EQ(int(asked.size()), 1);
    const bool asked_once = asked.size() == 1;
    REQUIRE(asked_once);
    NETW_CHECK_EQ(int(asked[0].tag), int(ControlTag::OPEN));
    NETW_CHECK_EQ(asked[0].route, ROUTE);
    NETW_CHECK_EQ(int(asked[0].ordinal), 0);
    NETW_CHECK_EQ(int(asked[0].family), int(StreamFamily::VOLATILE));
    NETW_CHECK_EQ(int64_t(asked[0].epoch), EPOCH);
    NETW_CHECK_EQ(
        int64_t(asked[0].schema),
        int64_t(uint32_t(body().shape_hash))
    );
    NETW_CHECK_ORDER(asked[0].request, uint64_t(0), >);
}

TEST_CASE(
    "[Networked][Wire][Hosted] G2 a READY seats the token and the very next "
    "pass sends the row the gate was holding"
) {
    SessionSend send;
    send.run(registry(), one(10), WIDE, 0);
    const LocalVector<ControlRecord> asked = drain(send, 0);
    const bool asked_once = asked.size() == 1;
    REQUIRE(asked_once);

    const uint64_t token = next_stream_token();
    send.stream_ready(PEER, asked[0].request, token);
    NETW_CHECK_EQ(send.token_of(PEER, volatile_lane()), token);

    const SessionResult after = send.run(registry(), one(10), WIDE, 0);
    NETW_CHECK_EQ(int(after.sends.size()), 1);
    NETW_CHECK_EQ(
        int(send.explain(ROUTE, 0, PEER).verdict),
        int(RowVerdict::SENT)
    );
}

TEST_CASE(
    "[Networked][Wire][Hosted] G3 a lane that waits many passes asks once per "
    "flush, because a queued OPEN coalesces onto its own subject"
) {
    SessionSend send;
    for (int at = 0; at < 10; ++at) {
        send.run(registry(), one(10 + at), WIDE, 0);
    }
    NETW_CHECK_EQ(int(send.control_scheduler().pending_lifecycle(PEER)), 1);

    const LocalVector<ControlRecord> asked = drain(send, 0);
    NETW_CHECK_EQ(int(asked.size()), 1);
    NETW_CHECK_EQ(int(send.control_scheduler().pending_lifecycle(PEER)), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] G4 a RESET takes the lane back behind the "
    "gate, so the writer asks again and sends nothing until it is answered"
) {
    SessionSend send;
    send.run(registry(), one(10), WIDE, 0);
    const LocalVector<ControlRecord> asked = drain(send, 0);
    const bool asked_once = asked.size() == 1;
    REQUIRE(asked_once);
    const uint64_t request = asked[0].request;
    send.stream_ready(PEER, request, next_stream_token());
    NETW_CHECK_EQ(int(send.run(registry(), one(11), WIDE, 0).sends.size()), 1);

    const uint64_t token = send.token_of(PEER, volatile_lane());
    CHECK(send.stream_reset(PEER, request, token));
    NETW_CHECK_EQ(send.token_of(PEER, volatile_lane()), 0);

    const SessionResult barred = send.run(registry(), one(12), WIDE, 0);
    NETW_CHECK_EQ(int(barred.sends.size()), 0);
    const LocalVector<ControlRecord> again = drain(send, 60);
    NETW_CHECK_EQ(int(again.size()), 1);
    const bool asked_again = again.size() == 1;
    REQUIRE(asked_again);
    NETW_CHECK_EQ(int(again[0].tag), int(ControlTag::OPEN));
    const bool a_reset_lane_asks_under_a_fresh_request
        = again[0].request > request;
    CHECK(a_reset_lane_asks_under_a_fresh_request);
}

TEST_CASE(
    "[Networked][Wire][Hosted] G5 an OPEN still waiting for its answer is "
    "asked again only once the repair interval has passed, so a slow link "
    "carries one OPEN per interval and not one per pass"
) {
    SessionSend send;
    send.run(registry(), one(10), WIDE, 0, 0);
    const LocalVector<ControlRecord> asked = drain(send, 0);
    const bool asked_once = asked.size() == 1;
    REQUIRE(asked_once);
    const int64_t interval = netw::wire::repair_interval_ms(0);

    int quiet = 0;
    for (int64_t now = 33; now < interval; now += 33) {
        send.run(registry(), one(10), WIDE, now / 33, now);
        quiet += int(drain(send, now).size());
    }
    NETW_CHECK_EQ(quiet, 0);

    send.run(registry(), one(10), WIDE, interval / 33 + 1, interval);
    const LocalVector<ControlRecord> again = drain(send, interval);
    NETW_CHECK_EQ(int(again.size()), 1);
    const bool asked_again = again.size() == 1;
    REQUIRE(asked_again);
    NETW_CHECK_EQ(int(again[0].tag), int(ControlTag::OPEN));
    NETW_CHECK_EQ(again[0].request, asked[0].request);
}

TEST_CASE(
    "[Networked][Wire][Hosted] G6 a lane resumed under a later tenure sends "
    "its next row from the binding floor on the same token, and never "
    "repairs a row of the earlier tenure the receiver would drop"
) {
    SessionSend send;
    const uint64_t token = seat(send, 10, 1);
    const SessionResult first = send.run(registry(), bound(11, 1), WIDE, 1, 33);
    const bool sent = first.sends.size() == 1;
    REQUIRE(sent);
    const SessionResult repaired
        = send.run(registry(), bound(11, 1), WIDE, 12, 400);
    const bool repairs = repaired.sends.size() == 1
        && repaired.sends[0].form == netw::repl::RowForm::REPAIR;
    REQUIRE(repairs);
    NETW_CHECK_ORDER(repaired.sends[0].revision, uint64_t(256), <);

    send.park_tenures(ROUTE, 3);
    NETW_CHECK_EQ(send.token_of(PEER, volatile_lane()), 0);
    send.run(registry(), LocalVector<RowOffer>(), WIDE, 2, 66);

    const SessionResult resumed
        = send.run(registry(), bound(11, 3), WIDE, 40, 2000);
    NETW_CHECK_EQ(send.token_of(PEER, volatile_lane()), token);
    NETW_CHECK_EQ(int(resumed.sends.size()), 1);
    const bool from_floor = resumed.sends.size() == 1
        && resumed.sends[0].token == token
        && resumed.sends[0].revision >= 2 * netw::wire::STREAM_BINDING_SPAN;
    CHECK(from_floor);
    NETW_CHECK_EQ(int(drain(send, 2000).size()), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] G7 a heartbeat that comes due while its lane "
    "is parked is kept, so the resumed lane refreshes the receiver although "
    "nothing changed"
) {
    SessionSend send;
    const uint64_t token = seat(send, 10, 1);
    const SessionResult first = send.run(registry(), bound(10, 1), WIDE, 1, 33);
    const bool sent = first.sends.size() == 1;
    REQUIRE(sent);
    send.stream_receipt(PEER, token, first.sends[0].revision);
    REQUIRE_FALSE(netw_test::row_owed(send.world, volatile_lane(), PEER));

    send.park_tenures(ROUTE, 3);
    send.run(registry(), LocalVector<RowOffer>(), WIDE, 40, 1333);

    const SessionResult resumed
        = send.run(registry(), bound(10, 3), WIDE, 41, 1366);
    NETW_CHECK_EQ(int(resumed.sends.size()), 1);
    const bool refreshed = resumed.sends.size() == 1
        && resumed.sends[0].token == token
        && int(resumed.sends[0].form) == int(netw::repl::RowForm::REFRESH);
    CHECK(refreshed);
}

} // namespace TestRowStreamGate

#include "support/netw_test.h"

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
using netw::repl::RowOffer;
using netw::repl::RowVerdict;
using netw::repl::SessionResult;
using netw::repl::SessionSend;
using netw::table::SchemaRecord;
using netw::wire::ChannelDecl;
using netw::wire::ControlRecord;
using netw::wire::ControlTag;
using netw::wire::Delivery;
using netw::wire::next_stream_token;
using netw::wire::StreamFamily;
using netw::wire::StreamLane;
using netw::wire::WireRegistry;

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
    out.values.push_back(p_value);
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
    send.writer_book().ready(PEER, asked[0].request, token);
    NETW_CHECK_EQ(send.writer_book().token_of(PEER, volatile_lane()), token);

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
    send.writer_book().ready(PEER, request, next_stream_token());
    NETW_CHECK_EQ(int(send.run(registry(), one(11), WIDE, 0).sends.size()), 1);

    const uint64_t token = send.writer_book().token_of(PEER, volatile_lane());
    CHECK(send.writer_book().reset(PEER, request, token));
    NETW_CHECK_EQ(send.writer_book().token_of(PEER, volatile_lane()), 0);

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

} // namespace TestRowStreamGate

#include "support/netw_test.h"

#include "support/send_rig.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/stream_book.hpp"

namespace TestRowStreamLifetime {

using netw::SchemaCore;
using netw::store::RowAddress;
using netw::store::World;
using netw::table::SchemaRecord;
using netw::wire::next_open_request;
using netw::wire::next_stream_token;
using netw::wire::OpenVerdict;
using netw::wire::ReadyVerdict;
using netw::wire::repair_interval_ms;
using netw::wire::StreamFamily;
using netw::wire::StreamLane;
using netw::wire::StreamReaderBook;
using netw::wire::StreamTenure;

const int HOST = 1;
const int CLIENT = 2;
const uint64_t EPOCH = 4;
const uint32_t SCHEMA = 0xC0FFEE;

StreamLane lane_of(int64_t p_route, uint8_t p_ordinal, StreamFamily p_family) {
    StreamLane lane;
    lane.route = p_route;
    lane.ordinal = p_ordinal;
    lane.family = p_family;
    return lane;
}

const SchemaRecord &body() {
    static SchemaRecord record = [] {
        SchemaRecord made;
        made.name = godot::StringName("LifetimeBody");
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

RowAddress declare(World &r_world, const StreamLane &p_lane, int p_peer) {
    netw_test::RowOffer offer;
    offer.route = p_lane.route;
    offer.comp = p_lane.ordinal;
    offer.schema = &body();
    offer.life = int64_t(EPOCH);
    offer.reliable = p_lane.family == StreamFamily::RETAINED;
    offer.windowed = p_lane.family == StreamFamily::WINDOW;
    netw_test::offer_value(offer, 1);
    offer.recipients.push_back(p_peer);
    return netw_test::stage_offer(r_world, offer, 0);
}

uint32_t open_streams(const World &p_world, int p_peer) {
    const uint32_t peer = p_world.peer_of(p_peer);
    uint32_t total = 0;
    for (uint32_t index = 0; index < p_world.table_count(); ++index) {
        const netw::store::Table &table = p_world.table(index);
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            total += peer != netw::store::Table::NO_SLOT
                    && table.peers[peer].stream[row]
                        != uint8_t(netw::store::Stream::NONE)
                ? 1
                : 0;
        }
    }
    return total;
}

uint64_t mint_one(World &r_world, const RowAddress &p_at, int p_peer) {
    const netw::store::Table &table = r_world.table(p_at.table);
    netw::wire::CodeRow image;
    image.assign(
        table.plan,
        table.codes_of(p_at.row),
        table.carried_of(p_at.row)
    );
    return r_world.mint(p_at, r_world.peer_of(p_peer), image, false);
}

uint64_t acked(const World &p_world, const StreamLane &p_lane, int p_peer) {
    return netw_test::row_acked(p_world, p_lane, p_peer);
}

TEST_CASE(
    "[Networked][Wire][Hosted] tokens and requests climb for the process and "
    "never repeat"
) {
    const uint64_t first = next_stream_token();
    const uint64_t second = next_stream_token();
    NETW_CHECK_EQ(second, first + 1);
    NETW_CHECK_ORDER(first, 0, >);

    const uint64_t asked = next_open_request();
    NETW_CHECK_EQ(next_open_request(), asked + 1);

    StreamReaderBook reader;
    uint64_t token = 0;
    reader.open(
        HOST,
        lane_of(7, 0, StreamFamily::VOLATILE),
        10,
        EPOCH,
        SCHEMA,
        token
    );
    const uint64_t before = token;
    reader.forget_peer(HOST);
    reader.open(
        HOST,
        lane_of(7, 0, StreamFamily::VOLATILE),
        11,
        EPOCH,
        SCHEMA,
        token
    );
    NETW_CHECK_ORDER(token, before, >);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a peer that reconnects is a new incarnation, "
    "so its old token resolves to nothing"
) {
    StreamReaderBook reader;
    const StreamLane lane = lane_of(7, 0, StreamFamily::VOLATILE);
    uint64_t token = 0;
    const uint64_t request = next_open_request();
    NETW_CHECK_EQ(
        int(reader.open(HOST, lane, request, EPOCH, SCHEMA, token)),
        int(OpenVerdict::MINTED)
    );
    const uint64_t incarnation = reader.incarnation_of(HOST);
    const bool reachable_1 = reader.reaches(HOST, token);
    CHECK(reachable_1);

    reader.forget_peer(HOST);
    NETW_CHECK_ORDER(reader.incarnation_of(HOST), incarnation, >);
    const bool reachable_2 = reader.reaches(HOST, token);
    CHECK(!reachable_2);
    NETW_CHECK_EQ(reader.unknown_token_count(), 1);

    uint64_t reopened = 0;
    reader.open(HOST, lane, next_open_request(), EPOCH, SCHEMA, reopened);
    NETW_CHECK_EQ(int(reopened != token), 1);
    const bool reachable_3 = reader.reaches(HOST, reopened);
    CHECK(reachable_3);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a duplicate OPEN repeats its answer and an "
    "older one cannot replace its successor"
) {
    StreamReaderBook reader;
    const StreamLane lane = lane_of(9, 1, StreamFamily::RETAINED);
    const uint64_t older = next_open_request();
    const uint64_t newer = next_open_request();

    uint64_t token = 0;
    NETW_CHECK_EQ(
        int(reader.open(CLIENT, lane, newer, EPOCH, SCHEMA, token)),
        int(OpenVerdict::MINTED)
    );
    uint64_t repeated = 0;
    NETW_CHECK_EQ(
        int(reader.open(CLIENT, lane, newer, EPOCH, SCHEMA, repeated)),
        int(OpenVerdict::REPEATED)
    );
    NETW_CHECK_EQ(repeated, token);

    uint64_t stale = 0;
    NETW_CHECK_EQ(
        int(reader.open(CLIENT, lane, older, EPOCH, SCHEMA, stale)),
        int(OpenVerdict::SUPERSEDED)
    );
    NETW_CHECK_EQ(stale, 0);
    NETW_CHECK_EQ(reader.stream_count(), 1);
    const bool reachable_4 = reader.reaches(CLIENT, token);
    CHECK(reachable_4);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an invalidated stream answers its duplicate "
    "with RESET rather than READY"
) {
    StreamReaderBook reader;
    const StreamLane lane = lane_of(9, 0, StreamFamily::VOLATILE);
    const uint64_t request = next_open_request();
    uint64_t token = 0;
    reader.open(CLIENT, lane, request, EPOCH, SCHEMA, token);

    uint64_t reset_request = 0;
    uint64_t reset_token = 0;
    const bool named
        = reader.invalidate(CLIENT, lane, reset_request, reset_token);
    CHECK(named);
    NETW_CHECK_EQ(reset_request, request);
    NETW_CHECK_EQ(reset_token, token);

    uint64_t repeated = 0;
    NETW_CHECK_EQ(
        int(reader.open(CLIENT, lane, request, EPOCH, SCHEMA, repeated)),
        int(OpenVerdict::INVALIDATED)
    );
    const bool reachable_5 = reader.reaches(CLIENT, token);
    CHECK(!reachable_5);

    uint64_t reopened = 0;
    NETW_CHECK_EQ(
        int(reader.open(
            CLIENT,
            lane,
            next_open_request(),
            EPOCH,
            SCHEMA,
            reopened
        )),
        int(OpenVerdict::MINTED)
    );
    const bool reachable_6 = reader.reaches(CLIENT, reopened);
    CHECK(reachable_6);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a writer seats only the token its outstanding "
    "request names"
) {
    World writer;
    const StreamLane lane = lane_of(3, 0, StreamFamily::VOLATILE);
    const RowAddress at = declare(writer, lane, HOST);
    const uint64_t request = writer.stream_open(HOST, lane, StreamTenure());
    NETW_CHECK_EQ(writer.stream_open(HOST, lane, StreamTenure()), request);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);
    NETW_CHECK_EQ(writer.opening_requests(HOST).size(), 1);

    NETW_CHECK_EQ(
        int(writer.stream_ready(HOST, request + 9999, 55)),
        int(ReadyVerdict::UNKNOWN)
    );
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);

    NETW_CHECK_EQ(
        int(writer.stream_ready(HOST, request, 55)),
        int(ReadyVerdict::SEATED)
    );
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 55);
    NETW_CHECK_EQ(writer.opening_requests(HOST).size(), 0);

    writer.place(at, lane.route, int64_t(EPOCH) + 1);
    writer.name_lane(at, lane.ordinal);
    const uint64_t successor
        = writer.stream_open(HOST, lane, StreamTenure());
    NETW_CHECK_ORDER(successor, request, >);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);
    NETW_CHECK_EQ(
        int(writer.stream_ready(HOST, request, 55)),
        int(ReadyVerdict::UNKNOWN)
    );
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a RESET for a stale token cannot close the "
    "stream that replaced it"
) {
    World writer;
    const StreamLane lane = lane_of(3, 0, StreamFamily::VOLATILE);
    declare(writer, lane, HOST);
    const uint64_t first = writer.stream_open(HOST, lane, StreamTenure());
    writer.stream_ready(HOST, first, 55);

    const bool wrong_token = writer.stream_reset(HOST, first, 56);
    CHECK(!wrong_token);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 55);

    const bool exact = writer.stream_reset(HOST, first, 55);
    CHECK(exact);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);

    const uint64_t second = writer.stream_open(HOST, lane, StreamTenure());
    writer.stream_ready(HOST, second, 77);
    const bool late = writer.stream_reset(HOST, first, 55);
    CHECK(!late);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 77);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a route closed on either side takes its "
    "streams and leaves the other routes standing"
) {
    StreamReaderBook reader;
    World writer;
    uint64_t token = 0;
    reader.open(
        HOST,
        lane_of(3, 0, StreamFamily::VOLATILE),
        next_open_request(),
        EPOCH,
        SCHEMA,
        token
    );
    const uint64_t doomed = token;
    reader.open(
        HOST,
        lane_of(3, 1, StreamFamily::RETAINED),
        next_open_request(),
        EPOCH,
        SCHEMA,
        token
    );
    reader.open(
        HOST,
        lane_of(4, 0, StreamFamily::VOLATILE),
        next_open_request(),
        EPOCH,
        SCHEMA,
        token
    );
    const uint64_t spared = token;
    NETW_CHECK_EQ(reader.stream_count(), 3);

    reader.close_route(3);
    NETW_CHECK_EQ(reader.stream_count(), 1);
    const bool reachable_7 = reader.reaches(HOST, doomed);
    CHECK(!reachable_7);
    const bool reachable_8 = reader.reaches(HOST, spared);
    CHECK(reachable_8);

    const StreamLane closing = lane_of(3, 0, StreamFamily::VOLATILE);
    const StreamLane staying = lane_of(4, 0, StreamFamily::WINDOW);
    declare(writer, closing, HOST);
    declare(writer, staying, HOST);
    writer.stream_open(HOST, closing, StreamTenure());
    writer.stream_open(HOST, staying, StreamTenure());
    NETW_CHECK_EQ(open_streams(writer, HOST), 2);
    writer.route_closed(3);
    NETW_CHECK_EQ(open_streams(writer, HOST), 1);
    const bool reachable_9 = writer.request_of(HOST, closing) != 0;
    CHECK(!reachable_9);
    const bool reachable_10 = writer.request_of(HOST, staying) != 0;
    CHECK(reachable_10);
}

TEST_CASE(
    "[Networked][Wire][Hosted] the repair interval is two round trips inside "
    "a quarter second floor and a one second ceiling"
) {
    NETW_CHECK_EQ(repair_interval_ms(0), 250);
    NETW_CHECK_EQ(repair_interval_ms(100), 250);
    NETW_CHECK_EQ(repair_interval_ms(200), 400);
    NETW_CHECK_EQ(repair_interval_ms(500), 1000);
    NETW_CHECK_EQ(repair_interval_ms(5000), 1000);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a receipt names its stream by token alone, so "
    "the writer finds the row an ACCEPT entry is about"
) {
    World writer;
    const StreamLane lane = lane_of(9, 1, StreamFamily::VOLATILE);
    const RowAddress at = declare(writer, lane, HOST);
    const uint64_t request = writer.stream_open(HOST, lane, StreamTenure());
    const uint64_t token = next_stream_token();
    NETW_CHECK_EQ(
        int(writer.stream_ready(HOST, request, token)),
        int(ReadyVerdict::SEATED)
    );
    const uint64_t revision = mint_one(writer, at, HOST);

    NETW_CHECK_EQ(
        int(writer.stream_receipt(HOST, token, revision)),
        int(netw::wire::ReceiptVerdict::PROMOTED)
    );
    NETW_CHECK_EQ(acked(writer, lane, HOST), revision);
    NETW_CHECK_EQ(writer.receipts.unknown, int64_t(0));
}

TEST_CASE(
    "[Networked][Wire][Hosted] a receipt naming a token this writer never "
    "held is counted and confirms nothing, so a stale peer cannot promote a "
    "row"
) {
    World writer;
    const StreamLane lane = lane_of(9, 1, StreamFamily::VOLATILE);
    const RowAddress at = declare(writer, lane, HOST);
    const uint64_t request = writer.stream_open(HOST, lane, StreamTenure());
    const uint64_t token = next_stream_token();
    writer.stream_ready(HOST, request, token);
    const uint64_t revision = mint_one(writer, at, HOST);

    NETW_CHECK_EQ(
        int(writer.stream_receipt(HOST, token + 1000, revision)),
        int(netw::wire::ReceiptVerdict::IGNORED)
    );
    NETW_CHECK_EQ(
        int(writer.stream_receipt(CLIENT, token, revision)),
        int(netw::wire::ReceiptVerdict::IGNORED)
    );
    NETW_CHECK_EQ(acked(writer, lane, HOST), uint64_t(0));
    NETW_CHECK_EQ(writer.receipts.unknown, int64_t(2));
}

TEST_CASE(
    "[Networked][Wire][Hosted] a reset takes the token out of the receipt "
    "index, so a receipt still in flight for it cannot confirm the successor"
) {
    World writer;
    const StreamLane lane = lane_of(9, 1, StreamFamily::VOLATILE);
    const RowAddress at = declare(writer, lane, HOST);
    const uint64_t request = writer.stream_open(HOST, lane, StreamTenure());
    const uint64_t token = next_stream_token();
    writer.stream_ready(HOST, request, token);
    const uint64_t revision = mint_one(writer, at, HOST);

    CHECK(writer.stream_reset(HOST, request, token));
    NETW_CHECK_EQ(
        int(writer.stream_receipt(HOST, token, revision)),
        int(netw::wire::ReceiptVerdict::IGNORED)
    );

    const uint64_t reopened = writer.stream_open(HOST, lane, StreamTenure());
    const uint64_t successor = next_stream_token();
    writer.stream_ready(HOST, reopened, successor);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), successor);
    NETW_CHECK_EQ(acked(writer, lane, HOST), uint64_t(0));
    NETW_CHECK_EQ(
        int(writer.stream_receipt(HOST, token, revision)),
        int(netw::wire::ReceiptVerdict::IGNORED)
    );
}

} // namespace TestRowStreamLifetime

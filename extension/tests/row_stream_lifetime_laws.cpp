#include "support/netw_test.h"

#include <cstdint>

#include "netw/wire/stream_book.hpp"

namespace TestRowStreamLifetime {

using netw::wire::next_open_request;
using netw::wire::next_stream_token;
using netw::wire::OpenVerdict;
using netw::wire::ReadyVerdict;
using netw::wire::repair_interval_ms;
using netw::wire::StreamFamily;
using netw::wire::StreamLane;
using netw::wire::StreamReaderBook;
using netw::wire::StreamWriterBook;

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
    const bool reachable_1 = (reader.receiver(HOST, token)) != nullptr;
    CHECK(reachable_1);

    reader.forget_peer(HOST);
    NETW_CHECK_ORDER(reader.incarnation_of(HOST), incarnation, >);
    const bool reachable_2 = (reader.receiver(HOST, token)) != nullptr;
    CHECK(!reachable_2);
    NETW_CHECK_EQ(reader.unknown_token_count(), 1);

    uint64_t reopened = 0;
    reader.open(HOST, lane, next_open_request(), EPOCH, SCHEMA, reopened);
    NETW_CHECK_EQ(int(reopened != token), 1);
    const bool reachable_3 = (reader.receiver(HOST, reopened)) != nullptr;
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
    const bool reachable_4 = (reader.receiver(CLIENT, token)) != nullptr;
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
    const bool reachable_5 = (reader.receiver(CLIENT, token)) != nullptr;
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
    const bool reachable_6 = (reader.receiver(CLIENT, reopened)) != nullptr;
    CHECK(reachable_6);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a writer seats only the token its outstanding "
    "request names"
) {
    StreamWriterBook writer;
    const StreamLane lane = lane_of(3, 0, StreamFamily::VOLATILE);
    const uint64_t request = writer.open(HOST, lane, EPOCH, SCHEMA);
    NETW_CHECK_EQ(writer.open(HOST, lane, EPOCH, SCHEMA), request);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);
    NETW_CHECK_EQ(writer.unready(HOST).size(), 1);

    NETW_CHECK_EQ(
        int(writer.ready(HOST, request + 9999, 55)),
        int(ReadyVerdict::UNKNOWN)
    );
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);

    NETW_CHECK_EQ(
        int(writer.ready(HOST, request, 55)),
        int(ReadyVerdict::SEATED)
    );
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 55);
    NETW_CHECK_EQ(writer.unready(HOST).size(), 0);

    const uint64_t successor = writer.open(HOST, lane, EPOCH + 1, SCHEMA);
    NETW_CHECK_ORDER(successor, request, >);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);
    NETW_CHECK_EQ(
        int(writer.ready(HOST, request, 55)),
        int(ReadyVerdict::UNKNOWN)
    );
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a RESET for a stale token cannot close the "
    "stream that replaced it"
) {
    StreamWriterBook writer;
    const StreamLane lane = lane_of(3, 0, StreamFamily::VOLATILE);
    const uint64_t first = writer.open(HOST, lane, EPOCH, SCHEMA);
    writer.ready(HOST, first, 55);

    const bool wrong_token = writer.reset(HOST, first, 56);
    CHECK(!wrong_token);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 55);

    const bool exact = writer.reset(HOST, first, 55);
    CHECK(exact);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 0);

    const uint64_t second = writer.open(HOST, lane, EPOCH + 1, SCHEMA);
    writer.ready(HOST, second, 77);
    const bool late = writer.reset(HOST, first, 55);
    CHECK(!late);
    NETW_CHECK_EQ(writer.token_of(HOST, lane), 77);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a route closed on either side takes its "
    "streams and leaves the other routes standing"
) {
    StreamReaderBook reader;
    StreamWriterBook writer;
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
    const bool reachable_7 = (reader.receiver(HOST, doomed)) != nullptr;
    CHECK(!reachable_7);
    const bool reachable_8 = (reader.receiver(HOST, spared)) != nullptr;
    CHECK(reachable_8);

    writer.open(HOST, lane_of(3, 0, StreamFamily::VOLATILE), EPOCH, SCHEMA);
    writer.open(HOST, lane_of(4, 0, StreamFamily::WINDOW), EPOCH, SCHEMA);
    NETW_CHECK_EQ(writer.stream_count(), 2);
    writer.close_route(3);
    NETW_CHECK_EQ(writer.stream_count(), 1);
    const bool reachable_9
        = (writer.sender(HOST, lane_of(3, 0, StreamFamily::VOLATILE)))
        != nullptr;
    CHECK(!reachable_9);
    const bool reachable_10
        = (writer.sender(HOST, lane_of(4, 0, StreamFamily::WINDOW))) != nullptr;
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

    StreamWriterBook writer;
    const StreamLane lane = lane_of(3, 0, StreamFamily::VOLATILE);
    writer.open(HOST, lane, EPOCH, SCHEMA);
    netw::wire::SnapshotSender *sender = writer.sender(HOST, lane);
    const bool reachable_11 = (sender) != nullptr;
    CHECK(reachable_11);

    const bool quiet_owes_nothing = writer.repair_due(HOST, lane, 100000, 0);
    CHECK(!quiet_owes_nothing);

    netw::wire::CodeRow row;
    sender->desire(row);
    const uint64_t revision = sender->reserve();
    sender->expose(revision, row);
    const bool first = writer.repair_due(HOST, lane, 100000, 0);
    CHECK(first);
    const bool too_soon = writer.repair_due(HOST, lane, 100200, 0);
    CHECK(!too_soon);
    const bool due = writer.repair_due(HOST, lane, 100250, 0);
    CHECK(due);

    writer.note_attempt(HOST, lane, 100400);
    const bool a_fresh_attempt_restarts_the_interval
        = !writer.repair_due(HOST, lane, 100600, 0);
    CHECK(a_fresh_attempt_restarts_the_interval);
    const bool the_interval_still_expires
        = writer.repair_due(HOST, lane, 100650, 0);
    CHECK(the_interval_still_expires);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a receipt names its stream by token alone, so "
    "the writer finds the sender an ACCEPT entry is about"
) {
    StreamWriterBook writer;
    const StreamLane lane = lane_of(9, 1, StreamFamily::VOLATILE);
    const uint64_t request = writer.open(HOST, lane, EPOCH, SCHEMA);
    const uint64_t token = next_stream_token();
    NETW_CHECK_EQ(
        int(writer.ready(HOST, request, token)),
        int(ReadyVerdict::SEATED)
    );

    netw::wire::SnapshotSender *sender = writer.sender(HOST, lane);
    const bool armed = (sender) != nullptr;
    REQUIRE(armed);
    netw::wire::CodeRow row;
    sender->desire(row);
    const uint64_t revision = sender->reserve();
    sender->expose(revision, row);

    StreamLane named;
    const bool names_it = writer.names(HOST, token, named);
    CHECK(names_it);
    NETW_CHECK_EQ(named.route, lane.route);
    NETW_CHECK_EQ(int(named.ordinal), int(lane.ordinal));

    NETW_CHECK_EQ(
        int(writer.receipt(HOST, token, revision)),
        int(netw::wire::ReceiptVerdict::PROMOTED)
    );
    NETW_CHECK_EQ(sender->confirmed_at(), revision);
    NETW_CHECK_EQ(int64_t(writer.unknown_receipt_count()), int64_t(0));
}

TEST_CASE(
    "[Networked][Wire][Hosted] a receipt naming a token this writer never "
    "held is counted and confirms nothing, so a stale peer cannot promote a "
    "row"
) {
    StreamWriterBook writer;
    const StreamLane lane = lane_of(9, 1, StreamFamily::VOLATILE);
    const uint64_t request = writer.open(HOST, lane, EPOCH, SCHEMA);
    const uint64_t token = next_stream_token();
    writer.ready(HOST, request, token);

    netw::wire::SnapshotSender *sender = writer.sender(HOST, lane);
    const bool armed = (sender) != nullptr;
    REQUIRE(armed);
    netw::wire::CodeRow row;
    sender->desire(row);
    const uint64_t revision = sender->reserve();
    sender->expose(revision, row);

    NETW_CHECK_EQ(
        int(writer.receipt(HOST, token + 1000, revision)),
        int(netw::wire::ReceiptVerdict::IGNORED)
    );
    NETW_CHECK_EQ(
        int(writer.receipt(CLIENT, token, revision)),
        int(netw::wire::ReceiptVerdict::IGNORED)
    );
    NETW_CHECK_EQ(sender->confirmed_at(), uint64_t(0));
    NETW_CHECK_EQ(int64_t(writer.unknown_receipt_count()), int64_t(2));

    StreamLane named;
    const bool strangers_name_nothing = writer.names(HOST, token + 1000, named);
    CHECK(!strangers_name_nothing);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a reset takes the token out of the receipt "
    "index, so a receipt still in flight for it cannot confirm the successor"
) {
    StreamWriterBook writer;
    const StreamLane lane = lane_of(9, 1, StreamFamily::VOLATILE);
    const uint64_t request = writer.open(HOST, lane, EPOCH, SCHEMA);
    const uint64_t token = next_stream_token();
    writer.ready(HOST, request, token);
    netw::wire::SnapshotSender *sender = writer.sender(HOST, lane);
    const bool armed = (sender) != nullptr;
    REQUIRE(armed);
    netw::wire::CodeRow row;
    sender->desire(row);
    const uint64_t revision = sender->reserve();
    sender->expose(revision, row);

    CHECK(writer.reset(HOST, request, token));
    NETW_CHECK_EQ(
        int(writer.receipt(HOST, token, revision)),
        int(netw::wire::ReceiptVerdict::IGNORED)
    );

    const uint64_t successor = next_stream_token();
    writer.ready(HOST, request, successor);
    netw::wire::SnapshotSender *fresh = writer.sender(HOST, lane);
    const bool still_there = (fresh) != nullptr;
    REQUIRE(still_there);
    NETW_CHECK_EQ(fresh->confirmed_at(), uint64_t(0));
    NETW_CHECK_EQ(
        int(writer.receipt(HOST, token, revision)),
        int(netw::wire::ReceiptVerdict::IGNORED)
    );

    StreamLane named;
    const bool dead_token_names_nothing = writer.names(HOST, token, named);
    CHECK(!dead_token_names_nothing);
    const bool live_token_names_it = writer.names(HOST, successor, named);
    CHECK(live_token_names_it);
}

} // namespace TestRowStreamLifetime

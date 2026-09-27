#include "support/netw_test.h"

#include "support/composed_link.h"

#include <cstdint>

#include "godot/templates.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/control_record.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/snapshot_stream.hpp"
#include "netw/wire/stream_book.hpp"

namespace TestWireComposedTransport {

using netw::SchemaCore;
using netw::repl::name_snapshot_row;
using netw::repl::read_snapshot_row;
using netw::repl::SnapshotHeader;
using netw::repl::SnapshotRefusal;
using netw::repl::write_snapshot_row;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::ControlAcceptEntry;
using netw::wire::ControlRecord;
using netw::wire::ControlTag;
using netw::wire::next_open_request;
using netw::wire::OpenVerdict;
using netw::wire::ReadyVerdict;
using netw::wire::ReceiptVerdict;
using netw::wire::RowAdmission;
using netw::wire::SnapshotReceiver;
using netw::wire::SnapshotSender;
using netw::wire::StreamFamily;
using netw::wire::StreamLane;
using netw::wire::StreamReaderBook;
using netw::wire::StreamWriterBook;
using netw::wire::WirePlan;
using netw_test::ComposedLink;
using netw_test::Landing;
using netw_test::Packet;
using netw_test::RowImageLedger;

const int WRITER = netw_test::COMPOSED_WRITER;
const int READER = netw_test::COMPOSED_READER;
const uint64_t EPOCH = netw_test::COMPOSED_EPOCH;
const uint32_t SCHEMA = netw_test::COMPOSED_SCHEMA;
const int64_t BASE_TICK = netw_test::COMPOSED_BASE_TICK;

WirePlan pair_plan() {
    SchemaRecord record;
    record.name = godot::StringName("ComposedRow");
    SchemaCore::append_column(
        &record,
        godot::StringName("x"),
        SchemaCore::I16,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("y"),
        SchemaCore::I16,
        1
    );
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

StreamLane lane_of(int64_t p_route) {
    StreamLane made;
    made.route = p_route;
    made.ordinal = 0;
    made.family = StreamFamily::VOLATILE;
    return made;
}

TEST_CASE(
    "[Networked][Wire][Hosted] X1 a lane sends nothing until READY seats its "
    "token, and the token the receiver minted is the one the row carries"
) {
    ComposedLink link;
    link.plan = pair_plan();
    const uint64_t request
        = link.writers.open(WRITER, lane_of(link.route), EPOCH, SCHEMA);
    const bool the_request_was_minted = request != 0;
    CHECK(the_request_was_minted);
    NETW_CHECK_EQ(link.writers.token_of(WRITER, lane_of(link.route)), 0);
    const bool no_row_can_leave_an_unseated_lane
        = link.publish(link.pair(1, 1)) == 0;
    CHECK(no_row_can_leave_an_unseated_lane);
    NETW_CHECK_EQ(link.flight.size(), 0);

    uint64_t minted = 0;
    NETW_CHECK_EQ(
        int(link.readers.open(
            READER,
            lane_of(link.route),
            request,
            EPOCH,
            SCHEMA,
            minted
        )),
        int(OpenVerdict::MINTED)
    );
    NETW_CHECK_EQ(
        int(link.writers.ready(WRITER, request, minted)),
        int(ReadyVerdict::SEATED)
    );
    link.token = minted;

    const uint64_t first = link.publish(link.pair(7, 8));
    NETW_CHECK_EQ(first, 1);
    NETW_REQUIRE_EQ(link.flight.size(), 1);
    NETW_CHECK_EQ(link.flight[0].token, minted);
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Landing::ACCEPTED));
    NETW_CHECK_EQ(link.column(0), 7);
    NETW_CHECK_EQ(link.column(1), 8);
    const bool clean = link.oracle_is_clean();
    CHECK(clean);
}

TEST_CASE(
    "[Networked][Wire][Hosted] X2 a row the consumer refuses commits nothing "
    "and elicits no receipt, and the next row still composes on the last "
    "committed snapshot"
) {
    ComposedLink link;
    link.open(pair_plan());
    link.publish(link.pair(1, 2));
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Landing::ACCEPTED));
    link.apply_all_receipts();

    const uint64_t refused = link.publish(link.pair(3, 2));
    NETW_REQUIRE_EQ(link.flight.size(), 1);
    NETW_CHECK_EQ(int(link.deliver_at(0, false)), int(Landing::UNBOUND));
    NETW_CHECK_EQ(link.consumer_refusals, 1);
    NETW_CHECK_EQ(link.receipts.size(), 0);
    NETW_CHECK_EQ(link.accepted_revision(), 1);
    NETW_CHECK_EQ(link.column(0), 1);

    link.now_ms += 300;
    const uint64_t again = link.publish(link.pair(3, 4));
    const bool a_refused_revision_is_not_reused = again > refused;
    CHECK(a_refused_revision_is_not_reused);
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Landing::ACCEPTED));
    NETW_CHECK_EQ(link.column(0), 3);
    NETW_CHECK_EQ(link.column(1), 4);
    const bool clean = link.oracle_is_clean();
    CHECK(clean);
}

TEST_CASE(
    "[Networked][Wire][Hosted] X3 a row lost on the way out is repaired under "
    "one pinned absolute revision and the final image converges"
) {
    ComposedLink link;
    link.open(pair_plan());
    link.publish(link.pair(1, 1));
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Landing::ACCEPTED));
    link.apply_all_receipts();

    link.now_ms += 300;
    link.publish(link.pair(5, 6));
    NETW_REQUIRE_EQ(link.flight.size(), 1);
    link.drop_at(0);

    link.now_ms += 300;
    const uint64_t first_repair = link.repair();
    const bool the_repair_took_its_own_revision = first_repair != 0;
    CHECK(the_repair_took_its_own_revision);
    NETW_REQUIRE_EQ(link.flight.size(), 1);
    link.drop_at(0);

    link.now_ms += 300;
    NETW_CHECK_EQ(link.repair(), first_repair);

    const bool settled = link.settle(8);
    CHECK(settled);
    NETW_CHECK_EQ(link.column(0), 5);
    NETW_CHECK_EQ(link.column(1), 6);
    const RowImageLedger::Verdict verdict = link.ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.diverged, 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] X4 receipts lost, delayed, duplicated and "
    "reordered never regress the confirmed revision and the lane still quiets"
) {
    ComposedLink link;
    link.open(pair_plan());
    link.publish(link.pair(1, 1));
    link.deliver_all();
    link.drop_receipts();

    link.now_ms += 300;
    link.publish(link.pair(2, 2));
    link.deliver_all();
    link.now_ms += 300;
    link.publish(link.pair(3, 3));
    link.deliver_all();
    NETW_REQUIRE_EQ(link.receipts.size(), 2);

    const ControlRecord older = link.receipts[0];
    link.receipts.remove_at(0);
    link.receipts.push_back(older);
    link.receipts.push_back(link.receipts[0]);

    link.apply_all_receipts();
    NETW_CHECK_EQ(link.sender()->confirmed_at(), 3);

    link.apply_all_receipts();
    NETW_CHECK_EQ(link.sender()->confirmed_at(), 3);

    const bool quiet = link.sender()->quiet();
    CHECK(quiet);
    NETW_CHECK_EQ(link.column(0), 3);
    const bool clean = link.oracle_is_clean();
    CHECK(clean);
}

TEST_CASE(
    "[Networked][Wire][Hosted] X5 a duplicated row is applied once and repeats "
    "the receipt the receiver already owes"
) {
    ComposedLink link;
    link.open(pair_plan());
    link.publish(link.pair(4, 4));
    NETW_REQUIRE_EQ(link.flight.size(), 1);
    const Packet carried = link.flight[0];

    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Landing::ACCEPTED));
    NETW_CHECK_EQ(link.receipts.size(), 1);
    NETW_CHECK_EQ(int(link.land(carried, true)), int(Landing::DUPLICATE));
    NETW_CHECK_EQ(link.receipts.size(), 2);
    NETW_CHECK_EQ(link.accepted_revision(), carried.revision);
    NETW_CHECK_EQ(link.ring_count(), 1);

    link.apply_all_receipts();
    NETW_CHECK_EQ(link.sender()->confirmed_at(), carried.revision);
    const RowImageLedger::Verdict verdict = link.ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.restated_revisions, 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] X6 a reset invalidates the token both sides "
    "hold, and the successor stream opens with an absolute row"
) {
    ComposedLink link;
    link.open(pair_plan());
    link.publish(link.pair(2, 3));
    link.deliver_all();
    link.apply_all_receipts();
    const uint64_t stale_token = link.token;

    uint64_t reset_request = 0;
    uint64_t reset_token = 0;
    const bool the_receiver_invalidated = link.readers.invalidate(
        READER,
        lane_of(link.route),
        reset_request,
        reset_token
    );
    CHECK(the_receiver_invalidated);
    NETW_CHECK_EQ(reset_token, stale_token);
    const bool the_writer_took_the_reset
        = link.writers.reset(WRITER, reset_request, reset_token);
    CHECK(the_writer_took_the_reset);

    const bool a_reset_lane_carries_no_row = link.publish(link.pair(9, 9)) == 0;
    CHECK(a_reset_lane_carries_no_row);

    link.seat(WRITER);
    const bool the_successor_took_a_fresh_token = link.token != stale_token;
    CHECK(the_successor_took_a_fresh_token);
    link.publish(link.pair(9, 9));
    NETW_REQUIRE_EQ(link.flight.size(), 1);
    SnapshotHeader named;
    const bool the_first_row_names_the_new_token
        = name_snapshot_row(link.flight[0].bytes, named)
        && named.token == link.token && named.absolute();
    CHECK(the_first_row_names_the_new_token);
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Landing::ACCEPTED));
    NETW_CHECK_EQ(link.column(0), 9);
    const bool clean = link.oracle_is_clean();
    CHECK(clean);
}

TEST_CASE(
    "[Networked][Wire][Hosted] X7 a reconnect mints a new token, and a row or "
    "a receipt carrying the old one changes nothing"
) {
    ComposedLink link;
    link.open(pair_plan());
    link.publish(link.pair(1, 5));
    NETW_REQUIRE_EQ(link.flight.size(), 1);
    const Packet in_flight = link.flight[0];
    link.deliver_all();
    link.apply_all_receipts();
    const uint64_t old_token = link.token;

    link.writers.forget_peer(WRITER);
    link.readers.forget_peer(READER);
    link.seat(WRITER);
    const bool the_incarnation_minted_a_new_token = link.token != old_token;
    CHECK(the_incarnation_minted_a_new_token);

    NETW_CHECK_EQ(int(link.land(in_flight, true)), int(Landing::UNBOUND));
    NETW_CHECK_EQ(
        int(link.writers.receipt(WRITER, old_token, 1)),
        int(ReceiptVerdict::IGNORED)
    );
    const bool the_old_receipt_was_counted
        = link.writers.unknown_receipt_count() > 0;
    CHECK(the_old_receipt_was_counted);

    link.publish(link.pair(6, 6));
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Landing::ACCEPTED));
    NETW_CHECK_EQ(link.column(0), 6);
    NETW_CHECK_EQ(link.accepted_revision(), 1);
    const bool clean = link.oracle_is_clean();
    CHECK(clean);
}

TEST_CASE(
    "[Networked][Wire][Hosted] X8 a second writer taking the lane opens its "
    "own stream, and the first writer's token no longer reaches the receiver"
) {
    const int SUCCESSOR = 5;
    ComposedLink link;
    link.open(pair_plan());
    link.publish(link.pair(3, 3));
    link.deliver_all();
    link.apply_all_receipts();
    const uint64_t first_token = link.token;

    link.seat(SUCCESSOR);
    const bool the_successor_took_its_own_token = link.token != first_token;
    CHECK(the_successor_took_its_own_token);

    const uint64_t stranded = link.publish(link.pair(4, 4), WRITER);
    const bool the_old_writer_still_encodes = stranded != 0;
    CHECK(the_old_writer_still_encodes);
    NETW_REQUIRE_EQ(link.flight.size(), 1);
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Landing::UNBOUND));
    const bool the_successor_stream_starts_empty = link.holds_nothing();
    CHECK(the_successor_stream_starts_empty);

    link.publish(link.pair(8, 9), SUCCESSOR);
    NETW_REQUIRE_EQ(link.flight.size(), 1);
    SnapshotHeader named;
    const bool the_successor_opens_absolute
        = name_snapshot_row(link.flight[0].bytes, named) && named.absolute();
    CHECK(the_successor_opens_absolute);
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Landing::ACCEPTED));
    NETW_CHECK_EQ(link.column(0), 8);
    NETW_CHECK_EQ(link.column(1), 9);
}

TEST_CASE(
    "[Networked][Wire][Hosted] X9 a frozen source behind a link that loses "
    "rows and receipts converges on the exact final image once it heals"
) {
    ComposedLink link;
    link.open(pair_plan());
    uint64_t seed = 0x5C5C5C5Cu;
    uint64_t x = 0;
    uint64_t y = 0;
    for (int step = 0; step < 24; ++step) {
        seed = seed * uint64_t(6364136223846793005)
            + uint64_t(1442695040888963407);
        x = (seed >> 33) % 400;
        if ((step % 4) == 0) {
            y = (seed >> 45) % 400;
        }
        link.now_ms += 300;
        link.publish(link.pair(x, y));
        link.repair();
        const bool the_link_drops_this_round = ((seed >> 20) & 3) == 0;
        if (the_link_drops_this_round) {
            link.flight.clear();
        } else {
            link.deliver_all();
        }
        const bool the_report_is_lost = ((seed >> 24) & 3) == 0;
        if (the_report_is_lost) {
            link.drop_receipts();
        } else {
            link.apply_all_receipts();
        }
    }

    link.now_ms += 300;
    link.publish(link.pair(321, 123));
    const bool settled = link.settle(16);
    CHECK(settled);
    NETW_CHECK_EQ(link.column(0), 321);
    NETW_CHECK_EQ(link.column(1), 123);

    const RowImageLedger::Verdict verdict = link.ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.diverged, 0);
    NETW_CHECK_EQ(verdict.accepted_unstaged, 0);
    NETW_CHECK_EQ(verdict.restated_revisions, 0);
    NETW_CHECK_GT(verdict.agreed, 8);
}

} // namespace TestWireComposedTransport

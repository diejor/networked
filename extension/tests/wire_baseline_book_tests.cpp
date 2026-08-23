#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/baseline_book.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

using namespace godot;

namespace TestNetwWireBaselineBook {

using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::wire::BaselineBook;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

const int PEER = 7;

WirePlan body_plan() {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Body");
    SchemaCore::append_column(
        record,
        godot::StringName("x"),
        SchemaCore::I16,
        1
    );
    SchemaCore::append_column(
        record,
        godot::StringName("y"),
        SchemaCore::I16,
        1
    );
    SchemaCore::append_column(
        record,
        godot::StringName("spin"),
        SchemaCore::I16,
        1
    );
    SchemaCore::fix(record);
    return WirePlan::compile(record);
}

void fill(
    const WirePlan &plan,
    CodeRow &row,
    uint64_t x,
    uint64_t y,
    uint64_t spin
) {
    row.write(plan.column(0), 0, x);
    row.write(plan.column(1), 0, y);
    row.write(plan.column(2), 0, spin);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a peer with no baseline is sent every column"
) {
    const WirePlan plan = body_plan();
    REQUIRE(plan.valid());
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);
    fill(plan, row, 10, 20, 30);

    REQUIRE_FALSE(book.has_baseline(PEER));
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), plan.full_mask());
    NETW_CHECK_EQ(plan.full_mask(), 7);
}

TEST_CASE("[Networked][Wire][Hosted] a caught-up peer is sent nothing") {
    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);
    fill(plan, row, 10, 20, 30);

    book.mask_to_send(PEER, plan, row);
    book.stage(PEER, 500, row);
    book.acknowledge(PEER, 500);

    REQUIRE(book.has_baseline(PEER));
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 0);
}

TEST_CASE("[Networked][Wire][Hosted] only the columns that moved are sent") {
    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);
    fill(plan, row, 10, 20, 30);

    book.mask_to_send(PEER, plan, row);
    book.stage(PEER, 500, row);
    book.acknowledge(PEER, 500);

    fill(plan, row, 10, 20, 31);
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 4);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a column stays sent until an ack carries it"
) {
    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow confirmed = CodeRow::for_plan(plan);
    fill(plan, confirmed, 10, 20, 30);
    CodeRow row = CodeRow::for_plan(plan);
    fill(plan, row, 10, 20, 30);

    book.stage(PEER, 500, row);
    book.acknowledge(PEER, 500);

    fill(plan, row, 10, 20, 31);
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 4);
    book.stage(PEER, 501, row);

    fill(plan, row, 10, 20, 30);
    REQUIRE(row.equals(confirmed));
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 4);
}

TEST_CASE("[Networked][Wire][Hosted] the ack that carries a column stops it") {
    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);
    fill(plan, row, 10, 20, 30);

    book.stage(PEER, 500, row);
    book.acknowledge(PEER, 500);

    fill(plan, row, 10, 20, 31);
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 4);
    book.stage(PEER, 501, row);
    book.acknowledge(PEER, 501);

    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ack promotes the freshest row at or under it"
) {
    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);

    fill(plan, row, 1, 0, 0);
    book.stage(PEER, 500, row);
    fill(plan, row, 2, 0, 0);
    book.stage(PEER, 501, row);
    fill(plan, row, 3, 0, 0);
    book.stage(PEER, 502, row);

    book.acknowledge(PEER, 501);

    fill(plan, row, 2, 0, 0);
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 0);
    NETW_CHECK_EQ(book.in_flight_count(PEER), 1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ack with nothing under it keeps the baseline"
) {
    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);
    fill(plan, row, 1, 0, 0);

    book.stage(PEER, 500, row);
    book.acknowledge(PEER, 499);

    REQUIRE_FALSE(book.has_baseline(PEER));
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), plan.full_mask());
    NETW_CHECK_EQ(book.in_flight_count(PEER), 1);
}

TEST_CASE("[Networked][Wire][Hosted] a seq that wrapped is still older") {
    const uint16_t before_wrap = 65534;
    const uint16_t after_wrap = 2;
    REQUIRE(before_wrap > after_wrap);

    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);

    fill(plan, row, 1, 0, 0);
    book.stage(PEER, before_wrap, row);
    fill(plan, row, 2, 0, 0);
    book.stage(PEER, after_wrap, row);

    book.acknowledge(PEER, after_wrap);
    fill(plan, row, 2, 0, 0);
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 0);
    NETW_CHECK_EQ(book.in_flight_count(PEER), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ack before the wrap leaves what follows"
) {
    const uint16_t before_wrap = 65534;
    const uint16_t after_wrap = 2;
    REQUIRE(before_wrap > after_wrap);

    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);

    fill(plan, row, 1, 0, 0);
    book.stage(PEER, before_wrap, row);
    fill(plan, row, 2, 0, 0);
    book.stage(PEER, after_wrap, row);

    book.acknowledge(PEER, before_wrap);
    fill(plan, row, 1, 0, 0);
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 0);
    NETW_CHECK_EQ(book.in_flight_count(PEER), 1);
}

TEST_CASE("[Networked][Wire][Hosted] a peer that loses the book heals whole") {
    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);
    fill(plan, row, 10, 20, 30);

    const int kept_peer = 8;
    book.stage(PEER, 500, row);
    book.stage(kept_peer, 500, row);
    book.acknowledge(PEER, 500);
    book.acknowledge(kept_peer, 500);

    LocalVector<int> recipients;
    recipients.push_back(kept_peer);
    book.retain(recipients);

    REQUIRE_FALSE(book.has_baseline(PEER));
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), plan.full_mask());
    REQUIRE(book.has_baseline(kept_peer));
    NETW_CHECK_EQ(book.mask_to_send(kept_peer, plan, row), 0);

    book.forget(kept_peer);
    REQUIRE_FALSE(book.has_baseline(kept_peer));
    NETW_CHECK_EQ(book.mask_to_send(kept_peer, plan, row), plan.full_mask());
}

TEST_CASE("[Networked][Wire][Hosted] the in-flight ring is bounded") {
    const WirePlan plan = body_plan();
    BaselineBook book;
    CodeRow row = CodeRow::for_plan(plan);

    for (uint32_t index = 0; index <= BaselineBook::MAX_IN_FLIGHT; ++index) {
        fill(plan, row, index + 1, 0, 0);
        book.stage(PEER, uint16_t(index + 1), row);
    }
    NETW_CHECK_EQ(book.in_flight_count(PEER), BaselineBook::MAX_IN_FLIGHT);

    book.acknowledge(PEER, 1);
    REQUIRE_FALSE(book.has_baseline(PEER));

    book.acknowledge(PEER, 2);
    REQUIRE(book.has_baseline(PEER));
    fill(plan, row, 2, 0, 0);
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 0);
}

} // namespace TestNetwWireBaselineBook

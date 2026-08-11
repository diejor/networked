// The per-peer baseline book against the property a masked lane rests on:
// what a peer is sent is decided against what that peer PROVABLY holds, and a
// staged row is not proof.
//
// The two load-bearing laws are the halves of the sticky rule, and they are
// asymmetric on purpose: one says a column keeps being sent, the other says
// the ack is what stops it. Dropping the sticky term reddens the first alone,
// and never clearing it on promotion reddens the second alone. Neither is a
// law by itself, because a lane that sent every column every pass would
// satisfy the first and a lane that only ever diffed would satisfy the second.
//
// Reaching a confirmed baseline in both is done by staging and acking WITHOUT
// a send, so the gain edge does not pre-load the sticky set and leave each
// mutation visible to the other's law.
//
// Seqs here are chosen for what they cross rather than for being distinct: the
// wrap law asserts that its older seq is numerically GREATER than the ack that
// retires it, so a plain integer comparison would refuse the promotion the law
// demands.

#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/wire/baseline_book.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

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
    // The revert race, which is what the sticky rule exists for. `spin` moves
    // off the confirmed baseline, is sent, and moves back before that send is
    // acked. Diffed against the confirmed baseline alone it now reads clean,
    // and the receiver would be left holding the interim value forever.
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
    // The reverted row IS the confirmed baseline, so the diff against it is
    // clean and the bit below can only come from the sticky rule.
    REQUIRE(row.equals(confirmed));
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 4);
}

TEST_CASE("[Networked][Wire][Hosted] the ack that carries a column stops it") {
    // The other half of the sticky rule. Without this the mask only ever
    // grows, and the lane ratchets toward sending a whole row every pass.
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

    // The row staged at 501 is the baseline, so diffing it in sends nothing
    // while the older and the newer staged rows both read as changed.
    fill(plan, row, 2, 0, 0);
    NETW_CHECK_EQ(book.mask_to_send(PEER, plan, row), 0);
    // Everything at or before the promoted seq is dropped, so only 502 is left.
    NETW_CHECK_EQ(book.in_flight_count(PEER), 1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ack with nothing under it keeps the baseline"
) {
    // A stalled peer is diffed against an older truth rather than a corrupted
    // one, which is the never-corrupts guarantee the reliable lane gets free.
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
    // A datagram seq is a u16 read across a half window, so the entry this ack
    // retires carries the LARGER number. A comparison that read the seqs as
    // plain integers would refuse this promotion and heal the peer with a full
    // row on every pass across the wrap.
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
    // The same two seqs, acked the other way round. The entry after the wrap
    // is FRESHER than the ack, so it survives and the older row is what the
    // peer is confirmed to hold.
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
    // A peer that never acks would otherwise stage a row per flush forever,
    // and an entry old enough to have wrapped would compare as fresher than
    // the ack meant to retire it. The oldest goes, and an ack naming it
    // promotes nothing rather than reaching a row that is no longer held.
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

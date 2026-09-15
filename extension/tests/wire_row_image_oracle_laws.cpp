#include "support/netw_test.h"

#include "support/row_image_oracle.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace TestWireRowImageOracle {

using netw::SchemaCore;
using netw::table::DeltaMode;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;
using netw_test::compare_row_images;
using netw_test::ImageComparison;
using netw_test::ImageDisagreement;
using netw_test::RowImageLedger;
using netw_test::take_row_image;

const uint64_t TOKEN = 41;

WirePlan pair_plan(int p_type) {
    SchemaRecord record;
    record.name = godot::StringName("OracleRow");
    SchemaCore::append_column(&record, godot::StringName("x"), p_type, 1);
    SchemaCore::append_column(&record, godot::StringName("y"), p_type, 1);
    record.at(0)->delta = DeltaMode::LADDER;
    record.at(1)->delta = DeltaMode::LADDER;
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

CodeRow pair(const WirePlan &p_plan, uint64_t p_x, uint64_t p_y) {
    CodeRow made = CodeRow::for_plan(p_plan);
    made.write(p_plan.column(0), 0, p_x);
    made.write(p_plan.column(1), 0, p_y);
    return made;
}

TEST_CASE("[Networked][Wire][Hosted] the oracle agrees on one complete image") {
    const WirePlan plan = pair_plan(SchemaCore::I16);
    RowImageLedger ledger;
    ledger.stage(TOKEN, 1, plan, pair(plan, 7, 9));
    ledger.accept(TOKEN, 1, plan, pair(plan, 7, 9));

    const RowImageLedger::Verdict verdict = ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.agreed, 1);
    NETW_CHECK_EQ(verdict.diverged, 0);
}

TEST_CASE("[Networked][Wire][Hosted] the oracle names an omitted column divergence") {
    const WirePlan plan = pair_plan(SchemaCore::I16);
    RowImageLedger ledger;
    ledger.stage(TOKEN, 4, plan, pair(plan, 0, 0));
    ledger.accept(TOKEN, 4, plan, pair(plan, 0, 1));

    const RowImageLedger::Verdict verdict = ledger.audit();
    const bool clean = verdict.clean();
    CHECK_FALSE(clean);
    NETW_CHECK_EQ(verdict.diverged, 1);
    NETW_CHECK_EQ(verdict.agreed, 0);
    NETW_CHECK_EQ(int64_t(verdict.fault_revision), 4);
    NETW_CHECK_EQ(verdict.fault.differing_column, 1);
    NETW_CHECK_EQ(verdict.fault.differing_element, 0);
    NETW_CHECK_EQ(int64_t(verdict.fault.staged_code), 0);
    NETW_CHECK_EQ(int64_t(verdict.fault.accepted_code), 1);
}

TEST_CASE("[Networked][Wire][Hosted] the oracle judges the image, not its provenance") {
    const WirePlan plan = pair_plan(SchemaCore::I16);
    const CodeRow staged = pair(plan, 300, 400);
    netw::repl::SnapshotHeader header;
    header.token = TOKEN;
    header.revision = 2;
    header.tick = -1;
    header.reconcile_ack = -1;
    header.mask = plan.full_mask();
    const godot::PackedByteArray bytes
        = netw::repl::write_snapshot_row(header, 0, plan, staged, nullptr);
    CodeRow decoded = pair(plan, 0, 0);
    netw::repl::SnapshotHeader arrival;
    const bool read_back = netw::repl::read_snapshot_row(
        bytes,
        0,
        plan,
        arrival,
        decoded,
        nullptr,
        nullptr
    );
    CHECK(read_back);

    RowImageLedger ledger;
    ledger.stage(TOKEN, 2, plan, staged);
    ledger.accept(TOKEN, 2, plan, decoded);

    const RowImageLedger::Verdict verdict = ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.agreed, 1);
}

TEST_CASE("[Networked][Wire][Hosted] the oracle refuses a row no sender staged") {
    const WirePlan plan = pair_plan(SchemaCore::I16);
    RowImageLedger ledger;
    ledger.accept(TOKEN, 9, plan, pair(plan, 1, 2));

    const RowImageLedger::Verdict verdict = ledger.audit();
    const bool clean = verdict.clean();
    CHECK_FALSE(clean);
    NETW_CHECK_EQ(verdict.accepted_unstaged, 1);
    NETW_CHECK_EQ(int64_t(verdict.fault_revision), 9);
}

TEST_CASE("[Networked][Wire][Hosted] an unaccepted staged row is owed, not wrong") {
    const WirePlan plan = pair_plan(SchemaCore::I16);
    RowImageLedger ledger;
    ledger.stage(TOKEN, 5, plan, pair(plan, 1, 2));
    ledger.stage(TOKEN, 6, plan, pair(plan, 3, 4));
    ledger.accept(TOKEN, 5, plan, pair(plan, 1, 2));

    const RowImageLedger::Verdict verdict = ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.staged_unaccepted, 1);
    NETW_CHECK_EQ(verdict.agreed, 1);
}

TEST_CASE("[Networked][Wire][Hosted] the oracle refuses a restated revision") {
    const WirePlan plan = pair_plan(SchemaCore::I16);
    RowImageLedger ledger;
    ledger.stage(TOKEN, 3, plan, pair(plan, 1, 1));
    ledger.accept(TOKEN, 3, plan, pair(plan, 1, 1));
    ledger.accept(TOKEN, 3, plan, pair(plan, 1, 2));

    const RowImageLedger::Verdict verdict = ledger.audit();
    const bool clean = verdict.clean();
    CHECK_FALSE(clean);
    NETW_CHECK_EQ(verdict.restated_revisions, 1);
}

TEST_CASE("[Networked][Wire][Hosted] one token does not answer for another") {
    const WirePlan plan = pair_plan(SchemaCore::I16);
    RowImageLedger ledger;
    ledger.stage(TOKEN, 1, plan, pair(plan, 5, 5));
    ledger.accept(TOKEN + 1, 1, plan, pair(plan, 5, 5));

    const RowImageLedger::Verdict verdict = ledger.audit();
    const bool clean = verdict.clean();
    CHECK_FALSE(clean);
    NETW_CHECK_EQ(verdict.accepted_unstaged, 1);
    NETW_CHECK_EQ(verdict.staged_unaccepted, 1);
    NETW_CHECK_EQ(verdict.agreed, 0);
}

TEST_CASE("[Networked][Wire][Hosted] the oracle refuses two rows of unequal shape") {
    const WirePlan narrow = pair_plan(SchemaCore::I16);
    const WirePlan wide = pair_plan(SchemaCore::I32);
    const ImageComparison compared = compare_row_images(
        take_row_image(narrow, pair(narrow, 1, 1)),
        take_row_image(wide, pair(wide, 1, 1))
    );
    const bool shaped = compared.disagreement == ImageDisagreement::SHAPE;
    CHECK(shaped);

    RowImageLedger ledger;
    ledger.stage(TOKEN, 1, narrow, pair(narrow, 1, 1));
    ledger.accept(TOKEN, 1, wide, pair(wide, 1, 1));
    const RowImageLedger::Verdict verdict = ledger.audit();
    const bool clean = verdict.clean();
    CHECK_FALSE(clean);
    NETW_CHECK_EQ(verdict.diverged, 1);
}

TEST_CASE("[Networked][Wire][Hosted] the oracle reads every column of a wide row") {
    SchemaRecord record;
    record.name = godot::StringName("OracleWide");
    for (int at = 0; at < 6; ++at) {
        SchemaCore::append_column(
            &record,
            godot::StringName(godot::String::num_int64(at)),
            SchemaCore::I16,
            1
        );
    }
    SchemaCore::fix(&record);
    const WirePlan plan = WirePlan::compile(record);

    CodeRow staged = CodeRow::for_plan(plan);
    CodeRow accepted = CodeRow::for_plan(plan);
    for (uint32_t at = 0; at < plan.column_count(); ++at) {
        staged.write(plan.column(at), 0, at + 1);
        accepted.write(plan.column(at), 0, at + 1);
    }
    accepted.write(plan.column(5), 0, 99);

    const ImageComparison compared = compare_row_images(
        take_row_image(plan, staged),
        take_row_image(plan, accepted)
    );
    const bool found = compared.disagreement == ImageDisagreement::BITS;
    CHECK(found);
    NETW_CHECK_EQ(compared.differing_column, 5);
    NETW_CHECK_EQ(int64_t(compared.staged_code), 6);
    NETW_CHECK_EQ(int64_t(compared.accepted_code), 99);
}

} // namespace TestWireRowImageOracle

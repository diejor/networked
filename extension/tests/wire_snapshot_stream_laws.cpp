#include "support/netw_test.h"

#include "support/row_image_oracle.h"

#include <cstdint>

#include "godot/templates.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/snapshot_stream.hpp"

namespace TestWireSnapshotStream {

using netw::SchemaCore;
using netw::repl::name_snapshot_row;
using netw::repl::read_snapshot_row;
using netw::repl::SnapshotHeader;
using netw::repl::SnapshotRefusal;
using netw::repl::write_snapshot_row;
using netw::table::DeltaMode;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::ReceiptVerdict;
using netw::wire::RowAdmission;
using netw::wire::SNAPSHOT_STAGE_DEPTH;
using netw::wire::SnapshotReceiver;
using netw::wire::SnapshotSender;
using netw::wire::WirePlan;
using netw_test::RowImageLedger;

const uint64_t TOKEN = 907;
const int64_t BASE_TICK = 100;

WirePlan triple_plan() {
    SchemaRecord record;
    record.name = godot::StringName("SnapshotRow");
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
    SchemaCore::append_column(
        &record,
        godot::StringName("z"),
        SchemaCore::I16,
        1
    );
    record.at(0)->delta = DeltaMode::LADDER;
    record.at(1)->delta = DeltaMode::LADDER;
    record.at(2)->delta = DeltaMode::LADDER;
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

CodeRow triple(
    const WirePlan &p_plan,
    uint64_t p_x,
    uint64_t p_y,
    uint64_t p_z
) {
    CodeRow made = CodeRow::for_plan(p_plan);
    made.write(p_plan.column(0), 0, p_x);
    made.write(p_plan.column(1), 0, p_y);
    made.write(p_plan.column(2), 0, p_z);
    return made;
}

enum class Arrival : uint8_t {
    ACCEPTED,
    DUPLICATE,
    STALE,
    REFUSED_BASELINE,
    MALFORMED,
};

struct Link {
    WirePlan plan;
    SnapshotSender sender;
    SnapshotReceiver receiver;
    RowImageLedger ledger;
    godot::Vector<godot::PackedByteArray> flight;
    godot::Vector<uint64_t> revisions;
    uint64_t last_distance = 0;
    uint64_t last_mask = 0;
    SnapshotRefusal last_refusal = SnapshotRefusal::NONE;

    void open() {
        plan = triple_plan();
    }

    uint64_t publish(const CodeRow &p_row) {
        sender.desire(p_row);
        const uint64_t revision = sender.reserve();
        const uint64_t distance = sender.distance_for(revision);
        const CodeRow *base = distance == 0 ? nullptr : sender.confirmed();
        SnapshotHeader header;
        header.token = TOKEN;
        header.revision = revision;
        header.distance = distance;
        header.tick = BASE_TICK;
        header.mask = distance == 0 ? plan.full_mask()
                                    : CodeRow::changed_mask(plan, *base, p_row);
        const godot::PackedByteArray bytes
            = write_snapshot_row(header, BASE_TICK, plan, p_row, base);
        if (bytes.is_empty()) {
            return 0;
        }
        sender.expose(revision, p_row);
        ledger.stage(TOKEN, revision, plan, p_row);
        flight.push_back(bytes);
        revisions.push_back(revision);
        last_distance = distance;
        last_mask = header.mask;
        return revision;
    }

    Arrival deliver(const godot::PackedByteArray &p_bytes) {
        last_refusal = SnapshotRefusal::NONE;
        SnapshotHeader named;
        if (!name_snapshot_row(p_bytes, named)) {
            last_refusal = SnapshotRefusal::MALFORMED;
            return Arrival::MALFORMED;
        }
        const RowAdmission admission = receiver.admits(named.revision);
        if (admission == RowAdmission::DUPLICATE) {
            return Arrival::DUPLICATE;
        }
        if (admission == RowAdmission::STALE) {
            return Arrival::STALE;
        }
        const CodeRow *base = named.absolute()
            ? nullptr
            : receiver.baseline(named.baseline_revision());
        SnapshotHeader header;
        CodeRow landed;
        if (receiver.accepted() != nullptr) {
            landed.copy_from(*receiver.accepted());
        }
        SnapshotRefusal refusal = SnapshotRefusal::NONE;
        if (!read_snapshot_row(
                p_bytes,
                BASE_TICK,
                plan,
                header,
                landed,
                base,
                &refusal
            )) {
            last_refusal = refusal;
            return refusal == SnapshotRefusal::BASELINE_UNKNOWN
                ? Arrival::REFUSED_BASELINE
                : Arrival::MALFORMED;
        }
        receiver.commit(header.revision, landed);
        ledger.accept(TOKEN, header.revision, plan, landed);
        return Arrival::ACCEPTED;
    }

    Arrival deliver_at(int32_t p_index) {
        return deliver(flight[p_index]);
    }

    Arrival flush() {
        Arrival last = Arrival::ACCEPTED;
        for (int32_t at = 0; at < flight.size(); ++at) {
            last = deliver(flight[at]);
        }
        flight.clear();
        return last;
    }

    uint64_t column(int p_column) const {
        const CodeRow *held = receiver.accepted();
        return held == nullptr ? 0 : held->read(plan.column(p_column), 0);
    }
};

uint64_t roll(uint64_t &r_seed) {
    r_seed = r_seed * uint64_t(6364136223846793005)
        + uint64_t(1442695040888963407);
    return r_seed >> 33;
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ABA return reaches the exact first image"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 0, 0, 0));
    link.flush();
    NETW_CHECK_EQ(
        int(link.sender.receipt(first)),
        int(ReceiptVerdict::PROMOTED)
    );

    const uint64_t middle = link.publish(triple(link.plan, 0, 0, 1));
    link.flush();
    const uint64_t back = link.publish(triple(link.plan, 0, 0, 0));
    NETW_CHECK_EQ(link.last_distance, 2);
    NETW_CHECK_EQ(link.last_mask, 0);
    NETW_CHECK_EQ(int(link.flush()), int(Arrival::ACCEPTED));

    NETW_CHECK_EQ(link.receiver.accepted_at(), back);
    NETW_CHECK_EQ(link.column(2), 0);
    const RowImageLedger::Verdict verdict = link.ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.agreed, 3);
    NETW_CHECK_EQ(
        int(link.sender.receipt(middle)),
        int(ReceiptVerdict::PROMOTED)
    );
    NETW_CHECK_EQ(
        int(link.sender.receipt(back)),
        int(ReceiptVerdict::PROMOTED)
    );
    const bool quiet = link.sender.quiet();
    CHECK(quiet);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a zero mask delta restores its named baseline"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 5, 6, 7));
    link.flush();
    link.sender.receipt(first);
    link.publish(triple(link.plan, 5, 6, 9));
    link.flush();
    NETW_CHECK_EQ(link.column(2), 9);

    link.publish(triple(link.plan, 5, 6, 7));
    NETW_CHECK_EQ(link.last_mask, 0);
    NETW_CHECK_EQ(int(link.flush()), int(Arrival::ACCEPTED));
    NETW_CHECK_EQ(link.column(0), 5);
    NETW_CHECK_EQ(link.column(1), 6);
    NETW_CHECK_EQ(link.column(2), 7);
    const bool clean = link.ledger.audit().clean();
    CHECK(clean);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an omitted column comes from the baseline, not "
    "the held row"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 1, 1, 1));
    link.flush();
    link.sender.receipt(first);

    link.publish(triple(link.plan, 1, 4, 1));
    link.flush();
    NETW_CHECK_EQ(link.column(1), 4);

    link.publish(triple(link.plan, 2, 1, 1));
    NETW_CHECK_EQ(link.last_distance, 2);
    NETW_CHECK_EQ(int(link.flush()), int(Arrival::ACCEPTED));
    NETW_CHECK_EQ(link.column(0), 2);
    NETW_CHECK_EQ(link.column(1), 1);
    const bool clean = link.ledger.audit().clean();
    CHECK(clean);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a lane stays scheduled until its exposure is "
    "confirmed"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 3, 3, 3));
    link.flush();
    link.sender.receipt(first);
    const bool quiet_at_first = link.sender.quiet();
    CHECK(quiet_at_first);

    const uint64_t exposed = link.publish(triple(link.plan, 3, 3, 4));
    link.sender.desire(triple(link.plan, 3, 3, 3));
    const bool quiet_while_exposed = link.sender.quiet();
    CHECK(!quiet_while_exposed);

    const uint64_t restored = link.publish(triple(link.plan, 3, 3, 3));
    link.flush();
    NETW_CHECK_EQ(
        int(link.sender.receipt(exposed)),
        int(ReceiptVerdict::PROMOTED)
    );
    const bool quiet_on_older_receipt = link.sender.quiet();
    CHECK(!quiet_on_older_receipt);
    NETW_CHECK_EQ(
        int(link.sender.receipt(restored)),
        int(ReceiptVerdict::PROMOTED)
    );
    const bool quiet_on_latest = link.sender.quiet();
    CHECK(quiet_on_latest);
}

TEST_CASE(
    "[Networked][Wire][Hosted] delayed, duplicated and reordered receipts "
    "never regress the confirmed revision"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 0, 0, 0));
    const uint64_t second = link.publish(triple(link.plan, 0, 0, 1));
    const uint64_t third = link.publish(triple(link.plan, 0, 0, 2));
    link.flush();

    NETW_CHECK_EQ(
        int(link.sender.receipt(third)),
        int(ReceiptVerdict::PROMOTED)
    );
    NETW_CHECK_EQ(link.sender.confirmed_at(), third);
    NETW_CHECK_EQ(
        int(link.sender.receipt(first)),
        int(ReceiptVerdict::IGNORED)
    );
    NETW_CHECK_EQ(
        int(link.sender.receipt(second)),
        int(ReceiptVerdict::IGNORED)
    );
    NETW_CHECK_EQ(
        int(link.sender.receipt(third)),
        int(ReceiptVerdict::IGNORED)
    );
    NETW_CHECK_EQ(link.sender.confirmed_at(), third);

    NETW_CHECK_EQ(
        link.sender.receipt(third + 9),
        int(ReceiptVerdict::UNEXPOSED)
    );
    NETW_CHECK_EQ(link.sender.confirmed_at(), third);
    const bool quiet = link.sender.quiet();
    CHECK(quiet);
    const bool clean = link.ledger.audit().clean();
    CHECK(clean);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a receipt for an evicted snapshot manufactures "
    "no row"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 1, 2, 3));
    for (uint32_t at = 0; at < SNAPSHOT_STAGE_DEPTH + 4; ++at) {
        link.publish(triple(link.plan, at + 8, 2, 3));
    }
    NETW_CHECK_EQ(link.sender.staged_count(), SNAPSHOT_STAGE_DEPTH);
    const bool dropped = !link.sender.holds(first);
    CHECK(dropped);

    NETW_CHECK_EQ(
        int(link.sender.receipt(first)),
        int(ReceiptVerdict::EVICTED)
    );
    NETW_CHECK_EQ(link.sender.confirmed_at(), 0);
    const CodeRow *confirmed = link.sender.confirmed();
    const bool no_row = confirmed == nullptr;
    CHECK(no_row);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a pinned repair survives the eviction its "
    "neighbours take"
) {
    Link link;
    link.open();
    const uint64_t repair = link.publish(triple(link.plan, 9, 9, 9));
    const bool pinned = link.sender.pin(repair);
    CHECK(pinned);
    for (uint32_t at = 0; at < SNAPSHOT_STAGE_DEPTH + 4; ++at) {
        link.publish(triple(link.plan, at + 1, 9, 9));
    }
    const bool kept = link.sender.holds(repair);
    CHECK(kept);
    NETW_CHECK_EQ(
        int(link.sender.receipt(repair)),
        int(ReceiptVerdict::PROMOTED)
    );
    NETW_CHECK_EQ(link.sender.confirmed_at(), repair);
}

TEST_CASE(
    "[Networked][Wire][Hosted] two revisions of one stream in one datagram "
    "land once each"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 4, 0, 0));
    const uint64_t second = link.publish(triple(link.plan, 4, 0, 1));
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Arrival::ACCEPTED));
    NETW_CHECK_EQ(int(link.deliver_at(1)), int(Arrival::ACCEPTED));
    NETW_CHECK_EQ(link.receiver.accepted_at(), second);

    NETW_CHECK_EQ(int(link.deliver_at(1)), int(Arrival::DUPLICATE));
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Arrival::STALE));
    NETW_CHECK_EQ(link.receiver.accepted_at(), second);
    NETW_CHECK_EQ(link.column(2), 1);

    const bool refused_commit
        = !link.receiver.commit(first, triple(link.plan, 0, 0, 0));
    CHECK(refused_commit);
    NETW_CHECK_EQ(link.column(0), 4);
    const RowImageLedger::Verdict verdict = link.ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.agreed, 2);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a delta whose baseline is gone refuses without "
    "touching the held row"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 7, 7, 7));
    link.flush();
    link.sender.receipt(first);
    link.publish(triple(link.plan, 7, 7, 8));

    SnapshotReceiver fresh;
    SnapshotHeader named;
    const bool named_ok = name_snapshot_row(link.flight[0], named);
    CHECK(named_ok);
    NETW_CHECK_EQ(named.distance, 1);
    const CodeRow *absent = fresh.baseline(named.baseline_revision());
    const bool nothing = absent == nullptr;
    CHECK(nothing);

    SnapshotHeader header;
    CodeRow landed;
    SnapshotRefusal refusal = SnapshotRefusal::NONE;
    const bool read = read_snapshot_row(
        link.flight[0],
        BASE_TICK,
        link.plan,
        header,
        landed,
        nullptr,
        &refusal
    );
    CHECK(!read);
    NETW_CHECK_EQ(int(refusal), int(SnapshotRefusal::BASELINE_UNKNOWN));
    NETW_CHECK_EQ(fresh.accepted_at(), 0);
    NETW_CHECK_EQ(fresh.ring_count(), 0);
    NETW_CHECK_EQ(link.column(2), 7);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a sparse lane keeps its identity past a "
    "sixteen bit datagram wrap"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 2, 2, 2));
    link.flush();
    link.sender.receipt(first);

    SnapshotReceiver busy;
    for (uint64_t at = 1; at <= 70000; ++at) {
        busy.commit(at, triple(link.plan, at & 0x3ff, 0, 0));
    }
    NETW_CHECK_EQ(busy.accepted_at(), 70000);
    NETW_CHECK_EQ(busy.ring_count(), 64);

    link.publish(triple(link.plan, 2, 2, 3));
    NETW_CHECK_EQ(link.last_distance, 1);
    NETW_CHECK_EQ(int(link.flush()), int(Arrival::ACCEPTED));
    NETW_CHECK_EQ(link.column(2), 3);
    const CodeRow *sparse_base = link.receiver.baseline(first);
    const bool sparse_base_is_held = sparse_base != nullptr;
    CHECK(sparse_base_is_held);

    SnapshotReceiver wide;
    wide.commit(1, triple(link.plan, 11, 0, 0));
    wide.commit(65537, triple(link.plan, 22, 0, 0));
    const CodeRow *low = wide.baseline(1);
    const CodeRow *high = wide.baseline(65537);
    const bool both_held = low != nullptr && high != nullptr;
    CHECK(both_held);
    const bool distinct = !low->equals(*high);
    CHECK(distinct);
    const bool clean = link.ledger.audit().clean();
    CHECK(clean);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a delta is offered at distance 32 and never at "
    "33"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 0, 0, 0));
    link.flush();
    link.sender.receipt(first);
    NETW_CHECK_EQ(link.sender.confirmed_at(), 1);

    NETW_CHECK_EQ(link.sender.distance_for(1 + 32), 32);
    NETW_CHECK_EQ(link.sender.distance_for(1 + 33), 0);
    NETW_CHECK_EQ(link.sender.distance_for(1), 0);
    NETW_CHECK_EQ(link.sender.distance_for(0), 0);

    SnapshotHeader header;
    header.token = TOKEN;
    header.revision = 40;
    header.distance = 33;
    const godot::PackedByteArray refused = write_snapshot_row(
        header,
        BASE_TICK,
        link.plan,
        triple(link.plan, 1, 1, 1),
        link.sender.confirmed()
    );
    const bool nothing_written = refused.is_empty();
    CHECK(nothing_written);

    header.revision = 8;
    header.distance = 8;
    const godot::PackedByteArray underflow = write_snapshot_row(
        header,
        BASE_TICK,
        link.plan,
        triple(link.plan, 1, 1, 1),
        link.sender.confirmed()
    );
    const bool underflow_refused = underflow.is_empty();
    CHECK(underflow_refused);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a generated schedule accepts only exact staged "
    "images"
) {
    Link link;
    link.open();
    uint64_t seed = 20260915;
    godot::Vector<uint64_t> unsettled;
    uint64_t x = 0;
    uint64_t y = 0;
    uint64_t z = 0;

    for (int32_t step = 0; step < 400; ++step) {
        const uint64_t which = roll(seed) % 3;
        const uint64_t value = roll(seed) % 5;
        if (which == 0) {
            x = value;
        } else if (which == 1) {
            y = value;
        } else {
            z = value;
        }
        const uint64_t revision = link.publish(triple(link.plan, x, y, z));
        const bool published = revision != 0;
        CHECK(published);

        if (roll(seed) % 8 != 0) {
            link.flush();
            unsettled.push_back(link.receiver.accepted_at());
        } else {
            link.flight.clear();
        }
        if (roll(seed) % 3 == 0 && !unsettled.is_empty()) {
            const int32_t at = int32_t(roll(seed) % uint64_t(unsettled.size()));
            link.sender.receipt(unsettled[at]);
            unsettled.remove_at(at);
        }
    }

    link.publish(triple(link.plan, 4, 4, 4));
    link.flush();
    link.sender.receipt(link.receiver.accepted_at());

    const RowImageLedger::Verdict verdict = link.ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.diverged, 0);
    NETW_CHECK_EQ(verdict.accepted_unstaged, 0);
    NETW_CHECK_EQ(verdict.restated_revisions, 0);
    NETW_CHECK_GT(verdict.agreed, 300);
    NETW_CHECK_EQ(link.column(0), 4);
    NETW_CHECK_EQ(link.column(1), 4);
    NETW_CHECK_EQ(link.column(2), 4);
    const bool quiet = link.sender.quiet();
    CHECK(quiet);
}

TEST_CASE(
    "[Networked][Wire][Hosted] C5c-1 a stream whose newest exposed revision "
    "already carries the target awaits its receipt instead of allocating "
    "another"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 2, 2, 2));
    const bool owes_nothing_new = link.sender.awaiting_receipt();
    CHECK(owes_nothing_new);
    const bool not_quiet_yet = !link.sender.quiet();
    CHECK(not_quiet_yet);

    link.sender.desire(triple(link.plan, 2, 2, 2));
    const bool a_restated_target_still_awaits = link.sender.awaiting_receipt();
    CHECK(a_restated_target_still_awaits);
    NETW_CHECK_EQ(link.sender.exposed_high_water(), first);

    link.sender.desire(triple(link.plan, 2, 2, 3));
    const bool a_moved_target_owes_a_revision = !link.sender.awaiting_receipt();
    CHECK(a_moved_target_owes_a_revision);

    link.sender.desire(triple(link.plan, 2, 2, 2));
    link.flush();
    link.sender.receipt(first);
    const bool a_confirmed_target_awaits_nothing
        = !link.sender.awaiting_receipt();
    CHECK(a_confirmed_target_awaits_nothing);
    const bool quiet = link.sender.quiet();
    CHECK(quiet);
}

TEST_CASE(
    "[Networked][Wire][Hosted] C5c-2 a repair is one pinned absolute revision "
    "however many times the interval asks for it"
) {
    Link link;
    link.open();
    link.publish(triple(link.plan, 5, 0, 0));
    link.flight.clear();

    const uint64_t repair = link.sender.pinned_repair();
    const bool the_repair_is_its_own_revision = repair > 1;
    CHECK(the_repair_is_its_own_revision);
    NETW_CHECK_EQ(link.sender.pinned_repair(), repair);
    NETW_CHECK_EQ(link.sender.pinned_repair(), repair);
    NETW_CHECK_EQ(link.sender.repair_at_revision(), repair);
    NETW_CHECK_EQ(link.sender.exposed_high_water(), repair);

    for (uint32_t at = 0; at < SNAPSHOT_STAGE_DEPTH + 4; ++at) {
        const uint64_t spare = link.sender.reserve();
        link.sender.expose(spare, triple(link.plan, 5, 0, uint64_t(at)));
    }
    const bool the_repair_outlives_the_eviction = link.sender.holds(repair);
    CHECK(the_repair_outlives_the_eviction);
    NETW_CHECK_EQ(link.sender.pinned_repair(), repair);
}

TEST_CASE(
    "[Networked][Wire][Hosted] C5c-3 a target that moves replaces the pinned "
    "repair rather than retrying a row the game no longer wants"
) {
    Link link;
    link.open();
    link.publish(triple(link.plan, 7, 0, 0));
    link.flight.clear();
    const uint64_t stale = link.sender.pinned_repair();

    link.sender.desire(triple(link.plan, 7, 0, 1));
    NETW_CHECK_EQ(link.sender.repair_at_revision(), 0);

    const uint64_t fresh = link.sender.pinned_repair();
    const bool the_repair_moved_with_the_target = fresh > stale;
    CHECK(the_repair_moved_with_the_target);
    NETW_CHECK_EQ(link.sender.exposed_high_water(), fresh);

    link.sender.receipt(fresh);
    NETW_CHECK_EQ(link.sender.confirmed_at(), fresh);
    const bool quiet = link.sender.quiet();
    CHECK(quiet);
}

} // namespace TestWireSnapshotStream

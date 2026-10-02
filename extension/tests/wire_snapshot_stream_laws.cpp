#include "support/netw_test.h"

#include "support/row_image_oracle.h"
#include "support/send_rig.h"
#include "support/store_reader.h"

#include <cstdint>

#include "godot/templates.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/registry.hpp"
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
using netw::wire::SNAPSHOT_STAGE_DEPTH;
using netw::wire::WirePlan;
using netw_test::RowImageLedger;

const int PEER = 6;
const int64_t ROUTE = 17;
const uint8_t CHANNEL = 48;
const int64_t BASE_TICK = 100;

SchemaRecord triple_schema() {
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
    return record;
}

const netw::wire::WireRegistry &registry() {
    static const netw::wire::WireRegistry made = [] {
        netw::wire::WireRegistry out;
        netw::wire::ChannelDecl decl;
        decl.id = CHANNEL;
        decl.name = godot::StringName("snapshot_probe");
        decl.delivery = netw::wire::Delivery::FITTED;
        out.register_channel(decl);
        return out;
    }();
    return made;
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

using Arrival = netw::store::Landing;

struct Link {
    SchemaRecord schema;
    WirePlan plan;
    netw_test::SessionSend send;
    netw_test::StoreReader reader;
    RowImageLedger ledger;
    godot::Vector<godot::PackedByteArray> flight;
    CodeRow target;
    int64_t now_ms = 0;
    uint64_t last_distance = 0;
    uint64_t last_mask = 0;

    netw::wire::StreamLane lane() const {
        netw::wire::StreamLane made;
        made.route = ROUTE;
        made.ordinal = 0;
        made.family = netw::wire::StreamFamily::VOLATILE;
        return made;
    }

    netw_test::RowOffer offer() const {
        netw_test::RowOffer made;
        made.route = ROUTE;
        made.channel = CHANNEL;
        made.schema = &schema;
        made.tick = BASE_TICK;
        netw_test::offer_row(made, target);
        made.recipients.push_back(PEER);
        return made;
    }

    void open() {
        schema = triple_schema();
        plan = WirePlan::compile(schema);
        target = CodeRow::for_plan(plan);
        reader.declare(schema, ROUTE, BASE_TICK);
        netw_test::stage_offer(send.world, offer(), BASE_TICK);
        const uint64_t request = send.stream_open(PEER, lane());
        send.stream_ready(PEER, request, netw::wire::next_stream_token());
    }

    uint64_t token() const {
        return send.token_of(PEER, lane());
    }

    uint32_t peer() const {
        return send.world.peer_of(PEER);
    }

    void desire(const CodeRow &p_row) {
        target.copy_from(p_row);
        netw_test::stage_offer(send.world, offer(), BASE_TICK);
    }

    uint64_t pass() {
        godot::LocalVector<netw_test::RowOffer> offers;
        offers.push_back(offer());
        const netw::repl::SessionResult result = send.run(
            registry(),
            offers,
            int64_t(1) << 20,
            BASE_TICK,
            now_ms
        );
        uint64_t revision = 0;
        for (uint32_t at = 0; at < result.sends.size(); ++at) {
            const netw::repl::RowSend &sent = result.sends[at];
            SnapshotHeader named;
            name_snapshot_row(sent.bytes, named);
            if (!sent.resend) {
                ledger.stage(sent.token, sent.revision, plan, target);
            }
            flight.push_back(sent.bytes);
            last_distance = named.distance;
            last_mask = sent.mask;
            revision = sent.revision;
        }
        return revision;
    }

    uint64_t publish(const CodeRow &p_row) {
        target.copy_from(p_row);
        return pass();
    }

    uint64_t repair() {
        return pass();
    }

    ReceiptVerdict receipt(uint64_t p_revision) {
        return send.stream_receipt(PEER, token(), p_revision);
    }

    bool quiet() const {
        return !netw_test::row_owed(send.world, lane(), PEER);
    }

    uint64_t confirmed_at() const {
        return netw_test::row_acked(send.world, lane(), PEER);
    }

    uint64_t exposed_high_water() const {
        return netw_test::row_exposed(send.world, lane(), PEER);
    }

    bool holds(uint64_t p_revision) const {
        return send.world.flight_of(peer(), token(), p_revision) != nullptr;
    }

    uint64_t pinned_at() const {
        const netw::store::Flight *pinned
            = send.world.pinned_of(peer(), token());
        return pinned != nullptr ? pinned->revision : 0;
    }

    bool awaiting_receipt() const {
        const netw::store::RowAddress at = send.world.lane_row(lane());
        const uint64_t exposed = exposed_high_water();
        if (!at.valid() || exposed <= confirmed_at()) {
            return false;
        }
        const netw::store::Flight *newest
            = send.world.flight_of(peer(), token(), exposed);
        return newest != nullptr
            && send.world.table(at.table).holds_image(at.row, newest->image);
    }

    uint64_t mint_spare(const CodeRow &p_image, bool p_pinned) {
        const netw::store::RowAddress at = send.world.lane_row(lane());
        return send.world.mint(at, peer(), p_image, p_pinned);
    }

    Arrival deliver(const godot::PackedByteArray &p_bytes) {
        SnapshotHeader named;
        if (!name_snapshot_row(p_bytes, named)) {
            return Arrival::MALFORMED;
        }
        const Arrival landed = reader.land(PEER, p_bytes);
        if (landed == Arrival::ACCEPTED) {
            ledger.accept(named.token, named.revision, plan, reader.row());
        }
        return landed;
    }

    uint64_t accepted_at() {
        return reader.accepted(PEER, token());
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

    uint64_t column(int p_column) {
        return reader.column(uint32_t(p_column));
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
    NETW_CHECK_EQ(int(link.receipt(first)), int(ReceiptVerdict::PROMOTED));

    const uint64_t middle = link.publish(triple(link.plan, 0, 0, 1));
    link.flush();
    const uint64_t back = link.publish(triple(link.plan, 0, 0, 0));
    NETW_CHECK_EQ(link.last_distance, 2);
    NETW_CHECK_EQ(link.last_mask, 0);
    NETW_CHECK_EQ(int(link.flush()), int(Arrival::ACCEPTED));

    NETW_CHECK_EQ(link.accepted_at(), back);
    NETW_CHECK_EQ(link.column(2), 0);
    const RowImageLedger::Verdict verdict = link.ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.agreed, 3);
    NETW_CHECK_EQ(int(link.receipt(middle)), int(ReceiptVerdict::PROMOTED));
    NETW_CHECK_EQ(int(link.receipt(back)), int(ReceiptVerdict::PROMOTED));
    const bool quiet = link.quiet();
    CHECK(quiet);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a zero mask delta restores its named baseline"
) {
    Link link;
    link.open();
    const uint64_t first = link.publish(triple(link.plan, 5, 6, 7));
    link.flush();
    link.receipt(first);
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
    link.receipt(first);

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
    link.receipt(first);
    const bool quiet_at_first = link.quiet();
    CHECK(quiet_at_first);

    const uint64_t exposed = link.publish(triple(link.plan, 3, 3, 4));
    link.desire(triple(link.plan, 3, 3, 3));
    const bool quiet_while_exposed = link.quiet();
    CHECK(!quiet_while_exposed);

    const uint64_t restored = link.publish(triple(link.plan, 3, 3, 3));
    link.flush();
    NETW_CHECK_EQ(int(link.receipt(exposed)), int(ReceiptVerdict::PROMOTED));
    const bool quiet_on_older_receipt = link.quiet();
    CHECK(!quiet_on_older_receipt);
    NETW_CHECK_EQ(int(link.receipt(restored)), int(ReceiptVerdict::PROMOTED));
    const bool quiet_on_latest = link.quiet();
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

    NETW_CHECK_EQ(int(link.receipt(third)), int(ReceiptVerdict::PROMOTED));
    NETW_CHECK_EQ(link.confirmed_at(), third);
    NETW_CHECK_EQ(int(link.receipt(first)), int(ReceiptVerdict::IGNORED));
    NETW_CHECK_EQ(int(link.receipt(second)), int(ReceiptVerdict::IGNORED));
    NETW_CHECK_EQ(int(link.receipt(third)), int(ReceiptVerdict::IGNORED));
    NETW_CHECK_EQ(link.confirmed_at(), third);

    NETW_CHECK_EQ(int(link.receipt(third + 9)), int(ReceiptVerdict::UNEXPOSED));
    NETW_CHECK_EQ(link.confirmed_at(), third);
    const bool quiet = link.quiet();
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
    NETW_CHECK_EQ(
        link.send.world.flight_count(link.peer()),
        SNAPSHOT_STAGE_DEPTH
    );
    const bool dropped = !link.holds(first);
    CHECK(dropped);

    NETW_CHECK_EQ(int(link.receipt(first)), int(ReceiptVerdict::EVICTED));
    NETW_CHECK_EQ(link.confirmed_at(), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a pinned repair survives the eviction its "
    "neighbours take"
) {
    Link link;
    link.open();
    link.desire(triple(link.plan, 9, 9, 9));
    const uint64_t repair = link.mint_spare(triple(link.plan, 9, 9, 9), true);
    const bool pinned = link.pinned_at() == repair;
    CHECK(pinned);
    for (uint32_t at = 0; at < SNAPSHOT_STAGE_DEPTH + 4; ++at) {
        link.mint_spare(triple(link.plan, at + 1, 9, 9), false);
    }
    const bool kept = link.holds(repair);
    CHECK(kept);
    NETW_CHECK_EQ(int(link.receipt(repair)), int(ReceiptVerdict::PROMOTED));
    NETW_CHECK_EQ(link.confirmed_at(), repair);
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
    NETW_CHECK_EQ(link.accepted_at(), second);

    NETW_CHECK_EQ(int(link.deliver_at(1)), int(Arrival::DUPLICATE));
    NETW_CHECK_EQ(int(link.deliver_at(0)), int(Arrival::STALE));
    NETW_CHECK_EQ(link.accepted_at(), second);
    NETW_CHECK_EQ(link.column(2), 1);
    NETW_CHECK_EQ(link.column(0), 4);
    NETW_CHECK_EQ(first + 1, second);
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
    link.receipt(first);
    link.publish(triple(link.plan, 7, 7, 8));

    netw_test::StoreReader fresh;
    fresh.declare(link.schema, ROUTE, BASE_TICK);
    SnapshotHeader named;
    const bool named_ok = name_snapshot_row(link.flight[0], named);
    CHECK(named_ok);
    NETW_CHECK_EQ(named.distance, 1);
    NETW_CHECK_EQ(
        int(fresh.land(PEER, link.flight[0])),
        int(Arrival::BASELINE_UNKNOWN)
    );

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
    NETW_CHECK_EQ(fresh.accepted(PEER, named.token), 0);
    NETW_CHECK_EQ(fresh.kept(PEER), 0);
    NETW_CHECK_EQ(fresh.column(2), 0);
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
    link.receipt(first);

    const uint64_t busy_token = 5;
    netw_test::StoreReader busy;
    busy.declare(link.schema, ROUTE, BASE_TICK);
    SnapshotHeader absolute;
    absolute.token = busy_token;
    for (uint64_t at = 1; at <= 70000; ++at) {
        absolute.revision = at;
        busy.land(
            PEER,
            write_snapshot_row(
                absolute,
                BASE_TICK,
                link.plan,
                triple(link.plan, at & 0x3ff, 0, 0),
                nullptr
            )
        );
    }
    NETW_CHECK_EQ(busy.accepted(PEER, busy_token), 70000);
    NETW_CHECK_EQ(busy.kept(PEER), netw::store::Table::RECEIVED_DEPTH);

    link.publish(triple(link.plan, 2, 2, 3));
    NETW_CHECK_EQ(link.last_distance, 1);
    NETW_CHECK_EQ(int(link.flush()), int(Arrival::ACCEPTED));
    NETW_CHECK_EQ(link.column(2), 3);
    const bool sparse_base_is_held = link.reader.keeps(PEER, first);
    CHECK(sparse_base_is_held);

    netw_test::StoreReader wide;
    wide.declare(link.schema, ROUTE, BASE_TICK);
    absolute.revision = 1;
    wide.land(
        PEER,
        write_snapshot_row(
            absolute,
            BASE_TICK,
            link.plan,
            triple(link.plan, 11, 0, 0),
            nullptr
        )
    );
    absolute.revision = 65537;
    wide.land(
        PEER,
        write_snapshot_row(
            absolute,
            BASE_TICK,
            link.plan,
            triple(link.plan, 22, 0, 0),
            nullptr
        )
    );
    const bool both_held = wide.keeps(PEER, 1) && wide.keeps(PEER, 65537);
    CHECK(both_held);
    const bool distinct
        = !wide.image_at(PEER, 1).equals(wide.image_at(PEER, 65537));
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
    link.receipt(first);
    NETW_CHECK_EQ(link.confirmed_at(), 1);

    for (uint64_t at = 1; at <= 31; ++at) {
        link.publish(triple(link.plan, at, 0, 0));
    }
    NETW_CHECK_EQ(link.publish(triple(link.plan, 32, 0, 0)), 1 + 32);
    NETW_CHECK_EQ(link.last_distance, 32);
    NETW_CHECK_EQ(link.publish(triple(link.plan, 33, 0, 0)), 1 + 33);
    NETW_CHECK_EQ(link.last_distance, 0);

    const CodeRow confirmed = triple(link.plan, 0, 0, 0);
    SnapshotHeader header;
    header.token = link.token();
    header.revision = 40;
    header.distance = 33;
    const godot::PackedByteArray refused = write_snapshot_row(
        header,
        BASE_TICK,
        link.plan,
        triple(link.plan, 1, 1, 1),
        &confirmed
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
        &confirmed
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
        const CodeRow before = triple(link.plan, x, y, z);
        if (which == 0) {
            x = value;
        } else if (which == 1) {
            y = value;
        } else {
            z = value;
        }
        const CodeRow wanted = triple(link.plan, x, y, z);
        const uint64_t revision = link.publish(wanted);
        const bool published = revision != 0 || wanted.equals(before);
        CHECK(published);

        if (roll(seed) % 8 != 0) {
            link.flush();
            unsettled.push_back(link.accepted_at());
        } else {
            link.flight.clear();
        }
        if (roll(seed) % 3 == 0 && !unsettled.is_empty()) {
            const int32_t at = int32_t(roll(seed) % uint64_t(unsettled.size()));
            link.receipt(unsettled[at]);
            unsettled.remove_at(at);
        }
    }

    link.publish(triple(link.plan, 9, 9, 9));
    link.publish(triple(link.plan, 4, 4, 4));
    link.flush();
    link.receipt(link.accepted_at());

    const RowImageLedger::Verdict verdict = link.ledger.audit();
    const bool clean = verdict.clean();
    CHECK(clean);
    NETW_CHECK_EQ(verdict.diverged, 0);
    NETW_CHECK_EQ(verdict.accepted_unstaged, 0);
    NETW_CHECK_EQ(verdict.restated_revisions, 0);
    NETW_CHECK_GT(verdict.agreed, 250);
    NETW_CHECK_EQ(link.column(0), 4);
    NETW_CHECK_EQ(link.column(1), 4);
    NETW_CHECK_EQ(link.column(2), 4);
    const bool quiet = link.quiet();
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
    const bool owes_nothing_new = link.awaiting_receipt();
    CHECK(owes_nothing_new);
    const bool not_quiet_yet = !link.quiet();
    CHECK(not_quiet_yet);

    const uint64_t restated = link.publish(triple(link.plan, 2, 2, 2));
    NETW_CHECK_EQ(restated, 0);
    const bool a_restated_target_still_awaits = link.awaiting_receipt();
    CHECK(a_restated_target_still_awaits);
    NETW_CHECK_EQ(link.exposed_high_water(), first);

    link.desire(triple(link.plan, 2, 2, 3));
    const bool a_moved_target_owes_a_revision = !link.awaiting_receipt();
    CHECK(a_moved_target_owes_a_revision);

    link.desire(triple(link.plan, 2, 2, 2));
    link.flush();
    link.receipt(first);
    const bool a_confirmed_target_awaits_nothing = !link.awaiting_receipt();
    CHECK(a_confirmed_target_awaits_nothing);
    const bool quiet = link.quiet();
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

    link.now_ms += 300;
    const uint64_t repair = link.repair();
    const bool the_repair_is_its_own_revision = repair > 1;
    CHECK(the_repair_is_its_own_revision);
    NETW_CHECK_EQ(link.last_distance, 0);
    link.now_ms += 300;
    NETW_CHECK_EQ(link.repair(), repair);
    link.now_ms += 300;
    NETW_CHECK_EQ(link.repair(), repair);
    NETW_CHECK_EQ(link.pinned_at(), repair);
    NETW_CHECK_EQ(link.exposed_high_water(), repair);

    for (uint32_t at = 0; at < SNAPSHOT_STAGE_DEPTH + 4; ++at) {
        link.mint_spare(triple(link.plan, 5, 0, uint64_t(at)), false);
    }
    const bool the_repair_outlives_the_eviction = link.holds(repair);
    CHECK(the_repair_outlives_the_eviction);
    NETW_CHECK_EQ(link.pinned_at(), repair);
}

TEST_CASE(
    "[Networked][Wire][Hosted] C5c-3 a target that moves replaces the pinned "
    "repair rather than retrying a row the game no longer wants"
) {
    Link link;
    link.open();
    link.publish(triple(link.plan, 7, 0, 0));
    link.flight.clear();
    link.now_ms += 300;
    const uint64_t stale = link.repair();
    NETW_CHECK_EQ(link.pinned_at(), stale);

    link.desire(triple(link.plan, 7, 0, 1));
    NETW_CHECK_EQ(link.pinned_at(), 0);

    const uint64_t fresh = link.publish(triple(link.plan, 7, 0, 1));
    const bool the_repair_moved_with_the_target = fresh > stale;
    CHECK(the_repair_moved_with_the_target);
    NETW_CHECK_EQ(link.exposed_high_water(), fresh);

    link.receipt(fresh);
    NETW_CHECK_EQ(link.confirmed_at(), fresh);
    const bool quiet = link.quiet();
    CHECK(quiet);
}

} // namespace TestWireSnapshotStream

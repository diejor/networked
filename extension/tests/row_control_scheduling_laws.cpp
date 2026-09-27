#include "support/netw_test.h"

#include <cstdint>

#include "netw/wire/control_scheduler.hpp"

namespace TestRowControlScheduling {

using netw::wire::ACCEPT_MAX_ENTRIES;
using netw::wire::CONTROL_FLUSH_PERIOD_MS;
using netw::wire::CONTROL_TICK_RESERVATION_BYTES;
using netw::wire::ControlRecord;
using netw::wire::ControlScheduler;
using netw::wire::ControlTag;
using netw::wire::StreamFamily;
using netw::wire::varuint_byte_span;
using netw::wire::write_control_record;

const int PEER = 2;

ControlRecord open_of(int64_t p_route, uint8_t p_ordinal, uint64_t p_request) {
    ControlRecord record;
    record.tag = ControlTag::OPEN;
    record.route = p_route;
    record.ordinal = p_ordinal;
    record.family = StreamFamily::VOLATILE;
    record.request = p_request;
    record.epoch = 1;
    record.schema = 9;
    return record;
}

int64_t payload_bytes(const godot::LocalVector<ControlRecord> &p_records) {
    int64_t total = 0;
    for (uint32_t at = 0; at < p_records.size(); ++at) {
        total += int64_t(write_control_record(p_records[at]).size());
    }
    return total;
}

TEST_CASE(
    "[Networked][Wire][Hosted] a receipt coalesces to the latest revision its "
    "token has accepted"
) {
    ControlScheduler scheduler;
    scheduler.accept(PEER, 7, 3);
    scheduler.accept(PEER, 7, 9);
    scheduler.accept(PEER, 7, 4);
    scheduler.accept(PEER, 8, 1);
    NETW_CHECK_EQ(scheduler.pending_receipts(PEER), 2);

    const godot::LocalVector<ControlRecord> out
        = scheduler.flush(PEER, 0, CONTROL_TICK_RESERVATION_BYTES);
    NETW_CHECK_EQ(out.size(), 1);
    const bool one_record = out.size() == 1;
    NETW_CHECK_EQ(one_record ? int(out[0].tag) : -1, int(ControlTag::ACCEPT));
    NETW_CHECK_EQ(one_record ? out[0].receipts.size() : 0, 2);
    const bool paired = one_record && out[0].receipts.size() == 2;
    NETW_CHECK_EQ(paired ? out[0].receipts[0].token : 0, 7);
    NETW_CHECK_EQ(paired ? out[0].receipts[0].revision : 0, 9);
    NETW_CHECK_EQ(paired ? out[0].receipts[1].token : 0, 8);
    NETW_CHECK_EQ(scheduler.pending_receipts(PEER), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a peer flushes control at most every fifty "
    "milliseconds"
) {
    ControlScheduler scheduler;
    const bool silent = scheduler.due(PEER, 0);
    CHECK(!silent);

    scheduler.accept(PEER, 7, 1);
    const bool first = scheduler.due(PEER, 0);
    CHECK(first);
    NETW_CHECK_EQ(
        scheduler.flush(PEER, 0, CONTROL_TICK_RESERVATION_BYTES).size(),
        1
    );

    scheduler.accept(PEER, 7, 2);
    const bool too_soon = scheduler.due(PEER, CONTROL_FLUSH_PERIOD_MS - 1);
    CHECK(!too_soon);
    NETW_CHECK_EQ(
        scheduler
            .flush(
                PEER,
                CONTROL_FLUSH_PERIOD_MS - 1,
                CONTROL_TICK_RESERVATION_BYTES
            )
            .size(),
        0
    );
    NETW_CHECK_EQ(scheduler.pending_receipts(PEER), 1);

    const bool ripe = scheduler.due(PEER, CONTROL_FLUSH_PERIOD_MS);
    CHECK(ripe);
    NETW_CHECK_EQ(
        scheduler
            .flush(
                PEER,
                CONTROL_FLUSH_PERIOD_MS,
                CONTROL_TICK_RESERVATION_BYTES
            )
            .size(),
        1
    );
}

TEST_CASE(
    "[Networked][Wire][Hosted] a batch splits at 32 entries and at 256 "
    "payload bytes, whichever it reaches first"
) {
    ControlScheduler scheduler;
    for (uint64_t at = 1; at <= 40; ++at) {
        scheduler.accept(PEER, at, at);
    }
    const godot::LocalVector<ControlRecord> out
        = scheduler.flush(PEER, 0, 4096);
    NETW_CHECK_EQ(out.size(), 2);
    const bool split = out.size() == 2;
    NETW_CHECK_EQ(split ? out[0].receipts.size() : 0, ACCEPT_MAX_ENTRIES);
    NETW_CHECK_EQ(split ? out[1].receipts.size() : 0, 8);
    for (uint32_t at = 0; at < out.size(); ++at) {
        const int64_t span = int64_t(write_control_record(out[at]).size());
        NETW_CHECK_ORDER(span, 256, <=);
    }

    ControlScheduler wide;
    for (uint64_t at = 0; at < ACCEPT_MAX_ENTRIES; ++at) {
        wide.accept(PEER, uint64_t(1) << 60, uint64_t(1) << 60);
        wide.accept(PEER, (uint64_t(1) << 60) + at, (uint64_t(1) << 60) + at);
    }
    NETW_CHECK_EQ(varuint_byte_span(uint64_t(1) << 60), 9);
    const godot::LocalVector<ControlRecord> fat = wide.flush(PEER, 0, 4096);
    NETW_CHECK_ORDER(fat.size(), 1, >);
    for (uint32_t at = 0; at < fat.size(); ++at) {
        NETW_CHECK_ORDER(fat[at].receipts.size(), ACCEPT_MAX_ENTRIES, <);
        const int64_t span = int64_t(write_control_record(fat[at]).size());
        NETW_CHECK_ORDER(span, 256, <=);
    }
}

TEST_CASE(
    "[Networked][Wire][Hosted] a smaller grant preserves the work it could "
    "not carry"
) {
    ControlScheduler scheduler;
    for (uint64_t at = 1; at <= 40; ++at) {
        scheduler.accept(PEER, at, at);
    }
    const godot::LocalVector<ControlRecord> tight = scheduler.flush(PEER, 0, 8);
    NETW_CHECK_EQ(tight.size(), 0);
    NETW_CHECK_EQ(scheduler.pending_receipts(PEER), 40);

    const godot::LocalVector<ControlRecord> roomy
        = scheduler.flush(PEER, CONTROL_FLUSH_PERIOD_MS, 80);
    NETW_CHECK_EQ(roomy.size(), 1);
    NETW_CHECK_ORDER(payload_bytes(roomy), 80, <=);
    NETW_CHECK_ORDER(roomy.size(), 2, <);
    NETW_CHECK_EQ(scheduler.pending_receipts(PEER), 8);
}

TEST_CASE(
    "[Networked][Wire][Hosted] lifecycle work coalesces per lane and rides "
    "ahead of the receipts"
) {
    ControlScheduler scheduler;
    scheduler.queue(PEER, open_of(3, 0, 1));
    scheduler.queue(PEER, open_of(3, 0, 1));
    scheduler.queue(PEER, open_of(3, 0, 2));
    scheduler.queue(PEER, open_of(4, 0, 3));
    NETW_CHECK_EQ(scheduler.pending_lifecycle(PEER), 2);
    scheduler.accept(PEER, 7, 1);

    const godot::LocalVector<ControlRecord> out
        = scheduler.flush(PEER, 0, CONTROL_TICK_RESERVATION_BYTES);
    NETW_CHECK_EQ(out.size(), 3);
    const bool ordered = out.size() == 3;
    NETW_CHECK_EQ(ordered ? int(out[0].tag) : -1, int(ControlTag::OPEN));
    NETW_CHECK_EQ(ordered ? out[0].request : 0, 2);
    NETW_CHECK_EQ(ordered ? int(out[1].tag) : -1, int(ControlTag::OPEN));
    NETW_CHECK_EQ(ordered ? out[1].request : 0, 3);
    NETW_CHECK_EQ(ordered ? int(out[2].tag) : -1, int(ControlTag::ACCEPT));
    NETW_CHECK_EQ(scheduler.pending_lifecycle(PEER), 0);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a torn down token takes its unsent receipt "
    "with it"
) {
    ControlScheduler scheduler;
    scheduler.accept(PEER, 7, 4);
    scheduler.accept(PEER, 8, 5);
    ControlRecord closing;
    closing.tag = ControlTag::CLOSE;
    closing.token = 7;
    scheduler.queue(PEER, closing);

    scheduler.drop_token(PEER, 7);
    NETW_CHECK_EQ(scheduler.pending_receipts(PEER), 1);
    NETW_CHECK_EQ(scheduler.pending_lifecycle(PEER), 0);

    const godot::LocalVector<ControlRecord> out
        = scheduler.flush(PEER, 0, CONTROL_TICK_RESERVATION_BYTES);
    NETW_CHECK_EQ(out.size(), 1);
    const bool one_record = out.size() == 1;
    NETW_CHECK_EQ(one_record ? out[0].receipts.size() : 0, 1);
    const bool one_receipt = one_record && out[0].receipts.size() == 1;
    NETW_CHECK_EQ(one_receipt ? out[0].receipts[0].token : 0, 8);
}

TEST_CASE("[Networked][Wire][Hosted] a forgotten peer owes no control at all") {
    ControlScheduler scheduler;
    scheduler.accept(PEER, 7, 4);
    scheduler.queue(PEER, open_of(3, 0, 1));
    scheduler.forget_peer(PEER);
    NETW_CHECK_EQ(scheduler.pending_receipts(PEER), 0);
    NETW_CHECK_EQ(scheduler.pending_lifecycle(PEER), 0);
    const bool owed = scheduler.due(PEER, 10000);
    CHECK(!owed);
    NETW_CHECK_EQ(scheduler.flush(PEER, 10000, 4096).size(), 0);
}

} // namespace TestRowControlScheduling

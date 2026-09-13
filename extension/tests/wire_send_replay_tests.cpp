#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/ack_book.hpp"
#include "netw/wire/baseline_book.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

using namespace godot;

namespace TestNetwWireSendReplay {

using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::table::SchemaRecord;
using netw::wire::AckBook;
using netw::wire::AckEntry;
using netw::wire::BaselineBook;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

const int PEER = 7;

WirePlan body_plan() {
    SchemaRecord record;
    record.name = godot::StringName("Body");
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

struct Pass {
    uint64_t x = 0;
    uint64_t y = 0;
    uint16_t seq = 0;
    int32_t ack = -1;
    uint64_t expect_mask = 0;
    uint32_t history = 0;
};

struct Lane {
    WirePlan plan = body_plan();
    BaselineBook baselines;
    AckBook acks;
    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;

    uint64_t run(const Pass &pass) {
        if (pass.ack >= 0) {
            const uint16_t acked = uint16_t(pass.ack);
            acks.process_ack(acked, pass.history, delivered, lost);
            baselines.acknowledge(PEER, acked, pass.history);
        }
        CodeRow row = CodeRow::for_plan(plan);
        row.write(plan.column(0), 0, pass.x);
        row.write(plan.column(1), 0, pass.y);

        const uint64_t mask = baselines.mask_to_send(PEER, plan, row);
        if (mask != 0) {
            baselines.stage(PEER, pass.seq, row);
            acks.record_send(pass.seq, 1, plan.row_bits());
        }
        return mask;
    }
};

const Pass SCHEDULE[] = {
    {10, 20, 1, -1, 0b11},
    {11, 20, 2, -1, 0b11},
    {11, 20, 3, -1, 0b11},
    {11, 20, 4, 3, 0b00},
};

const int SCHEDULE_LENGTH = int(sizeof(SCHEDULE) / sizeof(SCHEDULE[0]));

TEST_CASE(
    "[Networked][Wire][Hosted] a replayed lane sends what its schedule says"
) {
    Lane lane;
    REQUIRE(lane.plan.valid());

    for (int at = 0; at < SCHEDULE_LENGTH; ++at) {
        const uint64_t sent = lane.run(SCHEDULE[at]);
        NETW_CHECK_EQ(sent, SCHEDULE[at].expect_mask);
    }
}

TEST_CASE(
    "[Networked][Wire][Hosted] one ack settles the baseline book by delivery "
    "and the ack book by name"
) {
    Lane lane;
    for (int at = 0; at < SCHEDULE_LENGTH; ++at) {
        lane.run(SCHEDULE[at]);
    }

    NETW_CHECK_EQ(lane.baselines.in_flight_count(PEER), 0);
    CHECK(lane.baselines.has_baseline(PEER));
    NETW_CHECK_EQ(lane.acks.active_count(), 2);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a lost datagram leaves its columns owed and "
    "the next pass carries them"
) {
    Lane lane;
    lane.run({10, 20, 1, -1, 0b11});

    const uint64_t after_loss = lane.run({12, 20, 3, -1, 0b11});
    NETW_CHECK_EQ(after_loss, 0b11);
    CHECK_FALSE(lane.baselines.has_baseline(PEER));

    const uint64_t settled = lane.run({12, 20, 4, 3, 0b00});
    NETW_CHECK_EQ(settled, 0b00);
    CHECK(lane.baselines.has_baseline(PEER));
}

TEST_CASE(
    "[Networked][Wire][Hosted] a peer that leaves the lane heals whole when it "
    "returns"
) {
    Lane lane;
    lane.run({10, 20, 1, -1, 0b11});
    lane.run({10, 20, 2, 1, 0b00});
    REQUIRE(lane.baselines.has_baseline(PEER));

    LocalVector<int> nobody;
    lane.baselines.retain(nobody);
    CHECK_FALSE(lane.baselines.has_baseline(PEER));

    const uint64_t healed = lane.run({10, 20, 3, -1, 0b11});
    NETW_CHECK_EQ(healed, 0b11);
}

} // namespace TestNetwWireSendReplay

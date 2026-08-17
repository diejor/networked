// The two send-side books driven together over one scripted sequence.
//
// Each book has its own laws. This asks the question neither can: whether the
// pair, stepped through the same passes a real lane takes, agrees about what
// is outstanding. The books share no state and are coupled only through the
// caller, so a lane can stage a row against a datagram the ack book has
// already written off, or ack a datagram the baseline book never staged, and
// each book stays internally consistent while the pair disagrees.
//
// The replay is a table rather than a sequence of calls because the schedule
// IS the specification: a reader has to see which pass sent what, and which
// ack arrived when, without reconstructing it from control flow. A pass that
// sends nothing is written down as such, since a caught-up peer costing no
// bits is the property the masked lane exists for.
//
// This is the evidence a cutover is judged on. The books are stateful and
// `mask_to_send` mutates, so no shadow can ask them what they would have done
// without changing what they will do.

#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/wire/ack_book.hpp"
#include "netw/wire/baseline_book.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace TestNetwWireSendReplay {

using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::wire::AckBook;
using netw::wire::AckEntry;
using netw::wire::BaselineBook;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

const int PEER = 7;
const uint8_t CHANNEL = 19;

WirePlan body_plan() {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Body");
    SchemaCore::append_column(record, godot::StringName("x"), SchemaCore::I16, 1);
    SchemaCore::append_column(record, godot::StringName("y"), SchemaCore::I16, 1);
    SchemaCore::fix(record);
    return WirePlan::compile(record);
}

// One pass of a lane: the values polled, the datagram they ride, and the ack
// that arrived before the pass ran. A negative ack means none arrived.
struct Pass {
    uint64_t x = 0;
    uint64_t y = 0;
    uint16_t seq = 0;
    int32_t ack = -1;
    uint64_t expect_mask = 0;
};

struct Lane {
    WirePlan plan = body_plan();
    BaselineBook baselines;
    AckBook acks;
    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    int64_t next_send_id = 1000;

    // Runs one pass and answers the mask it sent, so a law reads the schedule
    // and the answers side by side.
    uint64_t run(const Pass &pass) {
        if (pass.ack >= 0) {
            const uint16_t acked = uint16_t(pass.ack);
            acks.process_ack(acked, delivered, lost);
            baselines.acknowledge(PEER, acked);
        }
        CodeRow row = CodeRow::for_plan(plan);
        row.write(plan.column(0), 0, pass.x);
        row.write(plan.column(1), 0, pass.y);

        const uint64_t mask = baselines.mask_to_send(PEER, plan, row);
        if (mask != 0) {
            baselines.stage(PEER, pass.seq, row);
            acks.record_send(pass.seq, CHANNEL, next_send_id++);
        }
        return mask;
    }
};

// The schedule both laws below read. Written once, because a second spelling
// of the same lane is a second thing to keep true.
//
// Pass 1 heals a peer that holds nothing. Pass 2 moves x alone, and the sticky
// rule keeps y with it because pass 1 is still unacked. Pass 3 moves nothing
// and still sends, for the same reason. Pass 4 arrives after the ack for pass
// 1 and sends nothing at all, which is the caught-up peer the lane is for.
const Pass SCHEDULE[] = {
    { 10, 20, 1, -1, 0b11 },
    { 11, 20, 2, -1, 0b11 },
    { 11, 20, 3, -1, 0b11 },
    { 11, 20, 4, 3, 0b00 },
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
    "[Networked][Wire][Hosted] one ack settles the baseline book cumulatively "
    "and the ack book by name"
) {
    Lane lane;
    for (int at = 0; at < SCHEDULE_LENGTH; ++at) {
        lane.run(SCHEDULE[at]);
    }

    // The two books read one ack differently, and both are right.
    //
    // An echo names the freshest datagram the peer has SEEN, which proves it
    // reconstructed that row and proves nothing about the two before it. The
    // baseline book only needs the former, so seq 3 promotes a baseline and
    // retires everything staged under it: rows 1 and 2 describe a peer state
    // that is now moot however they fared.
    //
    // The ack book is answering a different question, which datagrams arrived,
    // and to that question seqs 1 and 2 are still open. It holds them until
    // its window closes rather than inferring delivery from a later seq.
    //
    // A cutover that makes these agree has broken one of them. Reading the
    // baseline book's release as delivery would report a loss rate of zero
    // over a lossy link; reading the ack book's silence as a stalled baseline
    // would send the whole row every pass.
    NETW_CHECK_EQ(lane.baselines.in_flight_count(PEER), 0);
    CHECK(lane.baselines.has_baseline(PEER));
    NETW_CHECK_EQ(lane.acks.active_count(), 2);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a lost datagram leaves its columns owed and "
    "the next pass carries them"
) {
    Lane lane;
    lane.run({ 10, 20, 1, -1, 0b11 });

    // Seq 2 never lands. The peer's confirmed baseline is still nothing, so
    // the pass after it owes the whole row again rather than the delta.
    const uint64_t after_loss = lane.run({ 12, 20, 3, -1, 0b11 });
    NETW_CHECK_EQ(after_loss, 0b11);
    CHECK_FALSE(lane.baselines.has_baseline(PEER));

    // The ack for seq 3 promotes it, and only then does a still row cost
    // nothing.
    const uint64_t settled = lane.run({ 12, 20, 4, 3, 0b00 });
    NETW_CHECK_EQ(settled, 0b00);
    CHECK(lane.baselines.has_baseline(PEER));
}

TEST_CASE(
    "[Networked][Wire][Hosted] a peer that leaves the lane heals whole when it "
    "returns"
) {
    Lane lane;
    lane.run({ 10, 20, 1, -1, 0b11 });
    lane.run({ 10, 20, 2, 1, 0b00 });
    REQUIRE(lane.baselines.has_baseline(PEER));

    LocalVector<int> nobody;
    lane.baselines.retain(nobody);
    CHECK_FALSE(lane.baselines.has_baseline(PEER));

    // Back on the route with the same values it was last confirmed to hold. A
    // book that kept the baseline would send nothing and strand the peer.
    const uint64_t healed = lane.run({ 10, 20, 3, -1, 0b11 });
    NETW_CHECK_EQ(healed, 0b11);
}

} // namespace TestNetwWireSendReplay

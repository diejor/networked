#include "support/netw_test.h"

#include "support/send_drive.h"

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace TestStoreSendPass {

using netw::SchemaCore;
using netw::repl::RowSend;
using netw::repl::SessionResult;
using netw::repl::SnapshotHeader;
using netw::table::SchemaRecord;
using netw::wire::StreamLane;
using netw::wire::WireRegistry;
using netw_test::RowOffer;
using netw_test::SessionSend;

constexpr uint8_t CHANNEL = 49;
constexpr int PEER = 7;
constexpr int64_t WIDE = int64_t(1) << 22;

const WireRegistry &registry() {
    static const WireRegistry made = [] {
        WireRegistry out;
        netw::wire::ChannelDecl decl;
        decl.id = CHANNEL;
        decl.name = StringName("send_pass_probe");
        decl.delivery = netw::wire::Delivery::FITTED;
        out.register_channel(decl);
        return out;
    }();
    return made;
}

const SchemaRecord &wide_row() {
    static const SchemaRecord record = [] {
        SchemaRecord made;
        made.name = StringName("WideRow");
        for (int at = 0; at < 6; ++at) {
            SchemaCore::append_column(
                &made,
                StringName(String::num_int64(at)),
                SchemaCore::I32,
                1
            );
        }
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

const SchemaRecord &narrow_row() {
    static const SchemaRecord record = [] {
        SchemaRecord made;
        made.name = StringName("NarrowRow");
        SchemaCore::append_column(&made, StringName("x"), SchemaCore::I16, 1);
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

RowOffer wide(int64_t p_route, int64_t p_base, float p_weight) {
    RowOffer out;
    out.route = p_route;
    out.channel = CHANNEL;
    out.schema = &wide_row();
    Array values;
    for (int at = 0; at < 6; ++at) {
        values.push_back(p_base * 100003 + at * 7919);
    }
    netw_test::offer_values(out, values);
    out.recipients.push_back(PEER);
    out.priority = p_weight;
    return out;
}

RowOffer narrow(int64_t p_route, int64_t p_value, int64_t p_heartbeat = 0) {
    RowOffer out;
    out.route = p_route;
    out.channel = CHANNEL;
    out.schema = &narrow_row();
    netw_test::offer_value(out, p_value);
    out.recipients.push_back(PEER);
    out.heartbeat = p_heartbeat;
    return out;
}

StreamLane lane(int64_t p_route) {
    StreamLane made;
    made.route = p_route;
    made.ordinal = 0;
    made.family = netw::wire::StreamFamily::VOLATILE;
    return made;
}

const RowSend *sent_to(const SessionResult &p_result, int64_t p_route) {
    for (uint32_t at = 0; at < p_result.sends.size(); ++at) {
        if (p_result.sends[at].route == p_route) {
            return &p_result.sends[at];
        }
    }
    return nullptr;
}

LocalVector<RowOffer> pair(int64_t p_base) {
    LocalVector<RowOffer> out;
    out.push_back(wide(1, p_base, 10.0f));
    out.push_back(narrow(2, p_base));
    return out;
}

void confirm_pair(SessionSend &p_session) {
    const LocalVector<RowOffer> first = pair(1);
    netw_test::seat_streams(p_session, first);
    p_session.run(registry(), first, WIDE, 1);
    netw_test::accept_streams(p_session, first, PEER);
}

uint64_t next_revision(const SessionSend &p_session, int64_t p_route) {
    const netw::store::World &world = p_session.world;
    const netw::store::RowAddress at = world.lane_row(lane(p_route));
    return world.table(at.table).peers[world.peer_of(PEER)].next_rev[at.row];
}

TEST_CASE(
    "[Networked][Store][Hosted] SP1 a frame that does not fit mints nothing, "
    "so the row keeps its next revision and in-flight ring and is later sent "
    "one step past its confirmed revision"
) {
    SessionSend probe;
    confirm_pair(probe);
    const SessionResult measured = probe.run(registry(), pair(2), WIDE, 2);
    const RowSend *wide_probe = sent_to(measured, 1);
    const RowSend *narrow_probe = sent_to(measured, 2);
    const bool both_measured = wide_probe != nullptr && narrow_probe != nullptr
        && wide_probe->framed > narrow_probe->framed + 8;
    CHECK(both_measured);
    if (!both_measured) {
        return;
    }

    SessionSend session;
    confirm_pair(session);
    const uint32_t peer = session.world.peer_of(PEER);
    const uint64_t next_before = next_revision(session, 1);
    const uint32_t flights_before = session.world.flight_count(peer);

    const SessionResult tight
        = session.run(registry(), pair(2), (narrow_probe->framed + 2) * 8, 2);
    const bool only_the_narrow_row_went
        = tight.sends.size() == 1 && sent_to(tight, 2) != nullptr;
    CHECK(only_the_narrow_row_went);
    NETW_CHECK_EQ(next_revision(session, 1), next_before);
    NETW_CHECK_EQ(
        netw_test::row_exposed(session.world, lane(1), PEER),
        uint64_t(1)
    );
    NETW_CHECK_EQ(session.world.flight_count(peer), flights_before + 1);

    const SessionResult roomy = session.run(registry(), pair(2), WIDE, 3);
    const RowSend *late = sent_to(roomy, 1);
    const bool the_wide_row_went = late != nullptr;
    CHECK(the_wide_row_went);
    if (late == nullptr) {
        return;
    }
    NETW_CHECK_EQ(late->revision, next_before);
    SnapshotHeader named;
    const bool read = netw::repl::name_snapshot_row(late->bytes, named);
    CHECK(read);
    NETW_CHECK_EQ(named.distance, uint64_t(1));
}

TEST_CASE(
    "[Networked][Store][Hosted] SP2 a stream due for repair waits for budget "
    "without re-arming its timer, and goes on the first pass that can carry it"
) {
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(narrow(3, 5));
    netw_test::seat_streams(session, offers);
    const SessionResult first = session.run(registry(), offers, WIDE, 1, 0);
    NETW_REQUIRE_EQ(first.sends.size(), 1);
    const SessionResult rested = session.run(registry(), offers, WIDE, 2, 0);
    NETW_REQUIRE_EQ(rested.sends.size(), 1);

    const SessionResult starved = session.run(registry(), offers, 1, 3, 300);
    NETW_CHECK_EQ(starved.sends.size(), 0);
    NETW_CHECK_EQ(starved.deferred, 1);

    const SessionResult carried = session.run(registry(), offers, WIDE, 4, 310);
    const RowSend *repair = sent_to(carried, 3);
    const bool the_repair_went = repair != nullptr;
    CHECK(the_repair_went);
    if (repair == nullptr) {
        return;
    }
    const bool it_is_a_repair = repair->form == netw::repl::RowForm::REPAIR;
    CHECK(it_is_a_repair);
    const bool absolute = !repair->masked;
    CHECK(absolute);
}

TEST_CASE(
    "[Networked][Store][Hosted] SP3 at 900 idle rows and three peers a steady "
    "pass touches only the rows that are owed"
) {
    constexpr int ROWS = 900;
    constexpr int PEERS = 3;
    SessionSend session;
    LocalVector<RowOffer> offers;
    for (int row = 0; row < ROWS; ++row) {
        RowOffer made = narrow(10 + row, row);
        made.recipients.clear();
        for (int peer = 0; peer < PEERS; ++peer) {
            made.recipients.push_back(PEER + peer);
        }
        offers.push_back(made);
    }
    netw_test::seat_streams(session, offers);
    const SessionResult opened = session.run(registry(), offers, WIDE, 1);
    NETW_REQUIRE_EQ(opened.sends.size(), uint32_t(ROWS * PEERS));
    const SessionResult rested = session.run(registry(), WIDE, 2, 0);
    NETW_REQUIRE_EQ(rested.sends.size(), uint32_t(ROWS * PEERS));
    for (int peer = 0; peer < PEERS; ++peer) {
        netw_test::accept_streams(session, offers, PEER + peer);
    }

    session.rows_touched = 0;
    for (int64_t tick = 3; tick < 12; ++tick) {
        session.run(registry(), WIDE, tick, 0);
    }
    NETW_CHECK_EQ(session.rows_touched, int64_t(0));

    constexpr int MOVED = 5;
    for (int row = 0; row < MOVED; ++row) {
        netw::store::World &world = session.world;
        const netw::store::RowAddress at = world.lane_row(lane(10 + row));
        netw::store::Table &table = world.table(at.table);
        netw::wire::CodeRow changed;
        changed.assign(table.plan, table.codes_of(at.row), nullptr);
        changed.write(table.plan.column(0), 0, 999 + uint64_t(row));
        table.settle(at.row, changed, 12);
        world.note_changed(at);
    }
    session.rows_touched = 0;
    const SessionResult moved = session.run(registry(), WIDE, 12, 0);
    NETW_CHECK_EQ(moved.sends.size(), uint32_t(MOVED * PEERS));
    NETW_CHECK_EQ(session.rows_touched, int64_t(MOVED * PEERS));
}

TEST_CASE(
    "[Networked][Store][Hosted] RF1 a heartbeat on a confirmed row is a delta "
    "with an empty mask against the confirmed revision, smaller than the "
    "absolute frame"
) {
    constexpr int64_t BEAT = 3;
    SessionSend session;
    LocalVector<RowOffer> offers;
    offers.push_back(wide(1, 1, 1.0f));
    offers[0].heartbeat = BEAT;
    netw_test::seat_streams(session, offers);
    const SessionResult first = session.run(registry(), offers, WIDE, 1);
    NETW_REQUIRE_EQ(first.sends.size(), 1);
    netw_test::accept_streams(session, offers, PEER);
    const uint64_t confirmed
        = netw_test::row_acked(session.world, lane(1), PEER);
    NETW_REQUIRE_EQ(confirmed, first.sends[0].revision);

    const RowSend *beat = nullptr;
    SessionResult result;
    for (int64_t tick = 2; tick <= 2 + BEAT && beat == nullptr; ++tick) {
        result = session.run(registry(), offers, WIDE, tick);
        beat = sent_to(result, 1);
    }
    const bool a_beat_went = beat != nullptr;
    CHECK(a_beat_went);
    if (beat == nullptr) {
        return;
    }
    const bool refresh = beat->form == netw::repl::RowForm::REFRESH;
    CHECK(refresh);
    CHECK(beat->masked);
    NETW_CHECK_EQ(beat->mask, uint64_t(0));
    NETW_CHECK_LT(beat->framed, first.sends[0].framed);
    SnapshotHeader named;
    const bool read = netw::repl::name_snapshot_row(beat->bytes, named);
    CHECK(read);
    NETW_CHECK_EQ(named.revision, beat->revision);
    NETW_CHECK_EQ(named.distance, beat->revision - confirmed);
}

TEST_CASE(
    "[Networked][Store][Hosted] RF2 a heartbeat on a heavy resting row never "
    "takes the budget a moving row needs"
) {
    constexpr int MOVING = 8;
    constexpr int RESTING = 4;
    const auto frame = [](int64_t p_tick) {
        LocalVector<RowOffer> out;
        for (int at = 0; at < MOVING; ++at) {
            out.push_back(narrow(100 + at, p_tick * 10 + at));
        }
        for (int at = 0; at < RESTING; ++at) {
            RowOffer beat = narrow(200 + at, at, 2);
            beat.priority = 500.0f;
            out.push_back(beat);
        }
        return out;
    };
    SessionSend session;
    netw_test::seat_streams(session, frame(1));
    session.run(registry(), frame(1), WIDE, 1);
    netw_test::accept_streams(session, frame(1), PEER);
    const SessionResult measured = session.run(registry(), frame(2), WIDE, 2);
    netw_test::accept_streams(session, frame(2), PEER);
    int64_t largest = 0;
    for (int at = 0; at < MOVING; ++at) {
        const RowSend *moved = sent_to(measured, 100 + at);
        const bool measured_one = moved != nullptr;
        CHECK(measured_one);
        if (moved == nullptr) {
            return;
        }
        largest = moved->framed > largest ? moved->framed : largest;
    }
    session.run(registry(), frame(3), 8, 3);
    netw_test::accept_streams(session, frame(3), PEER);

    const int64_t fits = largest * MOVING * 8;
    int64_t short_ticks = 0;
    for (int64_t tick = 4; tick < 40; ++tick) {
        const LocalVector<RowOffer> rows = frame(tick);
        const SessionResult result = session.run(registry(), rows, fits, tick);
        int moved = 0;
        for (int at = 0; at < MOVING; ++at) {
            moved += sent_to(result, 100 + at) != nullptr ? 1 : 0;
        }
        short_ticks += moved < MOVING ? 1 : 0;
        netw_test::accept_streams(session, rows, PEER);
    }
    NETW_CHECK_EQ(short_ticks, int64_t(0));
}

LocalVector<RowOffer> watched(int64_t p_value, int64_t p_tick) {
    LocalVector<RowOffer> out;
    RowOffer made = narrow(5, p_value);
    made.recipients.push_back(PEER + 1);
    made.tick = p_tick;
    out.push_back(made);
    return out;
}

TEST_CASE(
    "[Networked][Store][Hosted] RC1 an author that stops sends exactly one "
    "rest edge per peer, an empty mask stepping from the newest row it sent "
    "and stamped one tick after its last change, and none while it moves"
) {
    constexpr int64_t STOP = 6;
    constexpr int64_t EDGE_BYTES = 6;
    constexpr int64_t EDGE_FRAMED = 10;
    SessionSend session;
    netw_test::seat_streams(session, watched(1, 1));
    HashMap<int, uint64_t> last_sent;
    int64_t moving_edges = 0;
    for (int64_t tick = 1; tick <= STOP; ++tick) {
        const SessionResult moved
            = session.run(registry(), watched(tick, tick), WIDE, tick);
        for (const RowSend &send : moved.sends) {
            moving_edges += send.form == netw::repl::RowForm::REFRESH ? 1 : 0;
            last_sent[send.peer] = send.revision;
        }
    }
    NETW_CHECK_EQ(moving_edges, int64_t(0));
    NETW_REQUIRE_EQ(int64_t(last_sent.size()), int64_t(2));

    LocalVector<RowSend> edges;
    LocalVector<int64_t> edge_ticks;
    for (int64_t tick = STOP + 1; tick <= STOP + 12; ++tick) {
        const SessionResult rested
            = session.run(registry(), watched(STOP, tick), WIDE, tick);
        for (const RowSend &send : rested.sends) {
            edges.push_back(send);
            edge_ticks.push_back(tick);
        }
    }
    NETW_REQUIRE_EQ(int64_t(edges.size()), int64_t(2));
    const netw::store::World &world = session.world;
    const netw::store::RowAddress at = world.lane_row(lane(5));
    const netw::store::Table &table = world.table(at.table);
    for (uint32_t index = 0; index < edges.size(); ++index) {
        const RowSend &edge = edges[index];
        const bool refresh = edge.form == netw::repl::RowForm::REFRESH;
        CHECK(refresh);
        CHECK(edge.masked);
        NETW_CHECK_EQ(edge.mask, uint64_t(0));
        NETW_CHECK_EQ(edge_ticks[index], STOP + 1);
        NETW_CHECK_EQ(int64_t(edge.bytes.size()), EDGE_BYTES);
        NETW_CHECK_EQ(edge.framed, EDGE_FRAMED);
        SnapshotHeader named;
        REQUIRE(netw::repl::name_snapshot_row(edge.bytes, named));
        NETW_CHECK_EQ(named.baseline_revision(), last_sent[edge.peer]);
        const netw::store::Flight *stepped = world.flight_of(
            world.peer_of(edge.peer),
            edge.token,
            named.baseline_revision()
        );
        REQUIRE(stepped != nullptr);
        SnapshotHeader read;
        netw::wire::CodeRow landed;
        const bool decoded = netw::repl::read_snapshot_row(
            edge.bytes,
            STOP + 1,
            table.plan,
            read,
            landed,
            &stepped->image
        );
        CHECK(decoded);
        NETW_CHECK_EQ(read.tick, STOP + 1);
    }
}

} // namespace TestStoreSendPass

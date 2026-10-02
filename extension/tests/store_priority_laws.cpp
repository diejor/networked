#include "support/netw_test.h"

#include "support/send_drive.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/handle_ledger.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/store/array.hpp"
#include "netw/store/schedule.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace TestStorePriority {

using netw::SchemaCore;
using netw::repl::SessionResult;
using netw::table::SchemaRecord;
using netw::wire::WireRegistry;
using netw_test::RowOffer;
using netw_test::SessionSend;

constexpr uint8_t CHANNEL = 51;
constexpr int PEER = 7;
constexpr int64_t WIDE = int64_t(1) << 22;

const WireRegistry &registry() {
    static const WireRegistry made = [] {
        WireRegistry out;
        netw::wire::ChannelDecl decl;
        decl.id = CHANNEL;
        decl.name = StringName("priority_probe");
        decl.delivery = netw::wire::Delivery::FITTED;
        out.register_channel(decl);
        return out;
    }();
    return made;
}

const SchemaRecord &row_schema() {
    static const SchemaRecord record = [] {
        SchemaRecord made;
        made.name = StringName("PriorityRow");
        SchemaCore::append_column(&made, StringName("x"), SchemaCore::I32, 1);
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

RowOffer row(int64_t p_route, int64_t p_value, uint8_t p_marks = 0) {
    RowOffer out;
    out.route = p_route;
    out.channel = CHANNEL;
    out.schema = &row_schema();
    netw_test::offer_value(out, p_value);
    out.recipients.push_back(PEER);
    out.marks = p_marks;
    return out;
}

bool sent(const SessionResult &p_result, int64_t p_route) {
    for (uint32_t at = 0; at < p_result.sends.size(); ++at) {
        if (p_result.sends[at].route == p_route) {
            return true;
        }
    }
    return false;
}

int64_t frame_bytes(const LocalVector<RowOffer> &p_rows) {
    SessionSend probe;
    netw_test::seat_streams(probe, p_rows);
    const SessionResult sent = probe.run(registry(), p_rows, WIDE, 0);
    int64_t most = 0;
    for (uint32_t at = 0; at < sent.sends.size(); ++at) {
        most = sent.sends[at].framed > most ? sent.sends[at].framed : most;
    }
    return most;
}

TEST_CASE(
    "[Networked][Store][Hosted] PR1 a player's row is never deferred in a "
    "tick that sends any resting row"
) {
    constexpr int BEATS = 30;
    SessionSend session;
    LocalVector<RowOffer> rows;
    rows.push_back(row(1, 0, netw::store::MARK_PLAYER));
    for (int at = 0; at < BEATS; ++at) {
        RowOffer beat = row(100 + at, at);
        beat.heartbeat = 3;
        rows.push_back(beat);
    }
    netw_test::seat_streams(session, rows);
    session.run(registry(), rows, WIDE, 1);
    netw_test::accept_streams(session, rows, PEER);
    const int64_t fill = frame_bytes(rows) * 4;

    int64_t resting_ticks = 0;
    int64_t player_skipped = 0;
    for (int64_t tick = 2; tick < 80; ++tick) {
        rows[0] = row(1, tick, netw::store::MARK_PLAYER);
        const SessionResult result
            = session.run(registry(), rows, fill * 8, tick);
        bool resting = false;
        for (int at = 0; at < BEATS; ++at) {
            resting = resting || sent(result, 100 + at);
        }
        resting_ticks += resting ? 1 : 0;
        if (resting && !sent(result, 1)) {
            player_skipped += 1;
        }
        netw_test::accept_streams(session, rows, PEER);
    }
    NETW_CHECK_GT(resting_ticks, int64_t(10));
    NETW_CHECK_EQ(player_skipped, int64_t(0));
}

TEST_CASE(
    "[Networked][Store][Hosted] PR2 a row whose claim is in flight wins the "
    "budget over moving rows"
) {
    constexpr int MOVERS = 20;
    SessionSend session;
    const auto frame = [](int64_t p_tick) {
        LocalVector<RowOffer> out;
        out.push_back(row(1, p_tick, netw::store::MARK_INTERACTING));
        for (int at = 0; at < MOVERS; ++at) {
            out.push_back(row(100 + at, p_tick * 1000 + at));
        }
        return out;
    };
    netw_test::seat_streams(session, frame(0));
    const int64_t fill = frame_bytes(frame(1)) * 4;
    int64_t claimed = 0;
    for (int64_t tick = 2; tick < 42; ++tick) {
        const SessionResult result
            = session.run(registry(), frame(tick), fill * 8, tick);
        claimed += sent(result, 1) ? 1 : 0;
    }
    NETW_CHECK_EQ(claimed, int64_t(40));
}

TEST_CASE(
    "[Networked][Store][Hosted] PR3 a resting row starved by moving rows is "
    "sent within the ticks its waiting weight needs to outrank them"
) {
    constexpr int MOVERS = 10;
    constexpr int PER_TICK = 2;
    SessionSend session;
    const auto frame = [](int64_t p_tick, bool p_with_resting) {
        LocalVector<RowOffer> out;
        for (int at = 0; at < MOVERS; ++at) {
            out.push_back(row(100 + at, p_tick * 1000 + at));
        }
        if (p_with_resting) {
            out.push_back(row(1, 77));
        }
        return out;
    };
    netw_test::seat_streams(session, frame(0, false));
    const int64_t fill = frame_bytes(frame(1, false)) * PER_TICK;
    session.run(registry(), frame(2, true), 0, 2);
    netw_test::seat_streams(session, frame(3, true));

    const int64_t bound = int64_t(
        netw::store::class_weight(netw::store::RowClass::MOVING) * MOVERS
            / PER_TICK
        + 2
    );
    int64_t waited = -1;
    for (int64_t tick = 20; tick < 20 + bound + 40; ++tick) {
        const SessionResult result
            = session.run(registry(), frame(tick, true), fill * 8, tick);
        if (sent(result, 1)) {
            waited = tick - 20;
            break;
        }
    }
    const bool it_went = waited >= 0;
    CHECK(it_went);
    NETW_CHECK_LE(waited, bound);
}

bool nodeless_rows_sent(const SessionResult &p_result) {
    for (uint32_t at = 0; at < p_result.tables.size(); ++at) {
        netw::store::ArrayFrameHead head;
        if (netw::store::Arrays::peek_header(p_result.tables[at].bytes, head)
            && head.rows > 0) {
            return true;
        }
    }
    return false;
}

void show_nodeless(SessionSend &r_session, uint32_t p_slot, bool p_visible) {
    netw::store::World &world = r_session.world;
    const uint32_t peer = world.peer_of(PEER);
    world.set_live(p_slot, true);
    world.set_reachable(peer, true);
    world.see(p_slot, peer, p_visible);
}

TEST_CASE(
    "[Networked][Store][Hosted] PR4 a nodeless row at rest waits behind "
    "moving entity rows under a constrained account, and is sent within the "
    "ticks its waiting weight needs to outrank them"
) {
    constexpr int MOVERS = 10;
    constexpr int PER_TICK = 2;
    SessionSend session;
    const auto frame = [](int64_t p_tick) {
        LocalVector<RowOffer> out;
        for (int at = 0; at < MOVERS; ++at) {
            out.push_back(row(100 + at, p_tick * 1000 + at));
        }
        return out;
    };
    netw_test::seat_streams(session, frame(0));
    const int64_t fill = frame_bytes(frame(1)) * PER_TICK;

    netw::NetwHandleLedger ledger;
    const RID table = ledger.rid_create();
    netw::store::Arrays &arrays = session.world.arrays;
    arrays.declare(table, &row_schema());
    PackedInt64Array routes;
    routes.push_back(900);
    PackedInt32Array values;
    values.push_back(5);
    arrays.write_routes(table, routes);
    arrays.write_column(table, 0, values);
    arrays.commit(table, 2);
    arrays.clear_dirty(table);
    show_nodeless(session, arrays.table_at(0).slots[0], true);

    const int64_t bound = int64_t(
        netw::store::class_weight(netw::store::RowClass::MOVING) * MOVERS
            / PER_TICK
        + 2
    );
    int64_t waited = -1;
    for (int64_t tick = 20; tick < 20 + bound + 40; ++tick) {
        const SessionResult result
            = session.run(registry(), frame(tick), fill * 8, tick);
        if (nodeless_rows_sent(result)) {
            waited = tick - 20;
            break;
        }
    }
    NETW_CHECK_GT(waited, int64_t(0));
    NETW_CHECK_LE(waited, bound);
    NETW_CHECK_EQ(int(arrays.owes(table, PEER, 900)), 0);
}

TEST_CASE(
    "[Networked][Store][Hosted] PR5 a nodeless row goes only to a peer its "
    "slot is visible to, and stays owed to a peer that cannot see it"
) {
    SessionSend session;
    LocalVector<RowOffer> rows;
    rows.push_back(row(1, 0));
    netw_test::seat_streams(session, rows);

    netw::NetwHandleLedger ledger;
    const RID table = ledger.rid_create();
    netw::store::Arrays &arrays = session.world.arrays;
    arrays.declare(table, &row_schema());
    PackedInt64Array routes;
    routes.push_back(900);
    PackedInt32Array values;
    values.push_back(5);
    arrays.write_routes(table, routes);
    arrays.write_column(table, 0, values);
    arrays.commit(table, 2);
    const uint32_t slot = arrays.table_at(0).slots[0];
    show_nodeless(session, slot, false);

    bool leaked = false;
    for (int64_t tick = 3; tick < 8; ++tick) {
        leaked = leaked
            || nodeless_rows_sent(session.run(registry(), rows, WIDE, tick));
    }
    NETW_CHECK_EQ(int(leaked), 0);
    NETW_CHECK_EQ(int(arrays.owes(table, PEER, 900)), 1);

    show_nodeless(session, slot, true);
    const SessionResult shown = session.run(registry(), rows, WIDE, 8);
    NETW_CHECK_EQ(int(nodeless_rows_sent(shown)), 1);
    NETW_CHECK_EQ(int(arrays.owes(table, PEER, 900)), 0);
}

} // namespace TestStorePriority

#include "support/netw_test.h"

#include <cstdint>
#include <string>

#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/store/world.hpp"

using namespace godot;

namespace TestNetwStorePeers {

using netw::SchemaCore;
using netw::store::Family;
using netw::store::PeerRows;
using netw::store::RowAddress;
using netw::store::Source;
using netw::store::Stream;
using netw::store::Table;
using netw::store::World;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;
using netw::wire::StreamFamily;
using netw::wire::StreamLane;

constexpr int PEER = 7;

SchemaRecord position_schema() {
    SchemaRecord record;
    SchemaColumn where;
    where.key = StringName("position");
    where.type = SchemaCore::VECTOR2;
    Ref<netw::NetwQuantizeScalar> grid;
    grid.instantiate();
    grid->set_bit_count(12);
    grid->set_min_limit(-8.0);
    grid->set_max_limit(8.0);
    where.quantizer = grid;
    record.columns.push_back(where);
    SchemaCore::fix(&record);
    return record;
}

SchemaRecord labelled_schema() {
    SchemaRecord record;
    record.columns.push_back(position_schema().columns[0]);
    SchemaColumn label;
    label.key = StringName("label");
    label.type = SchemaCore::STRING;
    record.columns.push_back(label);
    SchemaCore::fix(&record);
    return record;
}

StreamLane lane_of(int64_t p_route) {
    StreamLane lane;
    lane.route = p_route;
    lane.ordinal = 0;
    lane.family = StreamFamily::VOLATILE;
    return lane;
}

RowAddress named_row(
    World &p_world,
    uint32_t p_table,
    uint64_t p_owner,
    int64_t p_route,
    int64_t p_epoch
) {
    const RowAddress at = p_world.add_row(p_table, p_owner, 0, p_owner, 0);
    p_world.place(at, p_route, p_epoch);
    p_world.name_lane(at, 0);
    p_world.set_source(at, Source::LOCAL);
    return at;
}

void settle(World &p_world, const RowAddress &p_at, const Vector2 &p_where) {
    Table &table = p_world.table(p_at.table);
    table.write_value(p_at.row, 0, p_where);
    table.scratch.clear();
    table.encode(0, p_where, table.scratch);
    if (table.settle(p_at.row, table.scratch, 1)) {
        p_world.note_changed(p_at);
    }
}

void settle_labelled(
    World &p_world,
    const RowAddress &p_at,
    const Vector2 &p_where,
    const String &p_label
) {
    Table &table = p_world.table(p_at.table);
    table.write_value(p_at.row, 0, p_where);
    table.scratch.clear();
    table.encode(0, p_where, table.scratch);
    table.encode(1, p_label, table.scratch);
    if (table.settle(p_at.row, table.scratch, 1)) {
        p_world.note_changed(p_at);
    }
}

netw::wire::CodeRow image_of(const Table &p_table, uint32_t p_row) {
    netw::wire::CodeRow image;
    image.assign(
        p_table.plan,
        p_table.codes_of(p_row),
        p_table.carried_of(p_row)
    );
    return image;
}

void seat(World &p_world, int64_t p_route, uint64_t p_token) {
    const uint64_t request = p_world.stream_open(
        PEER,
        lane_of(p_route),
        netw::wire::StreamTenure()
    );
    p_world.stream_ready(PEER, request, p_token);
}

void expose(World &p_world, const RowAddress &p_at, uint64_t p_revision) {
    const uint32_t peer = p_world.peer_of(PEER);
    const netw::wire::CodeRow image
        = image_of(p_world.table(p_at.table), p_at.row);
    while (p_world.table(p_at.table).peers[peer].next_rev[p_at.row]
           <= p_revision) {
        if (p_world.mint(p_at, peer, image, false) == 0) {
            return;
        }
    }
}

void confirm(World &p_world, const RowAddress &p_at, uint64_t p_revision) {
    const Table &table = p_world.table(p_at.table);
    const uint32_t peer = p_world.peer_of(PEER);
    const uint64_t token = table.peers[peer].token[p_at.row];
    expose(p_world, p_at, p_revision);
    p_world.stream_receipt(PEER, token, p_revision);
}

TEST_CASE(
    "[Networked][Store][Hosted] PA1 a row created after a peer joined is "
    "owed to that peer with no stream, in every table that peer can reach"
) {
    World world;
    const uint32_t first
        = world.table_for(1, Family::VOLATILE, position_schema());
    const uint32_t peer = world.peer_join(PEER);
    const uint32_t second
        = world.table_for(2, Family::VOLATILE, position_schema());
    const uint32_t tables[2] = {first, second};
    for (uint32_t which = 0; which < 2; ++which) {
        const RowAddress at = named_row(
            world,
            tables[which],
            10 + which,
            5 + int64_t(which),
            1
        );
        const PeerRows &held = world.table(at.table).peers[peer];
        CHECK(held.owed.get(at.row));
        NETW_CHECK_EQ(held.stream[at.row], int(Stream::NONE));
        NETW_CHECK_EQ(held.token[at.row], 0);
        NETW_CHECK_EQ(held.acked_rev[at.row], 0);
        CHECK_FALSE(held.admitted.get(at.row));
    }
}

TEST_CASE(
    "[Networked][Store][Hosted] PA2 a swap-remove moves every per-peer array "
    "of the last row into the hole, and its stream still resolves to it"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, position_schema());
    const uint32_t peer = world.peer_join(PEER);
    for (int64_t seed = 1; seed <= 3; ++seed) {
        const RowAddress at
            = named_row(world, table, uint64_t(seed), seed * 10, 1);
        settle(world, at, Vector2(double(seed), 0.5));
        world.admit(at, peer, true);
        seat(world, seed * 10, uint64_t(100 + seed));
        confirm(world, at, uint64_t(seed));
        PeerRows &written = world.table(table).peers[peer];
        written.last_sent[at.row] = seed;
        written.repair_at[at.row] = seed * 1000;
        written.acc[at.row] = float(seed);
    }
    Table &held = world.table(table);
    settle(world, {table, 2}, Vector2(-3.0, 0.5));
    const PeerRows &rows = held.peers[peer];
    const bool owed = rows.owed.get(2);
    const uint64_t token = rows.token[2];
    const uint64_t next = rows.next_rev[2];
    const uint64_t acked = rows.acked_rev[2];
    const uint64_t exposed = rows.exposed_rev[2];
    const float accrued = rows.acc[2];
    const int64_t sent = rows.last_sent[2];
    const int64_t repaired = rows.repair_at[2];
    uint64_t baseline[8] = {};
    for (uint32_t word = 0; word < held.words && word < 8; ++word) {
        baseline[word] = rows.baseline[2 * held.words + word];
    }

    world.remove_row(world.row_of(1, 0));

    NETW_CHECK_EQ(held.row_count(), 2u);
    CHECK(owed);
    CHECK(rows.owed.get(0) == owed);
    CHECK(rows.admitted.get(0));
    CHECK(rows.sendable.get(0));
    NETW_CHECK_EQ(rows.stream[0], int(Stream::READY));
    NETW_CHECK_EQ(rows.token[0], token);
    NETW_CHECK_EQ(rows.next_rev[0], next);
    NETW_CHECK_EQ(rows.acked_rev[0], acked);
    NETW_CHECK_EQ(rows.exposed_rev[0], exposed);
    CHECK(rows.acc[0] == accrued);
    NETW_CHECK_EQ(rows.last_sent[0], sent);
    NETW_CHECK_EQ(rows.repair_at[0], repaired);
    for (uint32_t word = 0; word < held.words && word < 8; ++word) {
        NETW_CHECK_EQ(rows.baseline[word], baseline[word]);
    }
    const RowAddress followed = world.lane_row(lane_of(30));
    NETW_CHECK_EQ(followed.table, table);
    NETW_CHECK_EQ(followed.row, 0u);
    CHECK_FALSE(world.lane_row(lane_of(10)).valid());
}

TEST_CASE(
    "[Networked][Store][Hosted] PA3 a pull that changes a confirmed row owes "
    "it again to every peer"
) {
    Node2D *node = memnew(Node2D);
    node->set_position(Vector2(1.0, 1.0));
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, position_schema());
    Table &held = world.table(table);
    held.resolve_ports(node);
    const RowAddress at
        = world.add_row(table, 10, 0, uint64_t(node->get_instance_id()), 0);
    world.place(at, 5, 1);
    world.name_lane(at, 0);
    world.set_source(at, Source::LOCAL);
    const uint32_t peer = world.peer_join(PEER);
    world.admit(at, peer, true);
    world.pull(table, 1, false);
    seat(world, 5, 100);
    confirm(world, at, 1);
    const bool settled = !held.peers[peer].owed.get(at.row);
    CHECK(settled);

    node->set_position(Vector2(3.0, 1.0));
    world.pull(table, 2, false);

    CHECK(held.peers[peer].owed.get(at.row));
    memdelete(node);
}

TEST_CASE(
    "[Networked][Store][Hosted] PA4 an acknowledgement promotes the image it "
    "names, and one naming an evicted revision is counted and leaves the row "
    "owed"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, position_schema());
    const uint32_t peer = world.peer_join(PEER);
    const RowAddress at = named_row(world, table, 10, 5, 1);
    settle(world, at, Vector2(1.0, 1.0));
    world.admit(at, peer, true);
    seat(world, 5, 100);
    confirm(world, at, 1);

    Table &held = world.table(table);
    const PeerRows &rows = held.peers[peer];
    NETW_CHECK_EQ(rows.acked_rev[at.row], 1);
    CHECK_FALSE(rows.owed.get(at.row));
    for (uint32_t word = 0; word < held.words; ++word) {
        NETW_CHECK_EQ(rows.baseline[word], held.codes_of(at.row)[word]);
    }

    settle(world, at, Vector2(2.0, 1.0));
    expose(world, at, 2 + netw::wire::SNAPSHOT_STAGE_DEPTH + 1);
    NETW_CHECK_EQ(world.flight_count(peer), netw::wire::SNAPSHOT_STAGE_DEPTH);
    const int64_t evicted = world.receipts.evicted;
    world.stream_receipt(PEER, 100, 2);

    NETW_CHECK_EQ(world.receipts.evicted, evicted + 1);
    NETW_CHECK_EQ(rows.acked_rev[at.row], 1);
    CHECK(rows.owed.get(at.row));
}

TEST_CASE(
    "[Networked][Store][Hosted] PA5 an interest hide clears admission and "
    "closes the peer's stream with everything it had in flight"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, position_schema());
    const uint32_t peer = world.peer_join(PEER);
    const RowAddress at = named_row(world, table, 10, 5, 1);
    settle(world, at, Vector2(1.0, 1.0));
    world.admit(at, peer, true);
    seat(world, 5, 100);
    expose(world, at, 1);
    REQUIRE(world.flight_count(peer) == 1u);

    world.stream_hide(PEER, lane_of(5));

    const PeerRows &rows = world.table(table).peers[peer];
    CHECK_FALSE(rows.admitted.get(at.row));
    CHECK_FALSE(rows.sendable.get(at.row));
    NETW_CHECK_EQ(rows.stream[at.row], int(Stream::NONE));
    NETW_CHECK_EQ(rows.token[at.row], 0);
    NETW_CHECK_EQ(rows.exposed_rev[at.row], 0);
    NETW_CHECK_EQ(world.flight_count(peer), 0u);
    CHECK(rows.owed.get(at.row));
}

TEST_CASE(
    "[Networked][Store][Hosted] PA6 a leaving peer's index is cleared at once "
    "and the next peer to join takes it"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, position_schema());
    const uint32_t peer = world.peer_join(PEER);
    world.peer_join(PEER + 1);
    const RowAddress at = named_row(world, table, 10, 5, 1);
    settle(world, at, Vector2(1.0, 1.0));
    world.admit(at, peer, true);
    seat(world, 5, 100);
    confirm(world, at, 1);
    expose(world, at, 2);

    world.peer_leave(PEER);

    const PeerRows &rows = world.table(table).peers[peer];
    NETW_CHECK_EQ(world.peer_of(PEER), Table::NO_SLOT);
    CHECK_FALSE(world.peer_joined(peer));
    NETW_CHECK_EQ(rows.stream[at.row], int(Stream::NONE));
    NETW_CHECK_EQ(rows.token[at.row], 0);
    NETW_CHECK_EQ(rows.acked_rev[at.row], 0);
    CHECK_FALSE(rows.admitted.get(at.row));
    CHECK(rows.owed.get(at.row));
    NETW_CHECK_EQ(world.flight_count(peer), 0u);
    NETW_CHECK_EQ(world.lanes_in_flight(peer), 0u);

    NETW_CHECK_EQ(world.peer_join(PEER + 2), peer);
    NETW_CHECK_EQ(world.peer_id(peer), PEER + 2);
}

TEST_CASE(
    "[Networked][Store][Hosted] PA9 the peers that leave last release their "
    "rows in every table, and the next peer to join starts from fresh rows"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, position_schema());
    const uint32_t first = world.peer_join(PEER + 1);
    const uint32_t peer = world.peer_join(PEER);
    const RowAddress at = named_row(world, table, 10, 5, 1);
    settle(world, at, Vector2(1.0, 1.0));
    world.admit(at, peer, true);
    seat(world, 5, 100);
    confirm(world, at, 1);
    expose(world, at, 2);

    world.peer_leave(PEER + 1);
    NETW_CHECK_EQ(world.peer_capacity(), 2u);
    NETW_CHECK_EQ(world.table(table).peers.size(), 2u);

    world.peer_leave(PEER);
    NETW_CHECK_EQ(world.peer_capacity(), 0u);
    NETW_CHECK_EQ(world.table(table).peers.size(), 0u);
    NETW_CHECK_EQ(world.flight_count(peer), 0u);

    NETW_CHECK_EQ(world.peer_join(PEER + 2), first);
    const PeerRows &rows = world.table(table).peers[first];
    NETW_CHECK_EQ(rows.stream[at.row], int(Stream::NONE));
    NETW_CHECK_EQ(rows.token[at.row], 0);
    NETW_CHECK_EQ(rows.acked_rev[at.row], 0);
    CHECK_FALSE(rows.admitted.get(at.row));
    CHECK(rows.owed.get(at.row));
}

TEST_CASE(
    "[Networked][Store][Hosted] PA10 an acknowledged image is kept for reuse "
    "holding no values, and clearing the world releases every kept image"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, labelled_schema());
    const uint32_t peer = world.peer_join(PEER);
    const RowAddress at = named_row(world, table, 10, 5, 1);
    settle_labelled(world, at, Vector2(1.0, 1.0), String("carried"));
    world.admit(at, peer, true);
    seat(world, 5, 100);
    expose(world, at, 2);
    const netw::wire::CodeRow sent
        = image_of(world.table(at.table), at.row);
    REQUIRE(bool(String(sent.read_value(1)) == String("carried")));
    world.stream_receipt(PEER, 100, 2);

    REQUIRE(world.spare_image_count() == 2u);
    for (uint32_t at_spare = 0; at_spare < 2; ++at_spare) {
        const netw::wire::CodeRow *kept = world.spare_image(at_spare);
        for (uint32_t column = 0; column < 2; ++column) {
            NETW_CHECK_EQ(
                int(kept->read_value(column).get_type()),
                int(Variant::NIL)
            );
        }
    }

    world.clear();
    NETW_CHECK_EQ(world.spare_image_count(), 0u);
}

TEST_CASE(
    "[Networked][Store][Hosted] PA7 a claim that flips a row's source "
    "recomputes whether each admitted peer may be sent it"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, position_schema());
    const uint32_t peer = world.peer_join(PEER);
    const uint32_t hidden = world.peer_join(PEER + 1);
    const RowAddress at = named_row(world, table, 10, 5, 1);
    world.admit(at, peer, true);
    const PeerRows &seen = world.table(table).peers[peer];
    const PeerRows &unseen = world.table(table).peers[hidden];
    CHECK(seen.sendable.get(at.row));
    CHECK_FALSE(unseen.sendable.get(at.row));

    world.set_source(at, Source::REMOTE);
    CHECK_FALSE(seen.sendable.get(at.row));
    CHECK(seen.admitted.get(at.row));

    world.set_source(at, Source::LOCAL);
    CHECK(seen.sendable.get(at.row));
    CHECK_FALSE(unseen.sendable.get(at.row));
}

TEST_CASE(
    "[Networked][Store][Hosted] PA8 an acknowledgement for a previous life "
    "of the slot is counted and never reaches the new life"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, position_schema());
    const uint32_t peer = world.peer_join(PEER);
    const RowAddress old_life = named_row(world, table, 10, 5, 1);
    settle(world, old_life, Vector2(1.0, 1.0));
    world.admit(old_life, peer, true);
    seat(world, 5, 100);
    expose(world, old_life, 1);
    const uint32_t slot = world.table(table).slots[old_life.row];

    world.remove_row(world.row_of(10, 0));
    const RowAddress new_life = named_row(world, table, 11, 5, 2);
    NETW_CHECK_EQ(world.table(table).slots[new_life.row], slot);
    settle(world, new_life, Vector2(-2.0, 1.0));
    world.admit(new_life, peer, true);
    seat(world, 5, 200);
    expose(world, new_life, 1);
    const int64_t refused = world.receipts.stale + world.receipts.unknown;

    world.stream_receipt(PEER, 100, 1);

    NETW_CHECK_EQ(
        world.receipts.stale + world.receipts.unknown,
        refused + 1
    );
    const PeerRows &rows = world.table(table).peers[peer];
    NETW_CHECK_EQ(rows.acked_rev[new_life.row], 0);
    CHECK(rows.owed.get(new_life.row));
}

TEST_CASE(
    "[Networked][Store][Hosted] SI5 a route with no node cannot enter a "
    "layer, and its slot waits unjudged and reaches nobody"
) {
    Ref<netw::NetwMultiplayer> session;
    session.instantiate();
    const PackedInt64Array routes = session->liveness_claim_routes(1);
    REQUIRE(routes.size() == 1);
    const int64_t route = routes[0];
    World &world = session->store();
    const uint32_t slot
        = world.enter(route, session->liveness_route_epoch(route)).slot;
    const RID layer = session->interest_layer_create(StringName("sight"));
    const RID entity = session->get_liveness_core()->rid_from_route(int(route));

    const Error added = session->interest_layer_add_entity(layer, entity);
    session->store_refresh();

    CHECK(added == ERR_DOES_NOT_EXIST);
    CHECK(world.slot_stale(slot));
    CHECK_FALSE(world.is_live(slot));
    CHECK(session->store_reach(slot).is_empty());
}

} // namespace TestNetwStorePeers

#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/lagcomp_core.hpp"
#include "netw/predict/engine.hpp"
#include "netw/store/world.hpp"

using namespace godot;

namespace TestNetwStoreSlotBinding {

using netw::NetwLagCompCore;
using netw::NetwMultiplayer;
using netw::NetwPredictionEngine;
using netw::SchemaCore;
using netw::store::EntityRef;
using netw::store::Family;
using netw::store::RowAddress;
using netw::store::World;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;

SchemaRecord pose_schema() {
    SchemaRecord record;
    SchemaColumn where;
    where.key = StringName("where");
    where.type = SchemaCore::VECTOR3;
    record.columns.push_back(where);
    SchemaCore::fix(&record);
    return record;
}

EntityRef placed(
    World &p_world,
    uint32_t p_table,
    uint64_t p_owner,
    int64_t p_route,
    int64_t p_epoch,
    RowAddress &r_at
) {
    r_at = p_world.add_row(p_table, p_owner, 0, p_owner, 0);
    p_world.place(r_at, p_route, p_epoch);
    return p_world.ref_of(p_world.table(r_at.table).slots[r_at.row]);
}

TEST_CASE(
    "[Networked][Store][Hosted] B1 a route revived at the next epoch binds "
    "a new store slot, and the prediction and rewind slots bound to the "
    "previous life take nothing the new life receives"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    World &world = session->store();
    NetwPredictionEngine *pool = session->get_prediction_engine();
    NetwLagCompCore *rewind = session->get_lagcomp_core();
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, pose_schema());

    RowAddress old_row;
    const EntityRef old_life = placed(world, table, 10, 5, 1, old_row);
    const int64_t old_predict = pool->open();
    const int64_t old_rewind = rewind->timeline_open(64);
    pool->bind_store(old_predict, old_life);
    rewind->bind_store(old_rewind, old_life);
    NETW_CHECK_EQ(world.predict_of(old_life.slot), old_predict);
    NETW_CHECK_EQ(world.lagcomp_of(old_life.slot), old_rewind);
    CHECK(pool->takes_arrival(old_predict, old_life));

    world.remove_row(old_row);
    NETW_CHECK_EQ(world.predict_of(old_life.slot), int64_t(-1));
    NETW_CHECK_EQ(world.lagcomp_of(old_life.slot), int64_t(-1));

    RowAddress new_row;
    const EntityRef new_life = placed(world, table, 11, 5, 2, new_row);
    NETW_CHECK_EQ(new_life.slot, old_life.slot);
    CHECK(new_life.gen != old_life.gen);
    NETW_CHECK_EQ(world.predict_of(new_life.slot), int64_t(-1));

    const int64_t new_predict = pool->open();
    const int64_t new_rewind = rewind->timeline_open(64);
    pool->bind_store(new_predict, new_life);
    rewind->bind_store(new_rewind, new_life);
    NETW_CHECK_EQ(world.predict_of(new_life.slot), new_predict);
    NETW_CHECK_EQ(world.lagcomp_of(new_life.slot), new_rewind);
    CHECK(pool->takes_arrival(new_predict, new_life));
    CHECK_FALSE(pool->takes_arrival(old_predict, new_life));
    CHECK_FALSE(world.holds(pool->store_ref_of(old_predict)));
    CHECK_FALSE(world.holds(rewind->store_ref_of(old_rewind)));

    pool->close(old_predict);
    rewind->timeline_close(old_rewind);
    NETW_CHECK_EQ(world.predict_of(new_life.slot), new_predict);
    NETW_CHECK_EQ(world.lagcomp_of(new_life.slot), new_rewind);
}

TEST_CASE(
    "[Networked][Store][Hosted] B2 a slot entered without a row is held "
    "until it is left, and leaving it releases it"
) {
    World world;
    const EntityRef held = world.enter(7, 1);
    CHECK(world.holds(held));
    NETW_CHECK_EQ(world.live_slot_count(), 1u);
    NETW_CHECK_EQ(world.find(7, 1).slot, held.slot);
    world.leave(held);
    CHECK_FALSE(world.holds(held));
    NETW_CHECK_EQ(world.live_slot_count(), 0u);
    NETW_CHECK_EQ(world.find(7, 1).slot, EntityRef::NONE);
}

} // namespace TestNetwStoreSlotBinding

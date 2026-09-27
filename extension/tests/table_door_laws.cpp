#include "support/netw_test.h"

#include "support/netw_recorder.h"

#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/table_handle.hpp"

namespace TestNetwTableDoor {

using namespace godot;
using netw::Netw;
using netw::NetwMultiplayer;
using netw::NetwTableHandle;
using netw_test::Recorder;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    return core;
}

RID make_table(const Ref<NetwMultiplayer> &p_core, const StringName &p_name) {
    const RID schema = p_core->schema_create(p_name);
    p_core->schema_add_column(schema, "pos", NetwMultiplayer::COLUMN_F32, 1);
    p_core->schema_add_column(schema, "hp", NetwMultiplayer::COLUMN_I32, 1);
    REQUIRE(p_core->schema_seal(schema) == OK);
    return p_core->table_create(schema);
}

TEST_CASE(
    "[Networked][Table][Door][Hosted] TD1 one session mints one door per "
    "table, because a door carries a signal a game connects once"
) {
    const Ref<NetwMultiplayer> core = make_session();
    const RID table = make_table(core, "Mob");
    REQUIRE(table.is_valid());

    const Ref<NetwTableHandle> door = core->get_table_handle(table);
    REQUIRE(door.is_valid());
    CHECK(door == core->get_table_handle(table));

    const Ref<NetwMultiplayer> other = make_session();
    const RID elsewhere = make_table(other, "Mob");
    CHECK(other->get_table_handle(elsewhere) != door);
}

TEST_CASE(
    "[Networked][Table][Door][Hosted] TD2 every read on the door answers "
    "what the flat verb answers for the table it carries"
) {
    const Ref<NetwMultiplayer> core = make_session();
    const RID table = make_table(core, "Mob");
    const Ref<NetwTableHandle> door = core->get_table_handle(table);
    REQUIRE(door.is_valid());

    NETW_CHECK_EQ(int(door->get_table() == table), 1);
    CHECK(door->get_schema_name() == StringName("Mob"));
    NETW_CHECK_EQ(int(door->get_is_valid()), 1);
    NETW_CHECK_EQ(door->get_wire_hash(), core->table_get_wire_hash(table));
    NETW_CHECK_EQ(door->get_tick(), core->table_get_tick(table));
    NETW_CHECK_EQ(
        door->read_routes().size(),
        core->table_read_routes(table).size()
    );

    NETW_CHECK_EQ(int(door->get_reliable()), 0);
    door->set_reliable(true);
    NETW_CHECK_EQ(int(door->get_reliable()), 1);
    NETW_CHECK_EQ(int(core->get_table_core()->is_reliable(table)), 1);
}

TEST_CASE(
    "[Networked][Table][Door][Hosted] TD3 a wave written through the door "
    "is the wave the flat surface reads back"
) {
    const Ref<NetwMultiplayer> core = make_session();
    const RID table = make_table(core, "Mob");
    const Ref<NetwTableHandle> door = core->get_table_handle(table);
    REQUIRE(door.is_valid());

    PackedInt64Array routes;
    routes.push_back(41);
    routes.push_back(42);
    PackedFloat32Array pos;
    pos.push_back(1.5f);
    pos.push_back(2.5f);
    PackedInt32Array hp;
    hp.push_back(7);
    hp.push_back(9);

    NETW_CHECK_EQ(door->write_routes(routes), OK);
    NETW_CHECK_EQ(door->write_column(0, pos), OK);
    NETW_CHECK_EQ(door->write_column(1, hp), OK);
    NETW_CHECK_EQ(door->commit(), OK);

    NETW_CHECK_EQ(core->table_read_routes(table).size(), 2);
    NETW_CHECK_EQ(door->read_routes().size(), 2);
    NETW_CHECK_EQ(door->row_of(42), core->table_get_row(table, 42));
    NETW_CHECK_ORDER(door->row_of(42), -1, !=);

    const PackedFloat32Array read = door->read_column(0);
    NETW_CHECK_EQ(read.size(), 2);
    NETW_CHECK_ORDER(double(read[1]), 2.5, ==);
}

TEST_CASE(
    "[Networked][Table][Door][Hosted] TD4 an announced table reaches its own "
    "door carrying the committed tick, and reaches no other door"
) {
    const Ref<NetwMultiplayer> core = make_session();
    const RID table = make_table(core, "Mob");
    const RID other = make_table(core, "Rock");
    const Ref<NetwTableHandle> door = core->get_table_handle(table);
    const Ref<NetwTableHandle> bystander = core->get_table_handle(other);
    REQUIRE(door.is_valid());
    REQUIRE(bystander.is_valid());

    Vector<StringName> watched;
    watched.push_back("received");
    Recorder log(door.ptr(), watched);
    Recorder quiet(bystander.ptr(), watched);

    PackedInt64Array routes;
    routes.push_back(41);
    PackedFloat32Array pos;
    pos.push_back(1.5f);
    PackedInt32Array hp;
    hp.push_back(7);
    REQUIRE(door->write_routes(routes) == OK);
    REQUIRE(door->write_column(0, pos) == OK);
    REQUIRE(door->write_column(1, hp) == OK);
    REQUIRE(door->commit() == OK);

    core->table_announce(table);

    NETW_CHECK_EQ(log.count("received"), 1);
    NETW_CHECK_EQ(quiet.count("received"), 0);
    const Array args = log.args("received");
    REQUIRE(args.size() == 1);
    NETW_CHECK_EQ(int64_t(args[0]), core->table_get_tick(table));
}

TEST_CASE(
    "[Networked][Table][Door][Hosted] TD5 a door outliving its session "
    "answers its empty value rather than reaching through a dead pointer"
) {
    Ref<NetwTableHandle> door;
    {
        const Ref<NetwMultiplayer> core = make_session();
        const RID table = make_table(core, "Mob");
        door = core->get_table_handle(table);
        REQUIRE(door.is_valid());
    }

    NETW_CHECK_EQ(int(door->get_is_valid()), 0);
    NETW_CHECK_EQ(door->get_tick(), -1);
    NETW_CHECK_EQ(door->get_wire_hash(), 0);
    NETW_CHECK_EQ(door->row_of(41), -1);
    NETW_CHECK_EQ(door->read_routes().size(), 0);
    NETW_CHECK_EQ(door->commit(), ERR_DOES_NOT_EXIST);
    NETW_CHECK_EQ(door->write_routes(PackedInt64Array()), ERR_DOES_NOT_EXIST);
}

TEST_CASE(
    "[Networked][Table][Door][Hosted] TD6 a node no session governs opens "
    "no door, because a door resolves through the tree"
) {
    CHECK(Netw::table(nullptr, "Mob").is_null());
    CHECK(NetwTableHandle::of(nullptr, "Mob").is_null());
}

} // namespace TestNetwTableDoor

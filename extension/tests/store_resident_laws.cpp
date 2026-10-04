#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/store/world.hpp"

#if defined(NETW_TIER_HOSTED)

using namespace godot;

namespace TestNetwStoreResidentLaws {

using netw::NetwMultiplayer;
using netw::SchemaCore;
using netw::store::RowAddress;
using netw::store::Source;
using netw::store::World;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;

SchemaRecord pose_schema() {
    SchemaRecord record;
    SchemaColumn where;
    where.key = StringName("position");
    where.type = SchemaCore::VECTOR3;
    record.columns.push_back(where);
    SchemaCore::fix(&record);
    return record;
}

RigidBody3D *rooted_body(Node *p_parent) {
    RigidBody3D *body = memnew(RigidBody3D);
    body->set_gravity_scale(0.0);
    body->set_meta(netw::NetwEntityRecord::entity_meta(), true);
    p_parent->add_child(body);
    return body;
}

struct Bench {
    Ref<NetwMultiplayer> session;
    uint32_t table = 0;
    RowAddress at;

    explicit Bench(RigidBody3D *p_body) {
        session.instantiate();
        netw::gd::scene_tree()->connect(
            StringName("node_removed"),
            callable_mp(session.ptr(), &NetwMultiplayer::sync_note_node_removed)
        );
        World &world = session->store();
        world.residents.watch(true);
        table
            = world.table_for(1, netw::store::Family::VOLATILE, pose_schema());
        world.table(table).resolve_ports(p_body);
        at = world
                 .add_row(table, 10, 0, uint64_t(p_body->get_instance_id()), 0);
        world.place(at, 5, 1);
        world.set_source(at, Source::LOCAL);
    }

    World &world() const {
        return session->store();
    }

    const netw::store::NodeCell &cell() const {
        return world().table(table).cells[at.row];
    }

    bool read() const {
        LocalVector<Variant> values;
        return world().read_live(at, values);
    }

    bool cached() const {
        return cell().epoch == world().residents.current()
            && cell().object != nullptr;
    }
};

TEST_CASE(
    "[Networked][Store] a body freed inside the tree leaves its row's "
    "resolved node behind before the next pull reads it"
) {
    Node3D *parent = memnew(Node3D);
    netw::gd::scene_root()->add_child(parent);
    RigidBody3D *body = rooted_body(parent);
    Bench bench(body);
    REQUIRE(bench.world().residents.is_watching());
    REQUIRE(bench.world().table(bench.table).body_columns == 1);

    REQUIRE(bench.read());
    REQUIRE(bench.cached());
    const bool resolved_body = bench.cell().solid == body;
    CHECK(resolved_body);

    memdelete(body);
    REQUIRE_FALSE(bench.cached());
    CHECK_FALSE(bench.read());
    const bool read_nothing = bench.cell().object == nullptr;
    CHECK(read_nothing);
    memdelete(parent);
}

TEST_CASE(
    "[Networked][Store] a body moved to another parent keeps its row reading "
    "that body against its new parent"
) {
    Node3D *first = memnew(Node3D);
    Node3D *second = memnew(Node3D);
    second->set_position(Vector3(10.0, 0.0, 0.0));
    netw::gd::scene_root()->add_child(first);
    netw::gd::scene_root()->add_child(second);
    RigidBody3D *body = rooted_body(first);
    Bench bench(body);

    REQUIRE(bench.read());
    REQUIRE(bench.cached());
    const bool against_first = bench.cell().frame_3d == first;
    CHECK(against_first);

    first->remove_child(body);
    second->add_child(body);
    REQUIRE(bench.read());
    CHECK(bench.cached());
    const bool same_body = bench.cell().solid == body;
    CHECK(same_body);
    const bool against_second = bench.cell().frame_3d == second;
    CHECK(against_second);

    memdelete(body);
    memdelete(second);
    memdelete(first);
}

} // namespace TestNetwStoreResidentLaws

#endif

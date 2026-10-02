#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/api/sync_model.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/replication_send.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSyncPump {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwSyncModel;
using netw::repl::PropertyBinding;
using netw::repl::PropertyBindingPtr;
using netw::repl::PropertyColumn;
using netw::repl::PropertySet;
using netw::ReplicationSend;
using netw::SchemaCore;
using netw_test::CallLog;

const int64_t SYNC_ROW = 39;

uint64_t a_set(netw::repl::SetBook &r_sets) {
    const uint64_t declared = r_sets.create();
    PropertySet *set = r_sets.get(declared);
    PropertyColumn column = PropertyColumn::create(
        StringName("hp"),
        Ref<netw::NetwQuantize>(),
        false,
        NetwMultiplayer::ColumnType(SchemaCore::I32)
    );
    column.lane = PropertySet::VOLATILE;
    set->bind_column(column);
    return declared;
}

PropertyBindingPtr a_binding(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_node,
    int64_t p_route
) {
    netw::repl::SetBook &sets = p_core->property_set_book();
    const PropertyBindingPtr binding
        = p_core->property_binding_book().create(a_set(sets), p_node);
    binding->order_key = StringName(".");
    binding->route = p_route;
    return binding;
}

bool sends_nothing(const Ref<NetwMultiplayer> &p_core) {
    netw::store::World &world = p_core->store();
    for (uint32_t index = 0; index < world.table_count(); ++index) {
        const netw::store::Table &table = world.table(index);
        for (uint32_t peer = 0; peer < table.peers.size(); ++peer) {
            if (table.peers[peer].sendable.population() != 0) {
                return false;
            }
        }
    }
    return true;
}

NetwSyncModel a_model(
    const Ref<NetwMultiplayer> &p_core,
    const PropertyBindingPtr &p_binding
) {
    NetwSyncModel model;
    model.attach_book(&p_core->property_binding_book());
    model.declare(
        p_binding->get_route(),
        int64_t(NetwSyncModel::KIND_DERIVED),
        p_binding->get_order_key(),
        0,
        p_binding->get_set()->get_rid_handle(),
        p_binding->get_set()->record,
        p_binding->get_set()->wire_hash(),
        p_binding->get_set()->policy,
        p_binding->get_set()->audience
    );
    model.attach(
        p_binding->get_route(),
        int64_t(NetwSyncModel::KIND_DERIVED),
        p_binding->get_order_key(),
        p_binding->get_set()->record,
        p_binding
    );
    return model;
}

TEST_CASE(
    "[Networked][Sync][Hosted] SP1 a binding whose node is gone, whose node "
    "carries no entity, or that never bound a route is skipped and counted"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    ReplicationSend send;
    CallLog log;

    Node *gone = memnew(Node);
    Node *bare = memnew(Node);
    Node *carrying = memnew(Node);
    NetwEntity::ensure(carrying);

    LocalVector<PropertyBindingPtr> bindings;
    bindings.push_back(a_binding(core, gone, 1));
    bindings.push_back(a_binding(core, bare, 1));
    bindings.push_back(a_binding(core, carrying, 0));
    for (const PropertyBindingPtr &binding : bindings) {
        core->store_admit(binding);
    }
    memdelete(gone);

    NetwSyncModel model;
    core->sync_pump_rows(&model, &send, 4, Callable(), Callable());

    CHECK(sends_nothing(core));
    const Dictionary stats = core->sync_flush_stats();
    NETW_CHECK_EQ(
        int64_t(stats[StringName("pump_skips_invalid_node")]),
        int64_t(1)
    );
    NETW_CHECK_EQ(
        int64_t(stats[StringName("pump_skips_no_entity")]),
        int64_t(1)
    );
    NETW_CHECK_EQ(
        int64_t(stats[StringName("pump_skips_no_route")]),
        int64_t(1)
    );

    memdelete(carrying);
    memdelete(bare);
}

TEST_CASE(
    "[Networked][Sync][Hosted] SP2 an unbound binding is offered the bind "
    "callback once, and is skipped when it still has no route after it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    ReplicationSend send;
    CallLog log;
    Node *node = memnew(Node);
    NetwEntity::ensure(node);

    Node *bound = memnew(Node);
    NetwEntity::ensure(bound);

    LocalVector<PropertyBindingPtr> bindings;
    bindings.push_back(a_binding(core, node, 0));
    bindings.push_back(a_binding(core, bound, 3));
    for (const PropertyBindingPtr &binding : bindings) {
        core->store_admit(binding);
    }

    NetwSyncModel model;
    core->sync_pump_rows(&model, &send, 4, log.callable("bind"), Callable());
    CHECK(sends_nothing(core));
    NETW_CHECK_EQ(log.count("bind"), 1);
    const Array asked = log.args("bind", 0);
    NETW_CHECK_EQ(int64_t(asked.size()), int64_t(1));
    if (asked.size() == 1) {
        NETW_CHECK_EQ(uint64_t(int64_t(asked[0])), bindings[0]->id);
    }

    memdelete(bound);
    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync][Hosted][SceneTree] SP3 a bound binding the model gives "
    "no recipients for offers nothing and is not tapped, because the tap "
    "records what went on the wire"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    ReplicationSend send;
    CallLog log;
    Node *node = memnew(Node);
    netw::gd::scene_root()->add_child(node);
    NetwEntity::ensure(node);
    const PropertyBindingPtr binding = a_binding(core, node, 1);
    binding->set_authored_tick(41);
    NetwSyncModel model = a_model(core, binding);
    core->store_admit(binding);

    core->sync_pump_rows(&model, &send, 4, Callable(), log.callable("tap"));

    CHECK(sends_nothing(core));
    NETW_CHECK_EQ(log.count("tap"), 0);
    const Dictionary model_stats = model.stats();
    NETW_CHECK_EQ(
        int64_t(model_stats[StringName("skips_no_recipients")]),
        int64_t(1)
    );
    const Dictionary stats = core->sync_flush_stats();
    NETW_CHECK_EQ(
        int64_t(stats[StringName("pump_skips_no_route")]),
        int64_t(0)
    );

    core->sync_pump_rows(nullptr, &send, 4, Callable(), Callable());
    CHECK(sends_nothing(core));

    netw::gd::scene_root()->remove_child(node);
    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync][Hosted][SceneTree] SP4 a route the model never declared "
    "is counted as unrouted, not as one the model declined to send"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    ReplicationSend send;
    Node *node = memnew(Node);
    netw::gd::scene_root()->add_child(node);
    NetwEntity::ensure(node);
    const PropertyBindingPtr binding = a_binding(core, node, 7);

    NetwSyncModel model;
    core->store_admit(binding);

    core->sync_pump_rows(&model, &send, 4, Callable(), Callable());
    CHECK(sends_nothing(core));
    NETW_CHECK_EQ(
        int64_t(core->sync_flush_stats()[StringName("pump_skips_no_route")]),
        int64_t(1)
    );
    NETW_CHECK_EQ(
        int64_t(model.stats()[StringName("skips_not_author")]),
        int64_t(0)
    );

    netw::gd::scene_root()->remove_child(node);
    memdelete(node);
}

} // namespace TestNetwSyncPump

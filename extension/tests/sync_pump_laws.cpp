#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/api/sync_model.hpp"
#include "netw/replication_send.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSyncPump {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPropertySet;
using netw::NetwPropertySetBinding;
using netw::NetwPropertySetColumn;
using netw::NetwSyncModel;
using netw::ReplicationSend;
using netw::SchemaCore;
using netw_test::CallLog;

const int64_t SYNC_ROW = 39;

Ref<NetwPropertySet> a_set() {
    Ref<NetwPropertySet> set;
    set.instantiate();
    Ref<NetwPropertySetColumn> column = NetwPropertySetColumn::create(
        StringName("hp"),
        Ref<netw::NetwQuantize>(),
        false,
        SchemaCore::I32
    );
    column->lane = NetwPropertySet::VOLATILE;
    set->bind_column(column);
    return set;
}

Ref<NetwPropertySetBinding> a_binding(Node *p_node, int64_t p_route) {
    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(a_set(), p_node);
    binding->order_key = StringName(".");
    binding->route = p_route;
    return binding;
}

NetwSyncModel a_model(const Ref<NetwPropertySetBinding> &p_binding) {
    NetwSyncModel model;
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

    Node *bare = memnew(Node);
    Node *carrying = memnew(Node);
    NetwEntity::ensure(carrying);

    Array bindings;
    bindings.push_back(Ref<NetwPropertySetBinding>());
    bindings.push_back(a_binding(nullptr, 1));
    bindings.push_back(a_binding(bare, 1));
    bindings.push_back(a_binding(carrying, 0));

    NetwSyncModel model;
    const LocalVector<netw::repl::RowOffer> offers = core->sync_pump_offers(
        &model,
        &send,
        bindings,
        4,
        Callable(),
        Callable()
    );

    CHECK(offers.is_empty());
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

    Array bindings;
    bindings.push_back(a_binding(node, 0));
    bindings.push_back(a_binding(bound, 3));

    NetwSyncModel model;
    CHECK(core->sync_pump_offers(
                  &model,
                  &send,
                  bindings,
                  4,
                  log.callable("bind"),
                  Callable()
    )
              .is_empty());
    NETW_CHECK_EQ(log.count("bind"), 1);
    const Array asked = log.args("bind", 0);
    NETW_CHECK_EQ(int64_t(asked.size()), int64_t(1));
    if (asked.size() == 1) {
        const Ref<NetwPropertySetBinding> offered = asked[0];
        CHECK(offered == Ref<NetwPropertySetBinding>(bindings[0]));
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
    const Ref<NetwPropertySetBinding> binding = a_binding(node, 1);
    binding->set_authored_tick(41);
    NetwSyncModel model = a_model(binding);

    Array bindings;
    bindings.push_back(binding);

    const LocalVector<netw::repl::RowOffer> offers = core->sync_pump_offers(
        &model,
        &send,
        bindings,
        4,
        Callable(),
        log.callable("tap")
    );

    CHECK(offers.is_empty());
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

    CHECK(core->sync_pump_offers(
                  nullptr,
                  &send,
                  bindings,
                  4,
                  Callable(),
                  Callable()
    )
              .is_empty());

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
    const Ref<NetwPropertySetBinding> binding = a_binding(node, 7);

    NetwSyncModel model;
    Array bindings;
    bindings.push_back(binding);

    CHECK(core->sync_pump_offers(
                  &model,
                  &send,
                  bindings,
                  4,
                  Callable(),
                  Callable()
    )
              .is_empty());
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

#include "support/netw_test.h"

#include "support/declared_nodes.h"

#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/sync_pipeline.hpp"

namespace TestNetwDerivedRegistrationLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPropertySet;
using netw::NetwSyncModel;
using netw::SyncPipeline;

struct Stand {
    Ref<NetwMultiplayer> core;
    NetwSyncModel model;
    SyncPipeline *pipeline = nullptr;

    Stand() {
        core.instantiate();
        pipeline = core->get_replication_plane()->get_sync_pipeline();
        pipeline->set_sync_model(&model);
        pipeline->set_channels(20, 21, 22, 17, 18);
    }

    ~Stand() {
        pipeline->dispose();
    }

    int64_t active() const {
        return int64_t(pipeline->counters()[StringName("derived_sets_active")]);
    }
};

Node *scripted(const char *p_path) {
    const Ref<Script> script = netw_test::script_from(p_path);
    REQUIRE(script.is_valid());
    if (script.is_null()) {
        return nullptr;
    }
    return Object::cast_to<Node>(script->call("new"));
}

TEST_CASE(
    "[Networked][Sync] a script's own marks are what register_derived binds, "
    "one binding per record kind, and asking twice binds nothing more"
) {
    Stand stand;
    Node *node = scripted(netw_test::gdsrc::STATE_AND_INPUT);
    REQUIRE(node != nullptr);
    node->set_name("PredictedWithoutComponent");

    stand.pipeline->register_derived(node);
    NETW_CHECK_EQ(stand.active(), int64_t(2));
    CHECK(stand.pipeline->derived_binding(node, NetwPropertySet::RECORD_STATE)
              .is_valid());
    CHECK(stand.pipeline->derived_binding(node, NetwPropertySet::RECORD_INPUT)
              .is_valid());

    stand.pipeline->register_derived(node);
    NETW_CHECK_EQ(stand.active(), int64_t(2));

    stand.pipeline->unregister_derived(node);
    NETW_CHECK_EQ(stand.active(), int64_t(0));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync] a broadcast-marked script binds its broadcast set "
    "alone, so the trusted display stream never takes the state binding a "
    "rewind timeline hangs off"
) {
    Stand stand;
    Node *node = scripted(netw_test::gdsrc::BROADCAST_MASKED_AIM);
    REQUIRE(node != nullptr);
    node->set_name("BroadcastAim");

    stand.pipeline->register_derived(node);
    NETW_CHECK_EQ(stand.active(), int64_t(1));
    CHECK(stand.pipeline
              ->derived_binding(node, NetwPropertySet::RECORD_BROADCAST)
              .is_valid());
    CHECK(stand.pipeline->derived_binding(node, NetwPropertySet::RECORD_STATE)
              .is_null());
    CHECK(stand.pipeline->derived_binding(node, NetwPropertySet::RECORD_INPUT)
              .is_null());

    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync] a node carrying no script declares nothing, so "
    "register_derived binds nothing rather than an empty set"
) {
    Stand stand;
    Node *node = memnew(Node);

    stand.pipeline->register_derived(node);
    NETW_CHECK_EQ(stand.active(), int64_t(0));
    CHECK(stand.pipeline->derived_binding(node, NetwPropertySet::RECORD_STATE)
              .is_null());

    memdelete(node);
}

} // namespace TestNetwDerivedRegistrationLaws

#endif

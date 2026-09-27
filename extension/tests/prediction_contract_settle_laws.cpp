#include "support/netw_test.h"

#include "support/declared_nodes.h"

#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/multiplayer_synchronizer.hpp>

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/simulation_handle.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/sync_authoring.hpp"

namespace TestNetwPredictionContractSettleLaws {

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
        pipeline->set_channels(20, 21, 22, 43, 17, 18);
    }

    ~Stand() {
        pipeline->dispose();
    }

    bool holds(Node *p_node) const {
        return core->settle_has_key(StringName(
            String("sync-prediction-contract?")
            + String::num_int64(int64_t(netw::gd::instance_id(p_node)))
        ));
    }
};

Node *declaring(const char *p_path, const char *p_name) {
    const Ref<Script> script = netw_test::script_from(p_path);
    REQUIRE(script.is_valid());
    if (script.is_null()) {
        return nullptr;
    }
    Node *node = Object::cast_to<Node>(script->call("new"));
    if (node != nullptr) {
        node->set_name(p_name);
    }
    return node;
}

MultiplayerSynchronizer *predicting_child(Node *p_owner) {
    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    sync->set_name("Sync");
    sync->set_meta(StringName("netw_schedule"), 0);
    p_owner->add_child(sync);
    sync->set_owner(p_owner);
    return sync;
}

TEST_CASE(
    "[Networked][Sync][Settle] a state-plus-input declaration holds its "
    "contract report for the session's own settle rather than firing it "
    "inline, and the settle lands it with no frame driven"
) {
    Stand stand;
    Node *node = declaring(
        netw_test::gdsrc::STATE_AND_INPUT,
        "PredictedWithoutComponent"
    );
    REQUIRE(node != nullptr);

    stand.pipeline->register_derived(node);
    CHECK(stand.holds(node));

    stand.core->session_flush_deferred();
    CHECK_FALSE(stand.holds(node));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync][Settle] the grace is keyed by the node, so two "
    "declarations in one cascade are both checked rather than the first "
    "losing its turn to the second"
) {
    Stand stand;
    Node *first = declaring(netw_test::gdsrc::STATE_AND_INPUT, "FirstCovered");
    Node *second
        = declaring(netw_test::gdsrc::STATE_AND_INPUT, "SecondCovered");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    stand.pipeline->register_derived(first);
    stand.pipeline->register_derived(second);
    CHECK(stand.holds(first));
    CHECK(stand.holds(second));

    stand.core->session_flush_deferred();
    CHECK_FALSE(stand.holds(first));
    CHECK_FALSE(stand.holds(second));

    memdelete(second);
    memdelete(first);
}

TEST_CASE(
    "[Networked][Sync][Settle] a declaration that claims no contract queues "
    "no check, because the contradiction the report names is the PAIR"
) {
    Stand stand;
    Node *broadcast
        = declaring(netw_test::gdsrc::BROADCAST_MASKED_AIM, "BroadcastOnly");
    REQUIRE(broadcast != nullptr);

    stand.pipeline->register_derived(broadcast);
    CHECK_FALSE(stand.holds(broadcast));

    Node *bare = memnew(Node);
    stand.pipeline->register_derived(bare);
    CHECK_FALSE(stand.holds(bare));

    memdelete(bare);
    memdelete(broadcast);
}

TEST_CASE(
    "[Networked][Sync][Settle] the grace is spent against what the node "
    "declares at the pump, and a synchronizer carrying a prediction field is "
    "what satisfies the claim"
) {
    Node *bare = declaring(netw_test::gdsrc::STATE_AND_INPUT, "Uncovered");
    REQUIRE(bare != nullptr);
    CHECK_FALSE(netw::authoring::declares_prediction(bare));

    MultiplayerSynchronizer *sync = predicting_child(bare);
    CHECK(netw::authoring::declares_prediction(bare));

    sync->remove_meta(StringName("netw_schedule"));
    CHECK_FALSE(netw::authoring::declares_prediction(bare));

    memdelete(bare);
}

TEST_CASE(
    "[Networked][Sync][Sim] a synchronizer prediction block's schedule, "
    "restore and restore horizon integers read back through the entity's "
    "simulation handle with the values they carried"
) {
    Node *root = memnew(Node);
    root->set_name("BlockAuthored");
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::ensure(root);
    REQUIRE(entity.is_valid());
    MultiplayerSynchronizer *sync = predicting_child(root);
    sync->set_meta(
        StringName("netw_schedule"),
        int(netw::NetwSimulationHandle::SCHEDULE_STEPPED)
    );
    sync->set_meta(
        StringName("netw_snap_restore"),
        int(netw::NetwSimulationHandle::RESTORE_EXTRAPOLATED)
    );
    sync->set_meta(StringName("netw_max_restore_ticks"), 4);

    netw::authoring::apply(root, sync);

    const Ref<netw::NetwSimulationHandle> simulation = entity->get_simulation();
    REQUIRE(simulation.is_valid());
    NETW_CHECK_EQ(
        simulation->get_schedule(),
        netw::NetwSimulationHandle::SCHEDULE_STEPPED
    );
    NETW_CHECK_EQ(
        simulation->get_restore(),
        netw::NetwSimulationHandle::RESTORE_EXTRAPOLATED
    );
    NETW_CHECK_EQ(simulation->get_max_restore_ticks(), 4);

    memdelete(root);
}

} // namespace TestNetwPredictionContractSettleLaws

#endif

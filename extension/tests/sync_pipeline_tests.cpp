#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/repl/property_set.hpp"

namespace TestSyncPipeline {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwSyncModel;
using netw::SyncPipeline;
using netw::repl::PropertyColumn;
using netw::repl::PropertySet;
using netw::repl::SetBook;

uint64_t one_column(
    SetBook &r_sets,
    const StringName &p_key,
    int64_t p_record
) {
    const uint64_t declared = r_sets.create();
    PropertySet *set = r_sets.get(declared);
    set->columns.push_back(
        PropertyColumn::create(
            p_key,
            Ref<netw::NetwQuantize>(),
            false,
            NetwMultiplayer::ColumnType(Variant::NIL)
        )
    );
    set->record = p_record;
    return declared;
}

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

    uint64_t sealed(const char *p_key, int64_t p_record) {
        SetBook &sets = core->property_set_book();
        const uint64_t declared = one_column(sets, p_key, p_record);
        sets.get(declared)->sealed = true;
        return declared;
    }

    int64_t counter(const char *p_key) const {
        return int64_t(pipeline->counters()[StringName(p_key)]);
    }
};

TEST_CASE(
    "[Networked][Sync][Hosted] an unsealed set is refused, because a set with "
    "no wire hash is one the other peer cannot agree with"
) {
    Stand stand;
    Node *node = memnew(Node);

    const uint64_t unsealed = one_column(
        stand.core->property_set_book(),
        "position",
        PropertySet::RECORD_STATE
    );

    NETW_CHECK_EQ(
        int(stand.pipeline->register_property_set(node, unsealed)),
        int(ERR_INVALID_DATA)
    );
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(0));

    NETW_CHECK_EQ(
        int(stand.pipeline->register_property_set(nullptr, unsealed)),
        int(ERR_INVALID_DATA)
    );

    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a sealed set registers once per record kind, "
    "and a second set of the same kind replaces it rather than stacking"
) {
    Stand stand;
    Node *node = memnew(Node);

    const uint64_t state = stand.sealed("position", PropertySet::RECORD_STATE);
    NETW_CHECK_EQ(
        int(stand.pipeline->register_property_set(node, state)),
        int(OK)
    );
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(1));

    const uint64_t wider = stand.sealed("velocity", PropertySet::RECORD_STATE);
    stand.pipeline->register_property_set(node, wider);
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(1));

    const uint64_t input = stand.sealed("throttle", PropertySet::RECORD_INPUT);
    stand.pipeline->register_property_set(node, input);
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(2));

    const bool holds_state
        = stand.pipeline->derived_binding(node, PropertySet::RECORD_STATE)
        != nullptr;
    CHECK(holds_state);
    const bool holds_no_broadcast
        = stand.pipeline->derived_binding(node, PropertySet::RECORD_BROADCAST)
        == nullptr;
    CHECK(holds_no_broadcast);

    stand.pipeline->unregister_derived(node);
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(0));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a binding whose node is gone is pruned by the "
    "pump rather than answered as a live set"
) {
    Stand stand;
    Node *node = memnew(Node);
    const uint64_t state = stand.sealed("position", PropertySet::RECORD_STATE);
    stand.pipeline->register_property_set(node, state);
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(1));

    memdelete(node);

    stand.pipeline->pump(1);

    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted][SceneTree] a binding whose node leaves the tree "
    "is kept while the node lives and pruned by the first pump after it is "
    "freed"
) {
    Node *root = netw::gd::scene_root();
    REQUIRE(root != nullptr);
    Stand stand;
    Node *node = memnew(Node);
    root->add_child(node);
    const uint64_t state = stand.sealed("position", PropertySet::RECORD_STATE);
    stand.pipeline->register_property_set(node, state);
    stand.pipeline->pump(1);
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(1));

    root->remove_child(node);
    stand.pipeline->pump(1);
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(1));

    memdelete(node);
    stand.pipeline->pump(1);

    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] a send verb on a node in no entity is dropped "
    "as unroutable and counted, and reports exactly once"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] 'Loose' is not part of a NetwEntity, so the call "
        "to 'position' was dropped. Call Netw.configure_entity on its root.",
        1
    );

    Stand stand;
    Node *node = memnew(Node);
    node->set_name("Loose");

    stand.pipeline->send_property(node, StringName("position"));
    NETW_CHECK_EQ(stand.counter("sends_dropped_unroutable"), int64_t(1));

    stand.pipeline->send_property(node, StringName("position"));
    NETW_CHECK_EQ(stand.counter("sends_dropped_unroutable"), int64_t(2));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync][Hosted] an unreliable stream accepts its first stamp "
    "and afterward only a fresher one, per stream"
) {
    Stand stand;

    stand.pipeline->open_datagram(-1);
    CHECK(stand.pipeline->accept_unreliable(2, 7, 20, 100));
    stand.pipeline->open_datagram(-1);
    CHECK_FALSE(stand.pipeline->accept_unreliable(2, 7, 20, 99));
    stand.pipeline->open_datagram(-1);
    CHECK(stand.pipeline->accept_unreliable(2, 7, 20, 101));

    stand.pipeline->open_datagram(-1);
    CHECK(stand.pipeline->accept_unreliable(2, 8, 20, 5));

    NETW_CHECK_EQ(stand.counter("sync_drops_stale"), int64_t(1));

    stand.pipeline->clear_route(7);
    stand.pipeline->open_datagram(-1);
    CHECK(stand.pipeline->accept_unreliable(2, 7, 20, 1));
}

TEST_CASE(
    "[Networked][Sync][Hosted] every frame of one datagram shares the first "
    "frame's verdict, so a duplicated datagram drops whole"
) {
    Stand stand;

    stand.pipeline->open_datagram(-1);
    CHECK(stand.pipeline->accept_unreliable(2, 7, 20, 100));
    CHECK(stand.pipeline->accept_unreliable(2, 7, 20, 100));

    stand.pipeline->open_datagram(-1);
    CHECK_FALSE(stand.pipeline->accept_unreliable(2, 7, 20, 100));
    CHECK_FALSE(stand.pipeline->accept_unreliable(2, 7, 20, 100));
}

TEST_CASE(
    "[Networked][Sync][Hosted] a payload names only the fields its set "
    "declares that the node actually carries, both ways"
) {
    Node *node = memnew(Node);
    node->set_name("Body");

    SetBook sets;
    PropertySet *set
        = sets.get(one_column(sets, "name", PropertySet::RECORD_STATE));
    set->columns.push_back(
        PropertyColumn::create(
            StringName("nowhere"),
            Ref<netw::NetwQuantize>(),
            false,
            NetwMultiplayer::ColumnType(Variant::NIL)
        )
    );

    const Dictionary gathered = SyncPipeline::gather_payload(node, set);
    NETW_CHECK_EQ(gathered.size(), 1);
    CHECK(gathered.has(StringName("name")));

    Dictionary superset;
    superset[StringName("name")] = String("Renamed");
    superset[StringName("nowhere")] = 7;
    superset[StringName("undeclared")] = 9;
    SyncPipeline::apply_payload(node, set, superset);

    const bool renamed = node->get_name() == StringName("Renamed");
    CHECK(renamed);

    NETW_CHECK_EQ(SyncPipeline::gather_payload(nullptr, set).size(), 0);

    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a node with no script carries no property or "
    "signal id, so its token is the name both peers can read"
) {
    Stand stand;
    Node *node = memnew(Node);

    const Variant prop = stand.pipeline->encode_prop_val(
        Ref<NetwEntity>(),
        node,
        StringName("position")
    );
    const bool prop_is_name = StringName(prop) == StringName("position");
    CHECK(prop_is_name);

    const Variant sig = stand.pipeline->encode_signal_val(
        Ref<NetwEntity>(),
        node,
        StringName("fired")
    );
    const bool sig_is_name = StringName(sig) == StringName("fired");
    CHECK(sig_is_name);

    memdelete(node);
}

TEST_CASE(
    "[Networked][Sync][Hosted] the contract warning names the entity and says "
    "what is missing, so the law and the emitter cannot drift"
) {
    const String message = SyncPipeline::missing_prediction_component_message(
        String("racer@31")
    );

    CHECK(message.begins_with("racer@31 declares"));
    CHECK(message.contains("but no prediction"));
    CHECK(message.contains("MultiplayerSynchronizer"));
}

TEST_CASE(
    "[Networked][Sync][Hosted] the contract warning names the node when its "
    "entity carries no id yet, so the line never names nobody"
) {
    Node *node = memnew(Node);
    node->set_name("Racer");
    const Ref<NetwEntity> entity = NetwEntity::ensure(node);
    REQUIRE(entity.is_valid());

    const bool named_by_node
        = SyncPipeline::contract_subject_name(node) == String("Racer");
    CHECK(named_by_node);

    memdelete(node);
}

} // namespace TestSyncPipeline

#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/sync_pipeline.hpp"

namespace TestSyncPipeline {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPropertySet;
using netw::NetwPropertySetColumn;
using netw::NetwSyncModel;
using netw::SyncPipeline;

Ref<NetwPropertySet> one_column(const StringName &p_key, int64_t p_record) {
    Ref<NetwPropertySet> set;
    set.instantiate();
    const Ref<NetwPropertySetColumn> column = NetwPropertySetColumn::create(
        p_key,
        Ref<netw::NetwQuantize>(),
        false,
        int64_t(Variant::NIL)
    );
    TypedArray<NetwPropertySetColumn> columns;
    columns.push_back(column);
    set->set_columns(columns);
    set->set_record(p_record);
    return set;
}

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

    const Ref<NetwPropertySet> unsealed
        = one_column("position", NetwPropertySet::RECORD_STATE);

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

    Ref<NetwPropertySet> state
        = one_column("position", NetwPropertySet::RECORD_STATE);
    state->set_sealed(true);
    NETW_CHECK_EQ(
        int(stand.pipeline->register_property_set(node, state)),
        int(OK)
    );
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(1));

    Ref<NetwPropertySet> wider
        = one_column("velocity", NetwPropertySet::RECORD_STATE);
    wider->set_sealed(true);
    stand.pipeline->register_property_set(node, wider);
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(1));

    Ref<NetwPropertySet> input
        = one_column("throttle", NetwPropertySet::RECORD_INPUT);
    input->set_sealed(true);
    stand.pipeline->register_property_set(node, input);
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(2));

    CHECK(stand.pipeline->derived_binding(node, NetwPropertySet::RECORD_STATE)
              .is_valid());
    CHECK(stand.pipeline
              ->derived_binding(node, NetwPropertySet::RECORD_BROADCAST)
              .is_null());

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
    Ref<NetwPropertySet> state
        = one_column("position", NetwPropertySet::RECORD_STATE);
    state->set_sealed(true);
    stand.pipeline->register_property_set(node, state);
    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(1));

    memdelete(node);

    stand.pipeline->pump(1);

    NETW_CHECK_EQ(stand.counter("derived_sets_active"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] a send verb on a node in no entity is dropped "
    "as unroutable and counted, and warns exactly once"
) {
    Stand stand;
    Node *node = memnew(Node);
    node->set_name("Loose");

    stand.pipeline->send_property(node, StringName("position"));
    NETW_CHECK_EQ(stand.counter("sends_dropped_unroutable"), int64_t(1));

    stand.pipeline->send_property(node, StringName("position"));
    NETW_CHECK_EQ(stand.counter("sends_dropped_unroutable"), int64_t(2));
    CHECK(node->has_meta(StringName("_netw_unroutable_warned")));

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

    Ref<NetwPropertySet> set
        = one_column("name", NetwPropertySet::RECORD_STATE);
    const Ref<NetwPropertySetColumn> ghost = NetwPropertySetColumn::create(
        StringName("nowhere"),
        Ref<netw::NetwQuantize>(),
        false,
        int64_t(Variant::NIL)
    );
    TypedArray<NetwPropertySetColumn> columns = set->get_columns();
    columns.push_back(ghost);
    set->set_columns(columns);

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

    CHECK(message.begins_with("Prediction: racer@31 declares"));
    CHECK(message.contains("declares no prediction"));
    CHECK(message.contains("MultiplayerSynchronizer"));
}

} // namespace TestSyncPipeline

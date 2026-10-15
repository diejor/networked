#include "support/fault_log.h"
#include "support/netw_test.h"

#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/interest/relay.hpp"

namespace TestNetwInterestReads {

using namespace godot;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;

RID entity_of(const Ref<NetwMultiplayer> &p_core) {
    return p_core->get_liveness_core()->entity_create();
}

TEST_CASE(
    "[Networked][Interest][Hosted] IR1 a committed row reads by name, not by "
    "the order it was declared in"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const RID entity = entity_of(core);
    netw::interest::Engine &engine = core->interest_plane();

    engine.membership_add(entity.get_id(), StringName("sight"));
    engine.membership_add(entity.get_id(), StringName("audio"));
    engine.membership_add(entity.get_id(), StringName("radar"));

    const Array named = core->interest_membership_ids(entity);

    NETW_CHECK_EQ(named.size(), 3);
    CHECK(StringName(named[0]) == StringName("audio"));
    CHECK(StringName(named[1]) == StringName("radar"));
    CHECK(StringName(named[2]) == StringName("sight"));
}

TEST_CASE(
    "[Networked][Interest][Hosted] IR2 an entity the committed row does not "
    "name carries no filter"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const RID entity = entity_of(core);
    netw::interest::Engine &engine = core->interest_plane();

    REQUIRE(core->is_server());
    CHECK_FALSE(core->interest_is_filtered(entity));
    NETW_CHECK_EQ(core->interest_membership_ids(entity).size(), 0);

    engine.membership_add(entity.get_id(), StringName("sight"));

    CHECK(core->interest_is_filtered(entity));
    NETW_CHECK_EQ(core->interest_membership_ids(entity).size(), 1);

    engine.membership_remove(entity.get_id(), StringName("sight"));

    CHECK_FALSE(core->interest_is_filtered(entity));
    NETW_CHECK_EQ(core->interest_membership_ids(entity).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] IR4 awareness is admitted from the server "
    "and from nobody else, so a batch minted by another peer reaches no route"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    Array rows;
    rows.push_back(
        netw::interest::Awareness::layer_edge(
            4,
            StringName("zone"),
            netw::interest::Awareness::ENTER
        )
            .to_array()
    );
    const PackedByteArray batch = netw::interest::awareness_encode(rows);

    core->interest_receive_awareness(batch, 7);

    NETW_CHECK_EQ(core->liveness_pending_live_count(), int64_t(0));

    core->interest_receive_awareness(batch, 1);

    NETW_CHECK_EQ(core->liveness_pending_live_count(), int64_t(1));
}

TEST_CASE(
    "[Networked][Interest][Hosted] IR5 a listen host admits an entity into a "
    "layer, because a layer asks whether this session is the server"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_desired_role(NetwMultiplayer::ROLE_LISTEN_SERVER);

    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    REQUIRE(peer->create_server(1) == OK);
    core->set("multiplayer_peer", peer);

    REQUIRE(core->is_host());

    const RID handle = entity_of(core);
    Ref<netw::NetwEntity> wrapper;
    wrapper.instantiate();
    wrapper->get_record()->adopt_handle(handle);

    const Ref<NetwInterestLayer> layer
        = core->interest_layer(StringName("zone"));
    REQUIRE(layer.is_valid());

    CHECK(layer->add_entity(wrapper));
    CHECK(layer->remove_entity(wrapper));
}

TEST_CASE(
    "[Networked][Interest][Hosted] IL1 a layer name is opened once, so the "
    "second ask is the first handle and the first view"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    const RID first = core->layer_open(StringName("sight"));
    const RID again = core->layer_open(StringName("sight"));

    CHECK(first.is_valid());
    CHECK(first == again);
    CHECK(core->interest_layer_find(StringName("sight")) == first);
    CHECK(core->layer_name_of(first) == StringName("sight"));
    const Ref<NetwInterestLayer> view
        = core->interest_layer(StringName("sight"));
    CHECK(view.is_valid());
    CHECK(view == core->interest_layer_view(first));
    CHECK(view->get_layer_id() == StringName("sight"));
}

TEST_CASE(
    "[Networked][Interest][Hosted] IL2 closing a layer releases its name and "
    "its view together"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    const RID first = core->layer_open(StringName("sight"));
    const Ref<RefCounted> before = core->interest_layer_view(first);
    core->layer_close(first);

    NETW_CHECK_EQ(
        core->interest_layer_find(StringName("sight")).is_valid(),
        false
    );
    NETW_CHECK_EQ(core->interest_layer_view(first).is_valid(), false);

    const RID reopened = core->layer_open(StringName("sight"));

    CHECK(reopened.is_valid());
    CHECK(reopened != first);
    CHECK(core->interest_layer_view(reopened) != before);
    NETW_CHECK_EQ(core->interest_layer_view(first).is_valid(), false);
}

TEST_CASE(
    "[Networked][Interest][Hosted] IL3 a layer with no name is refused rather "
    "than answered with a dead handle"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][interest] Condition \"p_name.is_empty()\" is true. "
        "Returning: RID(). An interest layer needs a name. Pass a "
        "non-empty name.",
        1
    );

    Ref<NetwMultiplayer> core;
    core.instantiate();

    NETW_CHECK_EQ(core->layer_open(StringName()).is_valid(), false);
    NETW_CHECK_EQ(core->interest_layers().size(), 0);
    NETW_CHECK_EQ(
        core->interest_layer_named(StringName("sight")).is_valid(),
        false
    );
}

TEST_CASE(
    "[Networked][Interest][Hosted] IL4 forgetting the book frees every name "
    "and every view"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    const RID sight = core->layer_open(StringName("sight"));
    const RID audio = core->layer_open(StringName("audio"));
    NETW_CHECK_EQ(core->interest_layers().size(), 2);

    core->layer_forget_all();

    NETW_CHECK_EQ(core->interest_layer_view(sight).is_valid(), false);
    NETW_CHECK_EQ(core->interest_layer_view(audio).is_valid(), false);
    NETW_CHECK_EQ(
        core->interest_layer_find(StringName("sight")).is_valid(),
        false
    );
    NETW_CHECK_EQ(
        core->interest_layer_find(StringName("audio")).is_valid(),
        false
    );
    NETW_CHECK_EQ(core->interest_layers().size(), 0);
}

} // namespace TestNetwInterestReads

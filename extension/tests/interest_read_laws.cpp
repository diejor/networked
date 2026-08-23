#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwInterestReads {

using namespace godot;
using netw::NetwInterestLayer;
using netw::NetwMultiplayerCore;

RID entity_of(const Ref<NetwMultiplayerCore> &p_core) {
    return p_core->get_liveness_core()->entity_create();
}

TEST_CASE(
    "[Networked][Interest][Hosted] IR1 a committed row reads by name, not by "
    "the order it was declared in"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID entity = entity_of(core);
    netw::InterestEngine &engine = core->interest_plane();

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
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID entity = entity_of(core);
    netw::InterestEngine &engine = core->interest_plane();

    REQUIRE(core->is_server());
    CHECK_FALSE(core->interest_has_filter(entity));
    NETW_CHECK_EQ(core->interest_membership_ids(entity).size(), 0);

    engine.membership_add(entity.get_id(), StringName("sight"));

    CHECK(core->interest_has_filter(entity));
    NETW_CHECK_EQ(core->interest_membership_ids(entity).size(), 1);

    engine.membership_remove(entity.get_id(), StringName("sight"));

    CHECK_FALSE(core->interest_has_filter(entity));
    NETW_CHECK_EQ(core->interest_membership_ids(entity).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] IL1 a layer name is opened once, so the "
    "second ask is the first handle and the first view"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    const RID first = core->layer_open(StringName("sight"));
    const RID again = core->layer_open(StringName("sight"));

    CHECK(first.is_valid());
    CHECK(first == again);
    CHECK(core->layer_named(StringName("sight")) == first);
    CHECK(core->layer_name_of(first) == StringName("sight"));
    const Ref<NetwInterestLayer> view = core->interest_layer(
        StringName("sight")
    );
    CHECK(view.is_valid());
    CHECK(view == core->layer_view(first));
    CHECK(view->get_layer_id() == StringName("sight"));
}

TEST_CASE(
    "[Networked][Interest][Hosted] IL2 closing a layer releases its name and "
    "its view together"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    const RID first = core->layer_open(StringName("sight"));
    const Ref<RefCounted> before = core->layer_view(first);
    core->layer_close(first);

    NETW_CHECK_EQ(core->layer_named(StringName("sight")).is_valid(), false);
    NETW_CHECK_EQ(core->layer_view(first).is_valid(), false);

    const RID reopened = core->layer_open(StringName("sight"));

    CHECK(reopened.is_valid());
    CHECK(reopened != first);
    CHECK(core->layer_view(reopened) != before);
    NETW_CHECK_EQ(core->layer_view(first).is_valid(), false);
}

TEST_CASE(
    "[Networked][Interest][Hosted] IL3 a layer with no name is refused rather "
    "than answered with a dead handle"
) {
    Ref<NetwMultiplayerCore> core;
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
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    const RID sight = core->layer_open(StringName("sight"));
    const RID audio = core->layer_open(StringName("audio"));
    NETW_CHECK_EQ(core->interest_layers().size(), 2);

    core->layer_forget_all();

    NETW_CHECK_EQ(core->layer_view(sight).is_valid(), false);
    NETW_CHECK_EQ(core->layer_view(audio).is_valid(), false);
    NETW_CHECK_EQ(core->layer_named(StringName("sight")).is_valid(), false);
    NETW_CHECK_EQ(core->layer_named(StringName("audio")).is_valid(), false);
    NETW_CHECK_EQ(core->interest_layers().size(), 0);
}

} // namespace TestNetwInterestReads

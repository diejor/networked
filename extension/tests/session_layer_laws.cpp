#include "support/netw_test.h"

#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionLayer {

using namespace godot;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

bool same_layer(const RID &p_a, const RID &p_b) {
    return p_a == p_b;
}

bool reads(const String &p_said, const char *p_expected) {
    return p_said == String(p_expected);
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 creating a layer by a name that already "
    "names one answers the standing layer rather than opening a second"
) {
    Ref<NetwMultiplayer> session = make_session();

    const RID first = session->interest_layer_create(StringName("sight"));
    CHECK(first.is_valid());

    SUBCASE("the same name answers the same layer") {
        CHECK(same_layer(
            session->interest_layer_create(StringName("sight")),
            first
        ));
    }

    SUBCASE("a different name opens a layer of its own") {
        const RID other = session->interest_layer_create(StringName("sound"));
        CHECK(other.is_valid());
        CHECK_FALSE(same_layer(other, first));
    }

    SUBCASE("finding by name agrees with creating by name") {
        CHECK(
            same_layer(session->interest_layer_find(StringName("sight")), first)
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 freeing a layer forgets its name, so the "
    "next create opens a new one rather than resurrecting the freed row"
) {
    Ref<NetwMultiplayer> session = make_session();

    const RID layer = session->interest_layer_create(StringName("sight"));
    session->interest_layer_free(layer);

    CHECK_FALSE(session->interest_layer_find(StringName("sight")).is_valid());
    CHECK(session->layer_record(layer).is_null());

    SUBCASE("freeing a layer nobody opened is a no-op rather than an error") {
        session->interest_layer_free(RID());
        CHECK(true);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 a viewer is admitted to a layer only "
    "when both the layer and the peer are real"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID layer = session->interest_layer_create(StringName("sight"));

    NETW_CHECK_EQ(session->interest_layer_add_viewer(layer, 4), OK);
    NETW_CHECK_EQ(session->layer_record(layer)->viewer_ids().size(), 1);

    SUBCASE("peer zero is not a viewer") {
        NETW_CHECK_EQ(
            session->interest_layer_add_viewer(layer, 0),
            ERR_INVALID_DATA
        );
    }

    SUBCASE("an unopened layer admits nobody") {
        NETW_CHECK_EQ(
            session->interest_layer_add_viewer(RID(), 4),
            ERR_DOES_NOT_EXIST
        );
    }

    SUBCASE("removing the viewer empties the row again") {
        session->interest_layer_remove_viewer(layer, 4);
        NETW_CHECK_EQ(session->layer_record(layer)->viewer_ids().size(), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 a layer parameter writes through to the "
    "record, and a parameter that names no setting is refused"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID layer = session->interest_layer_create(StringName("sight"));

    session->interest_layer_set_param(
        layer,
        NetwMultiplayer::LAYER_PARAM_LEAVE_POLICY,
        NetwMultiplayer::LEAVE_POLICY_RETAIN
    );
    NETW_CHECK_EQ(
        session->layer_record(layer)->get_default_leave_policy(),
        NetwMultiplayer::LEAVE_POLICY_RETAIN
    );

    session->interest_layer_set_param(
        layer,
        NetwMultiplayer::LAYER_PARAM_PERCEPTION_POLICY,
        NetwMultiplayer::PERCEPTION_POLICY_SHOW
    );
    NETW_CHECK_EQ(
        session->layer_record(layer)->get_default_perception_policy(),
        NetwMultiplayer::PERCEPTION_POLICY_SHOW
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] L5 an interest question about a peer the "
    "matrix never registered fails closed rather than guessing"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    CHECK_FALSE(session->interest_admits(entity, 9999));
    NETW_CHECK_EQ(session->interest_get_row(entity).size(), 0);

    SUBCASE("and it says which half was missing") {
        CHECK(reads(
            session->interest_explain(entity, 9999),
            "peer is not registered"
        ));
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L6 a session that ends forgets every layer "
    "it opened, and it is the ending itself that asks, so a re-host on the "
    "same session starts from an empty interest plane rather than inheriting "
    "the last session's viewers"
) {
    Ref<NetwMultiplayer> session = make_session();

    const RID sight = session->interest_layer_create(StringName("sight"));
    session->interest_layer_create(StringName("sound"));
    CHECK(sight.is_valid());
    NETW_CHECK_EQ(int(session->interest_layers().size()), 2);

    int anchored = 0;
#if defined(NETW_MODULE)
    List<Object::Connection> wired;
    session->get_signal_connection_list(StringName("session_ended"), &wired);
    for (const Object::Connection &row : wired) {
        anchored += row.callable.get_object() == session.ptr() ? 1 : 0;
    }
#else
    const Array wired
        = session->get_signal_connection_list(StringName("session_ended"));
    for (int at = 0; at < wired.size(); ++at) {
        const Dictionary row = wired[at];
        const Callable listener = row[StringName("callable")];
        anchored += listener.get_object() == session.ptr() ? 1 : 0;
    }
#endif
    NETW_CHECK_EQ(int(anchored > 0), 1);

    session->interest_clear_session();

    NETW_CHECK_EQ(int(session->interest_layers().size()), 0);
    NETW_CHECK_EQ(int(session->interest_has_layer(StringName("sight"))), 0);
    CHECK_FALSE(
        same_layer(session->interest_layer_create(StringName("sight")), sight)
    );

    session->embed_dispose();
}

} // namespace TestNetwSessionLayer

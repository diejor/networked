#include "support/netw_test.h"

#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSessionLayerDriver {

using namespace godot;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;
using netw_test::CallLog;

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return session;
}

Variant answer_nothing(const Variant &) {
    return Array();
}

Variant answer_not_an_array(const Variant &) {
    return 7;
}

Variant answer_a_stray_rid(const Variant &) {
    Array out;
    out.push_back(RID());
    return out;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a layer driver that answers something "
    "other than an array of entity RIDs stops the flush rather than folding"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID layer = session->interest_layer_create(StringName("sight"));

    session->interest_layer_set_driver_callback(
        layer,
        callable_mp_static(&answer_not_an_array)
    );
    NETW_CHECK_EQ(session->run_layer_drivers(), ERR_INVALID_DATA);
    NETW_CHECK_EQ(session->interest_flush_now(), ERR_INVALID_DATA);

    SUBCASE("an RID that names no entity stops it too") {
        session->interest_layer_set_driver_callback(
            layer,
            callable_mp_static(&answer_a_stray_rid)
        );
        NETW_CHECK_EQ(session->run_layer_drivers(), ERR_DOES_NOT_EXIST);
    }

    SUBCASE("an empty roster is a clean fold") {
        session->interest_layer_set_driver_callback(
            layer,
            callable_mp_static(&answer_nothing)
        );
        NETW_CHECK_EQ(session->run_layer_drivers(), OK);
    }

    SUBCASE("clearing the driver leaves nothing to run") {
        session->interest_layer_set_driver_callback(layer, Callable());
        NETW_CHECK_EQ(session->run_layer_drivers(), OK);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 freeing a layer drops its driver, so a "
    "later flush never calls back about a layer that is gone"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID layer = session->interest_layer_create(StringName("sight"));

    session->interest_layer_set_driver_callback(
        layer,
        callable_mp_static(&answer_not_an_array)
    );
    NETW_CHECK_EQ(session->run_layer_drivers(), ERR_INVALID_DATA);

    session->interest_layer_free(layer);
    NETW_CHECK_EQ(session->run_layer_drivers(), OK);
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 a stepper is installed only when it says "
    "it can step, so a refusing stepper leaves the space with none"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID space;

    session->predict_stepper_install(space, Ref<netw::NetwPhysicsStepper>());
    CHECK(session->predict_get_stepper(space).is_null());
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 one monitor callback answers all four "
    "layer edges, and it reads them as a single membership fact: inside or "
    "not, for which entity, from whose point of view"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID layer = session->interest_layer_create(StringName("sight"));
    const Ref<NetwInterestLayer> view = session->layer_record(layer);
    REQUIRE(view.is_valid());

    const CallLog watched;
    session->interest_layer_set_monitor_callback(
        layer,
        watched.callable("seen")
    );

    Ref<netw::NetwEntity> subject;
    subject.instantiate();

    view->emit_signal(StringName("interest_enter"), subject, 9);
    NETW_CHECK_EQ(watched.count(StringName("seen")), 1);
    CHECK(bool(watched.args(StringName("seen"))[0]));
    NETW_CHECK_EQ(int64_t(watched.args(StringName("seen"))[2]), int64_t(9));

    view->emit_signal(StringName("interest_exit"), subject, 9);
    NETW_CHECK_EQ(watched.count(StringName("seen")), 2);
    CHECK_FALSE(bool(watched.args(StringName("seen"), 1)[0]));

    SUBCASE("visibility speaks for THIS peer rather than for a remote one") {
        view->emit_signal(StringName("entity_visible"), subject);
        NETW_CHECK_EQ(watched.count(StringName("seen")), 3);
        CHECK(bool(watched.args(StringName("seen"), 2)[0]));
        NETW_CHECK_EQ(
            int64_t(watched.args(StringName("seen"), 2)[2]),
            session->get_unique_id()
        );

        view->emit_signal(StringName("entity_hidden"), subject);
        NETW_CHECK_EQ(watched.count(StringName("seen")), 4);
        CHECK_FALSE(bool(watched.args(StringName("seen"), 3)[0]));
    }

    SUBCASE("withdrawing the callback stops the reports at once") {
        session->interest_layer_set_monitor_callback(layer, Callable());
        view->emit_signal(StringName("interest_enter"), subject, 9);
        NETW_CHECK_EQ(watched.count(StringName("seen")), 2);
    }

    SUBCASE("freeing the layer stops them too") {
        session->interest_layer_free(layer);
        view->emit_signal(StringName("interest_enter"), subject, 9);
        NETW_CHECK_EQ(watched.count(StringName("seen")), 2);
    }
}

} // namespace TestNetwSessionLayerDriver

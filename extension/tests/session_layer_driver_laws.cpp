#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/world.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/physics_stepper.hpp"
#include "support/installable_stepper.h"
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
    netw_test::FaultWatch expected_faults;

    Ref<NetwMultiplayer> session = hosting();
    const RID layer = session->interest_layer_create(StringName("sight"));

    session->interest_layer_set_driver_callback(
        layer,
        callable_mp_static(&answer_not_an_array)
    );
    NETW_CHECK_EQ(session->run_layer_drivers(), ERR_INVALID_DATA);
    NETW_CHECK_EQ(session->interest_flush_now(), ERR_INVALID_DATA);

    SUBCASE("an RID that names no entity stops it too") {
        expected_faults.expect_warning(
            "[warn][interest] The driver of layer '*' did not "
            "return an Array, so the layer was not changed. Return "
            "the entities that belong in it.",
            2
        );
        expected_faults.expect_warning(
            "[warn][interest] The driver of layer '*' returned "
            "something that is not an entity of this session, so "
            "the layer was not changed. Return only entities "
            "spawned in this session.",
            1
        );

        session->interest_layer_set_driver_callback(
            layer,
            callable_mp_static(&answer_a_stray_rid)
        );
        NETW_CHECK_EQ(session->run_layer_drivers(), ERR_DOES_NOT_EXIST);
    }

    SUBCASE("an empty roster is a clean fold") {
        expected_faults.expect_warning(
            "[warn][interest] The driver of layer '*' did not "
            "return an Array, so the layer was not changed. Return "
            "the entities that belong in it.",
            2
        );

        session->interest_layer_set_driver_callback(
            layer,
            callable_mp_static(&answer_nothing)
        );
        NETW_CHECK_EQ(session->run_layer_drivers(), OK);
    }

    SUBCASE("clearing the driver leaves nothing to run") {
        expected_faults.expect_warning(
            "[warn][interest] The driver of layer '*' did not "
            "return an Array, so the layer was not changed. Return "
            "the entities that belong in it.",
            2
        );

        session->interest_layer_set_driver_callback(layer, Callable());
        NETW_CHECK_EQ(session->run_layer_drivers(), OK);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 freeing a layer drops its driver, so a "
    "later flush never calls back about a layer that is gone"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][interest] The driver of layer '*' did not return an"
        " Array, so the layer was not changed. Return the entities "
        "that belong in it.",
        1
    );

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
    "[Networked][Session][Hosted][SceneTree] L3 a stepper is installed only "
    "when it says it can step, so a refusing stepper leaves the space with none"
) {
    Ref<NetwMultiplayer> session = hosting();
    Ref<World2D> world;
    world.instantiate();
    const RID space = world->get_space();
    REQUIRE(space.is_valid());
    Ref<netw_test::InstallableStepper> capable;
    capable.instantiate();
    NETW_CHECK_EQ(session->predict_stepper_install(space, capable), OK);
    CHECK(session->predict_get_stepper(space) == capable);
    Ref<netw::NetwPhysicsStepper> refusing;
    refusing.instantiate();
    CHECK_FALSE(refusing->can_step());
    NETW_CHECK_EQ(session->predict_stepper_install(space, refusing), OK);
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

    view->emit_signal(StringName("interest_entered"), subject, 9);
    NETW_CHECK_EQ(watched.count(StringName("seen")), 1);
    CHECK(bool(watched.args(StringName("seen"))[0]));
    NETW_CHECK_EQ(int64_t(watched.args(StringName("seen"))[2]), int64_t(9));

    view->emit_signal(StringName("interest_exited"), subject, 9);
    NETW_CHECK_EQ(watched.count(StringName("seen")), 2);
    CHECK_FALSE(bool(watched.args(StringName("seen"), 1)[0]));

    SUBCASE("visibility speaks for THIS peer rather than for a remote one") {
        view->emit_signal(StringName("visibility_changed"), subject, true);
        NETW_CHECK_EQ(watched.count(StringName("seen")), 3);
        CHECK(bool(watched.args(StringName("seen"), 2)[0]));
        NETW_CHECK_EQ(
            int64_t(watched.args(StringName("seen"), 2)[2]),
            session->get_unique_id()
        );

        view->emit_signal(StringName("visibility_changed"), subject, false);
        NETW_CHECK_EQ(watched.count(StringName("seen")), 4);
        CHECK_FALSE(bool(watched.args(StringName("seen"), 3)[0]));
    }

    SUBCASE("withdrawing the callback stops the reports at once") {
        session->interest_layer_set_monitor_callback(layer, Callable());
        view->emit_signal(StringName("interest_entered"), subject, 9);
        NETW_CHECK_EQ(watched.count(StringName("seen")), 2);
    }

    SUBCASE("freeing the layer stops them too") {
        session->interest_layer_free(layer);
        view->emit_signal(StringName("interest_entered"), subject, 9);
        NETW_CHECK_EQ(watched.count(StringName("seen")), 2);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L5 a typed monitor hears the entity, the "
    "peer and whether it is visible, and the typed and flat monitors replace "
    "each other"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID layer = session->interest_layer_create(StringName("sight"));
    const Ref<NetwInterestLayer> view = session->layer_record(layer);
    REQUIRE(view.is_valid());

    const CallLog watched;
    view->set_monitor(watched.callable("seen"));
    CHECK(view->get_monitor().is_valid());

    Ref<netw::NetwEntity> subject;
    subject.instantiate();
    view->emit_signal(StringName("interest_entered"), subject, 9);
    NETW_CHECK_EQ(watched.count(StringName("seen")), 1);
    const bool names_the_entity
        = Object::cast_to<Object>(watched.args(StringName("seen"))[0])
        == subject.ptr();
    CHECK(names_the_entity);
    NETW_CHECK_EQ(int64_t(watched.args(StringName("seen"))[1]), int64_t(9));
    CHECK(bool(watched.args(StringName("seen"))[2]));

    session->interest_layer_set_monitor_callback(
        layer,
        watched.callable("raw")
    );
    CHECK_FALSE(view->get_monitor().is_valid());
    view->emit_signal(StringName("interest_exited"), subject, 9);
    NETW_CHECK_EQ(watched.count(StringName("seen")), 1);
    NETW_CHECK_EQ(watched.count(StringName("raw")), 1);
}

Ref<netw::NetwEntity> typed_answer;
Ref<NetwInterestLayer> typed_saw;

Variant drive_typed(const Variant &p_layer) {
    typed_saw
        = Ref<NetwInterestLayer>(Object::cast_to<NetwInterestLayer>(p_layer));
    Array out;
    out.push_back(typed_answer);
    return out;
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L6 a typed driver receives its "
    "layer and returns entities, and an entity of another session is refused "
    "before the layer changes"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][interest] The driver of layer '*' returned "
        "something that is not an entity of this session, so the "
        "layer was not changed. Return only entities spawned in "
        "this session.",
        1
    );

    Ref<NetwMultiplayer> session = hosting();
    Ref<NetwMultiplayer> elsewhere = hosting();
    const RID layer = session->interest_layer_create(StringName("sight"));
    const Ref<NetwInterestLayer> view = session->layer_record(layer);
    REQUIRE(view.is_valid());

    Node *mine = memnew(Node);
    Node *theirs = memnew(Node);
    netw::gd::scene_root()->add_child(mine);
    netw::gd::scene_root()->add_child(theirs);
    const Ref<netw::NetwEntity> own = netw::NetwEntity::ensure(mine);
    const Ref<netw::NetwEntity> foreign = netw::NetwEntity::ensure(theirs);
    REQUIRE(session->entity_of(mine).is_valid());
    REQUIRE(elsewhere->entity_of(theirs).is_valid());

    view->set_driver(callable_mp_static(&drive_typed));
    CHECK(view->get_driver().is_valid());

    typed_answer = foreign;
    NETW_CHECK_EQ(session->run_layer_drivers(), ERR_DOES_NOT_EXIST);
    const bool saw_its_layer = typed_saw == view;
    CHECK(saw_its_layer);
    CHECK_FALSE(view->has_entity(foreign));

    typed_answer = own;
    NETW_CHECK_EQ(session->run_layer_drivers(), OK);
    CHECK(view->has_entity(own));

    view->set_driver(Callable());
    NETW_CHECK_EQ(session->layer_driver_count(), int64_t(0));

    typed_answer.unref();
    typed_saw.unref();
    session->clear_session_state();
    elsewhere->clear_session_state();
    mine->queue_free();
    theirs->queue_free();
}

} // namespace TestNetwSessionLayerDriver

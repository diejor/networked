#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionPredictParam {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 every prediction verb against an entity "
    "with no handle refuses rather than writing into nothing"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    CHECK(session->prediction_handle(entity).is_null());

    session->predict_set_param(
        entity,
        NetwMultiplayer::PREDICT_PARAM_COLLISION_COOLDOWN_TICKS,
        9
    );
    NETW_CHECK_EQ(
        session
            ->predict_get_param(
                entity,
                NetwMultiplayer::PREDICT_PARAM_COLLISION_COOLDOWN_TICKS
            )
            .get_type(),
        Variant::NIL
    );

    SUBCASE("the simulation verbs find no handle and write nothing") {
        CHECK(session->simulation_handle(entity).is_null());
        session->simulation_set_param(
            entity,
            NetwMultiplayer::SIMULATION_PARAM_MAX_RESTORE_TICKS,
            9
        );
        NETW_CHECK_EQ(
            session
                ->simulation_get_param(
                    entity,
                    NetwMultiplayer::SIMULATION_PARAM_MAX_RESTORE_TICKS
                )
                .get_type(),
            Variant::NIL
        );
        NETW_CHECK_EQ(
            session->simulation_get_mode(entity),
            netw::NetwSimulationHandle::MODE_NONE
        );
    }

    SUBCASE("binding an owner asks for the wrapper before the owner") {
        NETW_CHECK_EQ(
            session->predict_bind_owner(RID(), nullptr),
            ERR_DOES_NOT_EXIST
        );
        NETW_CHECK_EQ(
            session->predict_bind_owner(entity, nullptr),
            ERR_DOES_NOT_EXIST
        );
    }

    SUBCASE("a sensor sample falls back to the default the caller named") {
        NETW_CHECK_EQ(
            int(
                session->predict_sensor_sample(entity, StringName("ground"), 7)
            ),
            7
        );
    }

    SUBCASE("the callback setters are no-ops rather than a null deref") {
        session->predict_set_witness_callback(entity, Callable());
        session->predict_set_corridor_callback(entity, Callable());
        session->predict_set_commands_callback(entity, Callable());
        session->simulation_set_step_callback(entity, Callable());
        session->predict_set_sensor_callback(
            entity,
            StringName("ground"),
            Callable()
        );
        session->predict_notify_contact(entity);
        session->predict_unbind_owner(entity);
        CHECK(true);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a prediction parameter that names no "
    "setting is refused rather than silently landing on another one"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    NETW_CHECK_EQ(
        session
            ->predict_get_param(
                entity,
                static_cast<NetwMultiplayer::PredictParam>(9999)
            )
            .get_type(),
        Variant::NIL
    );
    NETW_CHECK_EQ(
        session
            ->simulation_get_param(
                entity,
                static_cast<NetwMultiplayer::SimulationParam>(9999)
            )
            .get_type(),
        Variant::NIL
    );
}

} // namespace TestNetwSessionPredictParam

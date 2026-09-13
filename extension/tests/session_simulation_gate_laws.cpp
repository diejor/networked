#include "support/netw_test.h"

#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwSessionSimulationGate {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return session;
}

RID seated(const Ref<NetwMultiplayer> &p_session, int p_route, Node *p_node) {
    const RID entity = p_session->entity_create();
    p_session->entity_bind_route(entity, p_route);
    p_session->entity_bind_node(entity, p_node);
    return entity;
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L1 a body the gate holds arms "
    "the clock, and releasing it gives the clock back"
) {
    Ref<NetwMultiplayer> session = hosting();
    Node3D *body = memnew(Node3D);
    netw::gd::scene_root()->add_child(body);
    const RID entity = seated(session, 41, body);

    CHECK_FALSE(session->clock_is_gated());

    session->simulation_gate_set(entity, true);
    NETW_CHECK_EQ(session->simulation_gate_count(), 1);
    CHECK(session->clock_is_gated());

    SUBCASE("releasing drops the row and the clock's gate together") {
        session->simulation_gate_set(entity, false);
        NETW_CHECK_EQ(session->simulation_gate_count(), 0);
        CHECK_FALSE(session->clock_is_gated());
    }

    body->queue_free();
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L2 the gate is a set, so "
    "arming a body it already holds and releasing one it never held both "
    "do nothing"
) {
    Ref<NetwMultiplayer> session = hosting();
    Node3D *body = memnew(Node3D);
    netw::gd::scene_root()->add_child(body);
    const RID entity = seated(session, 42, body);

    session->simulation_gate_set(entity, true);
    session->simulation_gate_set(entity, true);
    NETW_CHECK_EQ(session->simulation_gate_count(), 1);

    SUBCASE("a second arm did not arm the clock a second time") {
        session->simulation_gate_set(entity, false);
        CHECK_FALSE(session->clock_is_gated());
    }

    SUBCASE("releasing an unheld body leaves the held one alone") {
        Node3D *other = memnew(Node3D);
        netw::gd::scene_root()->add_child(other);
        const RID unheld = seated(session, 43, other);
        session->simulation_gate_set(unheld, false);
        NETW_CHECK_EQ(session->simulation_gate_count(), 1);
        CHECK(session->clock_is_gated());
        other->queue_free();
    }

    body->queue_free();
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 an entity nobody minted cannot arm the "
    "gate, so the clock is never held for a body that does not exist"
) {
    Ref<NetwMultiplayer> session = hosting();

    session->simulation_gate_set(RID(), true);

    NETW_CHECK_EQ(session->simulation_gate_count(), 0);
    CHECK_FALSE(session->clock_is_gated());
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L4 a pass drops the bodies "
    "whose node is gone, and the clock is fully released once the last "
    "one leaves"
) {
    Ref<NetwMultiplayer> session = hosting();
    Node3D *body = memnew(Node3D);
    netw::gd::scene_root()->add_child(body);
    const RID entity = seated(session, 44, body);

    session->simulation_gate_set(entity, true);
    NETW_CHECK_EQ(session->simulation_gate_count(), 1);

    netw::gd::scene_root()->remove_child(body);
    memdelete(body);
    session->simulation_gate_apply();

    NETW_CHECK_EQ(session->simulation_gate_count(), 0);
    CHECK_FALSE(session->clock_is_gated());
}

TEST_CASE(
    "[Networked][Session][Hosted] L5 a pass over an empty gate reads nothing "
    "and releases nothing, so an unrelated arm survives it"
) {
    Ref<NetwMultiplayer> session = hosting();
    session->clock_set_gate(true);

    session->simulation_gate_apply();

    NETW_CHECK_EQ(session->simulation_gate_count(), 0);
    CHECK(session->clock_is_gated());
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L6 the sync gate admits a "
    "live route only on a channel the sync plane carries and only with a "
    "body, and the route "
    "verdict outranks both so an unplaceable route is never judged on its "
    "channel"
) {
    Ref<NetwMultiplayer> session = hosting();
    Node3D *body = memnew(Node3D);
    netw::gd::scene_root()->add_child(body);
    const Ref<netw::NetwEntity> wrapper = netw::NetwEntity::ensure(body);
    REQUIRE(session->liveness_bind_route(51, wrapper.ptr()));
    REQUIRE(int(session->entity_frame_verdict(51)) == int(OK));

    PackedByteArray carried;
    carried.push_back(1);
    const int64_t sync_channels[] = {
        netw::wire::builtin_channel("SYNC"),
        netw::wire::builtin_channel("SYNC_ROW"),
        netw::wire::builtin_channel("SYNC_ROW_DELTA"),
        netw::wire::builtin_channel("SYNC_ROW_WINDOW"),
        netw::wire::builtin_channel("SYNC_DELTA"),
    };
    for (const int64_t channel : sync_channels) {
        NETW_CHECK_EQ(
            int(session->sync_admit_frame_default(
                2,
                51,
                0,
                channel,
                0,
                -1,
                carried
            )),
            int(OK)
        );
    }

    NETW_CHECK_EQ(
        int(session->sync_admit_frame_default(
            2,
            51,
            0,
            netw::wire::builtin_channel("SPAWN"),
            0,
            -1,
            carried
        )),
        int(ERR_INVALID_DATA)
    );
    NETW_CHECK_EQ(
        int(session->sync_admit_frame_default(
            2,
            51,
            0,
            netw::wire::builtin_channel("SYNC"),
            0,
            -1,
            PackedByteArray()
        )),
        int(ERR_INVALID_DATA)
    );
    NETW_CHECK_EQ(
        int(session->sync_admit_frame_default(
            2,
            52,
            0,
            netw::wire::builtin_channel("SPAWN"),
            0,
            -1,
            PackedByteArray()
        )),
        int(ERR_DOES_NOT_EXIST)
    );

    body->queue_free();
}

TEST_CASE(
    "[Networked][Session][Hosted] L7 an arriving frame is read against the "
    "clock once it is configured and against the frame counter before that, "
    "so a receiver always has a monotonic number and never a special case"
) {
    const Ref<NetwMultiplayer> session = hosting();
    CHECK_FALSE(session->clock_is_configured());
    NETW_CHECK_EQ(session->receive_tick(), session->get_frame_counter());

    session->advance_frame();
    session->advance_frame();
    NETW_CHECK_EQ(session->get_frame_counter(), 2);
    NETW_CHECK_EQ(session->receive_tick(), 2);

    session->clock_engine().set_configured(true);
    session->clock_engine().set_tick(41);

    NETW_CHECK_EQ(session->receive_tick(), 41);
    NETW_CHECK_EQ(session->get_frame_counter(), 2);

    session->advance_frame();
    NETW_CHECK_EQ(session->receive_tick(), 41);
}

} // namespace TestNetwSessionSimulationGate

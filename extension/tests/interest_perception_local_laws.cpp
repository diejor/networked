#include "support/netw_test.h"

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/node2d.hpp>

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_handle.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/interest/perception.hpp"
#include "support/entity_decl.h"
#include "support/loopback_rig.h"
#include "support/netw_call_log.h"

namespace TestNetwPerceptionLocal {

using namespace godot;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;
using netw::interest::Perception;
using netw_test::CallLog;
using netw_test::EntityDecl;
using netw_test::LoopbackRig;

constexpr int64_t HOST_PEER = 1;

struct Stealthed {
    NetwMultiplayer *core = nullptr;
    Ref<NetwEntity> entity;
    Node2D *owner = nullptr;
    AudioStreamPlayer *speaker = nullptr;
    Ref<NetwInterestLayer> layer;
};

Stealthed a_hidden_subject(
    LoopbackRig &p_rig,
    const char *p_name,
    int p_route
) {
    Stealthed made;
    made.core = Object::cast_to<NetwMultiplayer>(p_rig.server());
    made.core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    const RID handle
        = p_rig.declare_entity(EntityDecl().named(p_name).on_route(p_route));
    made.entity = made.core->entity_get_view(handle);
    made.owner = Object::cast_to<Node2D>(p_rig.node_of(handle));
    made.speaker = memnew(AudioStreamPlayer);
    made.speaker->set_name("Speaker");
    made.speaker->set_volume_db(-12.0);
    if (made.owner != nullptr) {
        made.owner->add_child(made.speaker);
    }
    made.layer = made.core->interest_layer(StringName("stealth"));
    return made;
}

TEST_CASE(
    "[Networked][Interest] IL5 hiding a copy locally takes back every "
    "channel it can be perceived through and restores each to the value it "
    "held, so a muted body that becomes visible again is as loud as it was "
    "rather than as loud as the default"
) {
    LoopbackRig rig;
    Stealthed made = a_hidden_subject(rig, "StealthSubject", 66);
    REQUIRE(made.owner != nullptr);
    REQUIRE(made.layer.is_valid());

    made.layer->add_entity(made.entity);
    rig.flush_interest();

    NETW_CHECK_EQ(
        made.core->interest_participant_sees(HOST_PEER, made.entity),
        false
    );
    NETW_CHECK_EQ(made.owner->is_visible(), false);
    NETW_CHECK_CLOSE(double(made.speaker->get_volume_db()), -80.0, 0.001);

    made.layer->add_viewer(HOST_PEER);
    rig.flush_interest();

    NETW_CHECK_EQ(
        made.core->interest_participant_sees(HOST_PEER, made.entity),
        true
    );
    NETW_CHECK_EQ(made.owner->is_visible(), true);
    NETW_CHECK_CLOSE(double(made.speaker->get_volume_db()), -12.0, 0.001);
}

TEST_CASE(
    "[Networked][Interest] IL9 a perception snapshot stops at a nested "
    "entity root, so a container hiding its subtree never records or "
    "restores a body that owns its own snapshot"
) {
    LoopbackRig rig;
    Stealthed outer = a_hidden_subject(rig, "OuterSubject", 70);
    REQUIRE(outer.owner != nullptr);
    REQUIRE(outer.layer.is_valid());

    const RID inner_handle
        = rig.declare_entity(EntityDecl().named("InnerSubject").on_route(71));
    const Ref<NetwEntity> inner_entity
        = outer.core->entity_get_view(inner_handle);
    Node2D *inner = Object::cast_to<Node2D>(rig.node_of(inner_handle));
    REQUIRE(inner != nullptr);
    if (inner->get_parent() != nullptr) {
        inner->get_parent()->remove_child(inner);
    }
    outer.owner->add_child(inner);

    outer.layer->add_entity(inner_entity);
    rig.flush_interest();
    NETW_CHECK_EQ(inner->is_visible(), false);

    outer.layer->add_entity(outer.entity);
    rig.flush_interest();
    NETW_CHECK_EQ(outer.owner->is_visible(), false);

    outer.layer->add_viewer(HOST_PEER);
    rig.flush_interest();

    NETW_CHECK_EQ(outer.owner->is_visible(), true);
    NETW_CHECK_EQ(inner->is_visible(), true);
}

TEST_CASE(
    "[Networked][Interest] IL6 a layer's declared perception is "
    "reapplied to the members it already holds, so an author who changes what "
    "an unadmitted copy looks like does not have to move admission to make it "
    "take"
) {
    LoopbackRig rig;
    Stealthed made = a_hidden_subject(rig, "ReapplySubject", 67);
    REQUIRE(made.owner != nullptr);
    REQUIRE(made.layer.is_valid());

    made.layer->add_entity(made.entity);
    rig.flush_interest();

    NETW_CHECK_EQ(
        made.core->interest_participant_sees(HOST_PEER, made.entity),
        false
    );
    NETW_CHECK_EQ(made.owner->is_visible(), false);

    made.layer->set_default_perception_policy(Perception::SHOW);

    NETW_CHECK_EQ(
        made.core->interest_participant_sees(HOST_PEER, made.entity),
        false
    );
    NETW_CHECK_EQ(made.owner->is_visible(), true);

    made.layer->set_default_perception_policy(Perception::HIDE);

    NETW_CHECK_EQ(
        made.core->interest_participant_sees(HOST_PEER, made.entity),
        false
    );
    NETW_CHECK_EQ(made.owner->is_visible(), false);
}

TEST_CASE(
    "[Networked][Interest] IL7 a declared perception action is run on "
    "both edges and is handed the layer it was declared for, so an author who "
    "replaces hiding with a fade is told when to start it and when to undo it"
) {
    LoopbackRig rig;
    Stealthed made = a_hidden_subject(rig, "CustomSubject", 68);
    REQUIRE(made.owner != nullptr);
    REQUIRE(made.layer.is_valid());
    CallLog heard;
    made.entity->get_interest()->on_perception_policy(
        StringName("stealth"),
        NetwMultiplayer::PERCEPTION_POLICY_CUSTOM,
        heard.callable("perceive")
    );

    made.layer->add_entity(made.entity);
    rig.flush_interest();

    NETW_CHECK_EQ(heard.count("perceive"), 1);
    NETW_CHECK_EQ(made.owner->is_visible(), true);
    const Array hiding = heard.args("perceive");
    NETW_CHECK_EQ(hiding.size(), 3);
    if (hiding.size() != 3) {
        return;
    }
    NETW_CHECK_EQ(bool(hiding[0]), false);
    NETW_CHECK_EQ(int64_t(hiding[1]), HOST_PEER);
    CHECK(StringName(hiding[2]) == StringName("stealth"));

    made.layer->add_viewer(HOST_PEER);
    rig.flush_interest();

    NETW_CHECK_EQ(heard.count("perceive"), 2);
    const Array showing = heard.args("perceive", 1);
    NETW_CHECK_EQ(showing.size(), 3);
    if (showing.size() != 3) {
        return;
    }
    NETW_CHECK_EQ(bool(showing[0]), true);
    NETW_CHECK_EQ(int64_t(showing[1]), HOST_PEER);
}

} // namespace TestNetwPerceptionLocal

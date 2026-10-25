#include "support/fault_log.h"
#include "support/level_spawn.h"
#include "support/mesh_stand.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/script.hpp>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/context.hpp"
#include "netw/api/display.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/scene.hpp"
#include "netw/api/session.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/call_args.hpp"
#include "netw/display/decl.hpp"
#include "netw/entity/control.hpp"
#include "netw/object_port.hpp"
#include "netw/property_set_builder.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"
#include "netw/wire/registry.hpp"
#include "netw/wire/stream.hpp"
#include "support/declared_nodes.h"
#include "support/minted_script.h"

namespace TestComposedWorld {

using namespace godot;
using namespace netw_test;
using netw::Netw;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwInterpolate;
using netw::NetwMultiplayer;
using netw::NetwPlayer;
using netw::NetwPropertyConfig;
using netw::NetwScene;
using netw::repl::PropertySet;
namespace property_set_builder = netw::property_set_builder;

constexpr int SERVER = 1;
constexpr int OTHER = 7;
constexpr int MEMBER = 9;
constexpr int LATECOMER = 11;

const char *LEVEL_PATH = "res://tests/support/declared_level.tscn";
const char *PLAYER_ID = "composed_player";
const char *INTERPOLATED_PLAYER_ID = "composed_interpolated_player";
const char *BODY_ID = "composed_body";

Node *build_player(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

Node *build_interpolated_player(const Variant &p_name) {
    Node *made = build_player(p_name);
    Node2D *visual = memnew(Node2D);
    visual->set_name("Visual");
    made->add_child(visual);
    Netw::configure_entity(made)->get_display()->set_visual_root(
        NodePath("Visual")
    );
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    const Ref<NetwPropertyConfig> config
        = Netw::configure_property(made, StringName("position"));
    config->interpolate(
        netw::gd::array_of(
            spec->lerp()->smooth(0.0)->to(StringName("position"))
        )
    );
    config->state();
    return made;
}

Node *build_body(const Variant &p_name) {
    Node *made = minted_node(netw_test::gdsrc::STATE_BODY);
    if (made == nullptr) {
        return nullptr;
    }
    made->set_name(String(p_name));
    return made;
}

Array one_string_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

void teach_player(NetwMultiplayer *p_session) {
    REQUIRE(p_session != nullptr);
    Array quantizers;
    quantizers.push_back(Variant());
    p_session->spawn_register_constructor(
        StringName(PLAYER_ID),
        callable_mp_static(&build_player),
        one_string_type(),
        quantizers
    );
    p_session->spawn_register_constructor(
        StringName(INTERPOLATED_PLAYER_ID),
        callable_mp_static(&build_interpolated_player),
        one_string_type(),
        quantizers
    );
    p_session->spawn_register_constructor(
        StringName(BODY_ID),
        callable_mp_static(&build_body),
        one_string_type(),
        quantizers
    );
}

constexpr int TICKRATE = 30;

void arm_clock(NetwMultiplayer *p_session) {
    REQUIRE(p_session != nullptr);
    Ref<netw::NetwClockConfig> config;
    config.instantiate();
    config->set("tickrate", TICKRATE);
    NETW_CHECK_EQ(int(p_session->clock_initialize(config)), int(OK));
    p_session->clock_engine().set_manual_tick(true);
}

Node *mount_under_root(const char *p_name) {
    Node *mount = memnew(Node);
    mount->set_name(StringName(p_name));
    netw::gd::scene_root()->add_child(mount);
    return mount;
}

void drop(Node *p_mount) {
    netw::gd::scene_root()->remove_child(p_mount);
    memdelete(p_mount);
}

RID first_scene(NetwMultiplayer *p_session) {
    const TypedArray<RID> held = p_session->scene_list();
    return held.is_empty() ? RID() : RID(held[0]);
}

void declare_state_stream(NetwMultiplayer *p_core, Node *p_node) {
    const Dictionary configs
        = netw::script::model::get_node_property_configs(p_node);
    netw::repl::SetBook &sets = p_core->property_set_book();
    const uint64_t declared = sets.create();
    PropertySet *set = sets.get(declared);
    REQUIRE(
        property_set_builder::from_property_configs(
            *set,
            configs,
            PropertySet::RECORD_STATE
        )
    );
    set->compile_against(p_node);
    NETW_CHECK_EQ(int(set->get_sealed()), 1);
    NETW_CHECK_EQ(
        int(p_core->sync_pipeline()->register_property_set(p_node, declared)),
        int(OK)
    );
    p_core->display_mark_dirty(
        NetwEntity::of(p_node)->get_rid_handle(),
        netw::display::DIRT_ROLE
    );
    p_core->session_flush_deferred();
}

int64_t display_role_of(NetwMultiplayer *p_core, Node *p_node) {
    return int64_t(p_core->display_get_track_stat(
        NetwEntity::of(p_node)->get_rid_handle(),
        StringName(),
        StringName("role")
    ));
}

struct ComposedWorld {
    const Ref<PackedScene> warm_level
        = netw::gd::load_scene(String(LEVEL_PATH));
    MeshStand stand;
    Node *held = nullptr;
    Node *at_seven = nullptr;
    Node *at_nine = nullptr;
    Node *at_late = nullptr;
    NetwMultiplayer *host = nullptr;
    NetwMultiplayer *seven = nullptr;
    NetwMultiplayer *nine = nullptr;
    NetwMultiplayer *late = nullptr;
    RID world;

    ComposedWorld() {
        stand.seat_server();
        stand.seat_member(OTHER);
        stand.seat_member(MEMBER);
        stand.wire(SERVER, OTHER);
        stand.wire(SERVER, MEMBER);
        stand.pump(4);

        host = stand.session_of(SERVER);
        seven = stand.session_of(OTHER);
        nine = stand.session_of(MEMBER);
        REQUIRE(host != nullptr);
        REQUIRE(seven != nullptr);
        REQUIRE(nine != nullptr);
        arm_clock(host);
        arm_clock(seven);
        arm_clock(nine);

        held = mount_under_root("CWHost");
        at_seven = mount_under_root("CWSeven");
        at_nine = mount_under_root("CWNine");
        stand.mount(SERVER, held);
        stand.mount(OTHER, at_seven);
        stand.mount(MEMBER, at_nine);
        NETW_CHECK_EQ(int(seven->embed_settle()), int(OK));
        NETW_CHECK_EQ(int(nine->embed_settle()), int(OK));

        NETW_CHECK_EQ(int(host->embed_settle()), int(OK));
        netw_test::register_level(host);
        netw_test::register_level(seven);
        netw_test::register_level(nine);
        REQUIRE(
            netw_test::spawn_level(host, String(LEVEL_PATH), false) != nullptr
        );
        stand.step_ticks(6);

        REQUIRE(stand.join(SERVER, StringName("one")).is_valid());
        REQUIRE(stand.join(OTHER, StringName("seven")).is_valid());
        REQUIRE(stand.join(MEMBER, StringName("nine")).is_valid());

        world = first_scene(host);
        const Ref<NetwScene> handle = host->scene_handle_of(world);
        REQUIRE(handle.is_valid());
        NETW_CHECK_EQ(int(handle->watch(host->player_of(OTHER))), int(OK));
        NETW_CHECK_EQ(int(handle->watch(host->player_of(MEMBER))), int(OK));
        stand.step_ticks(6);

        teach_player(host);
        teach_player(seven);
        teach_player(nine);
    }

    ComposedWorld(const ComposedWorld &) = delete;
    ComposedWorld &operator=(const ComposedWorld &) = delete;

    ~ComposedWorld() {
        if (at_late != nullptr) {
            drop(at_late);
        }
        drop(at_nine);
        drop(at_seven);
        drop(held);
    }

    Node *arena() const {
        return held->get_child_count() > 0 ? held->get_child(0) : nullptr;
    }

    RID hide_behind(const char *p_name, int p_only_viewer) {
        const Ref<NetwInterestLayer> layer = host->interest_layer(
            StringName(String("cw_seen_by_") + String::num_int64(p_only_viewer))
        );
        REQUIRE(layer.is_valid());
        NETW_CHECK_EQ(int(layer->add_viewer(p_only_viewer)), 1);
        const RID made = host->spawn_registered(
            StringName(PLAYER_ID),
            named(p_name),
            host->player_of(p_only_viewer).ptr()
        );
        REQUIRE(made.is_valid());
        Node *body = host->entity_get_node(made);
        REQUIRE(body != nullptr);
        NETW_CHECK_EQ(int(layer->add_entity(NetwEntity::of(body))), 1);
        host->interest_recompute();
        host->interest_commit();
        held->add_child(body);
        stand.step_ticks(10);
        return made;
    }

    void admit_latecomer() {
        stand.seat_member(LATECOMER);
        stand.wire(SERVER, LATECOMER);
        stand.pump(4);
        late = stand.session_of(LATECOMER);
        REQUIRE(late != nullptr);
        arm_clock(late);
        at_late = mount_under_root("CWLate");
        stand.mount(LATECOMER, at_late);
        NETW_CHECK_EQ(int(late->embed_settle()), int(OK));
        netw_test::register_level(late);
        teach_player(late);
        REQUIRE(stand.join(LATECOMER, StringName("eleven")).is_valid());
        const Ref<NetwScene> handle = host->scene_handle_of(world);
        REQUIRE(handle.is_valid());
        NETW_CHECK_EQ(int(handle->watch(host->player_of(LATECOMER))), int(OK));
        stand.step_ticks(10);
    }

    RID spawn_body_for(const char *p_name, int p_peer) {
        const RID made = host->spawn_registered(
            StringName(BODY_ID),
            named(p_name),
            host->player_of(p_peer).ptr()
        );
        REQUIRE(made.is_valid());
        Node *body = host->entity_get_node(made);
        REQUIRE(body != nullptr);
        Node *under = arena();
        REQUIRE(under != nullptr);
        under->add_child(body);
        stand.step_ticks(8);
        return made;
    }

    void settle(int p_rounds) {
        for (int round = 0; round < p_rounds; ++round) {
            stand.step_ticks(1);
            host->carrier_flush();
            seven->carrier_flush();
            nine->carrier_flush();
            stand.pump(2);
        }
    }

    RID spawn_for(
        const char *p_name,
        int p_peer,
        const char *p_constructor = PLAYER_ID
    ) {
        const RID made = host->spawn_registered(
            StringName(p_constructor),
            named(p_name),
            host->player_of(p_peer).ptr()
        );
        REQUIRE(made.is_valid());
        Node *body = host->entity_get_node(made);
        REQUIRE(body != nullptr);
        Node *under = arena();
        REQUIRE(under != nullptr);
        under->add_child(body);
        stand.step_ticks(8);
        return made;
    }
};

Node *route_node_at(NetwMultiplayer *p_session, int64_t p_route) {
    const Ref<NetwEntity> held = p_session->wrapper_for_route(p_route);
    return held.is_valid() ? held->get_owner() : nullptr;
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW1 the server admits two members, "
    "opens one world and publishes it to both, so the world every peer "
    "presents is the one the server holds"
) {
    ComposedWorld cw;

    NETW_CHECK_EQ(int(cw.host->is_host()), 1);
    NETW_CHECK_EQ(int(cw.host->is_server()), 1);
    NETW_CHECK_EQ(int(cw.seven->is_host()), 0);
    NETW_CHECK_EQ(int(cw.seven->is_server()), 0);
    NETW_CHECK_EQ(int(cw.nine->is_host()), 0);
    NETW_CHECK_EQ(int(cw.nine->is_server()), 0);

    NETW_CHECK_EQ(int(cw.host->get_connected_players().size()), 3);
    NETW_CHECK_EQ(int(cw.host->scene_list().size()), 1);
    NETW_CHECK_EQ(int(cw.seven->scene_list().size()), 1);
    NETW_CHECK_EQ(int(cw.nine->scene_list().size()), 1);
    NETW_CHECK_EQ(int(cw.host->scene_get_viewers(cw.world).size()), 2);

    NETW_CHECK_EQ(int(cw.at_seven->get_child_count()), 1);
    NETW_CHECK_EQ(int(cw.at_nine->get_child_count()), 1);
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW2 a player the server seats for the "
    "player at 9 reaches both members under one route, and each member "
    "displays it as the remote body while the server displays its own"
) {
    ComposedWorld cw;
    const RID seated
        = cw.spawn_for("NinePlayer", MEMBER, INTERPOLATED_PLAYER_ID);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);

    Node *at_host = cw.host->entity_get_node(seated);
    Node *at_seven = route_node_at(cw.seven, route);
    Node *at_nine = route_node_at(cw.nine, route);
    REQUIRE(at_host != nullptr);
    REQUIRE(at_seven != nullptr);
    REQUIRE(at_nine != nullptr);

    NETW_CHECK_EQ(int(cw.host->entity_get_peer(seated)), MEMBER);
    NETW_CHECK_EQ(int(NetwEntity::of(at_seven)->get_route()), int(route));
    NETW_CHECK_EQ(int(NetwEntity::of(at_nine)->get_route()), int(route));
    NETW_CHECK_EQ(int(NetwEntity::of(at_seven)->get_controller()), MEMBER);
    NETW_CHECK_EQ(int(NetwEntity::of(at_nine)->get_controller()), MEMBER);

    declare_state_stream(cw.host, at_host);
    declare_state_stream(cw.seven, at_seven);

    NETW_CHECK_EQ(
        int(cw.host->display_default_authors_streams(
            NetwEntity::of(at_host)->get_rid_handle()
        )),
        1
    );
    NETW_CHECK_EQ(
        int(cw.seven->display_default_authors_streams(
            NetwEntity::of(at_seven)->get_rid_handle()
        )),
        0
    );
    NETW_CHECK_EQ(
        int(display_role_of(cw.host, at_host)),
        int(NetwMultiplayer::DISPLAY_ROLE_AUTHORITY)
    );
    NETW_CHECK_EQ(
        int(display_role_of(cw.seven, at_seven)),
        int(NetwMultiplayer::DISPLAY_ROLE_REMOTE)
    );
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW4 the server computes the audience of "
    "each entity from the layer that entity is in, so an entity only 9 views "
    "is withheld from member 7 while the player every viewer of the world "
    "can see reaches both members"
) {
    ComposedWorld cw;
    const RID open = cw.spawn_for("OpenPlayer", MEMBER);
    const int64_t open_route = cw.host->entity_get_route(open);
    REQUIRE(open_route > 0);

    const RID for_seven = cw.hide_behind("SevenOnly", OTHER);
    const RID for_nine = cw.hide_behind("NineOnly", MEMBER);
    const int64_t seven_route = cw.host->entity_get_route(for_seven);
    const int64_t nine_route = cw.host->entity_get_route(for_nine);
    REQUIRE(seven_route > 0);
    REQUIRE(nine_route > 0);

    const Ref<NetwEntity> only_seven
        = NetwEntity::of(cw.host->entity_get_node(for_seven));
    const Ref<NetwEntity> only_nine
        = NetwEntity::of(cw.host->entity_get_node(for_nine));
    NETW_CHECK_EQ(int(cw.host->interest_membership_ids(for_seven).size()), 1);
    NETW_CHECK_EQ(int(cw.host->interest_membership_ids(for_nine).size()), 1);

    NETW_CHECK_EQ(int(cw.host->interest_wire_admits(OTHER, only_seven)), 1);
    NETW_CHECK_EQ(int(cw.host->interest_wire_admits(MEMBER, only_seven)), 0);
    NETW_CHECK_EQ(int(cw.host->interest_wire_admits(OTHER, only_nine)), 0);
    NETW_CHECK_EQ(int(cw.host->interest_wire_admits(MEMBER, only_nine)), 1);
    NETW_CHECK_EQ(int(cw.host->interest_wire_admits(SERVER, only_nine)), 1);

    NETW_CHECK_EQ(int(route_node_at(cw.seven, seven_route) != nullptr), 1);
    NETW_CHECK_EQ(int(route_node_at(cw.nine, seven_route) != nullptr), 0);
    NETW_CHECK_EQ(int(route_node_at(cw.seven, nine_route) != nullptr), 0);
    NETW_CHECK_EQ(int(route_node_at(cw.nine, nine_route) != nullptr), 1);

    NETW_CHECK_EQ(int(route_node_at(cw.seven, open_route) != nullptr), 1);
    NETW_CHECK_EQ(int(route_node_at(cw.nine, open_route) != nullptr), 1);
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW5 a call from 9 reaches the server "
    "carrying a node argument 9 holds, and an argument naming an entity 9 "
    "was never given arrives as nothing rather than as the server's own node"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][transport] An RPC argument is a node that is not "
        "spawned on the network, so it was sent as null.",
        1
    );

    ComposedWorld cw;
    const RID for_seven = cw.hide_behind("SevenOnly", OTHER);
    const RID for_nine = cw.hide_behind("NineOnly", MEMBER);
    const int64_t seven_route = cw.host->entity_get_route(for_seven);
    const int64_t nine_route = cw.host->entity_get_route(for_nine);
    REQUIRE(seven_route > 0);
    REQUIRE(nine_route > 0);

    Node *at_host = minted_node(netw_test::gdsrc::NODE_ARG_RPC_SINK);
    Node *at_member = minted_node(netw_test::gdsrc::NODE_ARG_RPC_SINK);
    REQUIRE(at_host != nullptr);
    REQUIRE(at_member != nullptr);
    at_host->set_name("CW5Sink");
    at_member->set_name("CW5Sink");
    cw.held->add_child(at_host);
    cw.at_nine->add_child(at_member);

    const RID seat = cw.host->entity_create();
    const int64_t sink_route = cw.host->entity_admit(seat);
    REQUIRE(sink_route > 0);
    NETW_CHECK_EQ(int(cw.host->entity_bind_node(seat, at_host)), int(OK));
    const RID mirror = cw.nine->entity_create();
    NETW_CHECK_EQ(int(cw.nine->entity_bind_route(mirror, sink_route)), int(OK));
    NETW_CHECK_EQ(int(cw.nine->entity_bind_node(mirror, at_member)), int(OK));
    cw.stand.pump(4);

    Array withheld;
    withheld.push_back(route_node_at(cw.nine, nine_route));
    withheld.push_back(51);
    cw.nine->rpc_call(
        Callable(at_member, StringName("receive_node")),
        withheld,
        SERVER
    );
    cw.stand.pump(8);
    NETW_CHECK_EQ(int(at_host->get(StringName("last_value"))), 51);
    NETW_CHECK_EQ(
        int(Object::cast_to<Node>(at_host->get(StringName("last_node")))
            == cw.host->entity_get_node(for_nine)),
        1
    );

    Array forbidden;
    forbidden.push_back(cw.host->entity_get_node(for_seven));
    forbidden.push_back(77);
    cw.nine->rpc_call(
        Callable(at_member, StringName("receive_node")),
        forbidden,
        SERVER
    );
    cw.stand.pump(8);
    NETW_CHECK_EQ(int(at_host->get(StringName("last_value"))), 77);
    NETW_CHECK_EQ(
        int(Object::cast_to<Node>(at_host->get(StringName("last_node")))
            == nullptr),
        1
    );
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW3 an identity a peer holds for a "
    "routed body does not move while the world runs on and a fourth peer "
    "joins, so a route names the same node it named before"
) {
    ComposedWorld cw;
    const RID seated = cw.spawn_for("NinePlayer", MEMBER);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);

    Node *at_host = cw.host->entity_get_node(seated);
    Node *at_nine = route_node_at(cw.nine, route);
    REQUIRE(at_host != nullptr);
    REQUIRE(at_nine != nullptr);
    const ObjectID host_object = netw::gd::instance_id(at_host);
    const ObjectID nine_object = netw::gd::instance_id(at_nine);
    const RID nine_entity = NetwEntity::of(at_nine)->get_rid_handle();

    cw.admit_latecomer();

    NETW_CHECK_EQ(int(cw.host->get_connected_players().size()), 4);
    NETW_CHECK_EQ(int(cw.host->entity_get_route(seated)), int(route));
    NETW_CHECK_EQ(
        int(netw::gd::instance_id(cw.host->entity_get_node(seated))
            == host_object),
        1
    );
    NETW_CHECK_EQ(
        int(netw::gd::instance_id(route_node_at(cw.nine, route))
            == nine_object),
        1
    );
    NETW_CHECK_EQ(
        int(NetwEntity::of(route_node_at(cw.nine, route))->get_rid_handle()
            == nine_entity),
        1
    );

    Node *at_late = route_node_at(cw.late, route);
    NETW_CHECK_EQ(int(at_late != nullptr), 1);
    NETW_CHECK_EQ(int(cw.late->scene_list().size()), 1);
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW6 a position the server authors on a "
    "routed body it owns and controls reaches member 7 as that value, and "
    "the member freezes that body kinematically for the display to drive "
    "while the server leaves its own body unfrozen with no display over it"
) {
    ComposedWorld cw;
    const RID seated = cw.spawn_body_for("ServerBody", SERVER);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);

    RigidBody2D *at_host
        = Object::cast_to<RigidBody2D>(cw.host->entity_get_node(seated));
    RigidBody2D *at_seven
        = Object::cast_to<RigidBody2D>(route_node_at(cw.seven, route));
    REQUIRE(at_host != nullptr);
    REQUIRE(at_seven != nullptr);

    NETW_CHECK_EQ(
        int(cw.host->display_default_authors_streams(
            NetwEntity::of(at_host)->get_rid_handle()
        )),
        1
    );
    NETW_CHECK_EQ(
        int(cw.seven->display_default_authors_streams(
            NetwEntity::of(at_seven)->get_rid_handle()
        )),
        0
    );

    netw::port_set(at_host, StringName("position"), Vector2(12.0, 34.0));
    cw.settle(8);
    cw.seven->display_pump(0.0);

    NETW_CHECK_CLOSE(double(at_seven->get_position().x), 12.0, 0.001);
    NETW_CHECK_CLOSE(double(at_seven->get_position().y), 34.0, 0.001);

    NETW_CHECK_EQ(
        int(display_role_of(cw.host, at_host)),
        int(NetwMultiplayer::DISPLAY_ROLE_DISABLED)
    );
    NETW_CHECK_EQ(
        int(display_role_of(cw.seven, at_seven)),
        int(NetwMultiplayer::DISPLAY_ROLE_REMOTE)
    );
    NETW_CHECK_EQ(int(at_host->is_freeze_enabled()), 0);
    NETW_CHECK_EQ(int(at_seven->is_freeze_enabled()), 1);
    NETW_CHECK_EQ(
        int(at_seven->get_freeze_mode()),
        int(RigidBody2D::FREEZE_MODE_KINEMATIC)
    );
}

struct ForgedCall {
    Node *sink = nullptr;
    int64_t sink_route = 0;
    Ref<Script> script;
    Ref<netw::NetwMemberConfig> options;
    Array arg_types;

    ForgedCall(ComposedWorld &p_world, const char *p_name) {
        sink = minted_node(netw_test::gdsrc::NODE_ARG_RPC_SINK);
        REQUIRE(sink != nullptr);
        sink->set_name(p_name);
        p_world.held->add_child(sink);

        const RID seat = p_world.host->entity_create();
        sink_route = p_world.host->entity_admit(seat);
        REQUIRE(sink_route > 0);
        NETW_CHECK_EQ(int(p_world.host->entity_bind_node(seat, sink)), int(OK));
        p_world.stand.pump(4);

        script = sink->get_script();
        REQUIRE(script.is_valid());
        options = netw::script::model::get_rpc_options(
            script,
            StringName("receive_node")
        );
        arg_types = netw::script::model::get_method_arg_types(
            script,
            StringName("receive_node")
        );
    }

    void deliver_naming(
        ComposedWorld &p_world,
        Node *p_argument,
        int p_value,
        int p_sender
    ) {
        const Ref<NetwEntity> wrapper = NetwEntity::of(sink);
        REQUIRE(wrapper.is_valid());

        LocalVector<netw::call_args::Slot> encoded;
        encoded.push_back(p_world.host->rpc_encoded_arg(p_argument));
        encoded.push_back(netw::call_args::of_value(p_value));
        NETW_CHECK_EQ(int(encoded[0].addresses_node), 1);

        netw::wire::WriteStream writer;
        uint64_t addresses_a_peer = 2;
        uint64_t arrives_at_server = uint64_t(SERVER);
        REQUIRE(writer.bits(addresses_a_peer, 8));
        REQUIRE(writer.bits(arrives_at_server, 32));
        REQUIRE(
            netw::script::model::write_call_body(
                writer,
                p_world.host->rpc_method_token(
                    wrapper,
                    sink,
                    StringName("receive_node")
                ),
                encoded,
                options.is_valid() ? options->get_quantizers() : Array(),
                arg_types
            )
        );
        REQUIRE(writer.align_verify());

        NETW_CHECK_EQ(
            int(p_world.host->receive_carrier(
                NetwMultiplayer::frame_pack(
                    sink_route,
                    0,
                    netw::wire::builtin_channel("CALL"),
                    writer.to_bytes(),
                    String()
                ),
                p_sender,
                true,
                -1,
                -1
            )),
            int(OK)
        );
        p_world.stand.pump(4);
    }
};

TEST_CASE(
    "[Networked][Session][SceneTree] CW7 a forged call frame from 9 naming a "
    "route only 7 may view leaves the server's node untouched, while the "
    "same frame naming a route 9 holds runs and resolves its argument"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][transport] Peer * sent a call naming a node it cannot see,"
        " so the call was dropped.",
        1
    );

    ComposedWorld cw;
    const RID for_seven = cw.hide_behind("SevenOnly", OTHER);
    const RID for_nine = cw.hide_behind("NineOnly", MEMBER);
    Node *only_seven_views = cw.host->entity_get_node(for_seven);
    Node *nine_views = cw.host->entity_get_node(for_nine);
    REQUIRE(only_seven_views != nullptr);
    REQUIRE(nine_views != nullptr);

    ForgedCall forged(cw, "CW7Sink");

    forged.deliver_naming(cw, nine_views, 51, MEMBER);
    NETW_CHECK_EQ(int(forged.sink->get(StringName("last_value"))), 51);
    NETW_CHECK_EQ(
        int(Object::cast_to<Node>(forged.sink->get(StringName("last_node")))
            == nine_views),
        1
    );

    forged.deliver_naming(cw, only_seven_views, 77, MEMBER);
    NETW_CHECK_EQ(int(forged.sink->get(StringName("last_value"))), 51);
    NETW_CHECK_EQ(
        int(Object::cast_to<Node>(forged.sink->get(StringName("last_node")))
            == nine_views),
        1
    );
}

Error deliver_control_apply(
    NetwMultiplayer *p_at,
    int64_t p_route,
    int64_t p_controller,
    int64_t p_sender
) {
    netw::session::ControlApply applied;
    applied.controller = uint64_t(p_controller);
    applied.revision = 1;
    applied.tenure_changed = true;
    return p_at->receive_carrier(
        NetwMultiplayer::frame_pack(
            p_route,
            0,
            netw::wire::builtin_channel("CONTROL_APPLY"),
            netw::session::frame_write(applied),
            String()
        ),
        p_sender,
        true,
        -1,
        -1
    );
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW8 a control frame forged by member 9 "
    "moves nothing at the server or at member 7, and the same frame from the "
    "server moves member 7's copy"
) {
    ComposedWorld cw;
    const RID seated = cw.spawn_for("Contested", OTHER);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);
    Node *at_host = cw.host->entity_get_node(seated);
    Node *at_seven = route_node_at(cw.seven, route);
    REQUIRE(at_host != nullptr);
    REQUIRE(at_seven != nullptr);
    const int64_t held_controller = NetwEntity::of(at_host)->get_controller();
    const int64_t held_authority = at_host->get_multiplayer_authority();
    NETW_REQUIRE_EQ(
        NetwEntity::of(at_seven)->get_controller(),
        held_controller
    );

    deliver_control_apply(cw.host, route, MEMBER, MEMBER);
    deliver_control_apply(cw.seven, route, MEMBER, MEMBER);

    NETW_CHECK_EQ(NetwEntity::of(at_host)->get_controller(), held_controller);
    NETW_CHECK_EQ(
        int64_t(at_host->get_multiplayer_authority()),
        held_authority
    );
    NETW_CHECK_EQ(NetwEntity::of(at_seven)->get_controller(), held_controller);

    deliver_control_apply(cw.seven, route, MEMBER, SERVER);

    NETW_CHECK_EQ(NetwEntity::of(at_seven)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(int64_t(at_seven->get_multiplayer_authority()), int64_t(1));
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW9 a control request from member 9 "
    "reaches the server, which grants it to every peer"
) {
    ComposedWorld cw;
    const RID seated = cw.spawn_for("Requested", OTHER);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);
    Node *at_host = cw.host->entity_get_node(seated);
    Node *at_seven = route_node_at(cw.seven, route);
    Node *at_nine = route_node_at(cw.nine, route);
    REQUIRE(at_host != nullptr);
    REQUIRE(at_seven != nullptr);
    REQUIRE(at_nine != nullptr);
    NetwEntity::of(at_host)->set_transfer(NetwEntity::TRANSFER_REQUESTABLE);

    NetwEntity::of(at_nine)->claim_control();
    cw.settle(12);

    NETW_CHECK_EQ(NetwEntity::of(at_host)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(NetwEntity::of(at_seven)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(NetwEntity::of(at_nine)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(int64_t(at_nine->get_multiplayer_authority()), int64_t(1));
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW10 the server transfers control to "
    "member 9, and member 7, which is not the server, is refused"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][entity] Condition \"!is_on_server()\" is true. "
        "Returning: ERR_UNAUTHORIZED. Only the server can give '*' "
        "to another peer. A client asks for it with "
        "NetwEntity.claim_control.",
        1
    );

    ComposedWorld cw;
    const RID seated = cw.spawn_for("Transferred", OTHER);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);
    Node *at_host = cw.host->entity_get_node(seated);
    Node *at_seven = route_node_at(cw.seven, route);
    Node *at_nine = route_node_at(cw.nine, route);
    REQUIRE(at_host != nullptr);
    REQUIRE(at_seven != nullptr);
    REQUIRE(at_nine != nullptr);

    NetwEntity::of(at_host)->set_controller(MEMBER);
    cw.settle(12);
    NetwEntity::of(at_seven)->set_controller(OTHER);
    cw.settle(12);

    NETW_CHECK_EQ(NetwEntity::of(at_host)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(NetwEntity::of(at_seven)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(NetwEntity::of(at_nine)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(int64_t(at_nine->get_multiplayer_authority()), int64_t(1));
}

} // namespace TestComposedWorld

#endif

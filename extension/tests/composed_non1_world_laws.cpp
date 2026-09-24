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
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/scene_handle.hpp"
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

namespace TestComposedNon1World {

using namespace godot;
using namespace netw_test;
using netw::Netw;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwInterpolate;
using netw::NetwMultiplayer;
using netw::NetwPlayer;
using netw::NetwPropertyConfig;
using netw::NetwPropertySet;
using netw::NetwSceneHandle;
namespace property_set_builder = netw::property_set_builder;

constexpr int COORDINATOR = 7;
constexpr int TRANSPORT_SERVER = 1;
constexpr int MEMBER = 9;
constexpr int LATECOMER = 11;

const char *LEVEL_PATH = "res://tests/support/declared_level.tscn";
const char *PLAYER_ID = "composed_player";
const char *BODY_ID = "composed_body";

Node *build_player(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
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

Node *a_declared_level() {
    const Ref<PackedScene> packed = netw::gd::load_scene(String(LEVEL_PATH));
    REQUIRE(packed.is_valid());
    Node *level = packed->instantiate();
    REQUIRE(level != nullptr);
    return level;
}

RID first_scene(NetwMultiplayer *p_session) {
    const TypedArray<RID> held = p_session->scene_list();
    return held.is_empty() ? RID() : RID(held[0]);
}

void declare_state_stream(NetwMultiplayer *p_core, Node *p_node) {
    Ref<NetwPropertyConfig> config
        = Netw::configure_property(p_node, StringName("position"), true);
    REQUIRE(config.is_valid());
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    config->interpolate(
        netw::gd::array_of(
            spec->lerp()->smooth(0.0)->to(StringName("position"))
        )
    );
    config->state();
    Dictionary configs;
    configs[StringName("position")] = config;
    const Ref<NetwPropertySet> set
        = property_set_builder::from_property_configs(
            configs,
            NetwPropertySet::RECORD_STATE
        );
    REQUIRE(set.is_valid());
    set->compile_against(p_node);
    NETW_CHECK_EQ(int(set->get_sealed()), 1);
    NETW_CHECK_EQ(
        int(p_core->sync_pipeline()->register_property_set(p_node, set)),
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
    MeshStand stand;
    Node *held = nullptr;
    Node *at_one = nullptr;
    Node *at_nine = nullptr;
    Node *at_late = nullptr;
    NetwMultiplayer *host = nullptr;
    NetwMultiplayer *one = nullptr;
    NetwMultiplayer *nine = nullptr;
    NetwMultiplayer *late = nullptr;
    RID world;

    ComposedWorld() {
        stand.seat_coordinator(COORDINATOR);
        stand.seat_member(TRANSPORT_SERVER);
        stand.seat_member(MEMBER);
        stand.wire(COORDINATOR, TRANSPORT_SERVER);
        stand.wire(COORDINATOR, MEMBER);
        stand.pump(4);

        host = stand.session_of(COORDINATOR);
        one = stand.session_of(TRANSPORT_SERVER);
        nine = stand.session_of(MEMBER);
        REQUIRE(host != nullptr);
        REQUIRE(one != nullptr);
        REQUIRE(nine != nullptr);
        arm_clock(host);
        arm_clock(one);
        arm_clock(nine);

        held = mount_under_root("CWHost");
        at_one = mount_under_root("CWOne");
        at_nine = mount_under_root("CWNine");
        stand.mount(COORDINATOR, held);
        stand.mount(TRANSPORT_SERVER, at_one);
        stand.mount(MEMBER, at_nine);
        NETW_CHECK_EQ(int(one->embed_settle()), int(OK));
        NETW_CHECK_EQ(int(nine->embed_settle()), int(OK));

        Node *level = a_declared_level();
        held->add_child(level);
        host->embed_offer_bare_level(level);
        NETW_CHECK_EQ(int(host->embed_settle()), int(OK));
        stand.step_ticks(6);

        REQUIRE(stand.join(COORDINATOR, StringName("seven")).is_valid());
        REQUIRE(stand.join(TRANSPORT_SERVER, StringName("one")).is_valid());
        REQUIRE(stand.join(MEMBER, StringName("nine")).is_valid());

        world = first_scene(host);
        const Ref<NetwSceneHandle> handle = host->scene_handle_of(world);
        REQUIRE(handle.is_valid());
        NETW_CHECK_EQ(
            int(handle->watch(host->player_of(TRANSPORT_SERVER))),
            int(OK)
        );
        NETW_CHECK_EQ(
            int(handle->watch(host->player_of(MEMBER))),
            int(OK)
        );
        stand.step_ticks(6);

        teach_player(host);
        teach_player(one);
        teach_player(nine);
    }

    ComposedWorld(const ComposedWorld &) = delete;
    ComposedWorld &operator=(const ComposedWorld &) = delete;

    ~ComposedWorld() {
        if (at_late != nullptr) {
            drop(at_late);
        }
        drop(at_nine);
        drop(at_one);
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
        stand.wire(COORDINATOR, LATECOMER);
        stand.pump(4);
        late = stand.session_of(LATECOMER);
        REQUIRE(late != nullptr);
        arm_clock(late);
        at_late = mount_under_root("CWLate");
        stand.mount(LATECOMER, at_late);
        NETW_CHECK_EQ(int(late->embed_settle()), int(OK));
        teach_player(late);
        REQUIRE(stand.join(LATECOMER, StringName("eleven")).is_valid());
        const Ref<NetwSceneHandle> handle = host->scene_handle_of(world);
        REQUIRE(handle.is_valid());
        NETW_CHECK_EQ(
            int(handle->watch(host->player_of(LATECOMER))),
            int(OK)
        );
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
            one->carrier_flush();
            nine->carrier_flush();
            stand.pump(2);
        }
    }

    RID spawn_for(const char *p_name, int p_peer) {
        const RID made = host->spawn_registered(
            StringName(PLAYER_ID),
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
    "[Networked][Session][SceneTree] CW1 a coordinator that is not transport "
    "peer 1 admits two members, adopts one world and publishes it to both, "
    "so the world every peer presents is the one peer 7 holds"
) {
    ComposedWorld cw;

    NETW_CHECK_EQ(int(cw.host->is_host()), 1);
    NETW_CHECK_EQ(int(cw.host->is_server()), 0);
    NETW_CHECK_EQ(int(cw.one->is_host()), 0);
    NETW_CHECK_EQ(int(cw.one->is_server()), 1);
    NETW_CHECK_EQ(int(cw.nine->is_host()), 0);
    NETW_CHECK_EQ(int(cw.nine->is_server()), 0);

    NETW_CHECK_EQ(int(cw.host->get_connected_players().size()), 3);
    NETW_CHECK_EQ(int(cw.host->scene_list().size()), 1);
    NETW_CHECK_EQ(int(cw.one->scene_list().size()), 1);
    NETW_CHECK_EQ(int(cw.nine->scene_list().size()), 1);
    NETW_CHECK_EQ(int(cw.host->scene_get_viewers(cw.world).size()), 2);

    NETW_CHECK_EQ(int(cw.at_one->get_child_count()), 1);
    NETW_CHECK_EQ(int(cw.at_nine->get_child_count()), 1);
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW2 a player coordinator 7 seats for "
    "the player at 9 reaches both members under one route, and each "
    "member displays it as the remote body while 7 displays its own"
) {
    ComposedWorld cw;
    const RID seated = cw.spawn_for("NinePlayer", MEMBER);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);

    Node *at_host = cw.host->entity_get_node(seated);
    Node *at_one = route_node_at(cw.one, route);
    Node *at_nine = route_node_at(cw.nine, route);
    REQUIRE(at_host != nullptr);
    REQUIRE(at_one != nullptr);
    REQUIRE(at_nine != nullptr);

    NETW_CHECK_EQ(int(cw.host->entity_get_peer(seated)), MEMBER);
    NETW_CHECK_EQ(int(NetwEntity::of(at_one)->get_route()), int(route));
    NETW_CHECK_EQ(int(NetwEntity::of(at_nine)->get_route()), int(route));
    NETW_CHECK_EQ(int(NetwEntity::of(at_one)->get_controller()), MEMBER);
    NETW_CHECK_EQ(int(NetwEntity::of(at_nine)->get_controller()), MEMBER);

    declare_state_stream(cw.host, at_host);
    declare_state_stream(cw.one, at_one);

    NETW_CHECK_EQ(
        int(cw.host->display_default_authors_streams(
            NetwEntity::of(at_host)->get_rid_handle()
        )),
        1
    );
    NETW_CHECK_EQ(
        int(cw.one->display_default_authors_streams(
            NetwEntity::of(at_one)->get_rid_handle()
        )),
        0
    );
    NETW_CHECK_EQ(
        int(display_role_of(cw.host, at_host)),
        int(NetwMultiplayer::DISPLAY_ROLE_AUTHORITY)
    );
    NETW_CHECK_EQ(
        int(display_role_of(cw.one, at_one)),
        int(NetwMultiplayer::DISPLAY_ROLE_REMOTE)
    );
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW4 coordinator 7 computes the audience "
    "of each entity from the layer that entity is in, so an entity only 9 "
    "views is withheld from transport peer 1 while the player every viewer "
    "of the world can see reaches both members"
) {
    ComposedWorld cw;
    const RID open = cw.spawn_for("OpenPlayer", MEMBER);
    const int64_t open_route = cw.host->entity_get_route(open);
    REQUIRE(open_route > 0);

    const RID for_one = cw.hide_behind("OneOnly", TRANSPORT_SERVER);
    const RID for_nine = cw.hide_behind("NineOnly", MEMBER);
    const int64_t one_route = cw.host->entity_get_route(for_one);
    const int64_t nine_route = cw.host->entity_get_route(for_nine);
    REQUIRE(one_route > 0);
    REQUIRE(nine_route > 0);

    const Ref<NetwEntity> only_one
        = NetwEntity::of(cw.host->entity_get_node(for_one));
    const Ref<NetwEntity> only_nine
        = NetwEntity::of(cw.host->entity_get_node(for_nine));
    NETW_CHECK_EQ(int(cw.host->interest_membership_ids(for_one).size()), 1);
    NETW_CHECK_EQ(int(cw.host->interest_membership_ids(for_nine).size()), 1);

    NETW_CHECK_EQ(
        int(cw.host->interest_wire_admits(TRANSPORT_SERVER, only_one)),
        1
    );
    NETW_CHECK_EQ(int(cw.host->interest_wire_admits(MEMBER, only_one)), 0);
    NETW_CHECK_EQ(
        int(cw.host->interest_wire_admits(TRANSPORT_SERVER, only_nine)),
        0
    );
    NETW_CHECK_EQ(int(cw.host->interest_wire_admits(MEMBER, only_nine)), 1);
    NETW_CHECK_EQ(
        int(cw.host->interest_wire_admits(COORDINATOR, only_nine)),
        1
    );

    NETW_CHECK_EQ(int(route_node_at(cw.one, one_route) != nullptr), 1);
    NETW_CHECK_EQ(int(route_node_at(cw.nine, one_route) != nullptr), 0);
    NETW_CHECK_EQ(int(route_node_at(cw.one, nine_route) != nullptr), 0);
    NETW_CHECK_EQ(int(route_node_at(cw.nine, nine_route) != nullptr), 1);

    NETW_CHECK_EQ(int(route_node_at(cw.one, open_route) != nullptr), 1);
    NETW_CHECK_EQ(int(route_node_at(cw.nine, open_route) != nullptr), 1);
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW5 a call from 9 reaches coordinator 7 "
    "carrying a node argument 9 holds, and an argument naming an entity 9 "
    "was never given arrives as nothing rather than as the coordinator's "
    "own node"
) {
    ComposedWorld cw;
    const RID for_one = cw.hide_behind("OneOnly", TRANSPORT_SERVER);
    const RID for_nine = cw.hide_behind("NineOnly", MEMBER);
    const int64_t one_route = cw.host->entity_get_route(for_one);
    const int64_t nine_route = cw.host->entity_get_route(for_nine);
    REQUIRE(one_route > 0);
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
        COORDINATOR
    );
    cw.stand.pump(8);
    NETW_CHECK_EQ(int(at_host->get(StringName("last_value"))), 51);
    NETW_CHECK_EQ(
        int(Object::cast_to<Node>(at_host->get(StringName("last_node")))
            == cw.host->entity_get_node(for_nine)),
        1
    );

    Array forbidden;
    forbidden.push_back(cw.host->entity_get_node(for_one));
    forbidden.push_back(77);
    cw.nine->rpc_call(
        Callable(at_member, StringName("receive_node")),
        forbidden,
        COORDINATOR
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
    "[Networked][Session][SceneTree] CW6 a position coordinator 7 authors on "
    "a routed body it owns and controls reaches member 1 as that value, and "
    "the member freezes that body kinematically for the display to drive "
    "while 7 leaves its own body unfrozen with no display over it"
) {
    ComposedWorld cw;
    const RID seated = cw.spawn_body_for("SevenBody", COORDINATOR);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);

    RigidBody2D *at_host
        = Object::cast_to<RigidBody2D>(cw.host->entity_get_node(seated));
    RigidBody2D *at_one
        = Object::cast_to<RigidBody2D>(route_node_at(cw.one, route));
    REQUIRE(at_host != nullptr);
    REQUIRE(at_one != nullptr);

    NETW_CHECK_EQ(
        int(cw.host->display_default_authors_streams(
            NetwEntity::of(at_host)->get_rid_handle()
        )),
        1
    );
    NETW_CHECK_EQ(
        int(cw.one->display_default_authors_streams(
            NetwEntity::of(at_one)->get_rid_handle()
        )),
        0
    );

    netw::port_set(at_host, StringName("position"), Vector2(12.0, 34.0));
    cw.settle(8);
    cw.one->display_pump(0.0);

    NETW_CHECK_CLOSE(double(at_one->get_position().x), 12.0, 0.001);
    NETW_CHECK_CLOSE(double(at_one->get_position().y), 34.0, 0.001);

    NETW_CHECK_EQ(
        int(display_role_of(cw.host, at_host)),
        int(NetwMultiplayer::DISPLAY_ROLE_DISABLED)
    );
    NETW_CHECK_EQ(
        int(display_role_of(cw.one, at_one)),
        int(NetwMultiplayer::DISPLAY_ROLE_REMOTE)
    );
    NETW_CHECK_EQ(int(at_host->is_freeze_enabled()), 0);
    NETW_CHECK_EQ(int(at_one->is_freeze_enabled()), 1);
    NETW_CHECK_EQ(
        int(at_one->get_freeze_mode()),
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
        NETW_CHECK_EQ(
            int(p_world.host->entity_bind_node(seat, sink)),
            int(OK)
        );
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
        uint64_t arrives_at_seven = uint64_t(COORDINATOR);
        REQUIRE(writer.bits(addresses_a_peer, 8));
        REQUIRE(writer.bits(arrives_at_seven, 32));
        REQUIRE(netw::script::model::write_call_body(
            writer,
            p_world.host->rpc_method_token(
                wrapper,
                sink,
                StringName("receive_node")
            ),
            encoded,
            options.is_valid() ? options->get_quantizers() : Array(),
            arg_types
        ));
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
    "route only 1 may view leaves coordinator 7's node untouched, while the "
    "same frame naming a route 9 holds runs and resolves its argument"
) {
    ComposedWorld cw;
    const RID for_one = cw.hide_behind("OneOnly", TRANSPORT_SERVER);
    const RID for_nine = cw.hide_behind("NineOnly", MEMBER);
    Node *only_one_views = cw.host->entity_get_node(for_one);
    Node *nine_views = cw.host->entity_get_node(for_nine);
    REQUIRE(only_one_views != nullptr);
    REQUIRE(nine_views != nullptr);

    ForgedCall forged(cw, "CW7Sink");

    forged.deliver_naming(cw, nine_views, 51, MEMBER);
    NETW_CHECK_EQ(int(forged.sink->get(StringName("last_value"))), 51);
    NETW_CHECK_EQ(
        int(Object::cast_to<Node>(forged.sink->get(StringName("last_node")))
            == nine_views),
        1
    );

    forged.deliver_naming(cw, only_one_views, 77, MEMBER);
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
    "moves nothing at coordinator 7 or at member 1, and the same frame from "
    "coordinator 7 moves member 1's copy"
) {
    ComposedWorld cw;
    const RID seated = cw.spawn_for("Contested", TRANSPORT_SERVER);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);
    Node *at_host = cw.host->entity_get_node(seated);
    Node *at_one = route_node_at(cw.one, route);
    REQUIRE(at_host != nullptr);
    REQUIRE(at_one != nullptr);
    const int64_t held_controller = NetwEntity::of(at_host)->get_controller();
    const int64_t held_authority = at_host->get_multiplayer_authority();
    NETW_REQUIRE_EQ(
        NetwEntity::of(at_one)->get_controller(),
        held_controller
    );

    deliver_control_apply(cw.host, route, MEMBER, MEMBER);
    deliver_control_apply(cw.one, route, MEMBER, MEMBER);

    NETW_CHECK_EQ(NetwEntity::of(at_host)->get_controller(), held_controller);
    NETW_CHECK_EQ(
        int64_t(at_host->get_multiplayer_authority()),
        held_authority
    );
    NETW_CHECK_EQ(NetwEntity::of(at_one)->get_controller(), held_controller);

    deliver_control_apply(cw.one, route, MEMBER, COORDINATOR);

    NETW_CHECK_EQ(NetwEntity::of(at_one)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(int64_t(at_one->get_multiplayer_authority()), int64_t(MEMBER));
}

TEST_CASE(
    "[Networked][Session][SceneTree] CW9 a control request from member 9 "
    "reaches coordinator 7, which grants it to every peer"
) {
    ComposedWorld cw;
    const RID seated = cw.spawn_for("Requested", TRANSPORT_SERVER);
    const int64_t route = cw.host->entity_get_route(seated);
    REQUIRE(route > 0);
    Node *at_host = cw.host->entity_get_node(seated);
    Node *at_one = route_node_at(cw.one, route);
    Node *at_nine = route_node_at(cw.nine, route);
    REQUIRE(at_host != nullptr);
    REQUIRE(at_one != nullptr);
    REQUIRE(at_nine != nullptr);
    NetwEntity::of(at_host)->set_transfer(NetwEntity::TRANSFER_REQUESTABLE);

    NetwEntity::of(at_nine)->claim_authority();
    cw.settle(12);

    NETW_CHECK_EQ(NetwEntity::of(at_host)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(NetwEntity::of(at_one)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(NetwEntity::of(at_nine)->get_controller(), int64_t(MEMBER));
    NETW_CHECK_EQ(
        int64_t(at_nine->get_multiplayer_authority()),
        int64_t(MEMBER)
    );
}

} // namespace TestComposedNon1World

#endif

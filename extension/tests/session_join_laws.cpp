#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

#include "godot/multiplayer.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "netw/api/join_config.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/resolved_join.hpp"
#include "netw/session_decl.hpp"
#include "support/declared_seams.h"
#include "support/loopback_rig.h"
#include "support/minted_script.h"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwSessionJoinLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;
using netw_test::EntityDecl;
using netw_test::LoopbackRig;

Ref<netw::NetwParticipant> participant_of(NetwMultiplayer *p_api, int p_peer) {
    return p_api->peer_get_participant(p_peer);
}

TEST_CASE(
    "[Networked][Session] a connected peer holds no participant until "
    "it joins"
) {
    LoopbackRig rig(1);
    rig.pump(4);

    CHECK(rig.client(0)->participant_local().is_null());
    CHECK(participant_of(rig.server(), rig.peer_id(0)).is_null());

    Ref<netw::NetwParticipant> seated = rig.join(0, StringName("alice"));

    CHECK(seated.is_valid());
    CHECK(participant_of(rig.server(), rig.peer_id(0)).is_valid());
}

TEST_CASE(
    "[Networked][Session] a host joins itself and is seated the same "
    "way a client is"
) {
    LoopbackRig rig(1);
    rig.pump(4);

    Ref<netw::NetwParticipant> seated = rig.join(-1, StringName("host"));

    CHECK(seated.is_valid());
    CHECK(participant_of(rig.server(), 1).is_valid());
    CHECK(rig.client(0)->participant_local().is_null());
}

TEST_CASE(
    "[Networked][Session] every joined peer reaches every peer's "
    "roster"
) {
    LoopbackRig rig(2);
    rig.pump(4);

    rig.join(-1, StringName("host"));
    rig.join(0, StringName("alice"));
    rig.join(1, StringName("bob"));
    rig.pump(4);

    for (int at = -1; at < 2; ++at) {
        NetwMultiplayer *api = at < 0 ? rig.server() : rig.client(at);
        CHECK(participant_of(api, 1).is_valid());
        CHECK(participant_of(api, rig.peer_id(0)).is_valid());
        CHECK(participant_of(api, rig.peer_id(1)).is_valid());
    }
}

TEST_CASE(
    "[Networked][Session] a joined peer's participant carries the name "
    "its payload named"
) {
    LoopbackRig rig(1);
    rig.pump(4);

    Ref<netw::NetwParticipant> seated = rig.join(0, StringName("alice"));

    REQUIRE(seated.is_valid());
    CHECK(seated->get_username() == StringName("alice"));
}

TEST_CASE(
    "[Networked][Session] a declared scene is not a live one until it "
    "enters"
) {
    LoopbackRig rig(1);
    rig.declare_scene(StringName("Arena"));
    rig.pump(4);

    NetwMultiplayer *scenes = rig.server();
    REQUIRE(scenes != nullptr);
    NETW_CHECK_EQ(int(scenes->scene_list().size()), 0);

    rig.enter_scene(StringName("Arena"));

    NETW_CHECK_EQ(int(scenes->scene_list().size()), 1);
    CHECK(scenes->scene_find(StringName("Arena")).is_valid());
}

TEST_CASE(
    "[Networked][Session] two instances of one stem are two live "
    "scenes and the stem answers with the later one"
) {
    LoopbackRig rig(1);
    rig.declare_scene(StringName("first"), StringName("Arena"));
    rig.declare_scene(StringName("second"), StringName("Arena"));
    rig.pump(4);

    rig.enter_scene(StringName("first"));
    rig.enter_scene(StringName("second"));

    NetwMultiplayer *scenes = rig.server();
    REQUIRE(scenes != nullptr);
    NETW_CHECK_EQ(int(scenes->scene_list().size()), 2);
    NETW_CHECK_EQ(
        int(rig.server()->scene_find_all(StringName("Arena")).size()),
        2
    );
    CHECK(
        scenes->scene_find(StringName("Arena"))
        == rig.entity_of(StringName("second"))
    );
}

TEST_CASE(
    "[Networked][Session] a client mirrors a scene without being "
    "admitted to it, and holds no seat there"
) {
    LoopbackRig rig(1);
    rig.declare_scene(StringName("Arena"));
    rig.pump(4);
    rig.enter_scene(StringName("Arena"));
    rig.join(-1, StringName("host"));
    rig.join(0, StringName("alice"));
    rig.mirror_scene(0, StringName("Arena"));
    rig.pump(8);

    NetwMultiplayer *client_scenes = rig.client(0);
    REQUIRE(client_scenes != nullptr);
    const Ref<netw::NetwParticipant> seated
        = rig.client(0)->participant_local();
    REQUIRE(seated.is_valid());

    NETW_CHECK_EQ(int(client_scenes->scene_list().size()), 1);
    CHECK(client_scenes->scene_find(StringName("Arena")).is_valid());
    CHECK(seated->get_current_scene().is_null());
}

TEST_CASE(
    "[Networked][Session] a client declares a scene only for a route the "
    "server admitted it"
) {
    LoopbackRig rig(1);
    rig.declare_scene(StringName("Arena"));
    rig.pump(4);
    rig.enter_scene(StringName("Arena"));
    rig.join(-1, StringName("host"));
    rig.join(0, StringName("alice"));

    netw::NetwMultiplayer *api = rig.client(0);
    const RID invented = api->entity_create();
    REQUIRE(invented.is_valid());
    NETW_CHECK_EQ(int(api->scene_declare(invented)), int(ERR_UNAUTHORIZED));
    CHECK_FALSE(api->scene_is_declared(invented));

    const RID mirror = rig.mirror_scene(0, StringName("Arena"));
    CHECK(api->scene_is_declared(mirror));
}

TEST_CASE(
    "[Networked][Session] a mirrored scene is the server's scene, not a "
    "second one"
) {
    LoopbackRig rig(1);
    rig.declare_scene(StringName("Arena"));
    rig.pump(4);
    rig.enter_scene(StringName("Arena"));

    const RID origin = rig.entity_of(StringName("Arena"));
    const RID mirror = rig.mirror_scene(0, StringName("Arena"));

    NETW_CHECK_EQ(
        int(rig.client(0)->entity_get_route(mirror)),
        int(rig.server()->entity_get_route(origin))
    );
}

TEST_CASE(
    "[Networked][Session] the session's player roster is every live "
    "scene's seated players and nothing else, so an entity carrying no "
    "peer is not a player and a player in a second scene is not lost"
) {
    LoopbackRig rig(1);
    rig.mount();
    rig.declare_scene(StringName("Arena"));
    rig.declare_scene(StringName("Annex"));
    Node *arena_container = rig.node_of(rig.entity_of(StringName("Arena")));
    Node *annex_container = rig.node_of(rig.entity_of(StringName("Annex")));
    REQUIRE(arena_container != nullptr);
    REQUIRE(annex_container != nullptr);
    rig.branch(-1)->add_child(arena_container);
    rig.branch(-1)->add_child(annex_container);
    rig.pump(4);
    rig.enter_scene(StringName("Arena"));
    rig.enter_scene(StringName("Annex"));

    netw::NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);
    NETW_CHECK_EQ(int(core->scene_list().size()), 2);
    CHECK(core->scene_players_all().is_empty());

    const RID pawn
        = rig.declare_entity(EntityDecl().named("Pawn").on_route(71));
    const RID crate
        = rig.declare_entity(EntityDecl().named("Crate").on_route(72));
    const RID guard
        = rig.declare_entity(EntityDecl().named("Guard").on_route(73));
    rig.seat(pawn, rig.entity_of(StringName("Arena")));
    rig.seat(crate, rig.entity_of(StringName("Arena")));
    rig.seat(guard, rig.entity_of(StringName("Annex")));

    const Ref<netw::NetwEntity> seated_pawn
        = netw::NetwEntity::of(rig.node_of(pawn));
    const Ref<netw::NetwEntity> seated_guard
        = netw::NetwEntity::of(rig.node_of(guard));
    REQUIRE(seated_pawn.is_valid());
    REQUIRE(seated_guard.is_valid());
    seated_pawn->set_peer_id(rig.peer_id(0));
    seated_guard->set_peer_id(1);

    const TypedArray<netw::NetwEntity> roster = core->scene_players_all();

    NETW_CHECK_EQ(int(roster.size()), 2);
    CHECK(roster.has(seated_pawn));
    CHECK(roster.has(seated_guard));
    CHECK_FALSE(roster.has(netw::NetwEntity::of(rig.node_of(crate))));

    rig.branch(-1)->remove_child(arena_container);
    rig.branch(-1)->remove_child(annex_container);
}

TEST_CASE(
    "[Networked][Session] a per-session join override answers only as a "
    "pair, so wire-arg quantizers never travel without the handler they "
    "were measured against"
) {
    LoopbackRig rig(0);
    netw::NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);

    CHECK_FALSE(core->session_join_override().is_valid());
    NETW_CHECK_EQ(int(core->session_join_override_quantizers().size()), 0);

    Array quantizers;
    quantizers.push_back(StringName("angle"));
    core->session_set_join_override(
        Callable(core, StringName("session_transition")),
        quantizers
    );

    CHECK(core->session_join_override().is_valid());
    NETW_CHECK_EQ(int(core->session_join_override_quantizers().size()), 1);
    CHECK(
        StringName(core->session_join_override_quantizers()[0])
        == StringName("angle")
    );

    SUBCASE("and an invalid handler withdraws the quantizers with it") {
        core->session_set_join_override(Callable(), quantizers);

        CHECK_FALSE(core->session_join_override().is_valid());
        NETW_CHECK_EQ(int(core->session_join_override_quantizers().size()), 0);
    }
}

TEST_CASE(
    "[Networked][Session] a join handler seats its synchronous answer before "
    "the announcement and its suspended answer after the announcement"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    const RID arena = core->entity_create();
    REQUIRE(arena.is_valid());
    NETW_CHECK_EQ(int(core->scene_declare(arena)), int(OK));
    Node *arena_node = memnew(Node);
    NETW_CHECK_GT(core->entity_admit(arena), 0);
    NETW_CHECK_EQ(int(core->entity_bind_node(arena, arena_node)), int(OK));
    CHECK(core->scene_of(core->entity_of(arena_node)) == arena);
    const Ref<Script> script
        = netw_test::script_from(netw_test::gdsrc::JOIN_HANDLERS);
    REQUIRE(script.is_valid());
    const Ref<RefCounted> handler = script->call("new");
    REQUIRE(handler.is_valid());
    CallLog announced;
    core->connect(
        StringName("participant_joined"),
        announced.callable("joined")
    );

    Ref<netw::ResolvedJoin> immediate;
    immediate.instantiate();
    immediate->set_peer_id(7);
    immediate->set_username(StringName("now"));
    Array immediate_args;
    immediate_args.push_back(arena_node);
    immediate->set_arg_values(immediate_args);
    core->session_set_join_override(
        Callable(handler.ptr(), StringName("immediate")),
        Array()
    );

    core->session_admit(immediate);

    NETW_CHECK_EQ(announced.count("joined"), 1);
    CHECK(core->participant_seat(7) == arena);

    Ref<netw::ResolvedJoin> suspended;
    suspended.instantiate();
    suspended->set_peer_id(8);
    suspended->set_username(StringName("later"));
    Array suspended_args;
    suspended_args.push_back(arena_node);
    suspended->set_arg_values(suspended_args);
    core->session_set_join_override(
        Callable(handler.ptr(), StringName("suspended")),
        Array()
    );

    core->session_admit(suspended);

    NETW_CHECK_EQ(announced.count("joined"), 2);
    CHECK_FALSE(core->participant_seat(8).is_valid());

    handler->emit_signal(StringName("released"), arena_node);

    CHECK(core->participant_seat(8) == arena);
    NETW_CHECK_EQ(int(handler->get(StringName("calls"))), 2);

    memdelete(arena_node);
}

TEST_CASE(
    "[Networked][Session] a join's typed arguments survive the wire and a "
    "schema the two ends disagree on is refused, so a server never reads a "
    "client's bytes against a handler signature they were not measured for"
) {
    const Ref<Script> shape
        = netw::gd::instantiate_class(StringName("GDScript"));
    shape->set_source_code(String(
        "extends RefCounted\nfunc handle(a: StringName, b: int) -> void:\n"
        "\tpass\n"
    ));
    shape->reload();
    const Ref<RefCounted> holder = shape->call("new");
    REQUIRE(holder.is_valid());

    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_join_override(
        Callable(holder.ptr(), StringName("handle")),
        Array()
    );

    netw::JoinRequest request;
    request.username = StringName("ada");
    Array claimed;
    claimed.push_back(StringName("north"));
    claimed.push_back(3);
    request.arg_values = claimed;
    core->session_encode_join_args(request);
    NETW_CHECK_EQ(int(request.arg_bytes.is_empty()), 0);

    netw::JoinRequest wire;
    wire.deserialize(request.serialize());
    NETW_CHECK_EQ(int(core->session_decode_join_args(wire)), 1);
    CHECK(wire.arg_values == claimed);

    SUBCASE("a schema the ends disagree on decodes nothing") {
        netw::JoinRequest drifted;
        drifted.deserialize(request.serialize());
        drifted.schema_hash += 1;
        NETW_CHECK_EQ(int(core->session_decode_join_args(drifted)), 0);
    }

    core->embed_dispose();
}

Ref<Script> shape_of(const char *p_source) {
    const Ref<Script> shape
        = netw::gd::instantiate_class(StringName("GDScript"));
    shape->set_source_code(String(p_source));
    shape->reload();
    return shape;
}

TEST_CASE(
    "[Networked][Session] a static join handler carries the wire schema its "
    "own signature declares, because the script a Callable declares is the "
    "Callable's object when that object is itself a script"
) {
    const Ref<Script> shape = shape_of(
        "extends RefCounted\n"
        "static func handle(_rj, a: StringName, b: int) -> void:\n"
        "\tpass\n"
    );
    REQUIRE(shape.is_valid());

    const Callable statically = Callable(shape.ptr(), StringName("handle"));
    const Array declared = NetwMultiplayer::join_arg_types(statically);

    Array typed;
    typed.push_back(int(Variant::STRING_NAME));
    typed.push_back(int(Variant::INT));

    CHECK(declared == typed);

    SUBCASE("and the same signature on an instance reads the same schema") {
        const Ref<Script> twin = shape_of(
            "extends RefCounted\n"
            "func handle(_rj, a: StringName, b: int) -> void:\n"
            "\tpass\n"
        );
        const Ref<RefCounted> holder = twin->call("new");
        REQUIRE(holder.is_valid());

        CHECK(
            NetwMultiplayer::join_arg_types(
                Callable(holder.ptr(), StringName("handle"))
            )
            == declared
        );
    }
}

TEST_CASE(
    "[Networked][Session] a join handler whose signature declares no wire "
    "argument runs on the resolved join alone, so a handler is skipped for "
    "carrying nothing only when it asked to be carried something"
) {
    const Ref<Script> shape = shape_of(
        "extends RefCounted\n"
        "var seated := 0\n"
        "func seat(_rj) -> void:\n"
        "\tseated += 1\n"
        "func seat_with(_rj, _where: StringName) -> void:\n"
        "\tseated += 1\n"
    );
    REQUIRE(shape.is_valid());
    const Ref<RefCounted> handler = shape->call("new");
    REQUIRE(handler.is_valid());

    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    core->session_set_join_override(
        Callable(handler.ptr(), StringName("seat")),
        Array()
    );

    Ref<netw::ResolvedJoin> bare;
    bare.instantiate();
    bare->set_peer_id(11);
    bare->set_username(StringName("ada"));
    core->session_admit(bare);

    NETW_CHECK_EQ(int(handler->get(StringName("seated"))), 1);

    SUBCASE("a handler that declared one stays skipped when none arrived") {
        core->session_set_join_override(
            Callable(handler.ptr(), StringName("seat_with")),
            Array()
        );

        Ref<netw::ResolvedJoin> starved;
        starved.instantiate();
        starved->set_peer_id(12);
        starved->set_username(StringName("grace"));
        core->session_admit(starved);

        NETW_CHECK_EQ(int(handler->get(StringName("seated"))), 1);
    }

    core->embed_dispose();
}

Ref<NetwMultiplayer> branch_session(Node *p_branch) {
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(p_branch->get_path());
    Ref<NetwMultiplayer> made;
    made.instantiate();
    made->session_set_inner(inner);
    made->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    netw::gd::scene_tree()->set_multiplayer(made, p_branch->get_path());
    return made;
}

void release_branch(Node *p_branch) {
    netw::gd::scene_tree()->set_multiplayer(
        Ref<MultiplayerAPI>(),
        p_branch->get_path()
    );
    netw::gd::scene_root()->remove_child(p_branch);
    memdelete(p_branch);
}

Node *mounted_branch(const char *p_name) {
    Node *branch = memnew(Node);
    branch->set_name(StringName(p_name));
    netw::gd::scene_root()->add_child(branch);
    return branch;
}

RID a_declared_scene(
    const Ref<NetwMultiplayer> &p_api,
    Node *p_parent,
    const char *p_stem
) {
    const RID scene = p_api->entity_create();
    REQUIRE(scene.is_valid());
    NETW_CHECK_EQ(int(p_api->scene_declare(scene)), int(OK));
    Node *level = memnew(Node);
    level->set_name(StringName(p_stem));
    p_parent->add_child(level);
    NETW_CHECK_GT(int(p_api->entity_admit(scene)), 0);
    NETW_CHECK_EQ(int(p_api->entity_bind_node(scene, level)), int(OK));
    CHECK(p_api->scene_is_declared(scene));
    return scene;
}

void admit_one(const Ref<NetwMultiplayer> &p_api, int p_peer) {
    Ref<netw::ResolvedJoin> joining;
    joining.instantiate();
    joining->set_peer_id(p_peer);
    joining->set_username(StringName("ada"));
    p_api->session_admit(joining);
}

TEST_CASE(
    "[Networked][SceneTree] a join handler is declared on a node and governs "
    "only the session that node belongs to, so two sessions running one "
    "script each seat through their own node rather than through whichever "
    "declared last"
) {
    const Ref<Script> shape = shape_of(
        "extends Node\n"
        "var seated := 0\n"
        "func seat(_rj) -> void:\n"
        "\tseated += 1\n"
    );
    REQUIRE(shape.is_valid());

    Node *first_branch = mounted_branch("JoinScopeFirst");
    Node *second_branch = mounted_branch("JoinScopeSecond");
    Node *first = Object::cast_to<Node>(shape->call("new"));
    Node *second = Object::cast_to<Node>(shape->call("new"));
    const bool built = first != nullptr && second != nullptr;
    REQUIRE(built);
    first_branch->add_child(first);
    second_branch->add_child(second);

    const Ref<NetwMultiplayer> first_api = branch_session(first_branch);
    const Ref<NetwMultiplayer> second_api = branch_session(second_branch);

    netw::Netw::configure_join(first, Callable(first, StringName("seat")));
    netw::Netw::configure_join(second, Callable(second, StringName("seat")));

    admit_one(first_api, 21);
    admit_one(second_api, 22);

    NETW_CHECK_EQ(int(first->get(StringName("seated"))), 1);
    NETW_CHECK_EQ(int(second->get(StringName("seated"))), 1);

    first_api->embed_dispose();
    second_api->embed_dispose();
    release_branch(first_branch);
    release_branch(second_branch);
}

TEST_CASE(
    "[Networked][SceneTree] Netw.join routes on the state its session is "
    "already in, so a caller never chooses between preparing and submitting "
    "and cannot choose wrong: offline it prepares, which is what holds the "
    "join for the authentication phase to carry, and online it submits at once"
) {
    Node *branch = mounted_branch("JoinFacade");
    Node *seat = memnew(Node);
    branch->add_child(seat);
    const Ref<NetwMultiplayer> api = branch_session(branch);

    CallLog log;
    api->connect(
        StringName("session_join_submitted"),
        log.callable("submitted")
    );

    SUBCASE("an offline session prepares, and nothing goes out yet") {
        const Ref<netw::NetwPromise> asked
            = netw::Netw::join(seat, StringName("ana"), Array());
        REQUIRE(asked.is_valid());
        NETW_CHECK_EQ(int(asked->get_result()), int(OK));
        REQUIRE(api->session_prepared_join().has_value());
        CHECK(api->session_prepared_join()->username == StringName("ana"));
        NETW_CHECK_EQ(log.count("submitted"), 0);
    }

    SUBCASE("an online session submits at once and holds nothing back") {
        api->session_set_state(NetwMultiplayer::SESSION_STATE_ONLINE);
        const Ref<netw::NetwPromise> asked
            = netw::Netw::join(seat, StringName("ana"), Array());
        REQUIRE(asked.is_valid());
        NETW_CHECK_EQ(int(asked->get_result()), int(OK));
        NETW_CHECK_EQ(log.count("submitted"), 1);
        CHECK_FALSE(api->session_prepared_join().has_value());
    }

    SUBCASE("an empty username joins nothing on either route") {
        NETW_CHECK_EQ(
            int(netw::Netw::join(seat, StringName(), Array())->get_result()),
            int(ERR_INVALID_PARAMETER)
        );
        api->session_set_state(NetwMultiplayer::SESSION_STATE_ONLINE);
        NETW_CHECK_EQ(
            int(netw::Netw::join(seat, StringName(), Array())->get_result()),
            int(ERR_INVALID_PARAMETER)
        );
        NETW_CHECK_EQ(log.count("submitted"), 0);
        CHECK_FALSE(api->session_prepared_join().has_value());
    }

    api->embed_dispose();
    release_branch(branch);
}

TEST_CASE(
    "[Networked][SceneTree] a session whose declared join handler left its "
    "branch refuses the placement rather than falling through to the "
    "built-in one, which would seat the player somewhere the game never "
    "chose"
) {
    const Ref<Script> shape = shape_of(
        "extends Node\n"
        "var seated := 0\n"
        "func seat(_rj) -> void:\n"
        "\tseated += 1\n"
    );
    REQUIRE(shape.is_valid());

    Node *branch = mounted_branch("JoinScopeLost");
    Node *carrier = Object::cast_to<Node>(shape->call("new"));
    REQUIRE(carrier != nullptr);
    branch->add_child(carrier);
    const Ref<NetwMultiplayer> api = branch_session(branch);
    netw::Netw::configure_join(carrier, Callable(carrier, StringName("seat")));

    admit_one(api, 31);
    NETW_CHECK_EQ(int(carrier->get(StringName("seated"))), 1);

    branch->remove_child(carrier);

    CHECK_FALSE(api->session_resolve_join().available);
    CHECK_FALSE(api->session_resolve_join().handler.is_valid());
    CHECK(api->session_get_join_schema().is_empty());

    branch->add_child(carrier);
    CHECK(api->session_resolve_join().available);
    CHECK(api->session_resolve_join().handler.is_valid());

    memdelete(carrier);
    api->embed_dispose();
    release_branch(branch);
}

TEST_CASE(
    "[Networked][SceneTree] a session with no declared join handler resolves "
    "the built-in one, which is what keeps an unconfigured game playable"
) {
    Node *branch = mounted_branch("JoinScopeAbsent");
    const Ref<NetwMultiplayer> api = branch_session(branch);

    const NetwMultiplayer::JoinPlan plan = api->session_resolve_join();
    CHECK(plan.available);
    CHECK(plan.handler.is_valid());
    CHECK_FALSE(plan.declared);
    CHECK(plan.quantizers.is_empty());

    api->embed_dispose();
    release_branch(branch);
}

TEST_CASE(
    "[Networked][SceneTree] two live join declarations on one session refuse "
    "the join, because picking the later entrant would seat a player through "
    "a policy the game did not choose, and clearing one restores the answer"
) {
    const Ref<Script> shape = shape_of(
        "extends Node\n"
        "var seated := 0\n"
        "func seat(_rj) -> void:\n"
        "\tseated += 1\n"
    );
    REQUIRE(shape.is_valid());

    Node *branch = mounted_branch("JoinScopeRival");
    Node *first = Object::cast_to<Node>(shape->call("new"));
    Node *second = Object::cast_to<Node>(shape->call("new"));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    branch->add_child(first);
    branch->add_child(second);
    const Ref<NetwMultiplayer> api = branch_session(branch);

    netw::Netw::configure_join(first, Callable(first, StringName("seat")));
    netw::Netw::configure_join(second, Callable(second, StringName("seat")));

    admit_one(api, 41);
    NETW_CHECK_EQ(int(first->get(StringName("seated"))), 0);
    NETW_CHECK_EQ(int(second->get(StringName("seated"))), 0);

    CHECK(netw::Netw::configure_join(second, Callable()).is_null());

    admit_one(api, 42);
    NETW_CHECK_EQ(int(first->get(StringName("seated"))), 1);
    NETW_CHECK_EQ(int(second->get(StringName("seated"))), 0);

    api->embed_dispose();
    release_branch(branch);
}

TEST_CASE(
    "[Networked][SceneTree] a join declaration carries its own packing, so "
    "two sessions running one script quantize independently and the schema "
    "each publishes is its own"
) {
    const Ref<Script> shape = shape_of(
        "extends Node\n"
        "func seat(_rj, team: int) -> void:\n"
        "\tpass\n"
    );
    REQUIRE(shape.is_valid());

    Node *first_branch = mounted_branch("JoinPackFirst");
    Node *second_branch = mounted_branch("JoinPackSecond");
    Node *first = Object::cast_to<Node>(shape->call("new"));
    Node *second = Object::cast_to<Node>(shape->call("new"));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    first_branch->add_child(first);
    second_branch->add_child(second);
    const Ref<NetwMultiplayer> first_api = branch_session(first_branch);
    const Ref<NetwMultiplayer> second_api = branch_session(second_branch);

    const Ref<netw::NetwJoinConfig> packed = netw::Netw::configure_join(
        first,
        Callable(first, StringName("seat"))
    );
    const Ref<netw::NetwJoinConfig> bare = netw::Netw::configure_join(
        second,
        Callable(second, StringName("seat"))
    );
    REQUIRE(packed.is_valid());
    REQUIRE(bare.is_valid());
    CHECK(packed != bare);

    Ref<netw::NetwQuantizeScalar> scalar;
    scalar.instantiate();
    Array declared;
    declared.push_back(scalar);
    packed->quantize(declared);

    NETW_CHECK_EQ(int(first_api->session_resolve_join().quantizers.size()), 1);
    CHECK(second_api->session_resolve_join().quantizers.is_empty());

    first_api->embed_dispose();
    second_api->embed_dispose();
    release_branch(first_branch);
    release_branch(second_branch);
}

TEST_CASE(
    "[Networked][SceneTree] a handler taking wire arguments that publishes "
    "no parameter list is refused at the door, because an unreadable schema "
    "would be sent as an empty one and decoded as an empty one"
) {
    const Ref<Script> shape = shape_of(
        "extends Node\n"
        "func lambda() -> Callable:\n"
        "\treturn func(_rj, _team: int) -> void: pass\n"
        "func opaque() -> Callable:\n"
        "\treturn func(_rj) -> void: pass\n"
    );
    REQUIRE(shape.is_valid());
    Node *branch = mounted_branch("JoinScopeOpaque");
    Node *holder = Object::cast_to<Node>(shape->call("new"));
    REQUIRE(holder != nullptr);
    branch->add_child(holder);
    const Ref<NetwMultiplayer> api = branch_session(branch);

    const Callable lambda = holder->call(StringName("lambda"));
    NETW_CHECK_EQ(lambda.get_argument_count(), 2);
    CHECK(netw::Netw::configure_join(holder, lambda).is_null());

    const NetwMultiplayer::JoinPlan plan = api->session_resolve_join();
    CHECK(plan.available);
    CHECK(plan.handler.is_valid());
    CHECK_FALSE(plan.declared);

    const Callable opaque = holder->call(StringName("opaque"));
    CHECK(netw::Netw::configure_join(holder, opaque).is_valid());
    CHECK(api->session_resolve_join().handler == opaque);

    api->embed_dispose();
    release_branch(branch);
}

TEST_CASE(
    "[Networked][SceneTree] a join handler that answers a scene admits the "
    "player to it as well as seating them, because a seat alone leaves them "
    "holding a scene they are not a viewer of, so its entities never reach "
    "them and its roster never learns they arrived"
) {
    Node *branch = mounted_branch("JoinSeats");
    const Ref<NetwMultiplayer> api = branch_session(branch);
    const RID arena = a_declared_scene(api, branch, "Arena");
    const StringName layer = api->scene_layer_id(arena);
    REQUIRE(!layer.is_empty());

    const Ref<Script> shape = shape_of(
        "extends RefCounted\n"
        "var destination\n"
        "func seat(_rj):\n"
        "\treturn destination\n"
    );
    REQUIRE(shape.is_valid());
    const Ref<RefCounted> handler = shape->call("new");
    REQUIRE(handler.is_valid());
    handler->set(StringName("destination"), api->scene_handle_of(arena));
    api->session_set_join_override(
        Callable(handler.ptr(), StringName("seat")),
        Array()
    );

    CHECK_FALSE(api->interest_plane().layer_has_viewer(layer, 31));

    admit_one(api, 31);

    CHECK(api->participant_seat(31) == arena);
    CHECK(api->interest_plane().layer_has_viewer(layer, 31));
    NETW_CHECK_EQ(int(api->scene_get_participants(arena).size()), 1);

    api->embed_dispose();
    release_branch(branch);
}

TEST_CASE(
    "[Networked][Session][Hosted] W0 a join whose build differs is refused by "
    "the part that differs, and a schema is one of those parts"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->auth_set_app_tag(
        NetwMultiplayer::session_app_tag(StringName("gated-arena"))
    );

    netw::JoinRequest agreeing;
    agreeing.username = StringName("ada");
    agreeing.app_tag = core->auth_app_tag_of();
    agreeing.wire_identity = netw::SessionCore::compute_wire_identity();
    agreeing.schema_identity = core->session_schema_identity();
    CHECK(core->session_identity_differs(agreeing) == nullptr);

    netw::JoinRequest other_app = agreeing;
    other_app.app_tag += 1;
    CHECK(String(core->session_identity_differs(other_app)) == String("app"));

    netw::JoinRequest other_wire = agreeing;
    other_wire.wire_identity += 1;
    CHECK(String(core->session_identity_differs(other_wire)) == String("wire"));

    netw::JoinRequest other_schema = agreeing;
    other_schema.schema_identity += 1;
    CHECK(
        String(core->session_identity_differs(other_schema)) == String("schema")
    );

    core->embed_dispose();
}

} // namespace TestNetwSessionJoinLaws

#endif

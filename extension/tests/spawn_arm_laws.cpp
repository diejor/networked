#include "support/fault_log.h"
#include "support/netw_test.h"

#include <cstdint>

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/script.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/spawn/record.hpp"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)
#include "support/loopback_rig.h"
#endif

namespace TestNetwSpawnArm {

using namespace godot;
using netw::NetwEntity;
using netw::NetwEntityRecord;
using netw::NetwMultiplayer;
using netw::NetwPlayer;
using netw::spawn::Record;
using netw_test::CallLog;

Ref<NetwPlayer> seated(const Ref<NetwMultiplayer> &p_core, int64_t p_peer) {
    Ref<NetwPlayer> owner;
    owner.instantiate();
    p_core->player_adopt(p_peer, owner);
    p_core->player_admit(p_peer);
    return owner;
}

Ref<NetwPlayer> a_stranger(const Ref<NetwMultiplayer> &p_core, int64_t p_peer) {
    Ref<NetwPlayer> stranger;
    stranger.instantiate();
    stranger->bind_to(p_core.ptr(), p_peer);
    return stranger;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SM1 an arm reserves a route, names the entity "
    "after its recipe, and copies the identity onto the record"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    Record record;
    record.set_recipe(netw::spawn::Book::RECIPE_FN);
    record.set_fn_method(StringName("make_crate"));

    const Ref<NetwEntity> entity = core->spawn_arm_identity(
        &record,
        node,
        log.answering("declare", int64_t(OK))
    );

    REQUIRE(entity.is_valid());
    NETW_CHECK_GT(entity->get_route(), int64_t(0));
    NETW_CHECK_EQ(record.get_route(), entity->get_route());
    NETW_CHECK_EQ(record.node(), node);
    CHECK(
        bool(
            String(entity->get_name())
            == String("make_crate@") + String::num_int64(entity->get_route())
        )
    );
    CHECK(bool(String(record.get_entity_id()) == String(entity->get_name())));
    NETW_CHECK_EQ(log.count("declare"), 1);

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SM2 a written player stamps both the owning "
    "peer and the controller, and an entity that already has an id keeps it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    const Ref<NetwEntity> pre = NetwEntity::ensure(node);
    pre->set_name(StringName("chosen"));
    pre->set_player(seated(core, 4));
    Record record;

    const Ref<NetwEntity> entity = core->spawn_arm_identity(
        &record,
        node,
        log.answering("declare", int64_t(OK))
    );

    REQUIRE(entity.is_valid());
    CHECK(bool(String(entity->get_name()) == String("chosen")));
    NETW_CHECK_EQ(entity->get_peer_id(), int64_t(4));
    NETW_CHECK_EQ(entity->get_controller(), int64_t(4));
    NETW_CHECK_EQ(record.get_peer_id(), int64_t(4));
    NETW_CHECK_EQ(record.get_controller(), int64_t(4));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SM3 a refused declaration puts every identity "
    "field back the way it found it, and arms nothing"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    const Ref<NetwEntity> pre = NetwEntity::ensure(node);
    Ref<NetwPlayer> ada;
    ada.instantiate();
    ada->bind_to(core.ptr(), 8, 5, StringName("Ada"));
    pre->set_player(ada);
    pre->set_controller(3);
    const int64_t held_route = pre->get_route();

    Record scratch;
    const Ref<NetwEntity> refused = core->spawn_arm_identity(
        &scratch,
        node,
        log.answering("declare", int64_t(ERR_UNAUTHORIZED))
    );

    CHECK(refused.is_null());
    NETW_CHECK_EQ(log.count("declare"), 1);
    CHECK(bool(pre->get_name() == StringName()));
    CHECK(bool(String(node->get_name()) == String("Crate")));
    NETW_CHECK_EQ(pre->get_peer_id(), int64_t(8));
    NETW_CHECK_EQ(pre->get_player_id(), int64_t(5));
    NETW_CHECK_EQ(pre->get_controller(), int64_t(3));
    NETW_CHECK_EQ(pre->get_route(), held_route);

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SM4 an arm with nothing to arm answers "
    "nothing, and a caller that declares nothing is not refused"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *node = memnew(Node);
    node->set_name("Crate");

    Record scratch;
    CHECK(core->spawn_arm_identity(nullptr, node, Callable()).is_null());
    CHECK(core->spawn_arm_identity(&scratch, nullptr, Callable()).is_null());
    CHECK(core->spawn_arm_identity(&scratch, node, Callable()).is_valid());

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SM7 a body seated beside a node "
    "that already holds its name takes a readable name from its entity id, "
    "because a colliding child is renamed to a form the stock "
    "MultiplayerSpawner refuses to auto-spawn"
) {
    Node *root = netw::gd::scene_root();
    REQUIRE(root != nullptr);
    Node *level = memnew(Node);
    level->set_name("Level");
    root->add_child(level);
    Node *squatter = memnew(Node);
    squatter->set_name("Avatar");
    level->add_child(squatter);

    Node *body = memnew(Node);
    body->set_name("Avatar");
    const Ref<NetwEntity> wrapper = NetwEntity::ensure(body);
    REQUIRE(wrapper.is_valid());
    NetwEntityRecord *const record = wrapper->get_record();
    REQUIRE(record != nullptr);
    record->set_entity_id(StringName("Avatar"));

    level->add_child(body);

    const String seated_name = String(body->get_name());
    CHECK(bool(seated_name == seated_name.validate_node_name()));
    CHECK(bool(seated_name.begins_with("Avatar")));
    CHECK_FALSE(bool(seated_name == String(squatter->get_name())));

    root->remove_child(level);
    memdelete(level);
}

#if defined(NETW_TIER_HOSTED)

Node *build_orphan_avatar() {
    Node *body = memnew(Node);
    body->set_name("Avatar");
    return body;
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] SM6 an orphan armed for a player is "
    "discarded rather than replicated when that player leaves before the "
    "body is mounted"
) {
    netw_test::LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *server = rig.server();

    Ref<NetwPlayer> leaving;
    leaving.instantiate();
    server->player_adopt(77, leaving);
    server->player_admit(77);
    REQUIRE(server->player_holds(leaving));

    rig.register_constructor(
        server,
        StringName("orphan_avatar"),
        callable_mp_static(&build_orphan_avatar),
        Array()
    );
    const RID armed = server->spawn_registered(
        StringName("orphan_avatar"),
        Array(),
        leaving.ptr()
    );
    REQUIRE(armed.is_valid());
    Node *body = server->entity_get_node(armed);
    REQUIRE(body != nullptr);
    const int route = int(server->entity_get_route(armed));
    netw::spawn::Pipeline *pipeline = rig.spawn_plane();
    REQUIRE(pipeline != nullptr);
    const bool orphaned = !body->is_inside_tree();
    CHECK(orphaned);

    server->player_release_id(leaving);
    const bool still_held = server->player_holds(leaving);
    CHECK_FALSE(still_held);

    rig.branch(-1)->add_child(body);
    rig.pump(6);

    const bool published = pipeline->owns_spawned_route(route);
    CHECK_FALSE(published);
}

Node *build_named(const String &p_name) {
    Node *made = memnew(Node);
    made->set_name(p_name);
    return made;
}

Node *spawn_named(NetwMultiplayer *p_server, const char *p_name) {
    Array args;
    args.push_back(String(p_name));
    const RID spawned
        = p_server->spawn_registered(StringName("named"), args, nullptr);
    Node *node = p_server->entity_get_node(spawned);
    REQUIRE(node != nullptr);
    return node;
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] SM9 a spawned node still out of the tree "
    "a tick later is reported once, and one added in time is not"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][spawn] 'Late' was spawned but never entered the tree. Add "
        "it before the next tick.",
        1
    );
    netw_test::LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *server = rig.server();
    Array types;
    types.push_back(int(Variant::STRING));
    rig.register_constructor(
        server,
        StringName("named"),
        callable_mp_static(&build_named),
        types
    );

    Node *late = spawn_named(server, "Late");
    Node *kept = spawn_named(server, "Kept");
    rig.branch(-1)->add_child(kept);
    rig.step_ticks(4);
    rig.branch(-1)->add_child(late);
    rig.step_ticks(2);
}

uint64_t built_crate = 0;

Node *build_crate(int64_t p_slot) {
    Node *crate = memnew(Node);
    crate->set_name(vformat("Crate%d", p_slot));
    built_crate = uint64_t(crate->get_instance_id());
    return crate;
}

const char *REFUSES_DECLARE = R"(extends NetwMultiplayer

func _spawn_declare(_entity: RID, _recipe: Variant) -> Error:
	return ERR_UNAUTHORIZED
)";

TEST_CASE(
    "[Networked][Spawn][SceneTree] SM5 a spawn for a player this session "
    "does not hold, or one whose declaration is refused, answers nothing and "
    "frees the node it built, and the same spawn for a seated player goes "
    "through"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][spawn] Nothing was spawned, because player '*' is not in "
        "the session. Spawn for a player the session has admitted.",
        1
    );
    expected_faults.expect_warning(
        "[warn][transport] A packet for route * was refused with error *.",
        1
    );
    netw_test::LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *server = rig.server();
    Array arg_types;
    arg_types.push_back(int64_t(Variant::INT));
    Array slot;
    slot.push_back(int64_t(0));
    rig.register_constructor(
        server,
        StringName("crate"),
        callable_mp_static(&build_crate),
        arg_types
    );

    const Ref<NetwPlayer> stranger
        = a_stranger(Ref<NetwMultiplayer>(server), 9);
    CHECK_FALSE(server->player_holds(stranger));
    const RID refused
        = server->spawn_registered(StringName("crate"), slot, stranger.ptr());

    CHECK_FALSE(refused.is_valid());
    CHECK(netw::gd::object_of(ObjectID(built_crate)) == nullptr);

    const Ref<NetwPlayer> admitted = seated(Ref<NetwMultiplayer>(server), 9);
    const Ref<Script> refusing
        = netw::gd::instantiate_class(StringName("GDScript"));
    refusing->set_source_code(String(REFUSES_DECLARE));
    refusing->reload();
    server->set_script(refusing);
    const RID undeclared
        = server->spawn_registered(StringName("crate"), slot, admitted.ptr());
    server->set_script(Variant());

    CHECK_FALSE(undeclared.is_valid());
    CHECK(netw::gd::object_of(ObjectID(built_crate)) == nullptr);

    const RID armed
        = server->spawn_registered(StringName("crate"), slot, admitted.ptr());
    REQUIRE(armed.is_valid());
    Node *crate = server->entity_get_node(armed);
    REQUIRE(crate != nullptr);
    NETW_CHECK_EQ(NetwEntity::of(crate)->get_peer_id(), int64_t(9));
    rig.branch(-1)->add_child(crate);
    rig.pump(2);
}

enum Writes : int64_t {
    PLAYER_THEN_SERVER = 0,
    SERVER_THEN_PLAYER = 1,
    SERVER_ONLY = 2,
    PLAYER_THEN_NOBODY = 3,
};

NetwPlayer *written_player = nullptr;

Node *build_server_steered_body(int64_t p_writes) {
    Node *body = memnew(Node);
    body->set_name("Body");
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    const Ref<NetwPlayer> player(written_player);
    if (p_writes == PLAYER_THEN_NOBODY) {
        entity->set_player(player);
        entity->set_player(Ref<NetwPlayer>());
        return body;
    }
    if (p_writes == PLAYER_THEN_SERVER) {
        entity->set_player(player);
    }
    entity->set_controller_player(Ref<NetwPlayer>());
    if (p_writes == SERVER_THEN_PLAYER) {
        entity->set_player(player);
    }
    return body;
}

struct SteeringRow {
    const char *label;
    Writes writes;
    bool through_spawn_player;
    bool represents;
};

TEST_CASE(
    "[Networked][Spawn][SceneTree] SM8 a body written for a player and "
    "then steered by the server keeps the server as controller on every "
    "peer, whatever the order of the writes"
) {
    const SteeringRow rows[] = {
        { "player then server", PLAYER_THEN_SERVER, false, true },
        { "server then player", SERVER_THEN_PLAYER, false, true },
        { "player then server, spawn_player", PLAYER_THEN_SERVER, true, true },
        { "server then player, spawn_player", SERVER_THEN_PLAYER, true, true },
        { "server only, spawn_player", SERVER_ONLY, true, true },
        { "player then no player", PLAYER_THEN_NOBODY, false, false },
    };
    for (const SteeringRow &row : rows) {
        INFO(row.label);
        netw_test::LoopbackRig rig(1);
        rig.mount();
        const Ref<NetwPlayer> player = seated(
            Ref<NetwMultiplayer>(rig.server()),
            rig.peer_id(0)
        );
        REQUIRE(rig.server()->player_holds(player));
        written_player = player.ptr();
        Array arg_types;
        arg_types.push_back(int64_t(Variant::INT));
        Array args;
        args.push_back(int64_t(row.writes));

        const int route = rig.spawn_registered(
            StringName("server_steered_body"),
            callable_mp_static(&build_server_steered_body),
            args,
            arg_types,
            nullptr,
            row.through_spawn_player ? Variant(player) : Variant()
        );
        written_player = nullptr;

        for (int peer = -1; peer < rig.count(); ++peer) {
            CAPTURE(peer);
            const Ref<NetwEntity> entity
                = NetwEntity::of(rig.route_node(route, peer));
            REQUIRE(entity.is_valid());
            NETW_CHECK_EQ(
                entity->get_peer_id(),
                row.represents ? int64_t(rig.peer_id(0)) : int64_t(0)
            );
            NETW_CHECK_EQ(entity->get_controller(), int64_t(1));
        }
    }
}

#endif

} // namespace TestNetwSpawnArm

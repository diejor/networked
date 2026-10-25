#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"

#if defined(NETW_TIER_HOSTED)

#include "support/fault_log.h"
#include "support/loopback_rig.h"
#include "support/minted_script.h"

#include "godot/object.hpp"
#include "godot/script.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/scene.hpp"
#include "netw/api/session.hpp"
#include "netw/api/session_config.hpp"
#include "netw/spawn/pipeline.hpp"

#endif

namespace TestNetwSceneDespawn {

using namespace godot;
using netw::NetwMultiplayer;

#if defined(NETW_TIER_HOSTED)

using netw::Netw;
using netw::NetwEntity;
using netw::NetwPlayer;
using netw::NetwScene;
using netw_test::LoopbackRig;

constexpr const char *HOST_ID = "sr_host";

constexpr const char *GATE_SOURCE = R"(extends Node

func admit(request: NetwJoinRequest) -> Error:
	request.account = "test:" + request.username
	return OK
)";

constexpr const char *LEVEL_SOURCE = R"(extends Node2D

static var isolate := false
static var linger := 0.0

func _init() -> void:
	var config: NetwSceneConfig = Netw.configure_scene(self)
	if isolate:
		config.isolated()
	if linger > 0.0:
		Netw.configure_despawn(self).linger(linger)
)";

constexpr const char *HOST_SOURCE = R"(extends Node

static var level_script: Script
static var fail_peer := 0

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_spawn(make_level)
	Netw.configure_spawn(make_pawn)
	Netw.configure_spawn(make_box)

func make_level(level_name: String) -> Node:
	if multiplayer.get_unique_id() == fail_peer:
		return null
	var level: Node = level_script.new()
	level.name = level_name
	return level

func make_box(box_name: String) -> Node:
	if multiplayer.get_unique_id() == fail_peer:
		return null
	var box := Node2D.new()
	box.name = box_name
	return box

func open_box(box_name: String) -> Node:
	var box: Node = Netw.spawn(make_box, box_name)
	get_parent().add_child(box)
	return box

func make_pawn(pawn_name: String) -> Node:
	var pawn := Node2D.new()
	pawn.name = pawn_name
	return pawn

func open(level_name: String, parent: Node) -> Node:
	var level: Node = Netw.spawn(make_level, level_name)
	parent.add_child(level)
	return level

func seat(level: Node, player: NetwPlayer, prefix: String) -> Node:
	var pawn: Node = Netw.spawn_player(player, make_pawn, prefix + player.username)
	Netw.scene(level).root.add_child(pawn)
	return pawn

func swap(old: Node, level_name: String, old_first: bool) -> Node:
	if old_first:
		Netw.despawn(old)
	var level := open(level_name, get_parent())
	for player: NetwPlayer in Netw.session(self).players:
		seat(level, player, "Car_")
	if not old_first:
		Netw.despawn(old)
	return level
)";

Script *host_script = nullptr;

Node *build_host(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(host_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

void collect_despawned(Node *p_node, LocalVector<ObjectID> &r_out) {
    for (int at = 0; at < p_node->get_child_count(); ++at) {
        Node *child = p_node->get_child(at);
        const Ref<NetwEntity> seen = NetwEntity::of(child);
        const bool despawning = seen.is_valid() && seen->get_owner() == child
            && seen->get_stage() == NetwEntity::STAGE_DESPAWNING;
        if (child->is_queued_for_deletion() || despawning) {
            r_out.push_back(netw::gd::instance_id(child));
        } else {
            collect_despawned(child, r_out);
        }
    }
}

void end_frame() {
    LocalVector<ObjectID> doomed;
    collect_despawned(netw::gd::scene_root(), doomed);
    for (const ObjectID id : doomed) {
        Node *node = Object::cast_to<Node>(netw::gd::instance_from_id(id));
        if (node != nullptr) {
            memdelete(node);
        }
    }
}

struct Stand {
    Ref<Script> gate = netw_test::minted_script(GATE_SOURCE);
    Ref<Script> host_kind = netw_test::minted_script(HOST_SOURCE);
    Ref<Script> level = netw_test::minted_script(LEVEL_SOURCE);
    LoopbackRig rig;
    Node *host = nullptr;

    explicit Stand(int p_clients, double p_window = 0.0) : rig(p_clients) {
        REQUIRE(gate.is_valid());
        REQUIRE(host_kind.is_valid());
        REQUIRE(level.is_valid());
        host_script = host_kind.ptr();
        host_kind->set("level_script", level);
        if (p_window > 0.0) {
            Ref<netw::NetwSessionConfig> config;
            config.instantiate();
            config->return_window(p_window);
            NETW_REQUIRE_EQ(
                int(rig.server()->session_initialize(config)),
                int(OK)
            );
        }
        rig.mount();
        Node *admit = Object::cast_to<Node>(gate->call("new"));
        rig.branch(-1)->add_child(admit);
        REQUIRE(
            Netw::configure_admission(Callable(admit, StringName("admit")))
                .is_valid()
        );
        rig.join(-1, StringName("host"));
        const char *names[] = {"ada", "bob"};
        for (int at = 0; at < p_clients; ++at) {
            rig.join(at, StringName(names[at]));
        }
        rig.pump(8);
        Array args;
        args.push_back(String("Host"));
        Array types;
        types.push_back(int(Variant::STRING));
        const int route = rig.spawn_registered(
            StringName(HOST_ID),
            callable_mp_static(&build_host),
            args,
            types
        );
        host = rig.route_node(route);
        REQUIRE(host != nullptr);
    }

    ~Stand() {
        end_frame();
        host_script = nullptr;
    }

    NetwMultiplayer *at(int p_seat) const {
        return p_seat < 0 ? rig.server() : rig.client(p_seat);
    }

    Node *open(const char *p_name, Node *p_parent = nullptr) {
        Node *parent = p_parent != nullptr ? p_parent : rig.branch(-1);
        return Object::cast_to<Node>(
            host->call(StringName("open"), String(p_name), parent)
        );
    }

    Node *seat(Node *p_level, int p_seat, const char *p_prefix) {
        return Object::cast_to<Node>(host->call(
            StringName("seat"),
            p_level,
            rig.player(p_seat),
            String(p_prefix)
        ));
    }

    bool lists(int p_seat, const char *p_label) const {
        return at(p_seat)
            ->get_session()
            ->find_scene(StringName(p_label))
            .is_valid();
    }

    int scene_count(int p_seat) const {
        return int(at(p_seat)->scene_list().size());
    }

    String scene_of(int p_seat) const {
        const Ref<NetwPlayer> me = Object::cast_to<NetwPlayer>(
            netw::gd::live_object(at(p_seat)->get("local_player"))
        );
        const Ref<NetwScene> scene
            = me.is_valid() ? me->get_scene() : Ref<NetwScene>();
        return scene.is_valid() ? String(scene->get_label()) : String("none");
    }
};

int route_of(Node *p_node) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    return entity.is_valid() ? int(entity->get_route()) : -1;
}

Node *parent_of(Node *p_node) {
    return p_node != nullptr ? p_node->get_parent() : nullptr;
}

int despawn(Node *p_node) {
    return int(Netw::despawn(p_node, Ref<netw::NetwDespawnOpts>()));
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR1 despawning a level's root on the "
    "server removes its scene in the same call, ends every watch on it, and "
    "leaves the node in the tree until the frame frees it"
) {
    Stand stand(1);
    Node *lobby = stand.open("Lobby");
    stand.seat(lobby, -1, "Pawn_");
    stand.rig.pump(8);
    const Ref<NetwScene> scene = Netw::scene(lobby);
    REQUIRE(scene.is_valid());
    REQUIRE(scene->watch(stand.rig.player(0)) == OK);
    REQUIRE(scene->is_watching(stand.rig.player(0)));
    REQUIRE(stand.lists(-1, "Lobby"));

    NETW_CHECK_EQ(despawn(lobby), int(OK));

    CHECK_FALSE(stand.lists(-1, "Lobby"));
    NETW_CHECK_EQ(stand.scene_count(-1), 0);
    CHECK_FALSE(scene->is_watching(stand.rig.player(0)));
    CHECK(stand.scene_of(-1) == "none");
    CHECK(lobby->is_inside_tree());
    stand.rig.pump(4);
    CHECK_FALSE(stand.lists(-1, "Lobby"));
    CHECK(stand.scene_of(-1) == "none");
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR2 a client removes a level's scene at "
    "the poll that applies its despawn, while the level's node still lingers"
) {
    Stand stand(1);
    stand.level->set("linger", 5.0);
    Node *lobby = stand.open("Lobby");
    stand.seat(lobby, 0, "Pawn_");
    stand.rig.pump(8);
    const int lobby_route = route_of(lobby);
    REQUIRE(stand.rig.route_node(lobby_route, 0) != nullptr);
    REQUIRE(stand.lists(0, "Lobby"));
    REQUIRE(stand.scene_of(0) == "Lobby");

    despawn(lobby);
    end_frame();
    bool applied = false;
    for (int round = 0; round < 8 && !applied; ++round) {
        stand.rig.pump(1);
        Node *mirror = stand.rig.route_node(lobby_route, 0);
        applied = mirror == nullptr
            || NetwEntity::of(mirror)->get_stage() != NetwEntity::STAGE_LIVE;
    }
    REQUIRE(applied);

    Node *mirror = stand.rig.route_node(lobby_route, 0);
    CHECK(mirror != nullptr);
    CHECK(bool(mirror != nullptr && mirror->is_inside_tree()));
    CHECK_FALSE(stand.lists(0, "Lobby"));
    NETW_CHECK_EQ(stand.scene_count(0), 0);
    CHECK(stand.scene_of(0) == "none");
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR3 a swap that despawns the old level "
    "first never warns about a split, and every player stands in the new "
    "level on both peers"
) {
    Stand stand(1);
    Node *lobby = stand.open("Lobby");
    stand.seat(lobby, -1, "Pawn_");
    stand.seat(lobby, 0, "Pawn_");
    stand.rig.pump(8);
    REQUIRE(stand.scene_of(0) == "Lobby");

    const netw_test::FaultWatch faults;
    stand.host->call(StringName("swap"), lobby, String("Arena"), true);

    CHECK(stand.scene_of(-1) == "Arena");
    const Ref<NetwScene> ada_scene = stand.rig.player(0)->get_scene();
    CHECK(
        bool(
            ada_scene.is_valid()
            && String(ada_scene->get_label()) == String("Arena")
        )
    );
    end_frame();
    stand.rig.pump(8);
    CHECK(stand.scene_of(0) == "Arena");
    NETW_CHECK_EQ(int(faults.warnings_containing("scenes at once")), 0);
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR4 nothing in a despawned level reaches "
    "a client that did not hold it, for the rest of the frame"
) {
    Stand stand(2);
    Node *lobby = stand.open("Lobby");
    const int pawn_route = route_of(stand.seat(lobby, 0, "Pawn_"));
    stand.rig.pump(8);
    const int lobby_route = route_of(lobby);
    REQUIRE(stand.rig.route_node(pawn_route, 0) != nullptr);
    REQUIRE(stand.rig.route_node(pawn_route, 1) == nullptr);

    despawn(lobby);
    stand.rig.pump(8);

    CHECK(stand.rig.route_node(lobby_route, 1) == nullptr);
    CHECK(stand.rig.route_node(pawn_route, 1) == nullptr);
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR5 a listen server that seats the new "
    "level before despawning the old one warns nothing, and a split that "
    "outlives a tick warns once"
) {
    Stand stand(1);
    Node *lobby = stand.open("Lobby");
    stand.seat(lobby, -1, "Pawn_");
    stand.rig.pump(8);
    REQUIRE(stand.scene_of(-1) == "Lobby");

    const netw_test::FaultWatch faults;
    Node *arena = Object::cast_to<Node>(
        stand.host->call(StringName("swap"), lobby, String("Arena"), false)
    );
    end_frame();
    stand.rig.pump(8);
    CHECK(stand.scene_of(-1) == "Arena");
    NETW_CHECK_EQ(int(faults.warnings_containing("scenes at once")), 0);

    Node *annex = stand.open("Annex");
    stand.seat(annex, -1, "Guest_");
    stand.rig.pump(8);
    NETW_CHECK_EQ(int(faults.warnings_containing("scenes at once")), 1);
    stand.rig.pump(8);
    NETW_CHECK_EQ(int(faults.warnings_containing("scenes at once")), 1);
    CHECK(arena->is_inside_tree());
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR6 despawning the node Netw.spawn "
    "returned for an isolated level removes that scene and despawns no "
    "other entity"
) {
    Stand stand(1);
    stand.level->set("isolate", true);
    Node *arena = stand.open("Arena", stand.host);
    const Ref<NetwScene> scene = Netw::scene(arena);
    REQUIRE(scene.is_valid());
    REQUIRE(scene->get_root() != arena);
    stand.rig.pump(8);

    NETW_CHECK_EQ(despawn(arena), int(OK));

    CHECK_FALSE(stand.lists(-1, "Arena"));
    NETW_CHECK_EQ(
        int(NetwEntity::of(stand.host)->get_stage()),
        int(NetwEntity::STAGE_LIVE)
    );
    NETW_CHECK_EQ(
        int(NetwEntity::of(scene->get_root())->get_stage()),
        int(NetwEntity::STAGE_DESPAWNING)
    );
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR7 a swap in either order reaches the "
    "client with the old level gone before the new one arrives, so the "
    "client never holds both and never warns"
) {
    for (const bool old_first : {true, false}) {
        CAPTURE(old_first);
        Stand stand(1);
        Node *lobby = stand.open("Lobby");
        stand.seat(lobby, -1, "Pawn_");
        stand.seat(lobby, 0, "Pawn_");
        stand.rig.pump(8);
        const int lobby_route = route_of(lobby);
        REQUIRE(stand.scene_of(0) == "Lobby");

        const netw_test::FaultWatch faults;
        stand.host->call(StringName("swap"), lobby, String("Arena"), old_first);
        for (int round = 0; round < 4; ++round) {
            stand.rig.pump(1);
            CHECK_FALSE(
                bool(stand.lists(0, "Lobby") && stand.lists(0, "Arena"))
            );
        }
        CHECK(stand.rig.route_node(lobby_route, 0) == nullptr);
        CHECK(stand.scene_of(0) == "Arena");
        NETW_CHECK_EQ(int(faults.warnings_containing("scenes at once")), 0);
    }
}

int64_t g_viewers_entered = 0;
int64_t g_viewers_exited = 0;

void on_viewer_entered(const Variant &) {
    g_viewers_entered += 1;
}

void on_viewer_exited(const Variant &) {
    g_viewers_exited += 1;
}

void count_viewers(const Ref<NetwScene> &p_scene) {
    g_viewers_entered = 0;
    g_viewers_exited = 0;
    p_scene->connect(
        StringName("viewer_entered"),
        callable_mp_static(&on_viewer_entered)
    );
    p_scene->connect(
        StringName("viewer_exited"),
        callable_mp_static(&on_viewer_exited)
    );
}

bool views(const Ref<NetwScene> &p_scene, const Ref<NetwPlayer> &p_player) {
    const TypedArray<NetwPlayer> viewers = p_scene->get_viewers();
    for (int at = 0; at < viewers.size(); ++at) {
        if (Ref<NetwPlayer>(viewers[at]) == p_player) {
            return true;
        }
    }
    return false;
}

enum Move {
    MOVE_NODE,
    MOVE_NETW,
};

void move(Node *p_node, Node *p_parent, Move p_how) {
    if (p_how == MOVE_NETW) {
        CHECK_FALSE(Netw::reparent(p_node, p_parent)->get_is_failed());
    } else {
        p_node->reparent(p_parent);
    }
}

void settle(Stand &p_stand) {
    p_stand.rig.step_ticks(24);
    end_frame();
    p_stand.rig.pump(4);
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR8 a level moved with Node.reparent or "
    "Netw.reparent keeps its scene, its viewers and every player's scene on "
    "the server and on each client"
) {
    for (const Move how : {MOVE_NODE, MOVE_NETW}) {
        CAPTURE(int(how));
        Stand stand(2);
        Node *lobby = stand.open("Lobby");
        stand.seat(lobby, 0, "Pawn_");
        stand.rig.pump(8);
        const Ref<NetwScene> scene = Netw::scene(lobby);
        REQUIRE(scene.is_valid());
        REQUIRE(scene->watch(stand.rig.player(1)) == OK);
        stand.rig.pump(8);
        const int lobby_route = route_of(lobby);
        const int host_route = route_of(stand.host);
        count_viewers(scene);
        const netw_test::FaultWatch faults;

        move(lobby, stand.host, how);
        CHECK(stand.lists(-1, "Lobby"));
        settle(stand);

        CHECK(lobby->get_parent() == stand.host);
        CHECK(stand.lists(-1, "Lobby"));
        CHECK(views(scene, stand.rig.player(0)));
        CHECK(scene->is_watching(stand.rig.player(1)));
        CHECK(stand.rig.player(0)->get_scene() == scene);
        NETW_CHECK_EQ(g_viewers_entered, int64_t(0));
        NETW_CHECK_EQ(g_viewers_exited, int64_t(0));
        for (const int seat : {0, 1}) {
            CAPTURE(seat);
            Node *mirror = stand.rig.route_node(lobby_route, seat);
            CHECK(mirror != nullptr);
            CHECK(parent_of(mirror) == stand.rig.route_node(host_route, seat));
            CHECK(stand.lists(seat, "Lobby"));
        }
        CHECK(stand.scene_of(0) == "Lobby");
        NETW_CHECK_EQ(int(faults.errors()), 0);
        NETW_CHECK_EQ(int(faults.warnings_logged()), 0);
    }
}

struct IsolatedMove {
    const char *label;
    bool root;
    Move how;
};

TEST_CASE(
    "[Networked][Scene][SceneTree] SR10 an isolated level moves as the node "
    "Netw.spawn returned, on the server and on the client, whether that node "
    "or the level's root is passed to Netw.reparent"
) {
    const IsolatedMove rows[] = {
        {"Node.reparent of the container", false, MOVE_NODE},
        {"Netw.reparent of the container", false, MOVE_NETW},
        {"Netw.reparent of the root", true, MOVE_NETW},
    };
    for (const IsolatedMove &row : rows) {
        CAPTURE(row.label);
        Stand stand(1);
        stand.level->set("isolate", true);
        Node *arena = stand.open("Arena");
        const Ref<NetwScene> scene = Netw::scene(arena);
        REQUIRE(scene.is_valid());
        Node *root = scene->get_root();
        REQUIRE(root != arena);
        stand.seat(arena, 0, "Pawn_");
        stand.rig.pump(8);
        const int route = route_of(root);
        const int host_route = route_of(stand.host);
        const netw_test::FaultWatch faults;

        move(row.root ? root : arena, stand.host, row.how);
        settle(stand);

        Node *held = stand.rig.route_node(route);
        CHECK(held != nullptr);
        CHECK(NetwMultiplayer::scene_outer_of(held) == arena);
        CHECK(parent_of(NetwMultiplayer::scene_outer_of(held)) == stand.host);
        CHECK(stand.lists(-1, "Arena"));
        CHECK(stand.rig.player(0)->get_scene() == scene);
        Node *mirror = stand.rig.route_node(route, 0);
        CHECK(mirror != nullptr);
        Node *outer = NetwMultiplayer::scene_outer_of(mirror);
        CHECK(outer != mirror);
        CHECK(parent_of(outer) == stand.rig.route_node(host_route, 0));
        CHECK(stand.lists(0, "Arena"));
        CHECK(stand.scene_of(0) == "Arena");
        NETW_CHECK_EQ(int(faults.errors()), 0);
        NETW_CHECK_EQ(int(faults.warnings_logged()), 0);
    }
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR11 Node.reparent that pulls an isolated "
    "level's root out of its SubViewport warns once"
) {
    Stand stand(1);
    stand.level->set("isolate", true);
    Node *arena = stand.open("Arena");
    Node *root = Netw::scene(arena)->get_root();
    stand.rig.pump(8);

    netw_test::FaultWatch faults;
    faults.expect_warning(
        "*'Arena' was moved out of the SubViewport that holds its world. "
        "Reparent the node Netw.spawn returned for it, or use Netw.reparent.*",
        1
    );
    root->reparent(stand.host);
    settle(stand);
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR12 a level removed with remove_child "
    "and kept closes its scene once the frame settles, and viewer_exited "
    "fires for its viewer"
) {
    Stand stand(1);
    Node *lobby = stand.open("Lobby");
    stand.seat(lobby, 0, "Pawn_");
    stand.rig.pump(8);
    const Ref<NetwScene> scene = Netw::scene(lobby);
    REQUIRE(scene.is_valid());
    count_viewers(scene);
    Node *parent = lobby->get_parent();

    parent->remove_child(lobby);
    stand.rig.pump(1);

    CHECK_FALSE(stand.lists(-1, "Lobby"));
    NETW_CHECK_EQ(g_viewers_exited, int64_t(1));
    parent->add_child(lobby);
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR13 a player who returns after its level "
    "moved lands in the moved level with its kept entity"
) {
    Stand stand(1, 60.0);
    Node *lobby = stand.open("Lobby");
    const int pawn_route = route_of(stand.seat(lobby, 0, "Pawn_"));
    stand.rig.pump(8);
    const int lobby_route = route_of(lobby);
    const int host_route = route_of(stand.host);
    stand.rig.drop_client(0);
    stand.rig.pump(8);

    lobby->reparent(stand.host);
    settle(stand);
    const int back = stand.rig.add_client();
    stand.rig.register_constructor(
        stand.rig.client(back),
        StringName(HOST_ID),
        callable_mp_static(&build_host),
        Array::make(int(Variant::STRING))
    );
    stand.rig.mount_late(back);
    stand.rig.join(back, StringName("ada"));
    settle(stand);

    Node *mirror = stand.rig.route_node(lobby_route, back);
    CHECK(mirror != nullptr);
    CHECK(parent_of(mirror) == stand.rig.route_node(host_route, back));
    Node *pawn = stand.rig.route_node(pawn_route, back);
    CHECK(pawn != nullptr);
    CHECK(parent_of(pawn) == mirror);
    CHECK(stand.scene_of(back) == "Lobby");
    const Ref<NetwPlayer> returned = stand.rig.player(back);
    CHECK(
        bool(returned.is_valid() && returned->get_scene() == Netw::scene(lobby))
    );
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR14 a level despawned and moved with "
    "Node.reparent in the same frame raises no error and leaves every client"
) {
    Stand stand(1);
    Node *lobby = stand.open("Lobby");
    stand.seat(lobby, 0, "Pawn_");
    stand.rig.pump(8);
    const int lobby_route = route_of(lobby);
    const netw_test::FaultWatch faults;

    despawn(lobby);
    lobby->reparent(stand.host);
    settle(stand);

    NETW_CHECK_EQ(int(faults.errors()), 0);
    CHECK_FALSE(stand.lists(-1, "Lobby"));
    CHECK(stand.rig.route_node(lobby_route, 0) == nullptr);
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SR9 a level despawned at the call leaves "
    "a departed player's kept entity that detaches from it spawned on the "
    "server and on every client that held it"
) {
    Stand stand(2, 60.0);
    Node *lobby = stand.open("Lobby");
    Node *kept = stand.seat(lobby, 0, "Pawn_");
    stand.seat(lobby, 1, "Pawn_");
    NetwEntity::of(kept)->set_on_parent_despawn(
        NetwEntity::PARENT_DESPAWN_DETACH
    );
    stand.rig.pump(8);
    const int kept_route = route_of(kept);
    REQUIRE(stand.rig.route_node(kept_route, 1) != nullptr);
    stand.rig.drop_client(0);
    stand.rig.pump(8);
    REQUIRE(NetwEntity::of(kept)->get_peer_id() == 0);

    despawn(lobby);
    end_frame();
    stand.rig.pump(8);

    CHECK(stand.rig.route_node(kept_route) != nullptr);
    CHECK(stand.rig.spawn_plane()->spawned_record(kept_route) != nullptr);
    CHECK(stand.rig.route_node(kept_route, 1) != nullptr);
}

struct FailedLevel {
    Stand stand;
    Node *lobby = nullptr;
    Ref<NetwScene> scene;
    int lobby_route = -1;

    FailedLevel() : stand(2) {
        g_viewers_exited = 0;
        stand.host_kind->set("fail_peer", stand.rig.peer_id(0));
        lobby = stand.open("Lobby");
        scene = Netw::scene(lobby);
        REQUIRE(scene.is_valid());
        scene->connect(
            StringName("viewer_exited"),
            callable_mp_static(&on_viewer_exited)
        );
        lobby_route = route_of(lobby);
        stand.seat(lobby, 1, "Pawn_");
        REQUIRE(scene->watch(stand.rig.player(0)) == OK);
    }

    bool views(int p_seat) const {
        return TestNetwSceneDespawn::views(scene, stand.rig.player(p_seat));
    }
};

TEST_CASE(
    "[Networked][Scene][SceneTree] SF1 a client whose spawn function returns "
    "no Node warns once, and the server warns once naming the player, the "
    "function and the reason, and sends that client nothing inside it"
) {
    netw_test::FaultWatch faults;
    FailedLevel world;
    world.stand.rig.pump(8);
    const int pawn_route = route_of(world.stand.seat(world.lobby, 0, "Pawn_"));
    world.stand.rig.pump(16);

    CHECK_FALSE(world.stand.rig.spawn_plane(0)->get_park().has(pawn_route));
    CHECK(world.stand.rig.route_node(pawn_route, 0) == nullptr);

    Node *box = Object::cast_to<Node>(
        world.stand.host->call(StringName("open_box"), String("Box"))
    );
    world.stand.rig.pump(8);
    const int crate_route = world.stand.rig.spawn_registered(
        StringName(HOST_ID),
        callable_mp_static(&build_host),
        Array::make(String("Crate")),
        Array::make(int(Variant::STRING)),
        box
    );
    world.stand.rig.pump(8);
    CHECK(world.stand.rig.route_node(crate_route, 1) != nullptr);
    CHECK_FALSE(world.stand.rig.spawn_plane(0)->get_park().has(crate_route));
    CHECK(world.stand.rig.route_node(crate_route, 0) == nullptr);

    NETW_CHECK_EQ(
        int(faults.warnings_containing(
            "Spawn function 'make_level' did not "
            "return a Node"
        )),
        1
    );
    NETW_CHECK_EQ(
        int(faults.warnings_containing(
            "Player 'ada' could not spawn a node, because 'make_level' "
            "returned no Node on their peer. Make 'make_level' return a Node "
            "on every peer."
        )),
        1
    );
    NETW_CHECK_EQ(int(faults.warnings_containing("does not exist")), 0);
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SF2 the server stops counting a client "
    "that could not build a level among its viewers, and viewer_exited fires "
    "for that player"
) {
    FailedLevel world;
    REQUIRE(world.views(0));
    world.stand.rig.pump(8);

    CHECK_FALSE(world.views(0));
    NETW_CHECK_EQ(g_viewers_exited, int64_t(1));
}

TEST_CASE(
    "[Networked][Scene][SceneTree] SF3 another client's level is untouched "
    "when one client could not build it"
) {
    FailedLevel world;
    world.stand.rig.pump(8);

    CHECK(world.views(1));
    CHECK(world.stand.rig.route_node(world.lobby_route, 1) != nullptr);
    CHECK(world.stand.scene_of(1) == "Lobby");
    CHECK(world.stand.rig.route_node(world.lobby_route, 0) == nullptr);
}

#endif

} // namespace TestNetwSceneDespawn

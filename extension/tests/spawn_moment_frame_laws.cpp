#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestSpawnMomentFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *SPAWNER_ID = "spawn_moment_spawner";

constexpr const char *PROBE_SOURCE = R"(extends Node

var stamp := 0
var moments: Array[Dictionary] = []

func _init() -> void:
	var part := Node.new()
	part.name = &"Part"
	add_child(part)
	Netw.configure_property(self, &"stamp").on_spawn()
	Netw.configure_entity(self)

func _enter_tree() -> void:
	note(&"enter_tree")

func _ready() -> void:
	note(&"ready")

func note(moment: StringName) -> void:
	var e := NetwEntity.of(self)
	var player := e.player
	moments.append({
		"moment": moment,
		"peer_id": e.peer_id,
		"player": player.peer_id if player != null else 0,
		"controller": e.controller.peer_id if e.controller != null else 1,
		"authority": e.authority,
		"godot_authority": get_multiplayer_authority(),
		"part_authority": $Part.get_multiplayer_authority(),
		"stamp": stamp,
		"name": String(name),
	}))";

constexpr const char *SPAWNER_SOURCE = R"(extends Node

var probe: Script

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_spawn(make_plain)
	Netw.configure_spawn(make_held)
	Netw.configure_spawn(make_steered)
	Netw.configure_spawn(make_immediate)

func make_plain() -> Node:
	return probe.new()

func make_held() -> Node:
	return probe.new()

func make_steered() -> Node:
	var made: Node = probe.new()
	Netw.configure_entity(made).authority = NetwEntity.AUTHORITY_CONTROLLER
	return made

func make_immediate() -> Node:
	var made: Node = probe.new()
	Netw.configure_entity(made).transfer = NetwEntity.TRANSFER_IMMEDIATE
	return made

func spawn_all(player: NetwPlayer) -> Array[Node]:
	var plain := Netw.spawn(make_plain)
	var held := Netw.spawn_player(player, make_held)
	var steered := Netw.spawn_player(player, make_steered)
	var immediate := Netw.spawn(make_immediate)
	var made: Array[Node] = [plain, held, steered, immediate]
	for node in made:
		node.stamp = 41
		get_parent().add_child(node)
	return made
)";

constexpr int CLIENTS = 1;
constexpr int A = 0;
constexpr int TICKRATE = 30;
constexpr int STAMP = 41;

enum Kind {
    PLAIN,
    HELD,
    STEERED,
    IMMEDIATE,
    KINDS,
};

enum Moment {
    ENTER_TREE,
    READY,
    MOMENTS,
};

const char *const KIND_NAMES[KINDS]
    = {"Netw.spawn",
       "spawn_player",
       "spawn_player AUTHORITY_CONTROLLER",
       "transfer only"};
const char *const MOMENT_NAMES[MOMENTS] = {"enter_tree", "ready"};

struct Seen {
    int order = -1;
    int64_t peer_id = -1;
    int64_t player = -1;
    int64_t controller = -1;
    int64_t authority = -1;
    int64_t godot_authority = -1;
    int64_t part_authority = -1;
    int64_t stamp = -1;
    String name;
};

struct Copy {
    bool found = false;
    int notes = 0;
    String name;
    Seen at[MOMENTS];
};

struct MomentEvidence {
    bool driven = false;
    bool seated = false;
    int64_t client_peer = 0;
    Copy copies[2][KINDS];
};

MomentEvidence &moment_evidence() {
    static MomentEvidence evidence;
    return evidence;
}

Script *spawner_script = nullptr;
Script *probe_script = nullptr;

Node *build_spawner(const Variant &p_name) {
    if (spawner_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(spawner_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
        made->set("probe", Variant(probe_script));
    }
    return made;
}

int moment_of(const StringName &p_name) {
    for (int at = 0; at < MOMENTS; ++at) {
        if (p_name == StringName(MOMENT_NAMES[at])) {
            return at;
        }
    }
    return -1;
}

void read_copy(Node *p_node, Copy &p_copy) {
    if (p_node == nullptr) {
        return;
    }
    p_copy.found = true;
    p_copy.name = String(p_node->get_name());
    const Array moments = p_node->get("moments");
    p_copy.notes = moments.size();
    for (int order = 0; order < moments.size(); ++order) {
        const Dictionary row = moments[order];
        const int at = moment_of(StringName(row["moment"]));
        if (at < 0 || p_copy.at[at].order >= 0) {
            continue;
        }
        Seen &seen = p_copy.at[at];
        seen.order = order;
        seen.peer_id = int64_t(row["peer_id"]);
        seen.player = int64_t(row["player"]);
        seen.controller = int64_t(row["controller"]);
        seen.authority = int64_t(row["authority"]);
        seen.godot_authority = int64_t(row["godot_authority"]);
        seen.part_authority = int64_t(row["part_authority"]);
        seen.stamp = int64_t(row["stamp"]);
        seen.name = String(row["name"]);
    }
}

class MomentScenario final : public netw_test::FrameScenario {
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> spawner;
    Ref<Script> probe;

    bool open() {
        spawner = netw_test::minted_script(SPAWNER_SOURCE);
        probe = netw_test::minted_script(PROBE_SOURCE);
        if (spawner.is_null() || probe.is_null()) {
            return false;
        }
        spawner_script = spawner.ptr();
        probe_script = probe.ptr();
        stand = new netw_test::SimStand(CLIENTS);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(
            StringName(SPAWNER_ID),
            callable_mp_static(&build_spawner)
        );
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        stand->session(A)->session_submit_join(StringName("a"), Array());
        stand->pump(4);
        NetwMultiplayer *server = stand->session(-1);
        const Ref<netw::NetwPlayer> player
            = server->peer_get_player(stand->peer_id(A));
        if (player.is_null() || arena == nullptr) {
            return false;
        }
        Array args;
        args.push_back(String("Spawner"));
        const RID made
            = server->spawn_registered(StringName(SPAWNER_ID), args, nullptr);
        Node *built = server->entity_get_node(made);
        if (built == nullptr) {
            return false;
        }
        arena->add_child(built);
        stand->pump(8);
        if (stand->node_at(A, int(server->entity_get_route(made))) == nullptr) {
            return false;
        }
        const Array spawned = built->call("spawn_all", player);
        stand->step_ticks(8);
        MomentEvidence &evidence = moment_evidence();
        evidence.client_peer = stand->peer_id(A);
        for (int kind = 0; kind < KINDS && kind < spawned.size(); ++kind) {
            Node *root = Object::cast_to<Node>(spawned[kind]);
            const Ref<NetwEntity> entity = NetwEntity::of(root);
            if (entity.is_null()) {
                continue;
            }
            const int route = int(entity->get_route());
            read_copy(root, evidence.copies[0][kind]);
            read_copy(stand->node_at(A, route), evidence.copies[1][kind]);
        }
        evidence.seated = true;
        return true;
    }

    void close() {
        moment_evidence().driven = true;
        delete stand;
        stand = nullptr;
        spawner_script = nullptr;
        probe_script = nullptr;
        spawner = Ref<Script>();
        probe = Ref<Script>();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        open();
        close();
        done = true;
        return false;
    }
};

NETW_FRAME_SCENARIO(MomentScenario, spawn_moment_scenario);

const char *const PEER_NAMES[2] = {"server", "client"};

TEST_CASE(
    "[Networked][Spawn][Frame] at _enter_tree a spawned root already reads "
    "its peer_id, player, controller and authority, and its multiplayer "
    "authority agrees with the authority mode, on the server and the client"
) {
    const MomentEvidence &evidence = moment_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    const int64_t client = evidence.client_peer;
    struct Expected {
        int64_t peer_id;
        int64_t controller;
        NetwEntity::Authority authority;
    };
    const Expected rows[KINDS] = {
        {0, 1, NetwEntity::AUTHORITY_SERVER},
        {client, client, NetwEntity::AUTHORITY_SERVER},
        {client, client, NetwEntity::AUTHORITY_CONTROLLER},
        {0, 1, NetwEntity::AUTHORITY_CONTROLLER},
    };
    for (int side = 0; side < 2; ++side) {
        for (int kind = 0; kind < KINDS; ++kind) {
            NETW_FORMAT_TEXT(peer_text, PEER_NAMES[side]);
            NETW_FORMAT_TEXT(kind_text, KIND_NAMES[kind]);
            CAPTURE(peer_text);
            CAPTURE(kind_text);
            const Copy &copy = evidence.copies[side][kind];
            REQUIRE(copy.found);
            const Expected &row = rows[kind];
            const int64_t godot_authority
                = row.authority == NetwEntity::AUTHORITY_CONTROLLER
                ? row.controller
                : int64_t(1);
            const Seen &seen = copy.at[ENTER_TREE];
            NETW_CHECK_EQ(seen.order, 0);
            NETW_CHECK_EQ(seen.peer_id, row.peer_id);
            NETW_CHECK_EQ(seen.player, row.peer_id);
            NETW_CHECK_EQ(seen.controller, row.controller);
            NETW_CHECK_EQ(seen.authority, int64_t(row.authority));
            NETW_CHECK_EQ(seen.godot_authority, godot_authority);
            NETW_CHECK_EQ(seen.part_authority, godot_authority);
        }
    }
}

TEST_CASE(
    "[Networked][Spawn][Frame] at _enter_tree a spawned root already reads "
    "its final node name, on the server and the client, unless a sibling "
    "already holds that name"
) {
    const MomentEvidence &evidence = moment_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    const bool taken[2][KINDS] = {
        {false, false, true, false},
        {false, false, false, false},
    };
    for (int side = 0; side < 2; ++side) {
        for (int kind = 0; kind < KINDS; ++kind) {
            NETW_FORMAT_TEXT(peer_text, PEER_NAMES[side]);
            NETW_FORMAT_TEXT(kind_text, KIND_NAMES[kind]);
            CAPTURE(peer_text);
            CAPTURE(kind_text);
            const Copy &copy = evidence.copies[side][kind];
            REQUIRE(copy.found);
            const String entered = copy.at[ENTER_TREE].name;
            NETW_FORMAT_TEXT(entered_text, entered.utf8().get_data());
            NETW_FORMAT_TEXT(final_text, copy.name.utf8().get_data());
            CAPTURE(entered_text);
            CAPTURE(final_text);
            CHECK_FALSE(copy.name.is_empty());
            CHECK(copy.name == copy.name.validate_node_name());
            if (!taken[side][kind]) {
                CHECK(entered == copy.name);
            }
        }
    }
}

TEST_CASE(
    "[Networked][Spawn][Frame] at _ready a spawned root already reads the "
    "server's value of an on_spawn property, on the server and the client"
) {
    const MomentEvidence &evidence = moment_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    for (int side = 0; side < 2; ++side) {
        for (int kind = 0; kind < KINDS; ++kind) {
            NETW_FORMAT_TEXT(peer_text, PEER_NAMES[side]);
            NETW_FORMAT_TEXT(kind_text, KIND_NAMES[kind]);
            CAPTURE(peer_text);
            CAPTURE(kind_text);
            const Copy &copy = evidence.copies[side][kind];
            REQUIRE(copy.found);
            NETW_CHECK_EQ(copy.at[READY].stamp, int64_t(STAMP));
        }
    }
}

} // namespace TestSpawnMomentFrameLaws

#endif

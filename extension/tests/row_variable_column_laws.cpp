#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/minted_script.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/replication_core.hpp"

namespace TestRowVariableColumn {

using namespace godot;
using namespace netw_test;
using netw::NetwControlRequest;
using netw::NetwEntity;
using netw::NetwPromise;

constexpr int TICKRATE = 30;
constexpr int CLAIM_FLIGHT_TICKS = 8;
constexpr const char *BOARD_ID = "variable_column_board";

constexpr const char *BOARD_SOURCE = R"(extends Node2D

var ink := PackedVector2Array()
var breaks := PackedInt32Array()
var pose := Transform3D()

func _init() -> void:
	var e := Netw.configure_entity(self)
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	Netw.configure_property(self, &"ink").broadcast()
	Netw.configure_property(self, &"breaks").broadcast()
	Netw.configure_property(self, &"pose").broadcast()

func draw(point: Vector2) -> void:
	ink.append(point)
)";

Script *board_script = nullptr;

Node *build_board(const Variant &p_name) {
    if (board_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(board_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct Board {
    LoopbackRig rig;
    Ref<Script> script;
    int route = 0;

    explicit Board(bool p_clocked = true) : rig(2) {
        script = minted_script(BOARD_SOURCE);
        board_script = script.ptr();
        rig.mount();
        if (p_clocked) {
            flow_clocks(rig, TICKRATE);
        }
        Node *arena = rig.mirror_child("Arena");
        Array types;
        types.push_back(int(Variant::STRING));
        Array args;
        args.push_back(String("Whiteboard"));
        route = rig.spawn_registered(
            StringName(BOARD_ID),
            callable_mp_static(&build_board),
            args,
            types,
            arena
        );
        rig.pump(4);
    }

    ~Board() {
        board_script = nullptr;
    }

    Node *node(int p_client) const {
        return rig.route_node(route, p_client);
    }

    bool every_peer_holds(
        const StringName &p_property,
        const Variant &p_value
    ) const {
        for (int client = -1; client < 2; ++client) {
            Node *held = node(client);
            if (held == nullptr || !(held->get(p_property) == p_value)) {
                return false;
            }
        }
        return true;
    }
};

PackedVector2Array stroke(int p_points) {
    PackedVector2Array made;
    for (int at = 0; at < p_points; ++at) {
        made.push_back(Vector2(float(at), float(at * 2)));
    }
    return made;
}

TEST_CASE(
    "[Networked][Sync][SceneTree] VC1 packed arrays and a Transform3D "
    "declared broadcast replicate from whoever controls the entity"
) {
    Board board;
    REQUIRE(board.node(0) != nullptr);
    REQUIRE(board.node(1) != nullptr);

    board.node(-1)->set("ink", stroke(3));
    board.rig.step_ticks(6);
    CHECK(board.every_peer_holds("ink", stroke(3)));

    NetwEntity::of(board.node(-1))->set_controller(board.rig.peer_id(1));
    board.rig.step_ticks(4);
    PackedInt32Array breaks;
    breaks.push_back(0);
    breaks.push_back(3);
    const Transform3D pose(Basis(Vector3(0, 1, 0), 0.5), Vector3(1, 2, 3));
    board.node(1)->set("ink", stroke(7));
    board.node(1)->set("breaks", breaks);
    board.node(1)->set("pose", pose);
    board.rig.step_ticks(6);
    CHECK(board.every_peer_holds("ink", stroke(7)));
    CHECK(board.every_peer_holds("breaks", breaks));
    CHECK(board.every_peer_holds("pose", pose));

    board.node(1)->set("ink", stroke(9));
    board.rig.step_ticks(6);
    CHECK(board.every_peer_holds("ink", stroke(9)));
    CHECK(board.every_peer_holds("breaks", breaks));
}

void draw_stroke(Node *p_node, int p_from, int p_to) {
    for (int at = p_from; at < p_to; ++at) {
        p_node->call("draw", Vector2(float(at), float(at * 2)));
    }
}

TEST_CASE(
    "[Networked][Sync][SceneTree] VC2 a packed array grown in place by its "
    "author replicates every growth"
) {
    Board board;
    REQUIRE(board.node(0) != nullptr);
    REQUIRE(board.node(1) != nullptr);

    for (int at = 0; at < 3; ++at) {
        draw_stroke(board.node(-1), at, at + 1);
        board.rig.step_ticks(3);
    }
    board.rig.step_ticks(3);
    CHECK(board.every_peer_holds("ink", stroke(3)));

    NetwEntity::of(board.node(-1))->set_controller(board.rig.peer_id(1));
    board.rig.step_ticks(4);
    draw_stroke(board.node(1), 3, 5);
    board.rig.step_ticks(6);
    CHECK(board.every_peer_holds("ink", stroke(5)));
    draw_stroke(board.node(1), 5, 6);
    board.rig.step_ticks(6);
    CHECK(board.every_peer_holds("ink", stroke(6)));
}

void deny_every_request(int64_t, Object *p_request) {
    NetwControlRequest *asked = Object::cast_to<NetwControlRequest>(p_request);
    if (asked != nullptr) {
        asked->deny();
    }
}

void quiet(int64_t, const String &) {
}

TEST_CASE(
    "[Networked][Sync][SceneTree] VC3 a refused claim writes back the board "
    "as received, whatever the claimant drew into it in place"
) {
    Board board;
    REQUIRE(board.node(0) != nullptr);
    board.node(-1)->set("ink", stroke(3));
    board.rig.step_ticks(6);
    REQUIRE(board.every_peer_holds("ink", stroke(3)));

    NetwEntity::of(board.node(-1))->connect(
        StringName("control_requested"),
        callable_mp_static(&deny_every_request)
    );
    Ref<netw::LocalLinkConditions> slow
        = netw::LocalLinkConditions::create(31);
    slow->set_latency_ms(double(CLAIM_FLIGHT_TICKS) * 1000.0 / TICKRATE);
    board.rig.conditions(-1, slow, board.rig.peer_id(0));

    const Ref<NetwEntity> claimant = NetwEntity::of(board.node(0));
    const Ref<NetwPromise> claim = claimant->claim_authority();
    claim->catch_error(callable_mp_static(&quiet));
    REQUIRE(claimant->get_is_controlled_locally());
    draw_stroke(board.node(0), 3, 7);
    board.rig.step_ticks(2);
    CHECK(PackedVector2Array(board.node(0)->get("ink")) == stroke(7));

    board.rig.step_ticks(CLAIM_FLIGHT_TICKS + 4);
    CHECK(claim->get_is_failed());
    CHECK(board.every_peer_holds("ink", stroke(3)));
}

TEST_CASE(
    "[Networked][Sync][SceneTree] VC4 a session that declares rows and has "
    "no clock warns once and sends none"
) {
    Board unclocked(false);
    REQUIRE(unclocked.node(0) != nullptr);
    CHECK_FALSE(unclocked.rig.server()->clock_is_configured());
    unclocked.node(-1)->set("ink", stroke(3));
    unclocked.rig.pump(12);
    CHECK(unclocked.rig.server()
              ->get_replication_plane()
              ->has_warned_rows_without_clock());
    CHECK(unclocked.rig.client(0)
              ->get_replication_plane()
              ->has_warned_rows_without_clock());
    CHECK(PackedVector2Array(unclocked.node(0)->get("ink")).is_empty());

    Board clocked;
    REQUIRE(clocked.node(0) != nullptr);
    clocked.node(-1)->set("ink", stroke(3));
    clocked.rig.step_ticks(6);
    CHECK(clocked.every_peer_holds("ink", stroke(3)));
    CHECK_FALSE(clocked.rig.server()
                    ->get_replication_plane()
                    ->has_warned_rows_without_clock());
}

} // namespace TestRowVariableColumn

#endif

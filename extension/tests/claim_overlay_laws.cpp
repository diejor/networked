#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/minted_script.h"
#include "support/netw_call_log.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/entity/control.hpp"

namespace TestClaimOverlay {

using namespace godot;
using namespace netw_test;
using netw::NetwControlRequest;
using netw::NetwEntity;
using netw::NetwPromise;
using Control = netw::entity::Control;

constexpr int TICKRATE = 30;
constexpr double TICK_MS = 1000.0 / TICKRATE;
constexpr int CLAIM_FLIGHT_TICKS = 8;
constexpr const char *BOARD_ID = "claim_overlay_board";

constexpr const char *BOARD_SOURCE = R"(extends Node2D

var strokes := 0

func _init() -> void:
	var e := Netw.configure_entity(self)
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	Netw.configure_property(self, &"strokes").broadcast().on_spawn()
)";

Script *board_script = nullptr;
int64_t first_board_strokes = 0;

Node *build_board(const Variant &p_name) {
    if (board_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(board_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
        made->set("strokes", first_board_strokes);
        first_board_strokes = 0;
    }
    return made;
}

void deny_every_request(int64_t, Object *p_request) {
    NetwControlRequest *asked = Object::cast_to<NetwControlRequest>(p_request);
    if (asked != nullptr) {
        asked->deny();
    }
}

Ref<netw::LocalLinkConditions> latency_of(int p_ticks) {
    Ref<netw::LocalLinkConditions> made
        = netw::LocalLinkConditions::create(31);
    made->set_latency_ms(double(p_ticks) * TICK_MS);
    return made;
}

struct Board {
    LoopbackRig rig;
    Ref<Script> script;
    int route = 0;

    explicit Board(int64_t p_first_strokes = 0) : rig(2) {
        script = minted_script(BOARD_SOURCE);
        board_script = script.ptr();
        first_board_strokes = p_first_strokes;
        rig.mount();
        flow_clocks(rig, TICKRATE);
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

    Ref<NetwEntity> entity(int p_client) const {
        Node *node = rig.route_node(route, p_client);
        return node == nullptr ? Ref<NetwEntity>() : NetwEntity::of(node);
    }

    int64_t strokes(int p_client) const {
        Node *node = rig.route_node(route, p_client);
        return node == nullptr ? -1 : int64_t(node->get("strokes"));
    }

    void draw(int p_client, int64_t p_strokes) const {
        Node *node = rig.route_node(route, p_client);
        if (node != nullptr) {
            node->set("strokes", p_strokes);
        }
    }

    void deny_at_the_server() const {
        entity(-1)->connect(
            StringName("control_requested"),
            callable_mp_static(&deny_every_request)
        );
    }

    void slow_requests_from(int p_client) {
        rig.conditions(
            -1,
            latency_of(CLAIM_FLIGHT_TICKS),
            rig.peer_id(p_client)
        );
    }

    void author_with(int p_client, int64_t p_strokes) {
        entity(-1)->set_controller(rig.peer_id(p_client));
        rig.step_ticks(4);
        draw(p_client, p_strokes);
        rig.step_ticks(6);
    }
};

void quiet(int64_t, const String &) {
}

Ref<NetwPromise> quietly(const Ref<NetwPromise> &p_promise) {
    p_promise->catch_error(callable_mp_static(&quiet));
    return p_promise;
}

TEST_CASE(
    "[Networked][Control][SceneTree] CO1 a refused immediate claim draws "
    "ahead of the decision, publishes nothing, and writes back the newest "
    "board its author sent, with or without a row during the claim"
) {
    bool author_writes = false;
    SUBCASE("the author sends nothing while the claim is out") {
        author_writes = false;
    }
    SUBCASE("the author sends a row while the claim is out") {
        author_writes = true;
    }
    Board stand;
    const int b = stand.rig.peer_id(1);
    stand.author_with(1, 2);
    NETW_REQUIRE_EQ(stand.strokes(0), 2);

    stand.deny_at_the_server();
    stand.slow_requests_from(0);
    const Ref<NetwEntity> claimant = stand.entity(0);
    CallLog log;
    claimant->connect(StringName("control_changed"), log.callable("changed"));

    const Ref<NetwPromise> claim = quietly(claimant->claim_authority());
    CHECK(claimant->get_is_controlled_locally());
    CHECK(claimant->get_is_control_pending());
    CHECK_FALSE(claim->get_is_settled());
    stand.draw(0, 9);

    if (author_writes) {
        stand.draw(1, 3);
    }
    stand.rig.step_ticks(3);
    NETW_CHECK_EQ(stand.strokes(0), 9);
    NETW_CHECK_EQ(stand.strokes(-1), author_writes ? 3 : 2);
    CHECK_FALSE(claim->get_is_settled());

    stand.rig.step_ticks(CLAIM_FLIGHT_TICKS + 4);
    CHECK(claim->get_is_failed());
    NETW_CHECK_EQ(int(claim->get_code()), int(ERR_UNAUTHORIZED));
    CHECK_FALSE(claimant->get_is_controlled_locally());
    CHECK_FALSE(claimant->get_is_control_pending());
    NETW_CHECK_EQ(log.count("changed"), 0);
    NETW_CHECK_EQ(claimant->get_controller(), int64_t(b));
    NETW_CHECK_EQ(stand.strokes(0), author_writes ? 3 : 2);
    NETW_CHECK_EQ(stand.strokes(-1), author_writes ? 3 : 2);
}

TEST_CASE(
    "[Networked][Control][SceneTree] CO2 a release issued while the "
    "caller's own claim is pending resolves whether the claim is granted "
    "or refused"
) {
    bool refused = false;
    SUBCASE("the claim is refused") {
        refused = true;
    }
    SUBCASE("the claim is granted") {
        refused = false;
    }
    Board stand;
    const int b = stand.rig.peer_id(1);
    stand.author_with(1, 4);
    if (refused) {
        stand.deny_at_the_server();
    }
    stand.slow_requests_from(0);
    const Ref<NetwEntity> claimant = stand.entity(0);

    const Ref<NetwPromise> claim = quietly(claimant->claim_authority());
    stand.draw(0, 5);
    const Ref<NetwPromise> release = claimant->release_authority();
    CHECK_FALSE(release->get_is_settled());
    stand.rig.step_ticks(CLAIM_FLIGHT_TICKS + 8);

    CHECK(release->get_is_completed());
    NETW_CHECK_EQ(int(claim->get_is_failed()), int(refused));
    CHECK_FALSE(claimant->get_is_control_pending());
    CHECK_FALSE(claimant->get_is_controlled_locally());
    const int64_t holder = refused ? int64_t(b) : int64_t(0);
    NETW_CHECK_EQ(stand.entity(-1)->get_controller(), holder);
    NETW_CHECK_EQ(claimant->get_controller(), holder);
    if (refused) {
        NETW_CHECK_EQ(stand.strokes(0), 4);
    }

    const Ref<NetwPromise> nothing
        = quietly(stand.entity(1)->release_authority());
    if (!refused) {
        CHECK(nothing->get_is_failed());
        NETW_CHECK_EQ(int(nothing->get_code()), int(ERR_UNAUTHORIZED));
    }
}

TEST_CASE(
    "[Networked][Control][SceneTree] CO3 a grab and then a throw issued "
    "before the grab is decided settle in order, and the throw keeps the "
    "grab's tenure"
) {
    Board stand;
    const int a = stand.rig.peer_id(0);
    stand.slow_requests_from(0);
    const Ref<NetwEntity> claimant = stand.entity(0);
    CallLog log;

    const Ref<NetwPromise> grab
        = claimant->claim_authority(Control::HOLD_EXCLUSIVE);
    grab->then(log.callable("grab"));
    const Ref<NetwPromise> toss
        = claimant->claim_authority(Control::HOLD_YIELDABLE);
    toss->then(log.callable("throw"));
    CHECK_FALSE(toss->get_is_settled());
    CHECK(claimant->get_is_controlled_locally());
    stand.rig.step_ticks(CLAIM_FLIGHT_TICKS + 6);

    CHECK(grab->get_is_completed());
    CHECK(toss->get_is_completed());
    const Vector<StringName> heard = log.order();
    const bool grab_first = heard.size() == 2
        && heard[0] == StringName("grab") && heard[1] == StringName("throw");
    CHECK(grab_first);
    const Ref<NetwEntity> host = stand.entity(-1);
    NETW_CHECK_EQ(host->get_controller(), int64_t(a));
    NETW_CHECK_EQ(int(host->get_hold()), int(Control::HOLD_YIELDABLE));
    NETW_CHECK_EQ(
        int64_t(host->get_control_tenure()),
        int64_t(host->get_control_revision()) - 1
    );
    NETW_CHECK_EQ(int(claimant->get_hold()), int(Control::HOLD_YIELDABLE));
}

TEST_CASE(
    "[Networked][Control][SceneTree] CO4 an immediate claim that times out "
    "writes back the author's board, and its late grant is released at once "
    "so every peer converges"
) {
    Board stand;
    stand.author_with(1, 7);
    const Ref<NetwEntity> claimant = stand.entity(0);
    CallLog log;
    claimant->connect(StringName("control_changed"), log.callable("changed"));

    stand.rig.hold(0);
    const Ref<NetwPromise> claim = quietly(claimant->claim_authority());
    stand.draw(0, 9);
    stand.rig.step_ticks(TICKRATE + 1);
    CHECK(claim->get_is_failed());
    NETW_CHECK_EQ(int(claim->get_code()), int(ERR_TIMEOUT));
    CHECK_FALSE(claimant->get_is_controlled_locally());
    NETW_CHECK_EQ(stand.strokes(0), 7);
    NETW_CHECK_EQ(log.count("changed"), 0);

    stand.rig.release(0);
    stand.rig.step_ticks(12);

    NETW_CHECK_EQ(log.count("changed"), 2);
    NETW_CHECK_EQ(int(claim->get_code()), int(ERR_TIMEOUT));
    for (int client = -1; client < 2; ++client) {
        NETW_FORMAT_INT(peer_text, client);
        CAPTURE(peer_text);
        NETW_CHECK_EQ(stand.entity(client)->get_controller(), int64_t(0));
        NETW_CHECK_EQ(stand.strokes(client), 7);
    }
}

TEST_CASE(
    "[Networked][Control][SceneTree] CO5 a claim issued before any row "
    "reached the claimant gives up to the board its spawn carried"
) {
    Board stand(3);
    const Ref<NetwEntity> claimant = stand.entity(0);
    NETW_REQUIRE_EQ(stand.strokes(0), 3);

    stand.rig.hold(0);
    const Ref<NetwPromise> claim = quietly(claimant->claim_authority());
    stand.draw(0, 9);
    stand.rig.step_ticks(TICKRATE + 1);

    CHECK(claim->get_is_failed());
    NETW_CHECK_EQ(int(claim->get_code()), int(ERR_TIMEOUT));
    NETW_CHECK_EQ(stand.strokes(0), 3);
    stand.rig.release(0);
    stand.rig.step_ticks(12);
}

} // namespace TestClaimOverlay

#endif

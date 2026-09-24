#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/lifecycle_request.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/wire/registry.hpp"
#include "support/minted_script.h"
#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

namespace TestLifecycleFilterLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwLifecycleRequest;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int SETTLE_TICKS = 24;
constexpr int64_t AUTHORED_HP = 7;

const char *JUDGED_SOURCE = R"(extends Node3D

static var verdicts := {}
static var relatch := false
static var asked := []
static var heard := []
var hp := 0


func _init() -> void:
	var entity: NetwEntity = Netw.configure_entity(self)
	entity.lifecycle = NetwEntity.LIFECYCLE_CONTROLLER
	entity.transfer = NetwEntity.TRANSFER_REQUESTABLE
	entity.lifecycle_requested.connect(judge)
	entity.lifecycle_refused.connect(refused)
	Netw.configure_property(self, &"hp").broadcast().on_spawn()


func judge(peer_id: int, request: NetwLifecycleRequest) -> void:
	var destination: Node = request.destination
	asked.append({
		"peer": peer_id,
		"kind": request.kind,
		"requester": request.requester,
		"hp": request.entity.owner.get(&"hp"),
		"placed": is_inside_tree(),
		"destination": String(destination.name) if destination != null else "",
	})
	var reason: String = verdicts.get(request.kind, "")
	if reason.is_empty():
		return
	request.deny(reason)
	if relatch:
		request.denied = false
		request.deny("again")


func refused(request: NetwLifecycleRequest) -> void:
	heard.append({
		"kind": request.kind,
		"reason": request.reason,
		"denied": request.denied,
		"at": multiplayer.get_unique_id(),
	})
)";

Node *build_judged(const Variant &p_name, const Variant &p_script) {
    const Ref<Script> script = p_script;
    Node3D *made
        = Object::cast_to<Node3D>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    return made;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Node3D *plain_node(Node *p_parent, const char *p_name) {
    Node3D *made = memnew(Node3D);
    made->set_name(p_name);
    p_parent->add_child(made);
    return made;
}

int64_t frames_in(NetwMultiplayer *p_api, int64_t p_from, const char *p_name) {
    const Dictionary rows
        = p_api->attribution_snapshot()["frames_in_by_channel"];
    const Dictionary channels = rows.get(p_from, Dictionary());
    return int64_t(channels.get(
        int64_t(netw::wire::builtin_channel(StringName(p_name))),
        0
    ));
}

int64_t counter_of(NetwMultiplayer *p_api, const char *p_name) {
    const Dictionary counted
        = p_api->get_replication_plane()->get_spawn_pipeline()->counters();
    return int64_t(counted.get(StringName(p_name), -1));
}

class FilterStage {
public:
    LoopbackRig rig;
    Ref<Script> judged;
    Node *arenas[3] = {nullptr, nullptr, nullptr};
    int crate = 0;

    FilterStage() : rig(2) {
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        rig.mirror_child("Arena");
        for (int side = -1; side < 2; ++side) {
            arenas[side + 1]
                = rig.branch(side)->get_node_or_null(NodePath("Arena"));
            REQUIRE(arenas[side + 1] != nullptr);
            plain_node(arenas[side + 1], "Shelf");
        }
        judged = netw_test::minted_script(JUDGED_SOURCE);
        REQUIRE(judged.is_valid());
        rig.join(0, StringName("author"));
        rig.join(1, StringName("observer"));
        rig.step_ticks(4);
        crate = rig.spawn_registered(
            StringName("judged_crate"),
            callable_mp_static(&build_judged).bind(judged),
            named("Crate"),
            one_type(),
            arenas[0]
        );
        NetwEntity::of(rig.route_node(crate))->set_controller(rig.peer_id(0));
        rig.step_ticks(6);
    }

    void deny(NetwLifecycleRequest::Kind p_kind, const char *p_reason) {
        Dictionary verdicts = judged->get(StringName("verdicts"));
        verdicts[int64_t(p_kind)] = String(p_reason);
    }

    Array asked() const {
        return judged->get(StringName("asked"));
    }

    Array heard() const {
        return judged->get(StringName("heard"));
    }

    Node *arena_on(int p_side) const {
        return arenas[p_side + 1];
    }

    Node *shelf_on(int p_side) const {
        return arena_on(p_side)->get_node_or_null(NodePath("Shelf"));
    }

    bool under(int p_route, int p_side, Node *p_parent) const {
        Node *held = rig.route_node(p_route, p_side);
        return held != nullptr && p_parent != nullptr
            && held->get_parent() == p_parent;
    }

    Node *author_spawns(const char *p_name) {
        NetwMultiplayer *author = rig.client(0);
        const RID entity = author->spawn_registered(
            StringName("judged_crate"),
            named(p_name),
            nullptr
        );
        Node *node = author->entity_get_node(entity);
        if (node != nullptr) {
            node->set(StringName("hp"), AUTHORED_HP);
            arena_on(0)->add_child(node);
        }
        return node;
    }

    Ref<NetwPromise> author_moves(Node *p_parent) {
        NetwMultiplayer *author = rig.client(0);
        return author->entity_reparent(
            author->entity_from_route(crate),
            p_parent
        );
    }

    void author_despawns() {
        NetwMultiplayer *author = rig.client(0);
        Node *freed = rig.route_node(crate, 0);
        REQUIRE(freed != nullptr);
        NETW_REQUIRE_EQ(
            author->entity_despawn(
                author->entity_from_route(crate),
                Ref<netw::NetwDespawnOpts>()
            ),
            OK
        );
        freed->get_parent()->remove_child(freed);
    }

    bool heard_once(
        NetwLifecycleRequest::Kind p_kind,
        const char *p_reason
    ) const {
        const Array rows = heard();
        if (rows.size() != 1) {
            return false;
        }
        const Dictionary row = rows[0];
        return int64_t(row["kind"]) == int64_t(p_kind)
            && String(row["reason"]) == String(p_reason) && bool(row["denied"])
            && int64_t(row["at"]) == rig.peer_id(0);
    }
};

int route_of(Node *p_node) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    return entity.is_valid() ? int(entity->get_route()) : 0;
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] deny, a spawn the session's "
    "handler denies is taken back from the author with the reason and never "
    "reaches an observer"
) {
    FilterStage stage;
    NetwMultiplayer *session = stage.rig.server();
    NetwMultiplayer *observer = stage.rig.client(1);
    const int64_t session_peer = stage.rig.peer_id(-1);
    const int64_t observed = frames_in(observer, session_peer, "SPAWN");
    stage.deny(NetwLifecycleRequest::KIND_SPAWN, "cell taken");

    Node *made = stage.author_spawns("Placed");
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    REQUIRE(route > 0);
    stage.rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(1));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(0));
    NETW_CHECK_EQ(stage.asked().size(), 1);
    CHECK(stage.heard_once(NetwLifecycleRequest::KIND_SPAWN, "cell taken"));
    for (int at = -1; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        const bool held = stage.rig.route_node(route, at) != nullptr;
        CHECK_FALSE(held);
    }
    NETW_CHECK_EQ(frames_in(observer, session_peer, "SPAWN") - observed, 0);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] deny, a move the session's "
    "handler denies is moved back on the author, rejects its promise with "
    "the reason and never reaches an observer"
) {
    FilterStage stage;
    NetwMultiplayer *session = stage.rig.server();
    NetwMultiplayer *observer = stage.rig.client(1);
    const int64_t session_peer = stage.rig.peer_id(-1);
    const int64_t observed = frames_in(observer, session_peer, "REPARENT");
    stage.deny(NetwLifecycleRequest::KIND_REPARENT, "shelf full");

    const Ref<NetwPromise> moved = stage.author_moves(stage.shelf_on(0));
    stage.rig.step_ticks(SETTLE_TICKS);

    CHECK(moved->get_is_failed());
    NETW_CHECK_EQ(moved->get_code(), ERR_UNAUTHORIZED);
    CHECK(moved->get_detail() == String("shelf full"));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(1));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(0));
    CHECK(stage.heard_once(NetwLifecycleRequest::KIND_REPARENT, "shelf full"));
    for (int at = -1; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(stage.under(stage.crate, at, stage.arena_on(at)));
    }
    NETW_CHECK_EQ(
        int64_t(stage.rig.client(0)->liveness_route_anchor(stage.crate)),
        int64_t(session->liveness_route_anchor(stage.crate))
    );
    NETW_CHECK_EQ(frames_in(observer, session_peer, "REPARENT") - observed, 0);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] deny, a despawn the session's "
    "handler denies serves the entity back to the author with the reason "
    "and never touches an observer"
) {
    FilterStage stage;
    NetwMultiplayer *session = stage.rig.server();
    NetwMultiplayer *observer = stage.rig.client(1);
    const int64_t session_peer = stage.rig.peer_id(-1);
    Node *seen = stage.rig.route_node(stage.crate, 1);
    REQUIRE(seen != nullptr);
    const int64_t observed = frames_in(observer, session_peer, "DESPAWN");
    stage.deny(NetwLifecycleRequest::KIND_DESPAWN, "not yours");

    stage.author_despawns();
    stage.rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(1));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(0));
    CHECK(stage.heard_once(NetwLifecycleRequest::KIND_DESPAWN, "not yours"));
    for (int at = -1; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(stage.under(stage.crate, at, stage.arena_on(at)));
    }
    const bool untouched = stage.rig.route_node(stage.crate, 1) == seen;
    CHECK(untouched);
    NETW_CHECK_EQ(frames_in(observer, session_peer, "DESPAWN") - observed, 0);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] values, the session's handler "
    "reads the author's spawn state on a copy not yet placed, and its "
    "destination"
) {
    FilterStage stage;
    NetwMultiplayer *session = stage.rig.server();
    Node *made = stage.author_spawns("Placed");
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    stage.rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(1));
    CHECK(stage.under(route, 1, stage.arena_on(1)));
    const Array asked = stage.asked();
    NETW_CHECK_EQ(asked.size(), 1);
    if (asked.size() != 1) {
        return;
    }
    const Dictionary row = asked[0];
    NETW_CHECK_EQ(
        int64_t(row["kind"]),
        int64_t(NetwLifecycleRequest::KIND_SPAWN)
    );
    NETW_CHECK_EQ(int64_t(row["peer"]), int64_t(stage.rig.peer_id(0)));
    NETW_CHECK_EQ(int64_t(row["requester"]), int64_t(stage.rig.peer_id(0)));
    NETW_CHECK_EQ(int64_t(row["hp"]), AUTHORED_HP);
    CHECK_FALSE(bool(row["placed"]));
    CHECK(String(row["destination"]) == String("Arena"));
    NETW_CHECK_EQ(stage.heard().size(), 0);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] quiet, the session's own "
    "spawn, move and despawn never ask the handler"
) {
    FilterStage stage;
    NetwMultiplayer *session = stage.rig.server();
    stage.deny(NetwLifecycleRequest::KIND_SPAWN, "no");
    stage.deny(NetwLifecycleRequest::KIND_REPARENT, "no");
    stage.deny(NetwLifecycleRequest::KIND_DESPAWN, "no");

    const Ref<NetwPromise> moved = session->entity_reparent(
        session->entity_from_route(stage.crate),
        stage.shelf_on(-1)
    );
    stage.rig.step_ticks(SETTLE_TICKS);
    CHECK_FALSE(moved->get_is_failed());
    CHECK(stage.under(stage.crate, 1, stage.shelf_on(1)));
    NETW_CHECK_EQ(
        session->entity_despawn(
            session->entity_from_route(stage.crate),
            Ref<netw::NetwDespawnOpts>()
        ),
        OK
    );
    Node *gone = stage.rig.route_node(stage.crate);
    gone->get_parent()->remove_child(gone);
    stage.rig.step_ticks(SETTLE_TICKS);

    const bool observed = stage.rig.route_node(stage.crate, 1) != nullptr;
    CHECK_FALSE(observed);
    NETW_CHECK_EQ(stage.asked().size(), 0);
    NETW_CHECK_EQ(stage.heard().size(), 0);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] latch, a write after a denial "
    "does not undo it, and the first reason reaches the author"
) {
    FilterStage stage;
    stage.judged->set(StringName("relatch"), true);
    stage.deny(NetwLifecycleRequest::KIND_REPARENT, "first");

    const Ref<NetwPromise> moved = stage.author_moves(stage.shelf_on(0));
    stage.rig.step_ticks(SETTLE_TICKS);

    CHECK(moved->get_is_failed());
    CHECK(moved->get_detail() == String("first"));
    CHECK(stage.heard_once(NetwLifecycleRequest::KIND_REPARENT, "first"));
    for (int at = -1; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(stage.under(stage.crate, at, stage.arena_on(at)));
    }
}

} // namespace TestLifecycleFilterLaws

#endif

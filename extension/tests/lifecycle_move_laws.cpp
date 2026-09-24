#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/wire/registry.hpp"
#include "support/minted_script.h"
#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

namespace TestLifecycleMoveLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int SETTLE_TICKS = 24;
constexpr int64_t SHELF = 2;
constexpr int64_t HAND = 3;
constexpr int64_t GHOST = 9;

const char *PROBE_SOURCE = R"(extends Node3D

var counter := 0
var seats := []
var synced_value: int:
	get:
		return frame_tag() * 1000 + counter
	set(value):
		seats.append(Vector2i(value, frame_tag()))


func _init() -> void:
	Netw.configure_property(self, &"synced_value").broadcast()


func frame_tag() -> int:
	var parent := get_parent()
	if parent == null:
		return 0
	if parent.name == &"Shelf":
		return 2
	if parent.name == &"Hand":
		return 3
	if parent.name == &"Ghost":
		return 9
	return 1
)";

Node *build_avatar(const Variant &p_name) {
    Node3D *made = memnew(Node3D);
    made->set_name(String(p_name));
    made->set_position(Vector3(2.0, 0.0, 1.0));
    Node3D *hand = memnew(Node3D);
    hand->set_name("Hand");
    hand->set_position(Vector3(0.5, 1.0, 0.0));
    made->add_child(hand);
    return made;
}

Node *build_cube(
    const Variant &p_name,
    const Variant &p_script,
    const Variant &p_transfer
) {
    const Ref<Script> script = p_script;
    Node *made
        = Object::cast_to<Node>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    const Ref<NetwEntity> entity = NetwEntity::ensure(made);
    entity->set_lifecycle(NetwEntity::LIFECYCLE_CONTROLLER);
    entity->set_transfer(NetwEntity::Transfer(int(p_transfer)));
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

int64_t channel_named(const char *p_name) {
    return int64_t(netw::wire::builtin_channel(StringName(p_name)));
}

int64_t frames_in(
    NetwMultiplayer *p_session,
    int64_t p_from,
    const char *p_name
) {
    const Dictionary rows
        = p_session->attribution_snapshot()["frames_in_by_channel"];
    const Dictionary channels = rows.get(p_from, Dictionary());
    return int64_t(channels.get(channel_named(p_name), 0));
}

int64_t bytes_out(
    NetwMultiplayer *p_session,
    int64_t p_to,
    const char *p_name
) {
    const Dictionary rows = p_session->attribution_snapshot()["bytes_out"];
    const Dictionary channels = rows.get(p_to, Dictionary());
    return int64_t(channels.get(channel_named(p_name), 0));
}

int64_t counter_of(NetwMultiplayer *p_session, const char *p_name) {
    const Dictionary counted
        = p_session->get_replication_plane()->get_spawn_pipeline()->counters();
    return int64_t(counted.get(StringName(p_name), -1));
}

struct Seats {
    int64_t seated = 0;
    int64_t misplaced = 0;
    int64_t from_the_branch = 0;
};

class MoveStage {
public:
    LoopbackRig rig;
    Ref<Script> probe;
    Node *arenas[3] = {nullptr, nullptr, nullptr};
    int avatar = 0;
    int cube = 0;

    explicit MoveStage(
        NetwEntity::Transfer p_transfer = NetwEntity::TRANSFER_REQUESTABLE
    ) :
            rig(2) {
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        rig.mirror_child("Arena");
        for (int side = -1; side < 2; ++side) {
            arenas[side + 1]
                = rig.branch(side)->get_node_or_null(NodePath("Arena"));
            REQUIRE(arenas[side + 1] != nullptr);
            plain_node(arenas[side + 1], "Shelf");
        }
        plain_node(arenas[1], "Ghost");
        probe = netw_test::minted_script(PROBE_SOURCE);
        REQUIRE(probe.is_valid());
        avatar = rig.spawn_registered(
            StringName("lifecycle_avatar"),
            callable_mp_static(&build_avatar),
            named("Avatar"),
            one_type(),
            arenas[0]
        );
        cube = rig.spawn_registered(
            StringName("lifecycle_cube"),
            callable_mp_static(&build_cube).bind(probe, int(p_transfer)),
            named("Cube"),
            one_type(),
            arenas[0]
        );
        NetwEntity::of(rig.route_node(cube))->set_controller(rig.peer_id(0));
        rig.step_ticks(6);
    }

    NetwMultiplayer *side(int p_side) const {
        return p_side < 0 ? rig.server() : rig.client(p_side);
    }

    Node *cube_on(int p_side) const {
        return rig.route_node(cube, p_side);
    }

    Node *hand_on(int p_side) const {
        Node *held = rig.route_node(avatar, p_side);
        return held != nullptr ? held->get_node_or_null(NodePath("Hand"))
                               : nullptr;
    }

    Node *arena_on(int p_side) const {
        return arenas[p_side + 1];
    }

    Node *named_on(int p_side, const char *p_name) const {
        return arena_on(p_side)->get_node_or_null(NodePath(p_name));
    }

    int64_t anchor_on(int p_side) const {
        return int64_t(side(p_side)->liveness_route_anchor(cube));
    }

    Ref<NetwPromise> controller_moves(Node *p_parent) {
        return moves_on(0, p_parent);
    }

    Ref<NetwPromise> moves_on(int p_side, Node *p_parent) {
        NetwMultiplayer *author = side(p_side);
        return author->entity_reparent(
            author->entity_from_route(cube),
            p_parent
        );
    }

    bool all_under(const char *p_name) const {
        for (int at = -1; at < 2; ++at) {
            if (!under(at, named_on(at, p_name))) {
                return false;
            }
        }
        return true;
    }

    bool all_under_arena() const {
        for (int at = -1; at < 2; ++at) {
            if (!under(at, arena_on(at))) {
                return false;
            }
        }
        return true;
    }

    bool anchors_agree() const {
        const NetwMultiplayer::AnchorRevision truth
            = side(-1)->anchor_installed(cube);
        for (int at = 0; at < 2; ++at) {
            const NetwMultiplayer::AnchorRevision here
                = side(at)->anchor_installed(cube);
            if (here.revision != truth.revision
                || here.author != truth.author) {
                return false;
            }
        }
        return true;
    }

    void write_ticks(int p_ticks) {
        Node *writer = cube_on(0);
        for (int step = 0; step < p_ticks; ++step) {
            if (writer != nullptr) {
                writer->set(
                    StringName("counter"),
                    int64_t(writer->get(StringName("counter"))) + 1
                );
            }
            rig.step_ticks(1);
        }
    }

    void clear_seats() {
        for (int at = -1; at < 2; ++at) {
            if (Node *held = cube_on(at)) {
                held->set(StringName("seats"), Array());
            }
        }
    }

    Seats seats_on(int p_side, int64_t p_branch_a, int64_t p_branch_b) const {
        Seats seen;
        Node *held = cube_on(p_side);
        if (held == nullptr) {
            return seen;
        }
        const Array seats = held->get(StringName("seats"));
        for (int at = 0; at < seats.size(); ++at) {
            const Vector2i seat = seats[at];
            const int64_t written = int64_t(seat.x) / 1000;
            const int64_t holding = int64_t(seat.y);
            seen.seated += 1;
            if (written != holding && holding != 0) {
                seen.misplaced += 1;
            }
            if (written == p_branch_a || written == p_branch_b) {
                seen.from_the_branch += 1;
            }
        }
        return seen;
    }

    bool under(int p_side, Node *p_parent) const {
        Node *held = cube_on(p_side);
        return held != nullptr && p_parent != nullptr
            && held->get_parent() == p_parent;
    }
};

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] HC4 a controller's move of its "
    "LIFECYCLE_CONTROLLER entity reaches every peer, the session holds it "
    "under the controller's hand, and the author never hears it back"
) {
    MoveStage stage;
    REQUIRE(stage.cube_on(0) != nullptr);
    REQUIRE(stage.cube_on(1) != nullptr);
    NETW_CHECK_EQ(
        NetwEntity::of(stage.cube_on(0))->get_controller(),
        int64_t(stage.rig.peer_id(0))
    );
    stage.write_ticks(6);
    stage.clear_seats();

    NetwMultiplayer *author = stage.rig.client(0);
    const int64_t session_peer = stage.rig.peer_id(-1);
    const int64_t author_peer = stage.rig.peer_id(0);
    const int64_t echoed = frames_in(author, session_peer, "REPARENT");
    const int64_t stale = counter_of(author, "drops_reparent_stale");
    const int64_t sent = bytes_out(author, session_peer, "REPARENT");
    const int64_t decided
        = bytes_out(stage.rig.server(), author_peer, "LIFECYCLE_DECISION");

    const Ref<NetwPromise> moved = stage.controller_moves(stage.hand_on(0));
    stage.write_ticks(SETTLE_TICKS);

    CHECK(moved->get_is_settled());
    CHECK_FALSE(moved->get_is_failed());
    CHECK(stage.under(-1, stage.hand_on(-1)));
    CHECK(stage.under(0, stage.hand_on(0)));
    CHECK(stage.under(1, stage.hand_on(1)));
    NETW_CHECK_EQ(stage.anchor_on(-1), int64_t(2));
    NETW_CHECK_EQ(stage.anchor_on(0), int64_t(2));
    NETW_CHECK_EQ(stage.anchor_on(1), int64_t(2));
    CHECK_FALSE(NetwEntity::of(stage.cube_on(0))->has_structure_ops());
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_admitted"),
        int64_t(1)
    );
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_refused"),
        int64_t(0)
    );

    SUBCASE("echo, the author never receives its own REPARENT back") {
        NETW_CHECK_EQ(
            frames_in(author, session_peer, "REPARENT") - echoed,
            int64_t(0)
        );
        NETW_CHECK_EQ(
            counter_of(author, "drops_reparent_stale") - stale,
            int64_t(0)
        );
    }

    SUBCASE("rows written under the hand are seated under the hand") {
        const Seats session = stage.seats_on(-1, HAND, HAND);
        const Seats observer = stage.seats_on(1, HAND, HAND);
        NETW_CHECK_EQ(session.misplaced, int64_t(0));
        NETW_CHECK_EQ(observer.misplaced, int64_t(0));
        NETW_CHECK_GT(session.from_the_branch, int64_t(0));
        NETW_CHECK_GT(observer.from_the_branch, int64_t(0));
    }

    std::printf(
        "lifecycle cost accepted REPARENT %lld LIFECYCLE_DECISION %lld\n",
        (long long)(bytes_out(author, session_peer, "REPARENT") - sent),
        (long long)(bytes_out(
                        stage.rig.server(),
                        author_peer,
                        "LIFECYCLE_DECISION"
                    )
                    - decided)
    );
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] chain, a refused move refuses "
    "every later move the author built on it, the author ends at the "
    "session's anchor, and no row from the refused branch is seated anywhere"
) {
    MoveStage stage;
    REQUIRE(stage.cube_on(0) != nullptr);
    stage.write_ticks(6);
    stage.clear_seats();

    NetwMultiplayer *author = stage.rig.client(0);
    const int64_t session_peer = stage.rig.peer_id(-1);
    const int64_t author_peer = stage.rig.peer_id(0);
    const int64_t decided
        = bytes_out(stage.rig.server(), author_peer, "LIFECYCLE_DECISION");
    const int64_t sent = bytes_out(author, session_peer, "REPARENT");

    stage.rig.hold(0);
    const Ref<NetwPromise> first
        = stage.controller_moves(stage.named_on(0, "Ghost"));
    stage.write_ticks(2);
    const Ref<NetwPromise> second
        = stage.controller_moves(stage.named_on(0, "Shelf"));
    stage.write_ticks(6);
    NETW_CHECK_EQ(stage.anchor_on(0), int64_t(3));
    stage.rig.release(0);
    stage.write_ticks(SETTLE_TICKS);

    CHECK(first->get_is_failed());
    CHECK(second->get_is_failed());
    NETW_CHECK_EQ(first->get_code(), ERR_INVALID_PARAMETER);
    CHECK(stage.under(-1, stage.arena_on(-1)));
    CHECK(stage.under(0, stage.arena_on(0)));
    CHECK(stage.under(1, stage.arena_on(1)));
    NETW_CHECK_EQ(stage.anchor_on(0), stage.anchor_on(-1));
    NETW_CHECK_EQ(stage.anchor_on(1), stage.anchor_on(-1));
    CHECK_FALSE(NetwEntity::of(stage.cube_on(0))->has_structure_ops());
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_admitted"),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_refused"),
        int64_t(2)
    );

    const Seats session = stage.seats_on(-1, GHOST, SHELF);
    const Seats observer = stage.seats_on(1, GHOST, SHELF);
    NETW_CHECK_EQ(session.from_the_branch, int64_t(0));
    NETW_CHECK_EQ(observer.from_the_branch, int64_t(0));
    NETW_CHECK_EQ(session.misplaced, int64_t(0));
    NETW_CHECK_EQ(observer.misplaced, int64_t(0));
    NETW_CHECK_GT(session.seated, int64_t(0));

    std::printf(
        "lifecycle cost refused pair REPARENT %lld LIFECYCLE_DECISION %lld\n",
        (long long)(bytes_out(author, session_peer, "REPARENT") - sent),
        (long long)(bytes_out(
                        stage.rig.server(),
                        author_peer,
                        "LIFECYCLE_DECISION"
                    )
                    - decided)
    );
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] arbiter, a session move while "
    "a controller's move is in flight refuses it, and every peer ends at the "
    "session's anchor"
) {
    MoveStage stage;
    REQUIRE(stage.cube_on(0) != nullptr);
    stage.write_ticks(6);

    stage.rig.hold(-1);
    const Ref<NetwPromise> refused
        = stage.controller_moves(stage.named_on(0, "Shelf"));
    stage.write_ticks(2);
    NetwMultiplayer *session = stage.rig.server();
    const Ref<NetwPromise> ruled = session->entity_reparent(
        session->entity_from_route(stage.cube),
        stage.hand_on(-1)
    );
    stage.write_ticks(2);
    stage.rig.release(-1);
    stage.write_ticks(SETTLE_TICKS);

    CHECK(ruled->get_is_settled());
    CHECK_FALSE(ruled->get_is_failed());
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(refused->get_code(), ERR_UNAUTHORIZED);
    CHECK(stage.under(-1, stage.hand_on(-1)));
    CHECK(stage.under(0, stage.hand_on(0)));
    CHECK(stage.under(1, stage.hand_on(1)));
    NETW_CHECK_EQ(stage.anchor_on(0), stage.anchor_on(-1));
    NETW_CHECK_EQ(stage.anchor_on(1), stage.anchor_on(-1));
    CHECK_FALSE(NetwEntity::of(stage.cube_on(0))->has_structure_ops());
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] cap, a controller's move past "
    "eight waiting on the session is refused and moved back on the author "
    "and never sent"
) {
    MoveStage stage;
    REQUIRE(stage.cube_on(0) != nullptr);
    stage.write_ticks(6);
    NetwMultiplayer *session = stage.rig.server();
    const int64_t author_peer = stage.rig.peer_id(0);
    const int64_t sent = frames_in(session, author_peer, "REPARENT");

    stage.rig.hold(0);
    Ref<NetwPromise> moves[9];
    for (int at = 0; at < 9; ++at) {
        Node *parent
            = at % 2 == 0 ? stage.named_on(0, "Shelf") : stage.arena_on(0);
        moves[at] = stage.controller_moves(parent);
        stage.write_ticks(1);
    }
    CHECK(moves[8]->get_is_failed());
    NETW_CHECK_EQ(moves[8]->get_code(), ERR_UNAVAILABLE);
    CHECK(stage.under(0, stage.arena_on(0)));
    stage.rig.release(0);
    stage.write_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(frames_in(session, author_peer, "REPARENT") - sent, 8);
    for (int at = 0; at < 8; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(moves[at]->get_is_settled());
        CHECK_FALSE(moves[at]->get_is_failed());
    }
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(8));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(0));
    for (int at = -1; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(stage.under(at, stage.arena_on(at)));
        NETW_CHECK_EQ(stage.anchor_on(at), int64_t(9));
    }
    CHECK_FALSE(NetwEntity::of(stage.cube_on(0))->has_structure_ops());
}

void deny_control(
    int64_t p_peer_id,
    const Ref<netw::NetwControlRequest> &p_request
) {
    p_request->deny();
}

PackedByteArray move_op(
    NetwMultiplayer *p_author,
    int64_t p_route,
    Node *p_parent,
    const NetwMultiplayer::AnchorRevision &p_base
) {
    netw::wire::WriteStream stream;
    uint64_t revision = p_base.revision;
    uint64_t author = p_base.author;
    REQUIRE(p_author->verb_head_write(stream, p_route));
    REQUIRE(p_author->anchor_encode(stream, p_parent));
    REQUIRE(stream.varuint(revision, 5));
    REQUIRE(stream.varuint(author, 5));
    REQUIRE(stream.align_verify());
    return stream.to_bytes();
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] arbiter, a controller's lane "
    "opened at the revision number the session already installed for its own "
    "move names another author, so it seats nowhere even when it reaches the "
    "session before the controller's move does"
) {
    MoveStage stage;
    REQUIRE(stage.cube_on(0) != nullptr);
    stage.write_ticks(6);
    stage.clear_seats();

    NetwMultiplayer *session = stage.rig.server();
    NetwMultiplayer *author = stage.rig.client(0);
    const int author_peer = stage.rig.peer_id(0);
    const NetwMultiplayer::AnchorRevision base
        = author->anchor_installed(stage.cube);

    stage.rig.hold(0);
    const Ref<NetwPromise> ruled = session->entity_reparent(
        session->entity_from_route(stage.cube),
        stage.hand_on(-1)
    );
    stage.write_ticks(2);
    NETW_REQUIRE_EQ(stage.anchor_on(-1), int64_t(2));

    stage.rig.hold(-1);
    const Ref<NetwPromise> refused
        = stage.controller_moves(stage.named_on(0, "Shelf"));
    stage.write_ticks(2);
    NETW_REQUIRE_EQ(stage.anchor_on(0), int64_t(2));
    stage.rig.session()->purge_packets_from(author_peer);
    stage.rig.release(-1);
    stage.rig.release(0);
    const int64_t control_in = frames_in(session, author_peer, "ROW_CONTROL");
    stage.write_ticks(SETTLE_TICKS);

    NETW_CHECK_GT(
        frames_in(session, author_peer, "ROW_CONTROL") - control_in,
        int64_t(0)
    );
    CHECK(NetwEntity::of(stage.cube_on(0))->has_structure_ops());
    CHECK(stage.under(0, stage.named_on(0, "Shelf")));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(0));

    session->spawn_handle_reparent_frame(
        move_op(author, stage.cube, stage.named_on(0, "Shelf"), base),
        author_peer
    );
    stage.write_ticks(SETTLE_TICKS);

    CHECK(ruled->get_is_settled());
    CHECK_FALSE(ruled->get_is_failed());
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(refused->get_code(), ERR_UNAUTHORIZED);
    CHECK(stage.under(-1, stage.hand_on(-1)));
    CHECK(stage.under(0, stage.hand_on(0)));
    CHECK(stage.under(1, stage.hand_on(1)));
    NETW_CHECK_EQ(stage.anchor_on(0), stage.anchor_on(-1));
    NETW_CHECK_EQ(stage.anchor_on(1), stage.anchor_on(-1));
    NETW_CHECK_EQ(
        int64_t(author->anchor_installed(stage.cube).author),
        int64_t(session->anchor_installed(stage.cube).author)
    );

    const Seats at_session = stage.seats_on(-1, SHELF, SHELF);
    const Seats at_observer = stage.seats_on(1, SHELF, SHELF);
    NETW_CHECK_EQ(at_session.from_the_branch, int64_t(0));
    NETW_CHECK_EQ(at_observer.from_the_branch, int64_t(0));
    NETW_CHECK_GT(at_session.seated, int64_t(0));
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] order, a drop then a release "
    "reaches the session as the move and then the release"
) {
    MoveStage stage;
    REQUIRE(stage.cube_on(0) != nullptr);
    stage.write_ticks(6);

    Node *held = stage.cube_on(0);
    held->reparent(stage.named_on(0, "Shelf"));
    const Ref<NetwPromise> released = NetwEntity::of(held)->release_authority();
    stage.write_ticks(SETTLE_TICKS);

    CHECK(released->get_is_settled());
    CHECK_FALSE(released->get_is_failed());
    CHECK(stage.under(-1, stage.named_on(-1, "Shelf")));
    CHECK(stage.under(0, stage.named_on(0, "Shelf")));
    CHECK(stage.under(1, stage.named_on(1, "Shelf")));
    NETW_CHECK_EQ(
        NetwEntity::of(stage.cube_on(-1))->get_controller(),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_admitted"),
        int64_t(1)
    );
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_refused"),
        int64_t(0)
    );
    NETW_CHECK_EQ(stage.anchor_on(0), stage.anchor_on(-1));
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] grant, a raw move made while "
    "this peer's requested claim waits is moved back when the grant lands "
    "before the move settles, and no move reaches the session"
) {
    MoveStage stage;
    REQUIRE(stage.cube_on(1) != nullptr);
    stage.write_ticks(6);
    const Ref<NetwPromise> released
        = NetwEntity::of(stage.cube_on(0))->release_authority();
    stage.write_ticks(6);
    REQUIRE(released->get_is_settled());
    NETW_REQUIRE_EQ(
        NetwEntity::of(stage.cube_on(-1))->get_controller(),
        int64_t(0)
    );

    Node *held = stage.cube_on(1);
    const Ref<NetwEntity> claimant = NetwEntity::of(held);
    stage.rig.hold(1);
    const Ref<NetwPromise> claimed = claimant->claim_authority();
    stage.rig.pump(4);
    REQUIRE(claimant->is_claim_pending());
    held->reparent(stage.named_on(1, "Shelf"));
    stage.rig.release(1);
    stage.rig.step_ticks(SETTLE_TICKS);

    CHECK(claimed->get_is_settled());
    CHECK_FALSE(claimed->get_is_failed());
    NETW_CHECK_EQ(claimant->get_controller(), int64_t(stage.rig.peer_id(1)));
    CHECK(stage.under(-1, stage.arena_on(-1)));
    CHECK(stage.under(0, stage.arena_on(0)));
    CHECK(stage.under(1, stage.arena_on(1)));
    NETW_CHECK_EQ(stage.anchor_on(0), stage.anchor_on(-1));
    NETW_CHECK_EQ(stage.anchor_on(1), stage.anchor_on(-1));
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_admitted"),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_refused"),
        int64_t(0)
    );
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] immediate, a peer whose claim "
    "on a TRANSFER_IMMEDIATE entity waits may move it at once, and a refused "
    "claim undoes the claim and the move together"
) {
    MoveStage stage(NetwEntity::TRANSFER_IMMEDIATE);
    REQUIRE(stage.cube_on(1) != nullptr);
    stage.write_ticks(6);
    NetwMultiplayer *session = stage.rig.server();
    const int64_t claimant_peer = stage.rig.peer_id(1);
    const int64_t holder_peer = stage.rig.peer_id(0);
    const Ref<NetwEntity> claimant = NetwEntity::of(stage.cube_on(1));

    SUBCASE("refused") {
        NetwEntity::of(stage.cube_on(-1))
            ->connect(
                StringName("control_requested"),
                callable_mp_static(&deny_control)
            );
        const Ref<NetwPromise> claimed = claimant->claim_authority();
        REQUIRE(claimant->is_claim_pending());
        const Ref<NetwPromise> moved
            = stage.moves_on(1, stage.named_on(1, "Shelf"));
        CHECK_FALSE(moved->get_is_failed());
        CHECK(stage.under(1, stage.named_on(1, "Shelf")));
        stage.write_ticks(SETTLE_TICKS);

        CHECK(claimed->get_is_failed());
        CHECK(moved->get_is_failed());
        NETW_CHECK_EQ(moved->get_code(), ERR_UNAUTHORIZED);
        NETW_CHECK_EQ(claimant->get_controller(), holder_peer);
        CHECK_FALSE(claimant->get_is_controlled_locally());
        CHECK_FALSE(claimant->has_structure_ops());
        CHECK(stage.all_under_arena());
        CHECK(stage.anchors_agree());
        NETW_CHECK_EQ(
            counter_of(session, "lifecycle_ops_refused"),
            int64_t(1)
        );
        NETW_CHECK_EQ(
            counter_of(session, "lifecycle_ops_admitted"),
            int64_t(0)
        );
    }

    SUBCASE("granted") {
        const Ref<NetwPromise> claimed = claimant->claim_authority();
        REQUIRE(claimant->is_claim_pending());
        const Ref<NetwPromise> moved
            = stage.moves_on(1, stage.named_on(1, "Shelf"));
        stage.write_ticks(SETTLE_TICKS);

        CHECK(claimed->get_is_settled());
        CHECK_FALSE(claimed->get_is_failed());
        CHECK(moved->get_is_settled());
        CHECK_FALSE(moved->get_is_failed());
        NETW_CHECK_EQ(claimant->get_controller(), claimant_peer);
        CHECK(stage.all_under("Shelf"));
        CHECK(stage.anchors_agree());
        NETW_CHECK_EQ(
            counter_of(session, "lifecycle_ops_admitted"),
            int64_t(1)
        );
    }
}

} // namespace TestLifecycleMoveLaws

#endif

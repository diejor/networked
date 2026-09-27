#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/stock_probes.h"
#include "support/stock_stand.h"
#include "support/value_flow_stand.h"

#include "netw/api/entity.hpp"

namespace TestSpawnReparentStreamLaws {

using namespace godot;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;
using netw_test::StockWorld;
using netw_test::SyncProp;

constexpr int TICKRATE = 30;
constexpr int64_t BEFORE_THE_MOVE = 11;
constexpr int64_t AFTER_THE_MOVE = 22;
const char *PROBE_SCRIPT = netw_test::gdsrc::STOCK_SYNC_PROBE;

SyncProp always_replicated(const char *p_path) {
    SyncProp row;
    row.path = NodePath(p_path);
    row.mode = SceneReplicationConfig::REPLICATION_MODE_ALWAYS;
    row.on_spawn = false;
    return row;
}

Ref<PackedScene> probe_scene() {
    Node *root = netw_test::scripted_root(PROBE_SCRIPT, "StreamProbe");
    Vector<SyncProp> props;
    props.push_back(always_replicated(".:synced_value"));
    netw_test::add_stock_sync(root, "Sync", props);
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("StreamProbe")
    );
    memdelete(root);
    return packed;
}

Node *plain_child(Node *p_parent, const char *p_name) {
    Node *made = memnew(Node);
    made->set_name(p_name);
    p_parent->add_child(made);
    return made;
}

int64_t route_of(Node *p_node) {
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::of(p_node);
    return entity.is_valid() ? entity->get_route() : 0;
}

int64_t counter_of(LoopbackRig &p_rig, int p_client, const char *p_name) {
    netw::spawn::Pipeline *pipeline = p_rig.spawn_plane(p_client);
    if (pipeline == nullptr) {
        return -1;
    }
    return int64_t(pipeline->counters().get(StringName(p_name), -1));
}

enum Destination {
    DESTINATION_IS_AN_ENTITY,
    DESTINATION_IS_A_PLAIN_NODE,
};

struct MoveScenario {
    String label;
    Destination destination = DESTINATION_IS_AN_ENTITY;
    String parent_after = "Home/Arena/Vehicle";
    bool holds_the_client = false;
    bool renames_the_peer_branch = false;
    bool crosses_to_another_spawner = false;
    bool moves_through_the_node_verb = false;
};

MoveScenario under_an_entity() {
    MoveScenario scenario;
    scenario.label = "under-an-entity";
    return scenario;
}

MoveScenario under_a_plain_node() {
    MoveScenario scenario;
    scenario.label = "under-a-plain-node";
    scenario.destination = DESTINATION_IS_A_PLAIN_NODE;
    scenario.parent_after = "Home/Arena/Garage";
    return scenario;
}

MoveScenario batched_behind_a_held_link() {
    MoveScenario scenario;
    scenario.label = "batched-behind-a-held-link";
    scenario.holds_the_client = true;
    return scenario;
}

MoveScenario onto_a_peer_shaped_differently() {
    MoveScenario scenario;
    scenario.label = "onto-a-peer-shaped-differently";
    scenario.renames_the_peer_branch = true;
    scenario.parent_after = "Elsewhere/Arena/Vehicle";
    return scenario;
}

MoveScenario through_the_node_verb() {
    MoveScenario scenario;
    scenario.label = "through-the-node-verb";
    scenario.moves_through_the_node_verb = true;
    return scenario;
}

MoveScenario into_another_spawners_arena() {
    MoveScenario scenario;
    scenario.label = "into-another-spawners-arena";
    scenario.destination = DESTINATION_IS_A_PLAIN_NODE;
    scenario.crosses_to_another_spawner = true;
    scenario.parent_after = "Away/Arena";
    return scenario;
}

struct MoveEvidence {
    bool materialized = false;
    bool same_instance = false;
    String parent_after;
    int64_t server_route_before = 0;
    int64_t server_route_after = 0;
    int64_t client_route_after = 0;
    int64_t value_before = -1;
    int64_t value_after = -1;
    int64_t drops_unresolved = -1;
    int64_t drops_bad_sender = -1;
};

class MoveRun {
    MoveScenario declared;
    MoveEvidence seen;

public:
    explicit MoveRun(const MoveScenario &p_scenario) : declared(p_scenario) {
        LoopbackRig rig(1);
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);

        const Ref<PackedScene> scene = probe_scene();
        PackedStringArray scenes;
        scenes.push_back(scene->get_path());
        StockWorld world = netw_test::mount_stock_world(rig, scenes);
        netw_test::relabel_stock_world(world, "Home");

        Node *mover = scene->instantiate();
        mover->set_name("Mover");
        world.arena(-1)->add_child(mover, true);

        Node *destination = nullptr;
        if (p_scenario.crosses_to_another_spawner) {
            StockWorld away = netw_test::mount_stock_world(rig, scenes);
            netw_test::relabel_stock_world(away, "Away");
            destination = away.arena(-1);
        } else if (p_scenario.destination == DESTINATION_IS_AN_ENTITY) {
            destination = scene->instantiate();
            destination->set_name("Vehicle");
            world.arena(-1)->add_child(destination, true);
            netw_test::pump_until_child(rig, world.arena(0), "Vehicle");
        } else {
            destination = plain_child(world.arena(-1), "Garage");
            plain_child(world.arena(0), "Garage");
        }

        Node *mirror
            = netw_test::pump_until_child(rig, world.arena(0), "Mover");
        seen.materialized = mirror != nullptr;
        if (mirror == nullptr) {
            return;
        }

        mover->set(StringName("synced_value"), BEFORE_THE_MOVE);
        seen.value_before = int64_t(
            netw_test::pump_until_value(
                rig,
                mirror,
                StringName("synced_value"),
                BEFORE_THE_MOVE
            )
        );
        seen.server_route_before = route_of(mover);

        if (p_scenario.renames_the_peer_branch) {
            Node *held = world.arena(0)->get_parent();
            REQUIRE_MESSAGE(held != nullptr, "the peer holds no world");
            held->set_name("Elsewhere");
        }

        const Ref<netw::NetwEntity> entity = netw::NetwEntity::of(mover);
        REQUIRE_MESSAGE(entity.is_valid(), "the spawned mover has no record");
        if (p_scenario.holds_the_client) {
            rig.hold(0);
        }
        if (p_scenario.moves_through_the_node_verb) {
            mover->reparent(destination);
        } else {
            netw::NetwMultiplayer::entity_move(mover, destination);
        }
        rig.pump(10);
        if (p_scenario.holds_the_client) {
            rig.release(0);
            rig.pump(10);
        }

        seen.server_route_after = route_of(mover);
        Node *moved = rig.route_node(int(seen.server_route_before), 0);
        seen.same_instance = moved == mirror;
        if (moved != nullptr && moved->get_parent() != nullptr) {
            seen.parent_after
                = String(rig.branch(0)->get_path_to(moved->get_parent()));
        }
        seen.client_route_after = route_of(moved);

        mover->set(StringName("synced_value"), AFTER_THE_MOVE);
        seen.value_after = int64_t(
            netw_test::pump_until_value(
                rig,
                moved != nullptr ? moved : mirror,
                StringName("synced_value"),
                AFTER_THE_MOVE
            )
        );

        seen.drops_unresolved = counter_of(rig, 0, "drops_spawn_unresolved");
        seen.drops_bad_sender = counter_of(rig, 0, "drops_spawn_bad_sender");
    }

    const MoveScenario &scenario() const {
        return declared;
    }

    const MoveEvidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<MoveRun> MoveLaw;

LawVerdict law_moves(const MoveRun &p_run) {
    const MoveEvidence &seen = p_run.evidence();
    if (!seen.materialized) {
        return law_broken("the peer never materialized the mover");
    }
    if (!seen.same_instance) {
        return law_broken("the peer holds a different instance after the move");
    }
    if (seen.parent_after != p_run.scenario().parent_after) {
        return law_broken(
            "the peer's mover sits under '%s'",
            seen.parent_after.utf8().get_data()
        );
    }
    return law_held();
}

const MoveLaw L_MOVES = {
    "moves",
    "the peer moves the instance it already holds under its own copy of the "
    "destination, rather than despawning and respawning it",
    &law_moves,
};

LawVerdict law_keeps_the_route(const MoveRun &p_run) {
    const MoveEvidence &seen = p_run.evidence();
    if (seen.server_route_before <= 0) {
        return law_broken("the spawn minted no route");
    }
    if (seen.server_route_after != seen.server_route_before) {
        return law_broken(
            "the authority renumbered %d to %d",
            int(seen.server_route_before),
            int(seen.server_route_after)
        );
    }
    if (seen.client_route_after != seen.server_route_before) {
        return law_broken(
            "the peer answers route %d for route %d",
            int(seen.client_route_after),
            int(seen.server_route_before)
        );
    }
    return law_held();
}

const MoveLaw L_KEEPS_THE_ROUTE = {
    "keeps-the-route",
    "the entity is addressed by the route it was spawned with, before and "
    "after the move, on both peers",
    &law_keeps_the_route,
};

LawVerdict law_keeps_the_stream(const MoveRun &p_run) {
    const MoveEvidence &seen = p_run.evidence();
    if (seen.value_before != BEFORE_THE_MOVE) {
        return law_broken(
            "the stream read %d before the move",
            int(seen.value_before)
        );
    }
    if (seen.value_after != AFTER_THE_MOVE) {
        return law_broken(
            "the stream read %d after the move",
            int(seen.value_after)
        );
    }
    return law_held();
}

const MoveLaw L_KEEPS_THE_STREAM = {
    "keeps-the-stream",
    "a value the authority writes after the move reaches the peer, because "
    "the sync address names the entity rather than where it sits",
    &law_keeps_the_stream,
};

LawVerdict law_resolves_quietly(const MoveRun &p_run) {
    const MoveEvidence &seen = p_run.evidence();
    if (seen.drops_unresolved != 0) {
        return law_broken(
            "the peer dropped %d frames as unresolved",
            int(seen.drops_unresolved)
        );
    }
    if (seen.drops_bad_sender != 0) {
        return law_broken(
            "the peer refused %d frames by sender",
            int(seen.drops_bad_sender)
        );
    }
    return law_held();
}

const MoveLaw L_RESOLVES_QUIETLY = {
    "resolves-quietly",
    "the move takes the resolved arm, so neither an unresolvable anchor nor "
    "a refused sender explains the peer's tree",
    &law_resolves_quietly,
};

const MoveLaw MOVE_LAWS[] = {
    L_MOVES,
    L_KEEPS_THE_ROUTE,
    L_KEEPS_THE_STREAM,
    L_RESOLVES_QUIETLY,
};

TEST_CASE(
    "[Networked][Spawn][Sync][SceneTree] a moved entity keeps the stream it "
    "had, whichever destination the move names"
) {
    const MoveScenario CORPUS[] = {
        under_an_entity(),
        under_a_plain_node(),
        batched_behind_a_held_link(),
        onto_a_peer_shaped_differently(),
        into_another_spawners_arena(),
        through_the_node_verb(),
    };
    for (const MoveScenario &scenario : CORPUS) {
        const MoveRun run(scenario);
        for (const MoveLaw &law : MOVE_LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

const char *FRAME_PROBE_SCRIPT = R"(extends BODY

var counter := 0
var seats := []
var synced_value: int:
	get:
		return frame_tag() * 1000 + counter
	set(value):
		seats.append(Vector2i(value, frame_tag()))


func _init() -> void:
	Netw.configure_property(self, &"synced_value").RECORD()


func frame_tag() -> int:
	var parent := get_parent()
	if parent == null:
		return 0
	return 2 if parent.name == &"Vehicle" else 1
)";

constexpr int64_t NEW_FRAME = 2;

Ref<PackedScene> frame_probe_scene(const char *p_record, const char *p_body) {
    const CharString source = String(FRAME_PROBE_SCRIPT)
                                  .replace("RECORD", p_record)
                                  .replace("BODY", p_body)
                                  .utf8();
    Node *root = netw_test::scripted_root(source.get_data(), "FrameProbe");
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        root,
        netw_test::mint_scene_path("FrameProbe")
    );
    memdelete(root);
    return packed;
}

enum Writer {
    THE_SESSION_WRITES,
    THE_PEER_WRITES,
};

struct FrameScenario {
    String label;
    Writer writer = THE_SESSION_WRITES;
    bool reader_guards_the_carry = false;
};

struct FrameEvidence {
    bool materialized = false;
    bool reader_moved = false;
    int64_t seated = 0;
    int64_t seated_in_the_new_frame = 0;
    int64_t seated_in_another_frame = 0;
    Vector2i first_misplaced;
    uint64_t token_before = 0;
    uint64_t token_after = 0;
    int64_t writer_anchor = 0;
    int64_t reader_anchor = 0;
};

uint64_t lane_token(
    netw::NetwMultiplayer *p_reader,
    int p_writer,
    int64_t p_route
) {
    netw::ReplicationCore *plane = p_reader->get_replication_plane();
    if (plane == nullptr || p_route <= 0) {
        return 0;
    }
    const netw::wire::StreamReaderBook &readers
        = plane->get_sync_pipeline()->row_send_under_test()->reader_book();
    for (int ordinal = 0; ordinal < 256; ++ordinal) {
        for (uint8_t family = 0; family <= netw::wire::STREAM_FAMILY_CEILING;
             ++family) {
            netw::wire::StreamLane lane;
            lane.route = p_route;
            lane.ordinal = uint8_t(ordinal);
            lane.family = netw::wire::StreamFamily(family);
            const uint64_t token = readers.token_at(p_writer, lane);
            if (token != 0) {
                return token;
            }
        }
    }
    return 0;
}

FrameEvidence run_frame_scenario(const FrameScenario &p_scenario);

class FrameRun {
    FrameScenario declared;
    FrameEvidence seen;

public:
    explicit FrameRun(const FrameScenario &p_scenario)
        : declared(p_scenario), seen(run_frame_scenario(p_scenario)) {
    }

    const FrameScenario &scenario() const {
        return declared;
    }

    const FrameEvidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<FrameRun> FrameLaw;

LawVerdict law_seats_in_its_own_frame(const FrameRun &p_run) {
    const FrameEvidence &seen = p_run.evidence();
    if (!seen.materialized) {
        return law_broken("the peer never held the mover");
    }
    if (seen.seated_in_another_frame != 0) {
        return law_broken(
            "%d of %d rows were seated in another frame, the first written "
            "in frame %d and seated in frame %d",
            int(seen.seated_in_another_frame),
            int(seen.seated),
            seen.first_misplaced.x,
            seen.first_misplaced.y
        );
    }
    return law_held();
}

const FrameLaw L_SEATS_IN_ITS_OWN_FRAME = {
    "seats-in-its-own-frame",
    "a row written under one parent is never seated while the reader holds "
    "the entity under another",
    &law_seats_in_its_own_frame,
};

LawVerdict law_reaches_the_new_frame(const FrameRun &p_run) {
    const FrameEvidence &seen = p_run.evidence();
    if (seen.seated_in_the_new_frame == 0) {
        return law_broken(
            "none of %d rows seated after the move was written in the new "
            "frame",
            int(seen.seated)
        );
    }
    return law_held();
}

const FrameLaw L_REACHES_THE_NEW_FRAME = {
    "reaches-the-new-frame",
    "rows written under the new parent reach the reader once it holds the "
    "entity there",
    &law_reaches_the_new_frame,
};

LawVerdict law_reopens_the_lane(const FrameRun &p_run) {
    const FrameEvidence &seen = p_run.evidence();
    if (seen.writer_anchor < 2 || seen.reader_anchor != seen.writer_anchor) {
        return law_broken(
            "the writer holds anchor revision %d and the reader %d",
            int(seen.writer_anchor),
            int(seen.reader_anchor)
        );
    }
    if (seen.token_before == 0 || seen.token_after == 0
        || seen.token_after == seen.token_before) {
        return law_broken(
            "the reader held the lane on token %d before the move and %d "
            "after",
            int(seen.token_before),
            int(seen.token_after)
        );
    }
    return law_held();
}

const FrameLaw L_REOPENS_THE_LANE = {
    "reopens-the-lane",
    "the move keeps the route and reopens the lane under the anchor revision "
    "both sides installed",
    &law_reopens_the_lane,
};

const FrameLaw FRAME_LAWS[] = {
    L_SEATS_IN_ITS_OWN_FRAME,
    L_REACHES_THE_NEW_FRAME,
    L_REOPENS_THE_LANE,
};

LawVerdict law_holds_the_window(const FrameRun &p_run) {
    if (p_run.evidence().reader_moved) {
        return law_broken(
            "the reader's carry landed, so no row crossed its guard window"
        );
    }
    return law_held();
}

const FrameLaw L_HOLDS_THE_WINDOW = {
    "holds-the-window",
    "a body the reader carries under its reparent guard stays under the old "
    "parent while no physics frame passes",
    &law_holds_the_window,
};

const FrameLaw GUARDED_LAWS[] = {
    L_HOLDS_THE_WINDOW,
    L_SEATS_IN_ITS_OWN_FRAME,
};

FrameEvidence run_frame_scenario(const FrameScenario &p_scenario) {
    FrameEvidence seen;
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);

    const Ref<PackedScene> scene = frame_probe_scene(
        p_scenario.writer == THE_PEER_WRITES ? "broadcast" : "state",
        p_scenario.reader_guards_the_carry ? "CharacterBody2D" : "Node2D"
    );
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *vehicle = scene->instantiate();
    vehicle->set_name("Vehicle");
    world.arena(-1)->add_child(vehicle, true);
    const String named = p_scenario.writer == THE_PEER_WRITES
        ? String("PeerMover")
        : String("Mover");
    Node *mover = scene->instantiate();
    mover->set_name(named);
    world.arena(-1)->add_child(mover, true);

    const bool vehicle_arrived
        = netw_test::pump_until_child(rig, world.arena(0), "Vehicle")
        != nullptr;
    Node *mirror = netw_test::pump_until_child(rig, world.arena(0), named);
    seen.materialized = vehicle_arrived && mirror != nullptr;
    if (!seen.materialized) {
        return seen;
    }
    if (p_scenario.writer == THE_PEER_WRITES) {
        netw::NetwEntity::of(mover)->set_controller(rig.peer_id(0));
        rig.step_ticks(4);
    }

    const bool peer_writes = p_scenario.writer == THE_PEER_WRITES;
    Node *writer = peer_writes ? mirror : mover;
    Node *reader = peer_writes ? mover : mirror;
    netw::NetwMultiplayer *reading = peer_writes ? rig.server() : rig.client(0);
    netw::NetwMultiplayer *writing = peer_writes ? rig.client(0) : rig.server();
    const int writer_peer = peer_writes ? rig.peer_id(0) : rig.peer_id(-1);
    const int64_t route = route_of(mover);
    int64_t counter = 0;
    for (int step = 0; step < 8; ++step) {
        writer->set(StringName("counter"), ++counter);
        rig.step_ticks(1);
    }
    seen.token_before = lane_token(reading, writer_peer, route);
    reader->set(StringName("seats"), Array());
    netw::NetwMultiplayer::entity_move(mover, vehicle);
    for (int step = 0; step < 24; ++step) {
        writer->set(StringName("counter"), ++counter);
        rig.step_ticks(1);
    }
    seen.token_after = lane_token(reading, writer_peer, route);
    seen.reader_moved = reader->get_parent() != nullptr
        && String(reader->get_parent()->get_name()) == "Vehicle";
    seen.writer_anchor = int64_t(writing->liveness_route_anchor(route));
    seen.reader_anchor = int64_t(reading->liveness_route_anchor(route));

    const Array seats = reader->get(StringName("seats"));
    for (int at = 0; at < seats.size(); ++at) {
        const Vector2i seat = seats[at];
        const int64_t written = int64_t(seat.x) / 1000;
        const int64_t held = int64_t(seat.y);
        seen.seated += 1;
        if (written == NEW_FRAME && held == NEW_FRAME) {
            seen.seated_in_the_new_frame += 1;
        }
        if (written != held && held != 0) {
            if (seen.seated_in_another_frame == 0) {
                seen.first_misplaced = Vector2i(int(written), int(held));
            }
            seen.seated_in_another_frame += 1;
        }
    }
    return seen;
}

TEST_CASE(
    "[Networked][Spawn][Sync][SceneTree] HC2 a row is seated only in the "
    "frame it was written in, whichever side writes it, when the move and "
    "the rows written around it cross on the link"
) {
    FrameScenario session_writes;
    session_writes.label = "the-session-writes";
    FrameScenario peer_writes;
    peer_writes.label = "the-peer-writes";
    peer_writes.writer = THE_PEER_WRITES;
    const FrameScenario CORPUS[] = {session_writes, peer_writes};
    for (const FrameScenario &scenario : CORPUS) {
        const FrameRun run(scenario);
        for (const FrameLaw &law : FRAME_LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Spawn][Sync][SceneTree] HC2 a row written under the new "
    "parent waits while the reader still carries the body under its guard, "
    "so the move reaches it before any row does"
) {
    FrameScenario guarded;
    guarded.label = "the-reader-guards-the-carry";
    guarded.reader_guards_the_carry = true;
    const FrameRun run(guarded);
    for (const FrameLaw &law : GUARDED_LAWS) {
        NETW_CELL(law, guarded);
        NETW_LAW_HOLDS(law, run);
    }
}

struct LateEvidence {
    int64_t server_anchor = 0;
    int64_t late_anchor = 0;
    String parent_on_arrival;
    String parent_after_stale_move;
    bool present_before_spawn = false;
};

LateEvidence run_late_join() {
    LateEvidence seen;
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);

    const Ref<PackedScene> scene = probe_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *vehicle = scene->instantiate();
    vehicle->set_name("Vehicle");
    world.arena(-1)->add_child(vehicle, true);
    Node *mover = scene->instantiate();
    mover->set_name("Mover");
    world.arena(-1)->add_child(mover, true);
    REQUIRE(
        netw_test::pump_until_child(rig, world.arena(0), "Mover") != nullptr
    );

    netw::NetwMultiplayer::entity_move(mover, vehicle);
    rig.pump(6);
    netw::wire::WriteStream stale;
    const int64_t route = route_of(mover);
    uint64_t stale_revision = rig.server()->liveness_route_anchor(route);
    REQUIRE(rig.server()->verb_head_write(stale, route));
    REQUIRE(rig.server()->anchor_encode(stale, world.arena(-1)));
    uint64_t stale_author = uint64_t(rig.peer_id(-1));
    REQUIRE(stale.varuint(stale_revision, 5));
    REQUIRE(stale.varuint(stale_author, 5));
    REQUIRE(stale.align_verify());
    netw::NetwMultiplayer::entity_move(mover, world.arena(-1));
    rig.pump(6);
    netw::NetwMultiplayer::entity_move(mover, vehicle);
    rig.pump(6);
    seen.server_anchor = int64_t(rig.server()->liveness_route_anchor(route));

    const int late = rig.add_client();
    rig.hold(late);
    rig.mount_late(late);
    netw_test::seat_stock_branch(rig.branch(late), world);
    rig.client(late)->spawn_handle_reparent_frame(
        stale.to_bytes(),
        rig.peer_id(-1)
    );
    seen.present_before_spawn = rig.route_node(int(route), late) != nullptr;
    rig.release(late);
    Node *arrived = nullptr;
    for (int round = 0; round < 60 && arrived == nullptr; ++round) {
        rig.pump();
        arrived = rig.route_node(int(route), late);
    }
    REQUIRE_MESSAGE(
        arrived != nullptr,
        "the late peer never spawned the mover"
    );
    seen.late_anchor = int64_t(rig.client(late)->liveness_route_anchor(route));
    seen.parent_on_arrival = String(arrived->get_parent()->get_name());

    rig.client(late)->spawn_handle_reparent_frame(
        stale.to_bytes(),
        rig.peer_id(-1)
    );
    rig.pump(4);
    seen.parent_after_stale_move = String(arrived->get_parent()->get_name());
    return seen;
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] a peer that joins after the moves spawns "
    "the entity at the newest anchor and revision, and a REPARENT that "
    "reaches it before or after that SPAWN moves nothing"
) {
    const LateEvidence seen = run_late_join();
    CHECK_FALSE(seen.present_before_spawn);
    NETW_CHECK_EQ(seen.server_anchor, int64_t(4));
    NETW_CHECK_EQ(seen.late_anchor, seen.server_anchor);
    NETW_CHECK_EQ(int(seen.parent_on_arrival == "Vehicle"), 1);
    NETW_CHECK_EQ(int(seen.parent_after_stale_move == "Vehicle"), 1);
}

struct LateParkedEvidence {
    int64_t spawn_revision = 0;
    int64_t reparent_revision = 0;
    int64_t late_anchor = 0;
    String parent_on_arrival;
    bool present_while_parked = false;
    int64_t stale_drops = 0;
};

LateParkedEvidence run_late_parked() {
    LateParkedEvidence seen;
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);

    const Ref<PackedScene> scene = probe_scene();
    PackedStringArray scenes;
    scenes.push_back(scene->get_path());
    StockWorld world = netw_test::mount_stock_world(rig, scenes);

    Node *vehicle = scene->instantiate();
    vehicle->set_name("Vehicle");
    world.arena(-1)->add_child(vehicle, true);
    Node *mover = scene->instantiate();
    mover->set_name("Mover");
    world.arena(-1)->add_child(mover, true);
    REQUIRE(
        netw_test::pump_until_child(rig, world.arena(0), "Mover") != nullptr
    );

    netw::NetwMultiplayer::entity_move(mover, vehicle);
    rig.pump(6);
    const int64_t route = route_of(mover);
    seen.spawn_revision = int64_t(rig.server()->liveness_route_anchor(route));
    const PackedByteArray parked_spawn = rig.spawn_frame_of(int(route));
    const PackedByteArray vehicle_spawn
        = rig.spawn_frame_of(int(route_of(vehicle)));

    netw::NetwMultiplayer::entity_move(mover, world.arena(-1));
    rig.pump(6);
    netw::wire::WriteStream newer;
    uint64_t newer_revision = rig.server()->liveness_route_anchor(route);
    seen.reparent_revision = int64_t(newer_revision);
    REQUIRE(rig.server()->verb_head_write(newer, route));
    REQUIRE(rig.server()->anchor_encode(newer, world.arena(-1)));
    uint64_t newer_author = uint64_t(rig.peer_id(-1));
    REQUIRE(newer.varuint(newer_revision, 5));
    REQUIRE(newer.varuint(newer_author, 5));
    REQUIRE(newer.align_verify());

    const int late = rig.add_client();
    rig.hold(late);
    rig.mount_late(late);
    netw_test::seat_stock_branch(rig.branch(late), world);
    rig.deliver_spawn(late, parked_spawn);
    rig.client(late)->spawn_handle_reparent_frame(
        newer.to_bytes(),
        rig.peer_id(-1)
    );
    seen.present_while_parked = rig.route_node(int(route), late) != nullptr;
    rig.deliver_spawn(late, vehicle_spawn);
    Node *arrived = nullptr;
    for (int round = 0; round < 30 && arrived == nullptr; ++round) {
        rig.pump();
        arrived = rig.route_node(int(route), late);
    }
    REQUIRE_MESSAGE(arrived != nullptr, "the parked SPAWN never placed");
    rig.pump(4);
    seen.late_anchor = int64_t(rig.client(late)->liveness_route_anchor(route));
    seen.parent_on_arrival = String(arrived->get_parent()->get_name());
    seen.stale_drops = counter_of(rig, late, "drops_reparent_stale");
    return seen;
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] a REPARENT that reaches a peer while its "
    "SPAWN waits on an absent parent is kept, and the entity lands at that "
    "REPARENT's anchor and revision once the parent arrives"
) {
    const LateParkedEvidence seen = run_late_parked();
    CHECK_FALSE(seen.present_while_parked);
    NETW_CHECK_EQ(seen.spawn_revision, int64_t(2));
    NETW_CHECK_EQ(seen.reparent_revision, int64_t(3));
    NETW_CHECK_EQ(seen.late_anchor, seen.reparent_revision);
    NETW_CHECK_EQ(int(seen.parent_on_arrival == "Arena"), 1);
    NETW_CHECK_EQ(seen.stale_drops, int64_t(0));
}

} // namespace TestSpawnReparentStreamLaws

#endif

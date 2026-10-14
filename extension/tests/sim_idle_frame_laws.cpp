#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/simulation.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/display/book.hpp"
#include "netw/display/runtime.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace TestNetwSimIdleFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *IDLE_ID = "sim_idle_cube";
constexpr const char *STEPPED_ID = "sim_idle_stepped";

constexpr const char *IDLE_SOURCE = R"(extends RigidBody3D

func _init() -> void:
	gravity_scale = 0.0
	collision_layer = 0
	collision_mask = 0
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	add_child(shape)
	var visual := Node3D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.authority = NetwEntity.AUTHORITY_CONTROLLER
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	e.simulation.replicas = NetwSimulation.REPLICAS_ACTIVE
	e.simulation.claim_on_contact = true
	e.simulation.release_on_rest = 1.0
	e.display.visual_root = ^"Visual"
	Netw.configure_property(self, &"position").broadcast() \
			.interpolate(NetwInterpolate.new().lerp())
)";

constexpr const char *STEP_SOURCE = R"(

func _network_tick(_delta: float, _tick: int, _is_fresh: bool) -> void:
	pass
)";

constexpr int IDLE_CUBES = 900;
constexpr int STEPPED_CUBES = 1;
constexpr int CUBES = IDLE_CUBES + STEPPED_CUBES;
constexpr int TICKRATE = 30;
constexpr int SETTLE_FRAMES = 60;
constexpr int WINDOW_FRAMES = 30;
constexpr int WAKE_FRAMES = 4;
constexpr int MOVE_FRAMES = 3 * int(netw::store::World::SWEEP_TICKS);
constexpr int MOVED_AUTHORITY = 7;
const Vector3 NUDGE(1.0, 0.0, 0.0);

Script *idle_script = nullptr;
Script *stepped_script = nullptr;

Node *build_with(Script *p_script, const Variant &p_name) {
    if (p_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(p_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

Node *build_idle(const Variant &p_name) {
    return build_with(idle_script, p_name);
}

Node *build_stepped(const Variant &p_name) {
    return build_with(stepped_script, p_name);
}

struct IdleEvidence {
    bool driven = false;
    bool seated = false;
    int recorded = 0;
    int bracketed = 0;
    int asleep = 0;
    int64_t runs = 0;
    int64_t walked = 0;
    int64_t announced = 0;
    int64_t sampled = 0;
    int64_t contacts = 0;
    int64_t contacted = 0;
    int64_t drowsy = 0;
    int64_t woken_contacted = 0;
    int64_t sources = 0;
    int64_t sourced = 0;
    int64_t pulls = 0;
    int64_t visits = 0;
    int64_t swept = 0;
    int64_t pumped = -1;
    int64_t recorders = -1;
    int64_t pruned = -1;
    int64_t rested_tick = -1;
    int64_t woken_tick = -1;
    int64_t unmoved_before = -1;
    int moved_seen_frame = -1;
};

IdleEvidence &idle_evidence() {
    static IdleEvidence evidence;
    return evidence;
}

class IdleScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> idle;
    Ref<Script> stepped;
    LocalVector<RID> cubes;
    NetwMultiplayer::IdleWork opened;
    netw::store::World::PullWork pull_opened;
    int64_t prune_opened = 0;
    int moved_at = -1;

    NetwMultiplayer *server() const {
        return stand->session(-1);
    }

    int64_t unmoved_rows() const {
        const netw::store::World &world = server()->store();
        int64_t local = 0;
        for (uint32_t at = 0; at < world.table_count(); ++at) {
            const netw::store::Table &table = world.table(at);
            if (table.body_columns == 0) {
                continue;
            }
            for (uint32_t row = 0; row < table.row_count(); ++row) {
                local += table.authorities[row] != MOVED_AUTHORITY ? 1 : 0;
            }
        }
        return local;
    }

    void move_authority() {
        moved_at = frame;
        idle_evidence().unmoved_before = unmoved_rows();
        for (const RID &cube : cubes) {
            server()->entity_get_node(cube)->set_multiplayer_authority(
                MOVED_AUTHORITY
            );
        }
    }

    bool spawn() {
        Node *arena = stand->arena();
        for (int index = 0; index < CUBES; ++index) {
            const StringName id(index < IDLE_CUBES ? IDLE_ID : STEPPED_ID);
            Array args;
            args.push_back(String(id) + String::num_int64(index));
            const RID made = server()->spawn_registered(id, args, nullptr);
            Node *built = server()->entity_get_node(made);
            if (built == nullptr) {
                return false;
            }
            arena->add_child(built);
            cubes.push_back(made);
        }
        stand->pump(8);
        return true;
    }

    bool open() {
        idle = netw_test::minted_script(IDLE_SOURCE);
        const String step_source = String(IDLE_SOURCE) + String(STEP_SOURCE);
        stepped = netw_test::minted_script(step_source.utf8().get_data());
        if (idle.is_null() || stepped.is_null()) {
            return false;
        }
        idle_script = idle.ptr();
        stepped_script = stepped.ptr();
        stand = new netw_test::SimStand(0);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(IDLE_ID), callable_mp_static(&build_idle));
        stand->teach(
            StringName(STEPPED_ID),
            callable_mp_static(&build_stepped)
        );
        stand->mount();
        stand->arm(TICKRATE);
        if (!spawn()) {
            return false;
        }
        idle_evidence().seated = true;
        return true;
    }

    void read() {
        IdleEvidence &seen = idle_evidence();
        const NetwMultiplayer::IdleWork &now = server()->idle_work;
        seen.runs = now.runs - opened.runs;
        seen.walked = now.walked - opened.walked;
        seen.announced = now.announced - opened.announced;
        seen.sampled = now.sampled - opened.sampled;
        seen.contacts = now.contacts - opened.contacts;
        seen.contacted = now.contacted - opened.contacted;
        seen.drowsy = now.drowsy - opened.drowsy;
        seen.sources = now.sources - opened.sources;
        seen.sourced = now.sourced - opened.sourced;
        seen.pumped = now.pumped - opened.pumped;
        seen.recorders = now.recorders - opened.recorders;
        seen.pruned = server()->sync_pipeline()->prune_visits - prune_opened;
        const netw::store::World::PullWork &pulled = server()->store().work;
        seen.pulls = pulled.pulls - pull_opened.pulls;
        seen.visits = pulled.visits - pull_opened.visits;
        seen.swept = pulled.swept - pull_opened.swept;
        const Ref<netw::display::Book> book = server()->get_display_book();
        for (const RID &cube : cubes) {
            const netw::sim::Row *row = server()->sim_row_of(cube);
            if (row != nullptr && row->bodies.recorded) {
                seen.recorded += 1;
            }
            const netw::display::Runtime *runtime = book->runtime_of(cube);
            if (runtime != nullptr
                && runtime->get_pump_mode() == netw::display::PUMP_BRACKETED) {
                seen.bracketed += 1;
            }
            RigidBody3D *body
                = Object::cast_to<RigidBody3D>(server()->entity_get_node(cube));
            if (body != nullptr && body->is_sleeping()) {
                seen.asleep += 1;
            }
        }
    }

    int64_t newest_of_first() const {
        const netw::display::Runtime *runtime
            = server()->get_display_book()->runtime_of(cubes[0]);
        if (runtime == nullptr || runtime->channels().is_empty()) {
            return -1;
        }
        return runtime->channels()[0]->display_history().newest_tick();
    }

    void wake_first() {
        idle_evidence().rested_tick = newest_of_first();
        Object::cast_to<RigidBody3D>(server()->entity_get_node(cubes[0]))
            ->set_linear_velocity(NUDGE);
    }

    void close() {
        idle_evidence().driven = true;
        delete stand;
        stand = nullptr;
        idle_script = nullptr;
        stepped_script = nullptr;
        idle = Ref<Script>();
        stepped = Ref<Script>();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            close();
            done = true;
            return false;
        }
        if (frame == SETTLE_FRAMES) {
            opened = server()->idle_work;
            pull_opened = server()->store().work;
            prune_opened = server()->sync_pipeline()->prune_visits;
        }
        if (frame == SETTLE_FRAMES + WINDOW_FRAMES) {
            read();
            wake_first();
            opened = server()->idle_work;
        }
        if (frame == SETTLE_FRAMES + WINDOW_FRAMES + WAKE_FRAMES) {
            idle_evidence().woken_tick = newest_of_first();
            idle_evidence().woken_contacted
                = server()->idle_work.contacted - opened.contacted;
            move_authority();
        }
        if (moved_at >= 0 && idle_evidence().moved_seen_frame < 0
            && unmoved_rows() == 0) {
            idle_evidence().moved_seen_frame = frame - moved_at;
        }
        if (moved_at >= 0
            && (idle_evidence().moved_seen_frame >= 0
                || frame - moved_at > MOVE_FRAMES)) {
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(IdleScenario, sim_idle_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] a pile of sleeping bodies with no step costs "
    "the runner no visit, no announcement and no display sample on a "
    "steady tick"
) {
    const IdleEvidence &evidence = idle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    REQUIRE(evidence.recorded == CUBES);
    REQUIRE(evidence.bracketed == CUBES);
    REQUIRE(evidence.asleep == CUBES);
    REQUIRE(evidence.runs > 0);
    NETW_CHECK_EQ(evidence.walked, evidence.runs * STEPPED_CUBES);
    NETW_CHECK_EQ(evidence.announced, int64_t(0));
    NETW_CHECK_EQ(evidence.sampled, int64_t(0));
}

TEST_CASE(
    "[Networked][Sim][Frame] a sleeping pile that claims on contact and "
    "releases on rest costs the contact pass no row on a steady tick, and a "
    "woken body is walked again"
) {
    const IdleEvidence &evidence = idle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.asleep == CUBES);
    NETW_CHECK_GE(evidence.contacts, int64_t(WINDOW_FRAMES / 2));
    NETW_CHECK_EQ(evidence.contacted, int64_t(0));
    NETW_CHECK_EQ(evidence.drowsy, int64_t(0));
    NETW_CHECK_GE(evidence.woken_contacted, int64_t(1));
    NETW_CHECK_LE(evidence.woken_contacted, int64_t(WAKE_FRAMES));
}

TEST_CASE(
    "[Networked][Sim][Frame] a steady sleeping pile costs the store's "
    "source pass no row, because no change reached any of them"
) {
    const IdleEvidence &evidence = idle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.asleep == CUBES);
    NETW_CHECK_GE(evidence.sources, int64_t(WINDOW_FRAMES / 2));
    NETW_CHECK_EQ(evidence.sourced, int64_t(0));
}

TEST_CASE(
    "[Networked][Store][Frame] a steady sleeping pile costs the pull only "
    "the sweep's share of rows a tick, a thirtieth of the pile, and no other "
    "row"
) {
    const IdleEvidence &evidence = idle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.asleep == CUBES);
    const int64_t sweep = int64_t(netw::store::World::SWEEP_TICKS);
    NETW_CHECK_GE(evidence.pulls, int64_t(WINDOW_FRAMES / 2));
    NETW_CHECK_GT(evidence.swept, int64_t(0));
    NETW_CHECK_EQ(evidence.visits, evidence.swept);
    const int64_t share = (CUBES + sweep - 1) / sweep;
    NETW_CHECK_LE(evidence.swept, evidence.pulls * share);
}

TEST_CASE(
    "[Networked][Display][Frame] a steady sleeping pile costs the display "
    "pump and the record tick no runtime, because nothing reached one"
) {
    const IdleEvidence &evidence = idle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.asleep == CUBES);
    NETW_CHECK_GE(evidence.pulls, int64_t(WINDOW_FRAMES / 2));
    NETW_CHECK_EQ(evidence.pumped, int64_t(0));
    NETW_CHECK_EQ(evidence.recorders, int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Frame] a steady sleeping pile costs the derived "
    "prune no binding, because no binding's node left the tree"
) {
    const IdleEvidence &evidence = idle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.asleep == CUBES);
    NETW_CHECK_GE(evidence.pulls, int64_t(WINDOW_FRAMES / 2));
    NETW_CHECK_EQ(evidence.pruned, int64_t(0));
}

TEST_CASE(
    "[Networked][Store][Frame] a node authority a game moves on every body "
    "of a sleeping pile is seen for all of them within one sweep, so no row "
    "waits longer than the sweep's latency"
) {
    const IdleEvidence &evidence = idle_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_EQ(evidence.unmoved_before, int64_t(CUBES));
    NETW_CHECK_GE(evidence.moved_seen_frame, 1);
    NETW_CHECK_LE(
        evidence.moved_seen_frame,
        int(netw::store::World::SWEEP_TICKS) + 2
    );
}

TEST_CASE(
    "[Networked][Sim][Frame] a body woken out of a sleeping pile is sampled "
    "into its display history again"
) {
    const IdleEvidence &evidence = idle_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_GE(evidence.rested_tick, int64_t(0));
    NETW_CHECK_GT(evidence.woken_tick, evidence.rested_tick);
}

constexpr int WAKE_SLEEP_FRAMES = 240;
constexpr int WAKE_FLIGHT_FRAMES = 90;
const Vector3 WAKE_GAP(2.0, 0.0, 0.0);
const Vector3 WAKE_THROW(4.0, 0.0, 0.0);

struct ScriptWakeEvidence {
    bool driven = false;
    bool seated = false;
    bool slept = false;
    int64_t host = 0;
    int64_t target_before = -1;
    int64_t target_after = -1;
    int walked_frame = -1;
    int claimed_frame = -1;
};

ScriptWakeEvidence &script_wake_evidence() {
    static ScriptWakeEvidence evidence;
    return evidence;
}

ScriptWakeEvidence &child_wake_evidence() {
    static ScriptWakeEvidence evidence;
    return evidence;
}

constexpr const char *CHILD_SOURCE = R"(extends Node3D

var sphere: RigidBody3D

var sphere_position: Vector3:
	get:
		return sphere.position
	set(value):
		sphere.position = value

func _init() -> void:
	sphere = RigidBody3D.new()
	sphere.name = &"Sphere"
	sphere.gravity_scale = 0.0
	sphere.collision_layer = 0
	sphere.collision_mask = 0
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	sphere.add_child(shape)
	add_child(sphere)
	var e := Netw.configure_entity(self)
	e.authority = NetwEntity.AUTHORITY_CONTROLLER
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	e.simulation.bodies = [^"Sphere"]
	e.simulation.replicas = NetwSimulation.REPLICAS_ACTIVE
	e.simulation.claim_on_contact = true
	Netw.configure_property(self, &"sphere_position").broadcast()
)";

using WakeEvidence = ScriptWakeEvidence &(*)();

class ScriptWakeScenario final : public netw_test::FrameScenario {
    WakeEvidence script_wake_evidence;
    const char *source;
    const char *body_path;
    int frame = 0;
    int woke_at = -1;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> idle;
    LocalVector<RID> cubes;
    int64_t contacted = 0;

    NetwMultiplayer *server() const {
        return stand->session(-1);
    }

    RigidBody3D *body_at(int p_index) const {
        Node *root = server()->entity_get_node(cubes[p_index]);
        if (root == nullptr || body_path[0] == '\0') {
            return Object::cast_to<RigidBody3D>(root);
        }
        return Object::cast_to<RigidBody3D>(
            root->get_node_or_null(NodePath(body_path))
        );
    }

    Ref<NetwEntity> entity_at(int p_index) const {
        return NetwEntity::of(server()->entity_get_node(cubes[p_index]));
    }

    bool open() {
        idle = netw_test::minted_script(source);
        if (idle.is_null()) {
            return false;
        }
        idle_script = idle.ptr();
        stand = new netw_test::SimStand(0);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(IDLE_ID), callable_mp_static(&build_idle));
        stand->mount();
        stand->arm(TICKRATE);
        for (int index = 0; index < 2; ++index) {
            Array args;
            args.push_back(String("script_wake") + String::num_int64(index));
            cubes.push_back(
                server()->spawn_registered(StringName(IDLE_ID), args, nullptr)
            );
            Node *built = server()->entity_get_node(cubes[index]);
            if (built == nullptr) {
                return false;
            }
            stand->arena()->add_child(built);
        }
        stand->pump(8);
        for (int index = 0; index < 2; ++index) {
            RigidBody3D *body = body_at(index);
            body->set_collision_layer(1);
            body->set_collision_mask(1);
            body->set_position(WAKE_GAP * double(index));
            entity_at(index)->get_simulation()->set_release_on_rest(0.0);
        }
        script_wake_evidence().host = stand->peer_id(-1);
        entity_at(0)->set_controller(script_wake_evidence().host);
        stand->pump(4);
        script_wake_evidence().seated = true;
        return true;
    }

    void close() {
        script_wake_evidence().driven = true;
        delete stand;
        stand = nullptr;
        idle_script = nullptr;
        idle = Ref<Script>();
    }

    void wake() {
        ScriptWakeEvidence &seen = script_wake_evidence();
        seen.slept = true;
        seen.target_before = entity_at(1)->get_controller();
        woke_at = frame;
        contacted = server()->idle_work.contacted;
        RigidBody3D *thrown = body_at(0);
        thrown->set_sleeping(false);
        thrown->set_linear_velocity(WAKE_THROW);
    }

    void watch() {
        ScriptWakeEvidence &seen = script_wake_evidence();
        const int since = frame - woke_at;
        if (seen.walked_frame < 0
            && server()->idle_work.contacted > contacted) {
            seen.walked_frame = since;
        }
        if (seen.claimed_frame < 0
            && entity_at(1)->get_controller() == seen.host) {
            seen.claimed_frame = since;
        }
    }

public:
    ScriptWakeScenario(
        WakeEvidence p_evidence,
        const char *p_source,
        const char *p_body_path
    )
        : script_wake_evidence(p_evidence), source(p_source),
          body_path(p_body_path) {
    }

    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            close();
            done = true;
            return false;
        }
        if (woke_at < 0) {
            const bool asleep
                = body_at(0)->is_sleeping() && body_at(1)->is_sleeping();
            if (asleep) {
                wake();
            } else if (frame >= WAKE_SLEEP_FRAMES) {
                close();
                done = true;
                return false;
            }
        } else {
            watch();
        }
        if (woke_at >= 0 && frame - woke_at >= WAKE_FLIGHT_FRAMES) {
            script_wake_evidence().target_after
                = entity_at(1)->get_controller();
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(
    ScriptWakeScenario,
    sim_script_wake_scenario,
    &script_wake_evidence,
    IDLE_SOURCE,
    ""
);

NETW_FRAME_SCENARIO(
    ScriptWakeScenario,
    sim_child_wake_scenario,
    &child_wake_evidence,
    CHILD_SOURCE,
    "Sphere"
);

TEST_CASE(
    "[Networked][Sim][Frame] a sleeping body a script wakes by writing "
    "sleeping false is walked by a contact pass within the pull's sweep "
    "latency and claims what it touches"
) {
    const ScriptWakeEvidence &evidence = script_wake_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    REQUIRE(evidence.slept);
    NETW_CHECK_EQ(evidence.target_before, int64_t(1));
    NETW_CHECK_GE(evidence.walked_frame, 1);
    NETW_CHECK_LE(
        evidence.walked_frame,
        int(netw::store::World::SWEEP_TICKS) + 2
    );
    NETW_CHECK_GE(evidence.claimed_frame, 1);
    NETW_CHECK_EQ(evidence.target_after, evidence.host);
}

TEST_CASE(
    "[Networked][Sim][Frame] a sleeping child body no store row reads, woken "
    "by a script writing sleeping false, is walked by the next contact pass "
    "and claims what it touches"
) {
    const ScriptWakeEvidence &evidence = child_wake_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    REQUIRE(evidence.slept);
    NETW_CHECK_EQ(evidence.target_before, int64_t(1));
    NETW_CHECK_GE(evidence.walked_frame, 1);
    NETW_CHECK_LE(evidence.walked_frame, 2);
    NETW_CHECK_GE(evidence.claimed_frame, 1);
    NETW_CHECK_EQ(evidence.target_after, evidence.host);
}

} // namespace TestNetwSimIdleFrameLaws

#endif

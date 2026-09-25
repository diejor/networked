#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
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
	e.simulation.replicas = NetwSimulationHandle.REPLICAS_ACTIVE
	e.interpolation.visual_root = ^"Visual"
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
    int64_t rested_tick = -1;
    int64_t woken_tick = -1;
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

    NetwMultiplayer *server() const {
        return stand->session(-1);
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
        const Ref<netw::display::Book> book = server()->get_display_book();
        for (const RID &cube : cubes) {
            const netw::sim::Row *row = server()->sim_row_of(cube);
            if (row != nullptr && row->bodies.recorded) {
                seen.recorded += 1;
            }
            const netw::display::Runtime *runtime = book->runtime_of(cube);
            if (runtime != nullptr
                && runtime->get_pump_mode()
                    == netw::display::PUMP_BRACKETED) {
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
        }
        if (frame == SETTLE_FRAMES + WINDOW_FRAMES) {
            read();
            wake_first();
        }
        if (frame == SETTLE_FRAMES + WINDOW_FRAMES + WAKE_FRAMES) {
            idle_evidence().woken_tick = newest_of_first();
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
    "[Networked][Sim][Frame] a body woken out of a sleeping pile is sampled "
    "into its display history again"
) {
    const IdleEvidence &evidence = idle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.rested_tick >= 0);
    NETW_CHECK_GT(evidence.woken_tick, evidence.rested_tick);
}

} // namespace TestNetwSimIdleFrameLaws

#endif

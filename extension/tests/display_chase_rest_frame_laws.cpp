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

using namespace godot;

namespace TestNetwDisplayChaseRestFrameLaws {

using netw::NetwMultiplayer;

constexpr const char *CUBE_ID = "display_rest_cube";

constexpr const char *CUBE_SOURCE = R"(extends RigidBody3D

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

constexpr int CLIENTS = 1;
constexpr int A = 0;
constexpr int CUBES = 16;
constexpr int TICKRATE = 30;
constexpr int SETTLE_FRAMES = 60;
constexpr int REST_FRAMES = 60;
const Vector3 NUDGE(1.0, 0.0, 0.0);

Script *cube_script = nullptr;

Node *build_cube(const Variant &p_name) {
    if (cube_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(cube_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct RestEvidence {
    bool driven = false;
    bool seated = false;
    int chasing = 0;
    int asleep = 0;
    int64_t rest_written = -1;
    int64_t woken_before = -1;
    int64_t woken_after = -1;
};

RestEvidence &rest_evidence() {
    static RestEvidence evidence;
    return evidence;
}

class ChaseRestScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    int routes[CUBES] = {};
    int64_t rest_opened = 0;

    netw::display::Runtime *runtime_at(int p_index) const {
        NetwMultiplayer *client = stand->session(A);
        const RID entity = client->entity_from_route(routes[p_index]);
        return client->get_display_book()->runtime_of(entity);
    }

    RigidBody3D *body_at(int p_index) const {
        return Object::cast_to<RigidBody3D>(
            stand->node_at(A, routes[p_index])
        );
    }

    int64_t written_at(int p_index) const {
        const netw::display::Runtime *runtime = runtime_at(p_index);
        int64_t total = 0;
        if (runtime == nullptr) {
            return total;
        }
        for (const netw::display::Channel *channel : runtime->channels()) {
            total += channel->written;
        }
        return total;
    }

    int64_t written_all() const {
        int64_t total = 0;
        for (int index = 0; index < CUBES; ++index) {
            total += written_at(index);
        }
        return total;
    }

    bool open() {
        script = netw_test::minted_script(CUBE_SOURCE);
        if (script.is_null()) {
            return false;
        }
        cube_script = script.ptr();
        stand = new netw_test::SimStand(CLIENTS);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(CUBE_ID), callable_mp_static(&build_cube));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        NetwMultiplayer *server = stand->session(-1);
        for (int index = 0; index < CUBES; ++index) {
            Array args;
            args.push_back(String(CUBE_ID) + String::num_int64(index));
            const RID made = server->spawn_registered(
                StringName(CUBE_ID),
                args,
                nullptr
            );
            Node *built = server->entity_get_node(made);
            if (built == nullptr) {
                return false;
            }
            arena->add_child(built);
            routes[index] = int(server->entity_get_route(made));
        }
        stand->pump(8);
        for (int index = 0; index < CUBES; ++index) {
            if (body_at(index) == nullptr) {
                return false;
            }
        }
        rest_evidence().seated = true;
        return true;
    }

    void read_rest() {
        RestEvidence &seen = rest_evidence();
        seen.rest_written = written_all() - rest_opened;
        for (int index = 0; index < CUBES; ++index) {
            const netw::display::Runtime *runtime = runtime_at(index);
            if (runtime != nullptr
                && runtime->get_pump_mode() == netw::display::PUMP_CHASE) {
                seen.chasing += 1;
            }
            if (body_at(index)->is_sleeping()) {
                seen.asleep += 1;
            }
        }
    }

    void close() {
        rest_evidence().driven = true;
        delete stand;
        stand = nullptr;
        cube_script = nullptr;
        script = Ref<Script>();
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
        const int woken = SETTLE_FRAMES + REST_FRAMES;
        if (frame == SETTLE_FRAMES) {
            rest_opened = written_all();
        }
        if (frame == woken) {
            read_rest();
            rest_evidence().woken_before = written_at(0);
            body_at(0)->set_linear_velocity(NUDGE);
        }
        if (frame == woken + 1) {
            rest_evidence().woken_after = written_at(0);
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(ChaseRestScenario, display_chase_rest_scenario);

TEST_CASE(
    "[Networked][Display][Frame] a resting active pile writes no visual for "
    "a second"
) {
    const RestEvidence &evidence = rest_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    REQUIRE(evidence.chasing == CUBES);
    REQUIRE(evidence.asleep == CUBES);
    NETW_CHECK_EQ(evidence.rest_written, int64_t(0));
}

TEST_CASE(
    "[Networked][Display][Frame] a cube woken out of a resting active pile "
    "writes its visual on the frame it wakes"
) {
    const RestEvidence &evidence = rest_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.woken_before >= 0);
    NETW_CHECK_GT(evidence.woken_after, evidence.woken_before);
}

} // namespace TestNetwDisplayChaseRestFrameLaws

#endif

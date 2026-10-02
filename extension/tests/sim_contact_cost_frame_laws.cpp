#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/alloc_probe.h"
#include "support/sim_stand.h"

#include "godot/collision_shape.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/store/validate.hpp"

using namespace godot;

namespace TestNetwSimContactCostFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *CUBE_ID = "contact_cost_cube";

constexpr const char *CUBE_SOURCE = R"(extends RigidBody3D

func _init() -> void:
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	add_child(shape)
	var e := Netw.configure_entity(self)
	e.transfer = NetwEntity.TRANSFER_IMMEDIATE
	e.simulation.replicas = NetwSimulationHandle.REPLICAS_ACTIVE
	e.simulation.claim_on_contact = true
	e.simulation.release_on_rest = 1.0
	Netw.configure_property(self, &"position").broadcast()
)";

constexpr int CLIENTS = 1;
constexpr int SESSIONS = CLIENTS + 1;
constexpr int TICKRATE = 30;
constexpr int CUBES = 12;
constexpr int SETTLE_FRAMES = 60;
constexpr int WINDOW_FRAMES = 30;
constexpr double JIGGLE = 0.6;

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

struct SessionEvidence {
    int64_t passes = 0;
    int64_t contacted = 0;
    int64_t touched = 0;
    int64_t allocations = 0;
    int64_t peer_reads = 0;
};

struct CostEvidence {
    bool driven = false;
    bool seated = false;
    int awake = 0;
    int led = 0;
    SessionEvidence sessions[SESSIONS];
};

CostEvidence &evidence() {
    static CostEvidence held;
    return held;
}

class CostScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    int routes[CUBES] = {};
    netw_test::AllocProbe probe;
    NetwMultiplayer::IdleWork opened[SESSIONS];

    RigidBody3D *cube_at(int p_client, int p_index) const {
        return Object::cast_to<RigidBody3D>(
            stand->node_at(p_client, routes[p_index])
        );
    }

    static uint32_t layer_of(int p_client) {
        return 1u << uint32_t(p_client + 2);
    }

    void lay_floor(int p_client) {
        StaticBody3D *floor = memnew(StaticBody3D);
        floor->set_name("Floor");
        CollisionShape3D *shape = memnew(CollisionShape3D);
        Ref<BoxShape3D> box;
        box.instantiate();
        box->set_size(Vector3(64.0, 1.0, 64.0));
        shape->set_shape(box);
        floor->add_child(shape);
        floor->set_collision_layer(layer_of(p_client));
        floor->set_collision_mask(layer_of(p_client));
        floor->set_position(Vector3(0.0, -1.0, 0.0));
        stand->arena_of(p_client)->add_child(floor);
    }

    bool spawn() {
        NetwMultiplayer *server = stand->session(-1);
        Node *arena = stand->arena();
        for (int client = -1; client < CLIENTS; ++client) {
            lay_floor(client);
        }
        stand->arm(TICKRATE);
        for (int index = 0; index < CUBES; ++index) {
            Array args;
            args.push_back(String(CUBE_ID) + String::num_int64(index));
            const RID made
                = server->spawn_registered(StringName(CUBE_ID), args, nullptr);
            Node *built = server->entity_get_node(made);
            if (built == nullptr) {
                return false;
            }
            arena->add_child(built);
            stand->pump(4);
            routes[index] = int(server->entity_get_route(made));
        }
        stand->pump(8);
        for (int index = 0; index < CUBES; ++index) {
            for (int client = -1; client < CLIENTS; ++client) {
                RigidBody3D *cube = cube_at(client, index);
                if (cube == nullptr) {
                    return false;
                }
                cube->set_collision_layer(layer_of(client));
                cube->set_collision_mask(layer_of(client));
            }
            cube_at(-1, index)->set_position(
                Vector3(1.02 * double(index), 0.0, 0.0)
            );
        }
        NetwEntity::of(cube_at(-1, 0))->set_controller(stand->peer_id(-1));
        return true;
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
        if (!spawn()) {
            return false;
        }
        evidence().seated = true;
        return true;
    }

    void close() {
        probe.disarm();
        evidence().driven = true;
        delete stand;
        stand = nullptr;
        cube_script = nullptr;
        script = Ref<Script>();
    }

    void jiggle() {
        const double sign = frame % 2 == 0 ? 1.0 : -1.0;
        for (int index = 0; index < CUBES; ++index) {
            const double along = index % 2 == 0 ? sign : -sign;
            for (int client = -1; client < CLIENTS; ++client) {
                cube_at(client, index)->set_linear_velocity(
                    Vector3(JIGGLE * along, 0.0, 0.0)
                );
            }
        }
    }

    void open_window() {
        for (int session = 0; session < SESSIONS; ++session) {
            NetwMultiplayer *api = stand->session(session - 1);
            api->store().validate_every = 0;
            opened[session] = api->idle_work;
        }
        probe.arm();
    }

    void read() {
        probe.disarm();
        CostEvidence &seen = evidence();
        for (int session = 0; session < SESSIONS; ++session) {
            const NetwMultiplayer::IdleWork &now
                = stand->session(session - 1)->idle_work;
            const NetwMultiplayer::IdleWork &was = opened[session];
            SessionEvidence &into = seen.sessions[session];
            into.passes = now.contacts - was.contacts;
            into.contacted = now.contacted - was.contacted;
            into.touched = now.touched - was.touched;
            into.allocations
                = now.contact_allocations - was.contact_allocations;
            into.peer_reads = now.contact_peer_reads - was.contact_peer_reads;
        }
        for (int index = 0; index < CUBES; ++index) {
            seen.awake += cube_at(-1, index)->is_sleeping() ? 0 : 1;
            seen.led += NetwEntity::of(cube_at(-1, index))->get_controller()
                    == stand->peer_id(-1)
                ? 1
                : 0;
        }
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
            open_window();
        }
        if (frame == SETTLE_FRAMES + WINDOW_FRAMES) {
            read();
            close();
            done = true;
            return false;
        }
        jiggle();
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(CostScenario, contact_cost_scenario);

TEST_CASE(
    "[Networked][Sim][Frame] a contact pass over awake bodies that claim, "
    "lead and fence while they touch makes no heap allocation through the "
    "extension's allocator on the host or on its client"
) {
    const CostEvidence &seen = evidence();
    REQUIRE(seen.driven);
    REQUIRE(seen.seated);
    NETW_CHECK_EQ(seen.awake, CUBES);
    NETW_CHECK_EQ(seen.led, CUBES);
    for (int session = 0; session < SESSIONS; ++session) {
        NETW_FORMAT_INT(session_text, session);
        CAPTURE(session_text);
        const SessionEvidence &into = seen.sessions[session];
        NETW_CHECK_EQ(into.passes, int64_t(WINDOW_FRAMES));
        NETW_CHECK_GE(into.contacted, int64_t(WINDOW_FRAMES * CUBES));
        NETW_CHECK_GE(into.touched, int64_t(WINDOW_FRAMES * CUBES));
        NETW_CHECK_EQ(into.allocations, int64_t(0));
    }
}

TEST_CASE(
    "[Networked][Sim][Frame] a contact pass asks the session who it is once, "
    "however many rows it walks"
) {
    const CostEvidence &seen = evidence();
    REQUIRE(seen.driven);
    REQUIRE(seen.seated);
    for (int session = 0; session < SESSIONS; ++session) {
        NETW_FORMAT_INT(session_text, session);
        CAPTURE(session_text);
        const SessionEvidence &into = seen.sessions[session];
        NETW_CHECK_GE(into.contacted, int64_t(WINDOW_FRAMES * CUBES));
        NETW_CHECK_EQ(into.peer_reads, into.passes);
    }
}

} // namespace TestNetwSimContactCostFrameLaws

#endif

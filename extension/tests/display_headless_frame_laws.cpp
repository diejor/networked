#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_reset.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/display/book.hpp"
#include "netw/display/runtime.hpp"

using namespace godot;

namespace TestNetwDisplayHeadlessFrameLaws {

using netw::NetwMultiplayer;

constexpr const char *CUBE_ID = "display_headless_cube";

constexpr const char *CUBE_SOURCE = R"(extends Node3D

func _init() -> void:
	var visual := Node3D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	Netw.configure_property(self, &"position").broadcast() \
			.interpolate(NetwInterpolate.new().lerp())
)";

constexpr int CLIENTS = 1;
constexpr int A = 0;
constexpr int CUBES = 8;
constexpr int TICKRATE = 30;
constexpr int SEATED_FRAMES = 20;
constexpr int HEADLESS_FRAMES = 80;
constexpr int RENDERED_FRAMES = 30;
const Vector3 DRIFT(0.05, 0.0, 0.0);

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

struct HeadlessEvidence {
    bool driven = false;
    bool seated = false;
    int64_t headless_written = -1;
    int64_t rendered_written = -1;
};

HeadlessEvidence &headless_evidence() {
    static HeadlessEvidence evidence;
    return evidence;
}

class HeadlessScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    int routes[CUBES] = {};
    Node3D *movers[CUBES] = {};
    int64_t opened = 0;

    int64_t written() const {
        NetwMultiplayer *client = stand->session(A);
        int64_t total = 0;
        for (int index = 0; index < CUBES; ++index) {
            const netw::display::Runtime *runtime
                = client->get_display_book()->runtime_of(
                    client->entity_from_route(routes[index])
                );
            if (runtime == nullptr) {
                continue;
            }
            for (const netw::display::Channel *channel : runtime->channels()) {
                total += channel->written;
            }
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
            const RID made
                = server->spawn_registered(StringName(CUBE_ID), args, nullptr);
            movers[index]
                = Object::cast_to<Node3D>(server->entity_get_node(made));
            if (movers[index] == nullptr) {
                return false;
            }
            arena->add_child(movers[index]);
            routes[index] = int(server->entity_get_route(made));
        }
        stand->pump(8);
        for (int index = 0; index < CUBES; ++index) {
            if (stand->node_at(A, routes[index]) == nullptr) {
                return false;
            }
        }
        headless_evidence().seated = true;
        return true;
    }

    void close() {
        headless_evidence().driven = true;
        netw_test::smooth_when_headless(true);
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
        const bool headless = frame < HEADLESS_FRAMES;
        netw_test::smooth_when_headless(!headless);
        if (frame == 0 && !open()) {
            close();
            done = true;
            return false;
        }
        HeadlessEvidence &seen = headless_evidence();
        if (frame == SEATED_FRAMES) {
            opened = written();
        }
        if (frame == HEADLESS_FRAMES) {
            seen.headless_written = written() - opened;
            opened = written();
        }
        if (frame == HEADLESS_FRAMES + RENDERED_FRAMES) {
            seen.rendered_written = written() - opened;
            close();
            done = true;
            return false;
        }
        for (Node3D *mover : movers) {
            mover->translate(DRIFT);
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(HeadlessScenario, display_headless_scenario);

TEST_CASE(
    "[Networked][Display][Frame] a client that does not render writes no "
    "smoothed value once its entities are seated"
) {
    const HeadlessEvidence &evidence = headless_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    NETW_CHECK_EQ(evidence.headless_written, int64_t(0));
}

TEST_CASE(
    "[Networked][Display][Frame] the same client writes smoothed values once "
    "smooth_when_headless is set"
) {
    const HeadlessEvidence &evidence = headless_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_GT(evidence.rendered_written, int64_t(0));
}

} // namespace TestNetwDisplayHeadlessFrameLaws

#endif

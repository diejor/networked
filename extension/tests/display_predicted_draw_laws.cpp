#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "support/minted_script.h"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/display/runtime.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestNetwDisplayPredictedDrawLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::LoopbackRig;

constexpr double STRIDE = 6.0;

const char *WALKER_SOURCE = R"(extends Node2D

var motion := 0


func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPrediction.ARCHETYPE_SCRIPTED
	e.display.visual_root = ^"Visual"
	e.display.live_mode = NetwDisplay.LIVE_MODE_BRACKETED
	Netw.configure_property(self, &"position").state() \
			.interpolate(NetwInterpolate.new().lerp().smooth(0.0))
	Netw.configure_property(self, &"motion").input()


func _network_tick(_delta: float, _tick: int, _is_fresh: bool) -> void:
	position.y += motion * 6.0
)";

Node *build_walker(const Variant &p_name, const Variant &p_script) {
    const Ref<Script> script = p_script;
    Node *made
        = Object::cast_to<Node>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    return made;
}

struct Stand {
    LoopbackRig rig;
    NetwMultiplayer *client = nullptr;
    Node2D *walker = nullptr;
    Node2D *visual = nullptr;

    Stand() : rig(1) {
        const Ref<Script> script = netw_test::minted_script(WALKER_SOURCE);
        rig.mount();
        Node *arena = rig.mirror_child("Arena");
        Array types;
        types.push_back(int(Variant::STRING));
        for (int at = -1; at < 1; ++at) {
            rig.register_constructor(
                at < 0 ? rig.server() : rig.client(at),
                StringName("walker"),
                callable_mp_static(&build_walker).bind(script),
                types
            );
        }
        rig.declare_world(netw_test::WorldDecl().clocked(15, 2));
        rig.join(-1, StringName("host"));
        rig.join(0, StringName("guest"));
        rig.pump(6);
        NetwMultiplayer *server = rig.server();
        Array args;
        args.push_back(String("Walker"));
        const RID spawned = server->spawn_registered(
            StringName("walker"),
            args,
            server->peer_get_player(rig.peer_id(0)).ptr()
        );
        arena->add_child(server->entity_get_node(spawned));
        const int64_t route = server->entity_get_route(spawned);
        rig.pump(6);

        client = rig.client(0);
        walker = Object::cast_to<Node2D>(
            client->entity_get_node(client->entity_from_route(route))
        );
        REQUIRE(walker != nullptr);
        visual = Object::cast_to<Node2D>(
            walker->get_node_or_null(NodePath("Visual"))
        );
    }

    void walk(int p_motion, int p_ticks) {
        walker->set(StringName("motion"), p_motion);
        for (int step = 0; step < p_ticks; ++step) {
            rig.step_ticks(1);
            client->display_pump(0.0);
        }
    }

    double drawn() const {
        return visual->get_global_position().y;
    }
};

TEST_CASE(
    "[Networked][Display][SceneTree] a client's bracketed predicted entity "
    "draws its own prediction while the server's rows are late"
) {
    Stand stand;
    stand.walk(1, 8);
    stand.rig.hold(0);
    double before = stand.drawn();
    for (int tick = 0; tick < 5; ++tick) {
        stand.walk(1, 1);
        NETW_CHECK_CLOSE(stand.drawn() - before, STRIDE, STRIDE * 0.25);
        before = stand.drawn();
    }
    stand.rig.release(0);
}

} // namespace TestNetwDisplayPredictedDrawLaws

#endif

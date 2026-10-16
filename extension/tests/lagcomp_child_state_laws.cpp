#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestNetwLagCompChildStateLaws {

constexpr const char *GEAR = R"(extends Node

class Part extends Node:
	var offset := 0.0

	func _init() -> void:
		name = &"Part"
		Netw.configure_property(self, &"offset").state()

var stride := 0.0
var part: Part

func _init() -> void:
	part = Part.new()
	add_child(part)
	Netw.configure_property(self, &"stride").state()
)";

constexpr int TICKS = 12;
constexpr int64_t AGO = 6;

Script *gear_script = nullptr;

Node *build_gear(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(gear_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

struct Seen {
    double stride = -1.0;
    double offset = -1.0;
};

Seen seen;
Node *watched = nullptr;

void look() {
    seen.stride = double(watched->get(StringName("stride")));
    seen.offset = double(
        watched->get_node<Node>(NodePath("Part"))->get(StringName("offset"))
    );
}

TEST_CASE(
    "[Networked][LagComp] K-RW6 a state property declared on a child node "
    "is rewound and sampled with the entity it belongs to"
) {
    const Ref<Script> script = netw_test::minted_script(GEAR);
    REQUIRE(script.is_valid());
    gear_script = script.ptr();

    netw_test::SimStand stand(0);
    REQUIRE(stand.ready());
    stand.teach(StringName("gear"), callable_mp_static(&build_gear));
    stand.mount();
    Node *arena = stand.arena();
    stand.arm(30);
    netw::NetwMultiplayer *server = stand.session(-1);
    REQUIRE(server->lagcomp_initialize(8, 12) == OK);

    Array args;
    args.push_back(String("gear"));
    const RID made
        = server->spawn_registered(StringName("gear"), args, nullptr);
    Node *gear = server->entity_get_node(made);
    REQUIRE(gear != nullptr);
    arena->add_child(gear);
    stand.pump(4);
    REQUIRE(server->lagcomp_timeline_declare(made) == OK);

    Node *part = gear->get_node<Node>(NodePath("Part"));
    REQUIRE(part != nullptr);
    for (int tick = 1; tick <= TICKS; ++tick) {
        gear->set(StringName("stride"), double(tick));
        part->set(StringName("offset"), double(tick) * 10.0);
        stand.step_ticks(1);
    }

    const int64_t past = server->clock_get_tick() - AGO;
    const Ref<netw::DictionaryRecord> sampled
        = server->lagcomp_sample(made, past);
    REQUIRE(sampled.is_valid());
    REQUIRE(sampled->has_value(StringName("stride")));
    REQUIRE(sampled->has_value(StringName("Part:offset")));
    const double stride_then = double(sampled->get_value(StringName("stride")));
    NETW_CHECK_CLOSE(
        double(sampled->get_value(StringName("Part:offset"))),
        stride_then * 10.0,
        0.0001
    );
    NETW_CHECK_LT(stride_then, double(TICKS));

    watched = gear;
    TypedArray<RID> entities;
    entities.push_back(made);
    server->lagcomp_rewind(entities, past, callable_mp_static(&look));
    watched = nullptr;
    gear_script = nullptr;

    NETW_CHECK_CLOSE(seen.stride, stride_then, 0.0001);
    NETW_CHECK_CLOSE(seen.offset, stride_then * 10.0, 0.0001);
    NETW_CHECK_CLOSE(
        double(part->get(StringName("offset"))),
        double(TICKS) * 10.0,
        0.0001
    );
}

} // namespace TestNetwLagCompChildStateLaws

#endif

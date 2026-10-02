#include "support/netw_test.h"

#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

#include "support/value_flow_stand.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/repl/property_binding.hpp"

namespace TestNetwPropertyIndexLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::repl::PropertyIndex;
using netw::repl::PropertySet;
using netw_test::FlowPair;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr const char *PLAIN_SOURCE = R"(extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").broadcast()
)";

constexpr const char *VARYING_SOURCE = R"(extends Node2D

var extra := &""

func _init() -> void:
	Netw.configure_property(self, &"position").broadcast()

func _get_property_list() -> Array[Dictionary]:
	if extra == &"":
		return []
	return [{"name": extra, "type": TYPE_INT,
			"usage": PROPERTY_USAGE_DEFAULT}]

func _get(property: StringName) -> Variant:
	return 0 if property == extra else null
)";

constexpr int TICKS = 6;

void author_ticks(
    LoopbackRig &p_rig,
    const LocalVector<FlowPair> &p_pairs,
    int p_ticks
) {
    for (int at = 0; at < p_ticks; ++at) {
        const Vector2 value
            = netw_test::authored_value(netw_test::clock_tick(p_rig, -1));
        for (const FlowPair &pair : p_pairs) {
            pair.authored->set_position(value);
        }
        p_rig.step_ticks(1);
    }
}

const PropertyIndex *index_of(const FlowPair &p_pair) {
    const netw::repl::PropertyBindingPtr binding = netw_test::binding_of(
        p_pair.mirror(0),
        PropertySet::RECORD_BROADCAST
    );
    REQUIRE(binding != nullptr);
    return binding->property_index();
}

bool indexes(const PropertyIndex *p_index, const char *p_name) {
    return p_index != nullptr && p_index->types.has(StringName(p_name));
}

TEST_CASE(
    "[Networked][Store][SceneTree] receiving nodes of one script share one "
    "property index, and a script whose property list varies by instance "
    "indexes each instance's own list"
) {
    LoopbackRig rig(1);
    rig.declare_world(WorldDecl().clocked(30));
    rig.mount();
    LocalVector<FlowPair> pairs;
    pairs.push_back(netw_test::stand_flow_pair(rig, PLAIN_SOURCE, "PlainA"));
    pairs.push_back(netw_test::stand_flow_pair(rig, PLAIN_SOURCE, "PlainB"));
    pairs.push_back(
        netw_test::stand_flow_pair(rig, VARYING_SOURCE, "VaryingA")
    );
    pairs.push_back(
        netw_test::stand_flow_pair(rig, VARYING_SOURCE, "VaryingB")
    );
    pairs[2].mirror(0)->set("extra", StringName("alpha"));
    pairs[3].mirror(0)->set("extra", StringName("beta"));
    for (const FlowPair &pair : pairs) {
        netw_test::steer(pair, 0);
    }
    author_ticks(rig, pairs, TICKS);

    const PropertyIndex *plain_a = index_of(pairs[0]);
    const PropertyIndex *plain_b = index_of(pairs[1]);
    const PropertyIndex *varying_a = index_of(pairs[2]);
    const PropertyIndex *varying_b = index_of(pairs[3]);
    REQUIRE(plain_a != nullptr);
    const bool plain_shared = plain_a == plain_b;
    CHECK(plain_shared);
    NETW_CHECK_EQ(int64_t(plain_a->users), int64_t(2));
    CHECK(indexes(plain_a, "position"));

    const bool varying_apart = varying_a != varying_b;
    CHECK(varying_apart);
    CHECK(indexes(varying_a, "alpha"));
    CHECK_FALSE(indexes(varying_a, "beta"));
    CHECK(indexes(varying_b, "beta"));
    CHECK_FALSE(indexes(varying_b, "alpha"));
}

} // namespace TestNetwPropertyIndexLaws

#endif

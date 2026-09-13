#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"

namespace TestSpawnRigLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

Node *build_probe(const Variant &p_marker) {
    Node *made = memnew(Node);
    made->set_name("Probe");
    made->set_meta(StringName("marker"), p_marker);
    return made;
}

Array string_arg(const String &p_value) {
    Array out;
    out.push_back(p_value);
    return out;
}

Array string_types() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

TEST_CASE(
    "[Networked][Spawn] SR1 a spawn reaches every peer as ITS OWN instance "
    "under its own session branch, carrying the argument it was built from"
) {
    LoopbackRig rig(1);
    rig.mount();

    const int route = rig.spawn_registered(
        StringName("probe"),
        callable_mp_static(&build_probe),
        string_arg("carried"),
        string_types()
    );
    NETW_CHECK_GT(route, 0);

    Node *host_node = rig.route_node(route);
    Node *seat_node = rig.route_node(route, 0);
    const bool host_built = host_node != nullptr;
    const bool seat_built = seat_node != nullptr;
    CHECK(host_built);
    CHECK(seat_built);
    if (!host_built || !seat_built) {
        return;
    }

    const bool two_instances = host_node != seat_node;
    CHECK(two_instances);

    const bool host_branch = host_node->get_parent() == rig.branch(-1);
    const bool seat_branch = seat_node->get_parent() == rig.branch(0);
    CHECK(host_branch);
    CHECK(seat_branch);

    const bool argument_rode
        = String(seat_node->get_meta(StringName("marker"))) == "carried";
    CHECK(argument_rode);
}

TEST_CASE(
    "[Networked][Spawn] SR2 both peers stamp the same route and the same "
    "entity id, which is what makes two instances one entity"
) {
    LoopbackRig rig(1);
    rig.mount();

    const int route = rig.spawn_registered(
        StringName("probe"),
        callable_mp_static(&build_probe),
        string_arg("named"),
        string_types()
    );

    const Ref<NetwEntity> host = NetwEntity::of(rig.route_node(route));
    const Ref<NetwEntity> seat = NetwEntity::of(rig.route_node(route, 0));
    CHECK(host.is_valid());
    CHECK(seat.is_valid());
    if (host.is_null() || seat.is_null()) {
        return;
    }

    NETW_CHECK_EQ(host->get_route(), int64_t(route));
    NETW_CHECK_EQ(seat->get_route(), int64_t(route));

    const bool ids_agree = host->get_entity_id() == seat->get_entity_id();
    CHECK(ids_agree);
}

TEST_CASE(
    "[Networked][Spawn] SR3 a host-less constructor that declares no argument "
    "schema writes nothing, because no receiver could decode the bytes"
) {
    LoopbackRig rig(1);
    rig.mount();

    const int route = rig.spawn_registered(
        StringName("undeclared"),
        callable_mp_static(&build_probe),
        string_arg("lost"),
        Array()
    );

    NETW_CHECK_GT(route, 0);
    const bool host_built = rig.route_node(route) != nullptr;
    CHECK(host_built);

    Node *seat_node = rig.route_node(route, 0);
    const bool nothing_crossed = seat_node == nullptr;
    CHECK(nothing_crossed);
}

} // namespace TestSpawnRigLaws

#endif

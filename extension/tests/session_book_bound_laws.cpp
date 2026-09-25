#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/script/model.hpp"
#include "support/value_flow_stand.h"

namespace TestNetwSessionBookBoundLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int ROUNDS = 10;
constexpr int PER_ROUND = 100;

Node *build_marker(const Variant &p_name) {
    Node3D *made = memnew(Node3D);
    made->set_name(String(p_name));
    NetwEntity::ensure(made);
    netw::script::model::configure_node_property(made, StringName("position"));
    return made;
}

Array named(int p_index) {
    Array out;
    out.push_back(String("Marker") + String::num_int64(p_index));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

class BookStage {
public:
    LoopbackRig rig;
    int minted = 0;

    BookStage() : rig(1) {
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        rig.step_ticks(4);
    }

    NetwMultiplayer *side(int p_side) const {
        return p_side < 0 ? rig.server() : rig.client(p_side);
    }

    void spawn_and_despawn(int p_count) {
        LocalVector<int> routes;
        for (int index = 0; index < p_count; ++index) {
            routes.push_back(rig.spawn_registered(
                StringName("book_marker"),
                callable_mp_static(&build_marker),
                named(minted++),
                one_type(),
                nullptr,
                Variant(),
                false
            ));
        }
        rig.step_ticks(6);
        NetwMultiplayer *server = rig.server();
        for (const int route : routes) {
            Node *gone = rig.route_node(route);
            REQUIRE(gone != nullptr);
            NETW_REQUIRE_EQ(
                server->entity_despawn(
                    server->entity_from_route(route),
                    Ref<netw::NetwDespawnOpts>()
                ),
                OK
            );
            gone->get_parent()->remove_child(gone);
        }
        rig.step_ticks(6);
    }
};

void check_bounded(
    const NetwMultiplayer::BookSizes &p_before,
    const NetwMultiplayer::BookSizes &p_after
) {
    NETW_CHECK_EQ(p_after.wrapper_owners, p_before.wrapper_owners);
    NETW_CHECK_EQ(p_after.handle_by_wrapper, p_before.handle_by_wrapper);
    NETW_CHECK_EQ(p_after.attributed_routes, p_before.attributed_routes);
    NETW_CHECK_EQ(p_after.node_overlays, p_before.node_overlays);
}

TEST_CASE(
    "[Networked][Lifecycle][SceneTree] a thousand entities spawned and "
    "despawned leave the wrapper owner index, the wrapper handle index, the "
    "per-route byte rows and the per-node property overlays at their size "
    "before the first spawn, on the server and on the client"
) {
    BookStage stage;
    for (int side = -1; side < 1; ++side) {
        stage.side(side)->attribution_set_armed(true);
    }
    stage.spawn_and_despawn(1);
    const NetwMultiplayer::BookSizes server_before
        = stage.side(-1)->book_sizes();
    const NetwMultiplayer::BookSizes client_before
        = stage.side(0)->book_sizes();
    for (int round = 0; round < ROUNDS; ++round) {
        stage.spawn_and_despawn(PER_ROUND);
    }
    check_bounded(server_before, stage.side(-1)->book_sizes());
    check_bounded(client_before, stage.side(0)->book_sizes());
}

} // namespace TestNetwSessionBookBoundLaws

#endif

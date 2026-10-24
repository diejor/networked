#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/script/model.hpp"
#include "support/declared_nodes.h"
#include "support/level_spawn.h"
#include "support/minted_script.h"
#include "support/value_flow_stand.h"

namespace TestNetwSessionChurnBoundLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int PER_ROUND = 4;
constexpr int ROUNDS = 3;

Node *build_body(const Variant &p_name) {
    Node3D *made = memnew(Node3D);
    made->set_name(String(p_name));
    NetwEntity::ensure(made);
    netw::script::model::configure_node_property(made, StringName("position"));
    return made;
}

Node *build_body_with_part(const Variant &p_name, const Ref<Script> &p_part) {
    Node *made = build_body(p_name);
    Node *part = Object::cast_to<Node>(p_part->call("new"));
    REQUIRE(part != nullptr);
    part->set_name("Part");
    made->add_child(part);
    return made;
}

Array named(int p_index) {
    Array out;
    out.push_back(String("Body") + String::num_int64(p_index));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

class ChurnStage {
public:
    LoopbackRig rig;
    Ref<netw::NetwInterestLayer> layer;
    Ref<netw::NetwInterestLayer> second;
    Ref<Script> part;
    int minted = 0;

    explicit ChurnStage(int p_clients = 1) : rig(p_clients) {
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        layer = rig.server()->interest_layer(StringName("arena"));
        second = rig.server()->interest_layer(StringName("team"));
        rig.step_ticks(4);
    }

    NetwMultiplayer *side(int p_side) const {
        return p_side < 0 ? rig.server() : rig.client(p_side);
    }

    int spawn(Node *p_parent, const Callable &p_build) {
        return rig.spawn_registered(
            StringName("churn_body"),
            p_build,
            named(minted++),
            one_type(),
            p_parent,
            Variant(),
            false
        );
    }

    int spawn(Node *p_parent) {
        return spawn(p_parent, callable_mp_static(&build_body));
    }

    void despawn_container(int p_container) {
        Node *held = rig.route_node(p_container);
        REQUIRE(held != nullptr);
        NetwMultiplayer *server = rig.server();
        NETW_REQUIRE_EQ(
            server->entity_despawn(
                server->entity_from_route(p_container),
                Ref<netw::NetwDespawnOpts>()
            ),
            OK
        );
        rig.step_ticks(6);
        if (held->get_parent() != nullptr) {
            held->get_parent()->remove_child(held);
        }
        rig.step_ticks(2);
        memdelete(held);
        rig.step_ticks(2);
    }

    void bodies_live_and_die() {
        const int container = spawn(nullptr);
        Node *held = rig.route_node(container);
        REQUIRE(held != nullptr);
        for (int index = 0; index < PER_ROUND; ++index) {
            const int route = spawn(held);
            layer->add_entity(NetwEntity::of(rig.route_node(route)));
            second->add_entity(NetwEntity::of(rig.route_node(route)));
        }
        rig.step_ticks(6);
        despawn_container(container);
    }

    void rewound_bodies_live_and_die() {
        const int container = spawn(nullptr);
        Node *held = rig.route_node(container);
        REQUIRE(held != nullptr);
        for (int index = 0; index < PER_ROUND; ++index) {
            const int route = spawn(held);
            rig.server()->register_rewind(
                NetwEntity::of(rig.route_node(route))
            );
        }
        rig.step_ticks(6);
        despawn_container(container);
    }

    void scene_lives_and_dies() {
        Node *root = netw_test::spawn_level(
            rig,
            String("res://tests/support/declared_level.tscn"),
            false
        );
        REQUIRE(root != nullptr);
        rig.step_ticks(2);
        for (int index = 0; index < PER_ROUND; ++index) {
            spawn(root);
        }
        rig.step_ticks(6);
        despawn_container(int(NetwEntity::of(root)->get_route()));
    }

    void peer_joins_and_leaves() {
        if (minted == 0) {
            spawn(nullptr);
        }
        const int joined = rig.add_client();
        rig.client(joined)->spawn_register_constructor(
            StringName("churn_body"),
            callable_mp_static(&build_body),
            one_type()
        );
        Ref<netw::NetwClockConfig> clock;
        clock.instantiate();
        clock->set_tickrate(TICKRATE);
        NETW_REQUIRE_EQ(rig.client(joined)->clock_initialize(clock), OK);
        rig.client(joined)->clock_engine().set_manual_tick(true);
        rig.mount_late(joined);
        rig.step_ticks(6);
        rig.drop_client(joined);
        rig.step_ticks(6);
        rig.peer(joined)->NETW_PEER_VIRTUAL(close)();
        rig.step_ticks(2);
    }

    int64_t loopback_peers() const {
        return int64_t(rig.session()->get_client_peers().size());
    }
};

struct Readings {
    NetwMultiplayer::BookSizes at_n;
    NetwMultiplayer::BookSizes at_2n;
    int64_t loopback_at_n = 0;
    int64_t loopback_at_2n = 0;
};

Readings churn(void (ChurnStage::*p_round)(), int p_clients = 1) {
    ChurnStage stage(p_clients);
    (stage.*p_round)();
    Readings out;
    for (int round = 0; round < ROUNDS; ++round) {
        (stage.*p_round)();
    }
    out.at_n = stage.side(-1)->book_sizes();
    out.loopback_at_n = stage.loopback_peers();
    for (int round = 0; round < ROUNDS; ++round) {
        (stage.*p_round)();
    }
    out.at_2n = stage.side(-1)->book_sizes();
    out.loopback_at_2n = stage.loopback_peers();
    return out;
}

TEST_CASE(
    "[Networked][Lifecycle][SceneTree] CB1 a body that dies inside a "
    "despawned parent while it belongs to two interest layers gives up its "
    "wrapper slot, so N and 2N rounds leave the same slot and owner counts"
) {
    const Readings read = churn(&ChurnStage::bodies_live_and_die);
    NETW_CHECK_EQ(read.at_2n.wrapper_slots, read.at_n.wrapper_slots);
    NETW_CHECK_EQ(read.at_2n.wrapper_owners, read.at_n.wrapper_owners);
}

TEST_CASE(
    "[Networked][Display][SceneTree] CB2 a body that dies drops its display "
    "row, so N and 2N rounds of bodies leave the same display book"
) {
    const Readings read = churn(&ChurnStage::bodies_live_and_die);
    NETW_CHECK_EQ(read.at_2n.display_rows, read.at_n.display_rows);
}

TEST_CASE(
    "[Networked][Scene][SceneTree] CB3 a destroyed scene closes its interest "
    "layer, so N and 2N scenes created and destroyed leave the same layers"
) {
    const Readings read = churn(&ChurnStage::scene_lives_and_dies);
    NETW_CHECK_EQ(read.at_2n.layer_views, read.at_n.layer_views);
    NETW_CHECK_EQ(read.at_2n.engine_layers, read.at_n.engine_layers);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] CB4 a scripted part that leaves the tree "
    "keeps no path from where it was, so the engine frees its path with "
    "the node and the part reports its new path once it enters elsewhere"
) {
    ChurnStage stage(0);
    stage.part = netw_test::script_from(netw_test::gdsrc::STATE_ONLY);
    REQUIRE(stage.part.is_valid());
    const int route = stage.spawn(
        nullptr,
        callable_mp_static(&build_body_with_part).bind(stage.part)
    );
    Node *body = stage.rig.route_node(route);
    REQUIRE(body != nullptr);
    Node *part = body->get_node_or_null(NodePath("Part"));
    REQUIRE(part != nullptr);
    stage.rig.step_ticks(4);

    Node *elsewhere = memnew(Node);
    elsewhere->set_name("Elsewhere");
    stage.rig.branch(-1)->add_child(elsewhere);
    Node *from = body->get_parent();
    from->remove_child(body);
    elsewhere->add_child(body);

    const String expected = String(elsewhere->get_path()) + "/"
        + String(body->get_name()) + "/Part";
    const String seen = String(part->get_path());
    NETW_CHECK_EQ(int(seen == expected), 1);
    elsewhere->remove_child(body);
    memdelete(body);
    elsewhere->get_parent()->remove_child(elsewhere);
    memdelete(elsewhere);
}

TEST_CASE(
    "[Networked][Transport][SceneTree] CB5 a loopback client that leaves is "
    "dropped by its loopback session, so N and 2N peers joining and leaving "
    "leave the same client list"
) {
    const Readings read = churn(&ChurnStage::peer_joins_and_leaves);
    NETW_CHECK_EQ(read.loopback_at_2n, read.loopback_at_n);
}

TEST_CASE(
    "[Networked][Wire][SceneTree] CB6 a peer that leaves takes its byte "
    "counts with it, so N and 2N peers joining and leaving leave the same "
    "attribution book"
) {
    const Readings read = churn(&ChurnStage::peer_joins_and_leaves);
    NETW_CHECK_EQ(
        read.at_2n.attribution_peer_keys,
        read.at_n.attribution_peer_keys
    );
}

TEST_CASE(
    "[Networked][LagComp][SceneTree] CB7 a body with a rewind history that "
    "dies releases the history and its hold on the body, so N and 2N rounds "
    "leave the same histories"
) {
    const Readings read = churn(&ChurnStage::rewound_bodies_live_and_die);
    NETW_CHECK_EQ(read.at_2n.lagcomp_timelines, read.at_n.lagcomp_timelines);
}

} // namespace TestNetwSessionChurnBoundLaws

#endif

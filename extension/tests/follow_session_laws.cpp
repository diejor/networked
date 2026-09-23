#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/minted_script.h"
#include "support/stock_stand.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"

#include <godot_cpp/classes/multiplayer_synchronizer.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/scene_replication_config.hpp>

namespace TestFollowSession {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

constexpr int TICKRATE = 30;

constexpr const char *SPLIT_SOURCE = R"(extends Node2D

var synced_value := 0
var watched_value := 0

func _init() -> void:
	Netw.configure_entity(self).follow_session(^"ServerSync")
)";

SyncProp prop_row(const char *p_path) {
    SyncProp row;
    row.path = NodePath(p_path);
    row.mode = SceneReplicationConfig::REPLICATION_MODE_ALWAYS;
    return row;
}

Ref<PackedScene> split_scene(bool p_input_first) {
    Node *root = scripted_root(SPLIT_SOURCE, "FollowSplit");
    Vector<SyncProp> server_props;
    server_props.push_back(prop_row(".:synced_value"));
    Vector<SyncProp> input_props;
    input_props.push_back(prop_row(".:watched_value"));
    if (p_input_first) {
        add_stock_sync(root, "InputSync", input_props);
        add_stock_sync(root, "ServerSync", server_props);
    } else {
        add_stock_sync(root, "ServerSync", server_props);
        add_stock_sync(root, "InputSync", input_props);
    }
    const Ref<PackedScene> packed
        = pack_stock_scene(root, mint_scene_path("FollowSplit"));
    memdelete(root);
    return packed;
}

struct Triple {
    int root = -1;
    int server_sync = -1;
    int input_sync = -1;
};

struct SplitStand {
    LoopbackRig rig;
    StockWorld world;
    Node *held[3] = {nullptr, nullptr, nullptr};

    explicit SplitStand(bool p_input_first) : rig(2) {
        rig.mount();
        flow_clocks(rig, TICKRATE);
        const Ref<PackedScene> scene = split_scene(p_input_first);
        PackedStringArray scenes;
        scenes.push_back(scene->get_path());
        world = mount_stock_world(rig, scenes);
        Node *node = scene->instantiate();
        node->set_name("Split");
        world.arena(-1)->add_child(node, true);
        held[0] = node;
        for (int client = 0; client < 2; ++client) {
            held[client + 1]
                = pump_until_child(rig, world.arena(client), "Split");
            REQUIRE(held[client + 1] != nullptr);
        }
        rig.pump(4);
    }

    Node *copy(int p_client) const {
        return held[p_client + 1];
    }

    Ref<NetwEntity> entity(int p_client) const {
        return NetwEntity::of(copy(p_client));
    }

    Triple triple(int p_client) const {
        Node *node = copy(p_client);
        Triple read;
        read.root = node->get_multiplayer_authority();
        read.server_sync = node->get_node<Node>(NodePath("ServerSync"))
                               ->get_multiplayer_authority();
        read.input_sync = node->get_node<Node>(NodePath("InputSync"))
                              ->get_multiplayer_authority();
        return read;
    }

    void every_peer_reads(int p_root, int p_server, int p_input) {
        for (int client = -1; client < 2; ++client) {
            const Triple read = triple(client);
            NETW_CHECK_EQ(read.root, p_root);
            NETW_CHECK_EQ(read.server_sync, p_server);
            NETW_CHECK_EQ(read.input_sync, p_input);
        }
    }

    void grant(int p_client) {
        entity(-1)->grant_control(rig.peer_id(p_client));
        rig.pump(10);
    }

    void revoke() {
        entity(-1)->revoke_control();
        rig.pump(10);
    }

    bool any_peer_warned() const {
        for (int client = -1; client < 2; ++client) {
            if (entity(client)->get_record()->get_native_write_warned()) {
                return true;
            }
        }
        return false;
    }
};

void acceptance(bool p_input_first) {
    SplitStand stand(p_input_first);
    const int a = stand.rig.peer_id(0);
    const int b = stand.rig.peer_id(1);

    stand.every_peer_reads(1, 1, 1);

    stand.grant(0);
    stand.every_peer_reads(a, 1, a);
    stand.copy(-1)->set("synced_value", 9);
    NETW_CHECK_EQ(
        int(pump_until_value(stand.rig, stand.copy(0), "synced_value", 9)),
        9
    );
    stand.copy(0)->set("watched_value", 7);
    NETW_CHECK_EQ(
        int(pump_until_value(stand.rig, stand.copy(-1), "watched_value", 7)),
        7
    );

    stand.grant(1);
    stand.every_peer_reads(b, 1, b);

    stand.revoke();
    stand.every_peer_reads(1, 1, 1);
    CHECK_FALSE(stand.any_peer_warned());
}

TEST_CASE(
    "[Networked][Control][SceneTree] FS1 a followed synchronizer stays with "
    "the session while the rest of the entity follows each grant"
) {
    SUBCASE("ServerSync declared first") {
        acceptance(false);
    }
    SUBCASE("InputSync declared first") {
        acceptance(true);
    }
}

TEST_CASE(
    "[Networked][Control][SceneTree] FS2 a child added after a grant takes "
    "the controller, and one added under a followed node takes the session"
) {
    SplitStand stand(false);
    const int a = stand.rig.peer_id(0);
    stand.grant(0);

    for (int client = -1; client < 2; ++client) {
        Node *node = stand.copy(client);
        Node *late = memnew(Node);
        late->set_name("Late");
        Node *leaf = memnew(Node);
        leaf->set_name("Leaf");
        late->add_child(leaf);
        node->add_child(late);
        Node *pinned_late = memnew(Node);
        node->get_node<Node>(NodePath("ServerSync"))->add_child(pinned_late);
        NETW_CHECK_EQ(late->get_multiplayer_authority(), a);
        NETW_CHECK_EQ(leaf->get_multiplayer_authority(), a);
        NETW_CHECK_EQ(pinned_late->get_multiplayer_authority(), 1);
    }

    stand.revoke();
    for (int client = -1; client < 2; ++client) {
        Node *node = stand.copy(client);
        NETW_CHECK_EQ(
            node->get_node<Node>(NodePath("Late/Leaf"))
                ->get_multiplayer_authority(),
            1
        );
    }
    CHECK_FALSE(stand.any_peer_warned());
}

TEST_CASE(
    "[Networked][Control][SceneTree] FS3 a native authority write inside an "
    "entity is replaced by the next grant and warned once"
) {
    SplitStand stand(false);
    const int b = stand.rig.peer_id(1);
    stand.grant(0);
    CHECK_FALSE(stand.any_peer_warned());

    stand.copy(-1)
        ->get_node<Node>(NodePath("InputSync"))
        ->set_multiplayer_authority(1, false);
    stand.grant(1);
    NETW_CHECK_EQ(stand.triple(-1).input_sync, b);
    CHECK(stand.entity(-1)->get_record()->get_native_write_warned());
    CHECK_FALSE(stand.entity(0)->get_record()->get_native_write_warned());
}

} // namespace TestFollowSession

#endif

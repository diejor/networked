#pragma once

#include "netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "loopback_rig.h"
#include "minted_script.h"

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

#include <godot_cpp/classes/multiplayer_spawner.hpp>
#include <godot_cpp/classes/multiplayer_synchronizer.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/scene_replication_config.hpp>

namespace netw_test {

struct SyncProp {
    godot::NodePath path;
    int mode = godot::SceneReplicationConfig::REPLICATION_MODE_ALWAYS;
    bool on_spawn = false;
};

inline godot::String mint_scene_path(const char *p_stem) {
    static int minted = 0;
    minted += 1;
    return godot::vformat("res://_netwtest/native/%d/%s.tscn", minted, p_stem);
}

inline void own_recursively(godot::Node *p_node, godot::Node *p_owner) {
    p_node->set_owner(p_owner);
    for (int at = 0; at < p_node->get_child_count(); ++at) {
        own_recursively(p_node->get_child(at), p_owner);
    }
}

inline godot::MultiplayerSynchronizer *add_stock_sync(
    godot::Node *p_root,
    const godot::String &p_name,
    const godot::Vector<SyncProp> &p_props,
    godot::Node *p_owner = nullptr,
    bool p_public = true
) {
    godot::MultiplayerSynchronizer *sync
        = memnew(godot::MultiplayerSynchronizer);
    sync->set_name(p_name);
    sync->set_root_path(godot::NodePath(".."));
    godot::Ref<godot::SceneReplicationConfig> config;
    config.instantiate();
    for (const SyncProp &prop : p_props) {
        config->add_property(prop.path);
        if (prop.on_spawn) {
            config->property_set_spawn(prop.path, true);
        }
        config->property_set_replication_mode(
            prop.path,
            godot::SceneReplicationConfig::ReplicationMode(prop.mode)
        );
    }
    sync->set_replication_config(config);
    sync->set_visibility_public(p_public);
    p_root->add_child(sync);
    sync->set_owner(p_owner != nullptr ? p_owner : p_root);
    return sync;
}

inline godot::Ref<godot::PackedScene> pack_stock_scene(
    godot::Node *p_root,
    const godot::String &p_path
) {
    if (p_root->has_meta(godot::StringName("netw_entity"))) {
        p_root->remove_meta(godot::StringName("netw_entity"));
    }
    godot::Ref<godot::PackedScene> packed;
    packed.instantiate();
    NETW_CHECK_EQ(int(packed->pack(p_root)), int(godot::OK));
    packed->take_over_path(p_path);
    return packed;
}

inline godot::Node *scripted_root(
    const godot::Ref<godot::Script> &p_script,
    const godot::String &p_name
) {
    REQUIRE_MESSAGE(p_script.is_valid(), "the probe script did not load");
    godot::Node *root
        = godot::Object::cast_to<godot::Node>(p_script->call("new"));
    REQUIRE_MESSAGE(root != nullptr, "the probe script built no node");
    root->set_name(p_name);
    return root;
}

inline godot::Node *scripted_root(
    const char *p_source,
    const godot::String &p_name
) {
    return scripted_root(script_from(p_source), p_name);
}

struct StockWorld {
    godot::Vector<godot::Node *> arenas;
    godot::Vector<godot::MultiplayerSpawner *> spawners;
    godot::PackedStringArray scenes;

    godot::Node *arena(int p_client) const {
        const int at = p_client + 1;
        REQUIRE_MESSAGE(at < arenas.size(), "that peer holds no arena");
        return arenas[at];
    }

    godot::MultiplayerSpawner *spawner(int p_client) const {
        const int at = p_client + 1;
        REQUIRE_MESSAGE(at < spawners.size(), "that peer holds no spawner");
        return spawners[at];
    }
};

inline void seat_stock_branch(godot::Node *p_branch, StockWorld &p_world) {
    godot::Node *world = memnew(godot::Node);
    world->set_name("StockWorld");
    godot::Node *arena = memnew(godot::Node);
    arena->set_name("Arena");
    world->add_child(arena);
    godot::MultiplayerSpawner *spawner = memnew(godot::MultiplayerSpawner);
    spawner->set_name("StockSpawner");
    spawner->set_spawn_path(godot::NodePath("../Arena"));
    for (int at = 0; at < p_world.scenes.size(); ++at) {
        spawner->add_spawnable_scene(p_world.scenes[at]);
    }
    world->add_child(spawner);
    p_branch->add_child(world);
    p_world.arenas.push_back(arena);
    p_world.spawners.push_back(spawner);
}

inline void relabel_stock_world(StockWorld &p_world, const char *p_name) {
    for (godot::Node *arena : p_world.arenas) {
        godot::Node *world = arena->get_parent();
        if (world != nullptr) {
            world->set_name(p_name);
        }
    }
}

inline StockWorld mount_stock_world(
    LoopbackRig &p_rig,
    const godot::PackedStringArray &p_scenes
) {
    StockWorld world;
    world.scenes = p_scenes;
    seat_stock_branch(p_rig.branch(-1), world);
    for (int index = 0; index < p_rig.count(); ++index) {
        seat_stock_branch(p_rig.branch(index), world);
    }
    p_rig.pump(4);
    return world;
}

inline void peers_agree_on_authority(
    const godot::Vector<godot::Node *> &p_held
) {
    REQUIRE_MESSAGE(p_held.size() >= 2, "agreement needs two peers");
    REQUIRE_MESSAGE(p_held[0] != nullptr, "the first peer holds nothing");
    const int64_t first = int64_t(p_held[0]->get_multiplayer_authority());
    for (int at = 1; at < p_held.size(); ++at) {
        REQUIRE_MESSAGE(p_held[at] != nullptr, "a peer holds nothing");
        NETW_CHECK_EQ(int64_t(p_held[at]->get_multiplayer_authority()), first);
    }
}

inline godot::Node *pump_until_child(
    LoopbackRig &p_rig,
    godot::Node *p_parent,
    const godot::String &p_name,
    int p_budget = 60
) {
    for (int round = 0; round < p_budget; ++round) {
        godot::Node *found
            = p_parent->get_node_or_null(godot::NodePath(p_name));
        if (found != nullptr) {
            return found;
        }
        p_rig.pump();
    }
    return p_parent->get_node_or_null(godot::NodePath(p_name));
}

inline godot::Variant pump_until_value(
    LoopbackRig &p_rig,
    godot::Object *p_target,
    const godot::StringName &p_property,
    const godot::Variant &p_wanted,
    int p_budget = 120
) {
    for (int round = 0; round < p_budget; ++round) {
        if (p_target->get(p_property) == p_wanted) {
            return p_wanted;
        }
        p_rig.step_ticks(1);
    }
    return p_target->get(p_property);
}

} // namespace netw_test

#endif

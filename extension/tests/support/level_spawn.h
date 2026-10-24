#pragma once

#include "loopback_rig.h"
#include "netw_test.h"

#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/resource.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"

namespace netw_test {

inline godot::StringName level_constructor() {
    return godot::StringName("netw_test_level");
}

inline godot::Node *build_level(
    const godot::String &p_path,
    int64_t p_isolation
) {
    const godot::Ref<godot::PackedScene> packed
        = netw::gd::load_scene(netw::gd::ensure_path(p_path));
    if (packed.is_null()) {
        return nullptr;
    }
    godot::Node *level = packed->instantiate();
    const godot::Ref<netw::NetwEntity> seat = netw::NetwEntity::ensure(level);
    if (seat.is_valid()) {
        seat->set_declares_scene(true);
        seat->set_scene_label(level->get_name());
        seat->set_scene_isolation(p_isolation);
    }
    return level;
}

inline void register_level(netw::NetwMultiplayer *p_session) {
    godot::Array types;
    types.push_back(int(godot::Variant::STRING));
    types.push_back(int(godot::Variant::INT));
    p_session->spawn_register_constructor(
        level_constructor(),
        callable_mp_static(&build_level),
        types
    );
}

inline godot::Node *spawn_level(
    netw::NetwMultiplayer *p_server,
    const godot::String &p_path,
    bool p_isolated,
    godot::Node *p_parent = nullptr
) {
    godot::Array args;
    args.push_back(p_path);
    args.push_back(int64_t(
        p_isolated ? netw::NetwSceneCore::ISOLATION_OWN_WORLD
                   : netw::NetwSceneCore::ISOLATION_NONE
    ));
    const godot::RID entity
        = p_server->spawn_registered(level_constructor(), args, nullptr);
    godot::Node *level = p_server->entity_get_node(entity);
    if (level == nullptr) {
        return nullptr;
    }
    godot::Node *parent
        = p_parent != nullptr ? p_parent : p_server->session_root();
    if (parent != nullptr) {
        parent->add_child(netw::NetwMultiplayer::scene_outer_of(level));
    }
    return level;
}

#if defined(NETW_TIER_HOSTED)

inline godot::Node *spawn_level(
    LoopbackRig &p_rig,
    const godot::String &p_path,
    bool p_isolated
) {
    register_level(p_rig.server());
    for (int index = 0; index < p_rig.count(); ++index) {
        register_level(p_rig.client(index));
    }
    return spawn_level(p_rig.server(), p_path, p_isolated);
}

#endif

} // namespace netw_test

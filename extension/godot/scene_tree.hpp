#pragma once

#include "godot/engine.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"

#if defined(NETW_MODULE)
#include "core/os/main_loop.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace godot {
using ::MainLoop;
using ::SceneTree;
using ::SceneTreeTimer;
using ::Window;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_tree_timer.hpp>
#include <godot_cpp/classes/window.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::SceneTree *scene_tree() {
#if defined(NETW_MODULE)
    return godot::SceneTree::get_singleton();
#else
    godot::Engine *engine = godot::Engine::get_singleton();
    if (engine == nullptr) {
        return nullptr;
    }
    return godot::Object::cast_to<godot::SceneTree>(engine->get_main_loop());
#endif
}

inline godot::Node *scene_root() {
    godot::SceneTree *tree = scene_tree();
    if (tree == nullptr) {
        return nullptr;
    }
    return tree->get_root();
}

} // namespace netw::gd

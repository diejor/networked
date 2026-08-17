#pragma once

#include "godot/ref_counted.hpp"

#if defined(NETW_MODULE)
#include "core/io/resource_loader.h"
#include "scene/resources/packed_scene.h"

namespace godot {
using ::PackedScene;
using ::ResourceLoader;
using ::SceneState;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::Ref<godot::PackedScene> load_scene(const godot::String &p_path) {
#if defined(NETW_MODULE)
    return ::ResourceLoader::load(p_path);
#else
    return godot::ResourceLoader::get_singleton()->load(p_path);
#endif
}

} // namespace netw::gd

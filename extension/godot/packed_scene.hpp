#pragma once

#include "godot/ref_counted.hpp"
#include "godot/resource_uid.hpp"
#include "godot/variant.hpp"

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

enum LoadStatus {
    LOAD_IN_PROGRESS,
    LOAD_LOADED,
    LOAD_FAILED,
};

inline godot::Error load_begin(const godot::String &p_path) {
#if defined(NETW_MODULE)
    return ::ResourceLoader::load_threaded_request(p_path, "PackedScene");
#else
    godot::ResourceLoader *loader = godot::ResourceLoader::get_singleton();
    if (loader == nullptr) {
        return godot::ERR_UNAVAILABLE;
    }
    return loader->load_threaded_request(p_path, "PackedScene");
#endif
}

inline LoadStatus load_status(const godot::String &p_path) {
#if defined(NETW_MODULE)
    const ::ResourceLoader::ThreadLoadStatus status
        = ::ResourceLoader::load_threaded_get_status(p_path);
    if (status == ::ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
        return LOAD_IN_PROGRESS;
    }
    return status == ::ResourceLoader::THREAD_LOAD_LOADED ? LOAD_LOADED
                                                          : LOAD_FAILED;
#else
    godot::ResourceLoader *loader = godot::ResourceLoader::get_singleton();
    if (loader == nullptr) {
        return LOAD_FAILED;
    }
    const godot::ResourceLoader::ThreadLoadStatus status
        = loader->load_threaded_get_status(p_path);
    if (status == godot::ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
        return LOAD_IN_PROGRESS;
    }
    return status == godot::ResourceLoader::THREAD_LOAD_LOADED ? LOAD_LOADED
                                                               : LOAD_FAILED;
#endif
}

inline godot::Ref<godot::PackedScene> load_collect(
    const godot::String &p_path
) {
#if defined(NETW_MODULE)
    return ::ResourceLoader::load_threaded_get(p_path);
#else
    godot::ResourceLoader *loader = godot::ResourceLoader::get_singleton();
    if (loader == nullptr) {
        return godot::Ref<godot::PackedScene>();
    }
    return loader->load_threaded_get(p_path);
#endif
}

inline bool resource_available(const godot::String &p_path) {
#if defined(NETW_MODULE)
    return ::ResourceCache::has(p_path) || ::ResourceLoader::exists(p_path);
#else
    godot::ResourceLoader *loader = godot::ResourceLoader::get_singleton();
    if (loader == nullptr) {
        return false;
    }
    return loader->has_cached(p_path) || loader->exists(p_path);
#endif
}

inline int64_t resource_uid_for(const godot::String &p_path) {
#if defined(NETW_MODULE)
    return int64_t(::ResourceLoader::get_resource_uid(p_path));
#else
    godot::ResourceLoader *loader = godot::ResourceLoader::get_singleton();
    return loader != nullptr ? loader->get_resource_uid(p_path)
                             : int64_t(godot::ResourceUID::INVALID_ID);
#endif
}

inline godot::String resource_uid_path(int64_t p_uid) {
    godot::ResourceUID *uid = godot::ResourceUID::get_singleton();
    if (uid == nullptr || !uid->has_id(p_uid)) {
        return godot::String();
    }
    return uid->get_id_path(p_uid);
}

} // namespace netw::gd

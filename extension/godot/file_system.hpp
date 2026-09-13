#pragma once

#include "godot/project_settings.hpp"
#include "godot/resource.hpp"
#include "godot/resource_loader.hpp"

#if defined(NETW_MODULE)
#include "core/io/dir_access.h"
#include "core/io/resource_saver.h"

namespace godot {
using ::DirAccess;
using ::ResourceSaver;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::Ref<godot::Resource> load_fresh(
    const godot::String &p_path,
    const godot::String &p_type_hint
) {
#if defined(NETW_MODULE)
    return ::ResourceLoader::load(
        p_path,
        p_type_hint,
        ::ResourceFormatLoader::CACHE_MODE_REPLACE
    );
#else
    godot::ResourceLoader *loader = godot::ResourceLoader::get_singleton();
    if (loader == nullptr) {
        return godot::Ref<godot::Resource>();
    }
    return loader
        ->load(p_path, p_type_hint, godot::ResourceLoader::CACHE_MODE_REPLACE);
#endif
}

inline bool resource_exists(const godot::String &p_path) {
#if defined(NETW_MODULE)
    return ::ResourceLoader::exists(p_path);
#else
    godot::ResourceLoader *loader = godot::ResourceLoader::get_singleton();
    return loader != nullptr && loader->exists(p_path);
#endif
}

inline godot::Error save_resource(
    const godot::Ref<godot::Resource> &p_resource,
    const godot::String &p_path
) {
#if defined(NETW_MODULE)
    return ::ResourceSaver::save(p_resource, p_path);
#else
    godot::ResourceSaver *saver = godot::ResourceSaver::get_singleton();
    if (saver == nullptr) {
        return godot::ERR_UNAVAILABLE;
    }
    return saver->save(p_resource, p_path);
#endif
}

inline godot::String globalized(const godot::String &p_path) {
    godot::ProjectSettings *settings = godot::ProjectSettings::get_singleton();
    return settings != nullptr ? settings->globalize_path(p_path) : p_path;
}

} // namespace netw::gd

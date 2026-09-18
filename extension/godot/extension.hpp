#pragma once

#if defined(NETW_MODULE)
#elif defined(NETW_GDEXTENSION)
#include <gdextension_interface.h>

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#include "godot/object.hpp"
#include "godot/variant.hpp"

#include <cstdint>

namespace netw::gd {

void *method_bind(
    const godot::StringName &p_class,
    const godot::StringName &p_method,
    uint64_t p_hash
);

void method_ptrcall(
    void *p_bind,
    void *p_instance,
    const void **p_args,
    void *r_return
);

void method_call(
    void *p_bind,
    void *p_instance,
    const void **p_args,
    int64_t p_argument_count,
    void *r_return
);

void retain_reference(void *p_instance);

void release_reference(void *p_instance);

void *engine_object(godot::Object *p_object);

} // namespace netw::gd

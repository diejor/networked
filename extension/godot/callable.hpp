#pragma once

#include "godot/object.hpp"

#if defined(NETW_MODULE)
#include "core/object/callable_mp.h"
#include "core/variant/callable.h"

namespace godot {
using ::Callable;
using ::CallableCustom;
using ::ObjectID;
using ::Signal;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/signal.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

#if defined(NETW_MODULE)
using CallError = godot::Callable::CallError;
#else
using CallError = GDExtensionCallError;
#endif

inline void call_ok(CallError &error) {
#if defined(NETW_MODULE)
    error.error = godot::Callable::CallError::CALL_OK;
#else
    error.error = GDEXTENSION_CALL_OK;
#endif
}

inline godot::ObjectID instance_id(const godot::Object *object) {
    if (object == nullptr) {
        return godot::ObjectID();
    }
#if defined(NETW_MODULE)
    return object->get_instance_id();
#else
    return godot::ObjectID(object->get_instance_id());
#endif
}

inline godot::Object *instance_from_id(const godot::ObjectID &id) {
    if (!id.is_valid()) {
        return nullptr;
    }
#if defined(NETW_MODULE)
    return ObjectDB::get_instance(id);
#else
    return godot::ObjectDB::get_instance(uint64_t(id));
#endif
}

} // namespace netw::gd

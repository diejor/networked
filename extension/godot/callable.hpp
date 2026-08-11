#pragma once

#include "godot/object.hpp"

#if defined(NETW_MODULE)
#include "core/variant/callable.h"

// Engine types are global, so the aliases keep `godot::` spellings compiling.
namespace godot {
using ::Callable;
using ::CallableCustom;
using ::ObjectID;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/core/object_id.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/callable_custom.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

// A custom callable is written once and compiled twice. The three places the
// two surfaces differ are named here so a subclass reads the same in both.
namespace netw::gd {

// The out-parameter `CallableCustom::call` reports through.
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

// The engine returns an ObjectID, godot-cpp returns the raw integer inside it.
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

// The way back, for a reference that must not keep its target alive. Answers
// null once the object is gone, which is the whole point of holding the id.
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

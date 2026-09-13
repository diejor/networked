#pragma once

#include "godot/local_vector.hpp"
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

inline godot::Variant call_checked(
    const godot::Callable &callable,
    const godot::Variant **args,
    int arg_count,
    bool &ok
) {
    godot::Variant result;
    CallError error;
    call_ok(error);
#if defined(NETW_MODULE)
    callable.callp(args, arg_count, result, error);
    ok = error.error == godot::Callable::CallError::CALL_OK;
#else
    godot::Variant held(callable);
    held.callp(godot::StringName("call"), args, arg_count, result, error);
    ok = error.error == GDEXTENSION_CALL_OK;
#endif
    return result;
}

inline godot::Variant call_checked(const godot::Callable &callable, bool &ok) {
    return call_checked(callable, nullptr, 0, ok);
}

inline godot::Variant call_checked(
    const godot::Callable &callable,
    const godot::Variant &argument,
    bool &ok
) {
    const godot::Variant *args[1] = {&argument};
    return call_checked(callable, args, 1, ok);
}

inline godot::Variant call_checked(
    const godot::Callable &callable,
    const godot::Array &arguments,
    bool &ok
) {
    const int count = int(arguments.size());
    godot::LocalVector<godot::Variant> held;
    godot::LocalVector<const godot::Variant *> pointers;
    held.resize(uint32_t(count));
    pointers.resize(uint32_t(count));
    for (int at = 0; at < count; ++at) {
        held[uint32_t(at)] = arguments[at];
        pointers[uint32_t(at)] = &held[uint32_t(at)];
    }
    return call_checked(callable, pointers.ptr(), count, ok);
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

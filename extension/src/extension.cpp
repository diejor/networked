#include "godot/extension.hpp"

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"

#if defined(NETW_MODULE)
#include "core/object/class_db.h"
#include "core/object/method_bind.h"
#endif

using namespace godot;

namespace netw::gd {

void *method_bind(
    const StringName &p_class,
    const StringName &p_method,
    uint64_t p_hash
) {
#if defined(NETW_MODULE)
    return ::ClassDB::get_method_with_compatibility(p_class, p_method, p_hash);
#else
    return const_cast<void *>(
        godot::gdextension_interface::classdb_get_method_bind(
            p_class._native_ptr(),
            p_method._native_ptr(),
            GDExtensionInt(p_hash)
        )
    );
#endif
}

void method_ptrcall(
    void *p_bind,
    void *p_instance,
    const void **p_args,
    void *r_return
) {
    if (p_bind == nullptr) {
        return;
    }
#if defined(NETW_MODULE)
    reinterpret_cast<::MethodBind *>(p_bind)
        ->ptrcall(reinterpret_cast<::Object *>(p_instance), p_args, r_return);
#else
    godot::gdextension_interface::object_method_bind_ptrcall(
        p_bind,
        p_instance,
        reinterpret_cast<GDExtensionConstTypePtr *>(p_args),
        r_return
    );
#endif
}

void method_call(
    void *p_bind,
    void *p_instance,
    const void **p_args,
    int64_t p_argument_count,
    void *r_return
) {
    if (p_bind == nullptr) {
        return;
    }
#if defined(NETW_MODULE)
    Callable::CallError error;
    Variant answered = reinterpret_cast<::MethodBind *>(p_bind)->call(
        reinterpret_cast<::Object *>(p_instance),
        reinterpret_cast<const Variant **>(p_args),
        int(p_argument_count),
        error
    );
    if (r_return != nullptr) {
        *reinterpret_cast<Variant *>(r_return) = answered;
    }
#else
    GDExtensionCallError error;
    godot::gdextension_interface::object_method_bind_call(
        p_bind,
        p_instance,
        reinterpret_cast<GDExtensionConstVariantPtr *>(p_args),
        GDExtensionInt(p_argument_count),
        r_return,
        &error
    );
#endif
}

void *engine_object(Object *p_object) {
    if (p_object == nullptr) {
        return nullptr;
    }
#if defined(NETW_MODULE)
    return p_object;
#else
    return p_object->_owner;
#endif
}

void retain_reference(void *p_instance) {
    if (p_instance == nullptr) {
        return;
    }
#if defined(NETW_MODULE)
    reinterpret_cast<::RefCounted *>(p_instance)->reference();
#else
    static void *reference = method_bind(
        StringName("RefCounted"),
        StringName("reference"),
        2240911060
    );
    GDExtensionBool taken = false;
    method_ptrcall(reference, p_instance, nullptr, &taken);
#endif
}

void release_reference(void *p_instance) {
    if (p_instance == nullptr) {
        return;
    }
#if defined(NETW_MODULE)
    ::RefCounted *counted = reinterpret_cast<::RefCounted *>(p_instance);
    if (counted->unreference()) {
        memdelete(counted);
    }
#else
    static void *unreference = method_bind(
        StringName("RefCounted"),
        StringName("unreference"),
        2240911060
    );
    GDExtensionBool dropped = false;
    method_ptrcall(unreference, p_instance, nullptr, &dropped);
    if (dropped) {
        godot::gdextension_interface::object_destroy(p_instance);
    }
#endif
}

} // namespace netw::gd

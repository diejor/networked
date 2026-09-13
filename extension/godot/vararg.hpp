#pragma once

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/variant.hpp"

#include <tuple>
#include <type_traits>
#include <utility>

namespace netw::gd {

#if defined(NETW_MODULE)
using VarargBase = ::MethodBind;
using VarargInstance = godot::Object *;
using ::GetTypeInfo;
using ::VariantCaster;
using ::VariantObjectClassChecker;
#else
using VarargBase = godot::MethodBind;
using VarargInstance = GDExtensionClassInstancePtr;
using godot::GetTypeInfo;
using godot::VariantCaster;
using godot::VariantObjectClassChecker;
#endif

template <typename R, typename... A> struct StaticVarargTarget {
    R (*function)(A...);

    template <typename... P> R operator()(VarargInstance, P &&...p_args) const {
        return function(std::forward<P>(p_args)...);
    }
};

template <typename C, typename R, typename... A> struct InstanceVarargTarget {
    R (C::*method)(A...);

    template <typename... P>
    R operator()(VarargInstance p_instance, P &&...p_args) const {
        return (static_cast<C *>(p_instance)->*method)(
            std::forward<P>(p_args)...
        );
    }
};

template <typename Target, typename R, typename... A>
class VarargBind : public VarargBase {
    using Arguments = std::tuple<A...>;
    static constexpr int fixed_count = sizeof...(A) - 1;
    Target target;

    template <typename T>
    std::decay_t<T> argument(
        const godot::Variant **p_args,
        int p_index,
        CallError &r_error
    ) const {
        const auto type = godot::Variant::Type(GetTypeInfo<T>::VARIANT_TYPE);
        if (!godot::Variant::can_convert_strict(
                p_args[p_index]->get_type(),
                type
            )
            || !VariantObjectClassChecker<T>::check(*p_args[p_index])) {
#if defined(NETW_MODULE)
            r_error.error
                = godot::Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
#else
            r_error.error = GDEXTENSION_CALL_ERROR_INVALID_ARGUMENT;
#endif
            r_error.argument = p_index;
            r_error.expected = type;
            return {};
        }
        return VariantCaster<std::decay_t<T>>::cast(*p_args[p_index]);
    }

    template <size_t... I>
    godot::PropertyInfo argument_info(
        int p_index,
        std::index_sequence<I...>
    ) const {
        godot::PropertyInfo result;
        ((p_index == int(I)
              ? (result = GetTypeInfo<
                     std::tuple_element_t<I, Arguments>>::get_class_info(),
                 true)
              : false),
         ...);
        return result;
    }

    godot::PropertyInfo info(int p_index) const {
        if (p_index < 0) {
            if constexpr (!std::is_void_v<R>) {
                return GetTypeInfo<R>::get_class_info();
            }
            return {};
        }
        return argument_info(p_index, std::make_index_sequence<fixed_count>());
    }

    template <size_t... I>
    godot::Variant invoke(
        VarargInstance p_instance,
        const godot::Variant **p_args,
        int p_count,
        CallError &r_error,
        std::index_sequence<I...>
    ) const {
        std::tuple<std::decay_t<std::tuple_element_t<I, Arguments>>...> prefix{
            argument<std::tuple_element_t<I, Arguments>>(p_args, I, r_error)...
        };
        if (r_error.error != 0) {
            return {};
        }
        godot::Array tail;
        tail.resize(p_count - fixed_count);
        for (int i = fixed_count; i < p_count; ++i) {
            tail[i - fixed_count] = *p_args[i];
        }
        if constexpr (std::is_void_v<R>) {
            target(p_instance, std::get<I>(prefix)..., tail);
            return {};
        } else {
            return target(p_instance, std::get<I>(prefix)..., tail);
        }
    }

    godot::Variant dispatch(
        VarargInstance p_instance,
        const godot::Variant **p_args,
        int p_count,
        CallError &r_error
    ) const {
        call_ok(r_error);
        if (p_count < fixed_count) {
#if defined(NETW_MODULE)
            r_error.error
                = godot::Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
#else
            r_error.error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
#endif
            r_error.expected = fixed_count;
            return {};
        }
        return invoke(
            p_instance,
            p_args,
            p_count,
            r_error,
            std::make_index_sequence<fixed_count>()
        );
    }

public:
    VarargBind(Target p_target, bool p_is_static) : target(p_target) {
        static_assert(std::is_same_v<
                      std::tuple_element_t<fixed_count, Arguments>,
                      const godot::Array &>);
        _set_static(p_is_static);
        _set_returns(!std::is_void_v<R>);
#if defined(NETW_GDEXTENSION)
        _set_vararg(true);
#endif
        set_argument_count(fixed_count);
        _generate_argument_types(fixed_count);
    }

#if defined(NETW_MODULE)
    godot::PropertyInfo _gen_argument_type_info(int p_index) const override {
        return info(p_index);
    }
    godot::Variant::Type _gen_argument_type(int p_index) const override {
        return info(p_index).type;
    }
#ifdef DEBUG_ENABLED
    GodotTypeInfo::Metadata get_argument_meta(int) const override {
        return GodotTypeInfo::METADATA_NONE;
    }
#endif
    bool is_vararg() const override {
        return true;
    }
    godot::Variant call(
        godot::Object *p_instance,
        const godot::Variant **p_args,
        int p_count,
        CallError &r_error
    ) const override {
        return dispatch(p_instance, p_args, p_count, r_error);
    }
    void validated_call(
        godot::Object *,
        const godot::Variant **,
        godot::Variant *
    ) const override {
        ERR_FAIL_MSG("Varargs require a counted call.");
    }
    void ptrcall(godot::Object *, const void **, void *) const override {
        ERR_FAIL_MSG("Varargs require a counted call.");
    }
#else
    godot::PropertyInfo gen_argument_type_info(int p_index) const override {
        return info(p_index);
    }
    GDExtensionVariantType gen_argument_type(int p_index) const override {
        return GDExtensionVariantType(info(p_index).type);
    }
    GDExtensionClassMethodArgumentMetadata get_argument_metadata(
        int
    ) const override {
        return GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE;
    }
    godot::Variant call(
        GDExtensionClassInstancePtr p_instance,
        const GDExtensionConstVariantPtr *p_args,
        GDExtensionInt p_count,
        CallError &r_error
    ) const override {
        return dispatch(
            p_instance,
            const_cast<const godot::Variant **>(
                reinterpret_cast<const godot::Variant *const *>(p_args)
            ),
            p_count,
            r_error
        );
    }
    void ptrcall(
        GDExtensionClassInstancePtr,
        const GDExtensionConstTypePtr *,
        GDExtensionTypePtr
    ) const override {
        ERR_FAIL_MSG("Varargs require a counted call.");
    }
#endif
};

struct VarargRegistration {
    VarargBase *binding;
};

inline VarargBase *create_method_bind(VarargRegistration p_bind) {
    return p_bind.binding;
}

template <typename N>
void register_vararg(
    const godot::StringName &p_class,
    N p_name,
    VarargBase *p_binding
) {
    p_binding->set_instance_class(p_class);
#if defined(NETW_MODULE)
#ifdef DEBUG_ENABLED
    p_binding->set_name(p_name.name);
    p_binding->set_argument_names(p_name.args);
#else
    p_binding->set_name(p_name);
#endif
    godot::ClassDB::bind_method_custom(p_class, p_binding);
#else
    godot::ClassDB::bind_method(p_name, VarargRegistration{p_binding});
#endif
}

template <typename N, typename R, typename... A>
void bind_static_vararg(
    const godot::StringName &p_class,
    N p_name,
    R (*p_function)(A...)
) {
    NETW_REFUSE_UNREFERENCED_RETURN(decltype(p_function));
    using Target = StaticVarargTarget<R, A...>;
    register_vararg(
        p_class,
        p_name,
        memnew((VarargBind<Target, R, A...>)(Target{p_function}, true))
    );
}

template <typename N, typename C, typename R, typename... A>
void bind_vararg_method(N p_name, R (C::*p_method)(A...)) {
    NETW_REFUSE_UNREFERENCED_RETURN(decltype(p_method));
    using Target = InstanceVarargTarget<C, R, A...>;
    register_vararg(
        godot::StringName(C::get_class_static()),
        p_name,
        memnew((VarargBind<Target, R, A...>)(Target{p_method}, false))
    );
}

} // namespace netw::gd

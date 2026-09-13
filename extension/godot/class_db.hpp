#pragma once

#if defined(NETW_MODULE)
#include "core/object/class_db.h"

namespace godot {
using ::ClassDB;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/core/class_db.hpp>

using godot::D_METHOD;
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

#include <type_traits>
#include <utility>

namespace netw::gd {

godot::Variant instantiate_class(const godot::StringName &p_class);

} // namespace netw::gd

namespace netw {

namespace bind_return {

template <typename T, typename = void>
struct complete_and_refcounted : std::false_type {};

template <typename T>
struct complete_and_refcounted<T, decltype(void(sizeof(T)))>
    : std::is_base_of<godot::RefCounted, T> {};

template <typename R> struct refcounted_by_pointer : std::false_type {};

template <typename T>
struct refcounted_by_pointer<T *> : complete_and_refcounted<T> {};

template <typename M> struct declared {
    using type = void;
};

template <typename R, typename C, typename... A>
struct declared<R (C::*)(A...)> {
    using type = R;
};

template <typename R, typename C, typename... A>
struct declared<R (C::*)(A...) const> {
    using type = R;
};

template <typename R, typename C, typename... A>
struct declared<R (C::*)(A...) noexcept> {
    using type = R;
};

template <typename R, typename C, typename... A>
struct declared<R (C::*)(A...) const noexcept> {
    using type = R;
};

template <typename R, typename... A> struct declared<R (*)(A...)> {
    using type = R;
};

template <typename R, typename... A> struct declared<R (*)(A...) noexcept> {
    using type = R;
};

template <typename M> constexpr bool escapes_unreferenced() {
    return refcounted_by_pointer<typename declared<M>::type>::value;
}

} // namespace bind_return

#define NETW_REFUSE_UNREFERENCED_RETURN(m_method) \
    static_assert( \
        !::netw::bind_return::escapes_unreferenced<m_method>(), \
        "a bound method answering a RefCounted by raw pointer hands the " \
        "script an object that took no reference, which frees it under the " \
        "caller and corrupts the heap. Return Ref<T> from the bound form." \
    )

struct ClassDB {
    template <typename N, typename M, typename... VarArgs>
    static auto bind_method(N p_name, M p_method, VarArgs... p_args) {
        NETW_REFUSE_UNREFERENCED_RETURN(M);
        return godot::ClassDB::bind_method(p_name, p_method, p_args...);
    }

    template <typename C, typename N, typename M, typename... VarArgs>
    static auto bind_static_method(
        C p_class,
        N p_name,
        M p_method,
        VarArgs... p_args
    ) {
        NETW_REFUSE_UNREFERENCED_RETURN(M);
        return godot::ClassDB::bind_static_method(
            p_class,
            p_name,
            p_method,
            p_args...
        );
    }

    template <typename... A> static auto bind_integer_constant(A &&...p_args) {
        return godot::ClassDB::bind_integer_constant(
            std::forward<A>(p_args)...
        );
    }

    template <typename... A> static auto add_property(A &&...p_args) {
        return godot::ClassDB::add_property(std::forward<A>(p_args)...);
    }

    template <typename... A> static auto class_exists(A &&...p_args) {
        return godot::ClassDB::class_exists(std::forward<A>(p_args)...);
    }

    template <typename... A> static auto get_parent_class(A &&...p_args) {
        return godot::ClassDB::get_parent_class(std::forward<A>(p_args)...);
    }

    template <typename... A> static auto can_instantiate(A &&...p_args) {
        return godot::ClassDB::can_instantiate(std::forward<A>(p_args)...);
    }
};

} // namespace netw

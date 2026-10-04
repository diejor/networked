#pragma once

#include <cstdint>
#include <type_traits>

#include "godot/local_vector.hpp"

namespace netw::held {

template <typename T> int64_t vector(const godot::LocalVector<T> &p_vector) {
    return int64_t(p_vector.get_capacity()) * int64_t(sizeof(T));
}

template <typename T>
int64_t vectors(const godot::LocalVector<godot::LocalVector<T>> &p_vectors) {
    int64_t bytes = vector(p_vectors);
    for (const godot::LocalVector<T> &inner : p_vectors) {
        bytes += vector(inner);
    }
    return bytes;
}

template <typename M> int64_t map(const M &p_map) {
    if (p_map.is_empty()) {
        return 0;
    }
    using Pair
        = std::remove_cv_t<std::remove_reference_t<decltype(*p_map.begin())>>;
    return int64_t(p_map.get_capacity())
        * int64_t(sizeof(uint32_t) + sizeof(void *))
        + int64_t(p_map.size()) * int64_t(sizeof(Pair) + 2 * sizeof(void *));
}

template <typename M> int64_t map_of_vectors(const M &p_map) {
    int64_t bytes = map(p_map);
    for (const auto &pair : p_map) {
        bytes += vector(pair.value);
    }
    return bytes;
}

template <typename S> int64_t set(const S &p_set) {
    if (p_set.is_empty()) {
        return 0;
    }
    using Key
        = std::remove_cv_t<std::remove_reference_t<decltype(*p_set.begin())>>;
    return int64_t(p_set.get_capacity())
        * int64_t(sizeof(Key) + 3 * sizeof(uint32_t));
}

} // namespace netw::held

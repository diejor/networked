#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/variant.hpp"
#include "netw/property_set_builder.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/repl/property_set.hpp"

namespace netw_test {

struct Books {
    netw::repl::SetBook sets;
    netw::repl::BindingBook bindings{sets};
};

struct DeclaredSet {
    Books *books = nullptr;
    uint64_t id = netw::repl::SetBook::NONE;

    netw::repl::PropertySet *operator->() const {
        return books->sets.get(id);
    }
};

inline DeclaredSet declare(Books &r_books) {
    DeclaredSet made;
    made.books = &r_books;
    made.id = r_books.sets.create();
    return made;
}

inline netw::repl::PropertyBinding *bind(
    const DeclaredSet &p_set,
    godot::Node *p_node
) {
    return p_set.books->bindings.create(p_set.id, p_node);
}

inline uint64_t sealed_set(
    netw::repl::SetBook &r_sets,
    const godot::Dictionary &p_configs,
    int64_t p_record
) {
    const uint64_t made = r_sets.create();
    netw::repl::PropertySet *set = r_sets.get(made);
    if (!netw::property_set_builder::from_property_configs(
            *set,
            p_configs,
            p_record
        )) {
        r_sets.free(made);
        return netw::repl::SetBook::NONE;
    }
    set->sealed = true;
    return made;
}

} // namespace netw_test

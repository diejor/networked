#pragma once

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "netw/persistence_book.hpp"

namespace netw::persist {

struct Plane {
    PersistenceBook engines;
    godot::Callable quit_guard;
    godot::Callable drain;
    bool shutting_down = false;
};

void snapshot_tick(
    PersistenceBook &r_book,
    double p_delta,
    bool p_is_server
);

void flush_all(
    PersistenceBook &r_book,
    bool p_is_server
);

void owner_exiting(
    PersistenceBook &r_book,
    const godot::RID &p_entity,
    bool p_is_server
);

} // namespace netw::persist

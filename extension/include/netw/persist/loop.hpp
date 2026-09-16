#pragma once

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "netw/persist/book.hpp"
#include "netw/persist/write_fence.hpp"

namespace netw::persist {

struct Plane {
    Book engines;
    godot::Callable quit_guard;
    godot::Callable drain;
    bool shutting_down = false;
};

void snapshot_tick(
    Book &r_book,
    double p_delta,
    bool p_may_issue,
    const WriteFence &p_issuer
);

void flush_all(Book &r_book, bool p_may_issue);

void owner_exiting(Book &r_book, const godot::RID &p_entity, bool p_may_issue);

} // namespace netw::persist

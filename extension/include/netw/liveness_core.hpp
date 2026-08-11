#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

// The identity substrate: it mints entity handles, bridges them to the wire
// names called routes, and holds the existence state machine those routes move
// through. It holds no Object, which is what lets the records outlive every
// entity they name.
class NetwLivenessCore : public godot::RefCounted {
    GDCLASS(NetwLivenessCore, godot::RefCounted)

public:
    enum State {
        STATE_UNKNOWN = 0,
        STATE_LIVE = 1,
        STATE_LINGERING = 2,
        STATE_DEAD = 3,
    };

private:
    struct Record {
        int32_t route = 0;
        State state = STATE_UNKNOWN;
    };

    // One caller waiting for a route to go live. The deadline is a plain
    // integer because the counter it is measured against lives above this
    // class: a tick clock is a session service and the record plane holds no
    // session. So arming names which counter the deadline belongs to, and the
    // sweep is handed that counter's reading again.
    struct Pending {
        godot::Callable callback;
        godot::Callable on_timeout;
        int32_t deadline = 0;
        bool on_clock = false;
    };

    // Mutable because RID_Owner's lookups are non-const in both build tiers,
    // and reading a record does not change the store.
    mutable godot::RID_Owner<Record> records;
    godot::HashMap<int32_t, godot::RID> by_route;
    godot::HashMap<int32_t, godot::LocalVector<Pending>> pending;
    int32_t route_counter = 0;
    int32_t frame_counter = 0;

    Record *record_of(const godot::RID &entity) const;
    godot::PackedInt32Array sweep_pending(int clock_tick);

protected:
    static void _bind_methods();

public:
    NetwLivenessCore();
    ~NetwLivenessCore();

    // The mint. A fresh record starts unrouted and STATE_UNKNOWN.
    godot::RID entity_create();
    bool entity_is_valid(const godot::RID &entity) const;

    // The route bridge. Routes are session-monotonic and never reused, so
    // reserve_route hands out a name before any record claims it.
    int reserve_route();
    bool bind_route(const godot::RID &entity, int route);
    int route_of(const godot::RID &entity) const;
    godot::RID rid_from_route(int route) const;

    // The state machine. set_state moves forward only; bind_route is the one
    // backward door, and only from STATE_DEAD to STATE_LIVE.
    State state_of(const godot::RID &entity) const;
    State route_state(int route) const;
    bool set_state(const godot::RID &entity, State state);

    // Reads. Routes come back sorted so two calls in one frame agree.
    godot::PackedInt32Array live_routes() const;
    int route_count() const;

    // The bulk doors, which mint identities with no wrapper and no node: the
    // shape a replicated table row is. They emit nothing per route, because a
    // wave of two thousand rows carries no wrapper for a listener to reach.
    void bind_routes_data(const godot::PackedInt64Array &routes);
    void tombstone_routes_data(const godot::PackedInt64Array &routes);

    // The waiting room. A caller that must not lose a reliable event across the
    // spawn window parks on the route instead of retrying, and flush_live is
    // what answers it. Binding does not answer it implicitly: the two bulk
    // doors and the wrapper bind flush deliberately, and a bind that means
    // nothing to a waiting caller stays silent.
    void when_live(
        int route,
        const godot::Callable &callback,
        int deadline,
        bool on_clock,
        const godot::Callable &on_timeout
    );
    void flush_live(int route);
    // The other end of a wait. A tombstoned route can never go live, so its
    // waiters are dropped rather than answered and rather than timed out.
    void abandon_live(int route);
    int pending_live_count() const;

    // Advances the frame counter and expires whatever aged out, answering the
    // routes whose last waiter timed out so the caller can say so. Pass the
    // tick a clock is reading, or 0 when no clock is configured.
    godot::PackedInt32Array poll(int clock_tick);
    int frame() const;

    // Session teardown. The only place a record is freed.
    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwLivenessCore::State);

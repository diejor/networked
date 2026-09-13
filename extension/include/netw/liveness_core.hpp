#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwLivenessCore : public godot::RefCounted {
public:
    enum State {
        STATE_UNKNOWN = 0,
        STATE_LIVE = 1,
        STATE_LINGERING = 2,
        STATE_DEAD = 3,
        STATE_ABSENT = 4,
    };

private:
    struct Record {
        int32_t route = 0;
        State state = STATE_UNKNOWN;
        int32_t epoch = 0;
        int32_t wire_epoch = -1;
    };

    struct Pending {
        godot::Callable callback;
        godot::Callable on_timeout;
        int32_t deadline = 0;
        bool on_clock = false;
    };

    godot::HashMap<godot::RID, Record> records;
    godot::HashMap<int32_t, godot::RID> by_route;
    godot::HashMap<int32_t, godot::LocalVector<Pending>> pending;
    int32_t route_counter = 0;
    int32_t frame_counter = 0;

    Record *record_of(const godot::RID &entity) const;
    godot::PackedInt32Array sweep_pending(int clock_tick);

public:
    NetwLivenessCore();
    ~NetwLivenessCore();

    godot::RID entity_create();

    bool adopt(const godot::RID &entity);

    bool entity_is_valid(const godot::RID &entity) const;

    int reserve_route();

    bool bind_route(const godot::RID &entity, int route);
    int route_of(const godot::RID &entity) const;
    godot::RID rid_from_route(int route) const;

    int epoch_of(const godot::RID &entity) const;
    int route_epoch(int route) const;
    int route_wire_epoch(int route) const;
    bool epoch_admits(int route, int epoch) const;
    bool adopt_epoch(int route, int epoch);

    State state_of(const godot::RID &entity) const;
    State route_state(int route) const;
    bool set_state(const godot::RID &entity, State state);
    bool hide_route(int route);

    godot::PackedInt32Array live_routes() const;
    int route_count() const;

    void bind_routes_data(const godot::PackedInt64Array &routes);
    void tombstone_routes_data(const godot::PackedInt64Array &routes);

    void when_live(
        int route,
        const godot::Callable &callback,
        int deadline,
        bool on_clock,
        const godot::Callable &on_timeout
    );
    void flush_live(int route);
    void fail_live(int route);
    int pending_live_count() const;

    godot::PackedInt32Array poll(int clock_tick);
    int frame() const;

    void clear();
};

} // namespace netw

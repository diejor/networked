#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwMultiplayer;
class NetwPredictSlotEngine;
class NetwPredictTiming;

class NetwPredictRunner {
    godot::LocalVector<NetwPredictSlotEngine *> engines;
    godot::LocalVector<NetwPredictSlotEngine *> sorted;
    godot::LocalVector<NetwPredictSlotEngine *> phase_roster;
    godot::HashMap<int64_t, NetwPredictSlotEngine *> by_slot;
    bool sort_dirty = true;
    godot::ObjectID core_id;

    NetwMultiplayer *core() const;
    int64_t index_of(NetwPredictSlotEngine *p_engine) const;
    const godot::LocalVector<NetwPredictSlotEngine *> &phase(int p_phase);
    void rebuild_sorted();
    bool resolve_pool_order();
    void step_stepped_spaces(const NetwPredictTiming &p_timing);

public:
    void seat_core(NetwMultiplayer *p_core);

    void register_engine(NetwPredictSlotEngine *p_engine);
    void unregister_engine(NetwPredictSlotEngine *p_engine);
    void tick_step(const NetwPredictTiming &p_timing);
    void frame_step(const NetwPredictTiming &p_timing);
    void before_frame_step();
    int64_t engine_count() const;
};

} // namespace netw

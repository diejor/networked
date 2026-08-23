#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/predict/engine.hpp"

namespace netw {

class NetwPredictStats : public godot::RefCounted {
    GDCLASS(NetwPredictStats, godot::RefCounted)

public:
    enum {
        ARRIVAL_BUCKETS = 9,
        REPLAY_DEPTH_BUCKETS = 33,
    };

    enum Source : uint8_t {
        SOURCE_HELD = 0,
        SOURCE_DRIVE = 1,
        SOURCE_COMPARE = 2,
        SOURCE_JOINT = 3,
        SOURCE_FLOOR_MOVES = 4,
        SOURCE_ARRIVALS = 5,
        SOURCE_REPLAY_DEPTH = 6,
        SOURCE_CONSUME_SHAPE = 7,
        SOURCE_JOINT_DEPTH = 8,
        SOURCE_ISLAND_MEMBERS = 9,
        SOURCE_SIMULATED_MEMBERS = 10,
    };

    struct Fact {
        const char *name;
        Source source;
        int index;
        int64_t fallback;
    };

private:
    godot::Ref<NetwPredictionEngine> pool;
    godot::Ref<godot::RefCounted> entity;

    godot::LocalVector<int64_t> held;
    godot::PackedInt32Array arrivals;
    godot::PackedInt32Array replay_depth;
    godot::Dictionary consume_shape;
    godot::Dictionary joint_depth;
    godot::PackedStringArray island_members;
    godot::PackedStringArray simulated_members;

    int64_t slot() const;
    int64_t column(Source source, int index, int64_t fallback) const;
    godot::Variant read(const Fact &fact) const;

protected:
    static void _bind_methods();

public:
    void set_int_fact(int index, int64_t value);
    int64_t get_int_fact(int index) const;
    void set_dict_fact(int index, const godot::Dictionary &value);
    godot::Dictionary get_dict_fact(int index) const;
    void set_buckets_fact(int index, const godot::PackedInt32Array &value);
    godot::PackedInt32Array get_buckets_fact(int index) const;
    void set_names_fact(int index, const godot::PackedStringArray &value);
    godot::PackedStringArray get_names_fact(int index) const;

    NetwPredictStats();

    void bind_slot(
        const godot::Ref<NetwPredictionEngine> &p_pool,
        const godot::Ref<godot::RefCounted> &p_entity
    );

    godot::Dictionary to_dictionary() const;

    static const Fact *facts();
    static int fact_count();
};

} // namespace netw

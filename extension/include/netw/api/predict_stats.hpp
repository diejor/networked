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

    enum FactAt {
        FACT_QUANTUM_STEPS = 2,
        FACT_CONSUMED = 7,
        FACT_MISSING = 8,
        FACT_STARVED = 9,
        FACT_HELD = 10,
        FACT_DRIVE_SEQ = 12,
        FACT_LAST_DRIVE_LABEL = 13,
        FACT_TAPE_EPOCH = 15,
        FACT_TAPE_INDEX = 16,
        FACT_TAPE_QUEUE_DEPTH = 17,
        FACT_RESYNC = 18,
        FACT_SKIPPED = 19,
        FACT_FRAMES_DROPPED_INVALID = 20,
        FACT_COMMAND_FRAMES_SENT = 21,
        FACT_COMMAND_FRAMES_RECEIVED = 22,
        FACT_COMMAND_QUEUE_DEPTH = 23,
        FACT_RELAYED_RECORDED = 24,
        FACT_RELAYED_DROPPED_LATE = 25,
        FACT_ACK_CONFIRMED = 26,
        FACT_ACK_FRONTIER = 27,
        FACT_JOURNAL_CLOSED = 28,
        FACT_SUBSTITUTED = 31,
        FACT_ARRIVALS = 32,
        FACT_REPLAY_DEPTH = 33,
        FACT_CONSUME_SHAPE = 34,
        FACT_CLIENT_FP_VERIFIED = 39,
        FACT_CLIENT_MISMATCHES = 40,
        FACT_JOINT_DEPTH = 44,
        FACT_LINGER_HELD = 52,
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
    NetwPredictionEngine *pool = nullptr;
    godot::Ref<NetwEntity> entity;

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
        NetwPredictionEngine *p_pool,
        const godot::Ref<NetwEntity> &p_entity
    );

    godot::Dictionary to_dictionary() const;

    static const Fact *facts();
    static int fact_count();
};

} // namespace netw

#include "netw/api/predict_stats.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

namespace {

const NetwPredictStats::Fact FACTS[] = {
    { "authoring_clamped", NetwPredictStats::SOURCE_DRIVE,
      NetwPredictionEngine::STAT_AUTHORING_CLAMPED, 0 },
    { "speculation_held", NetwPredictStats::SOURCE_DRIVE,
      NetwPredictionEngine::STAT_SPECULATION_HELD, 0 },
    { "quantum_steps", NetwPredictStats::SOURCE_DRIVE,
      NetwPredictionEngine::STAT_QUANTUM_STEPS, 0 },
    { "quantum_declared", NetwPredictStats::SOURCE_DRIVE,
      NetwPredictionEngine::STAT_QUANTUM_DECLARED, 1 },
    { "quantum_faults", NetwPredictStats::SOURCE_DRIVE,
      NetwPredictionEngine::STAT_QUANTUM_FAULTS, 0 },
    { "corrections", NetwPredictStats::SOURCE_HELD, 0, 0 },
    { "max_replay_depth", NetwPredictStats::SOURCE_HELD, 1, 0 },
    { "consumed", NetwPredictStats::SOURCE_HELD, 2, 0 },
    { "missing", NetwPredictStats::SOURCE_HELD, 3, 0 },
    { "starved", NetwPredictStats::SOURCE_HELD, 4, 0 },
    { "held", NetwPredictStats::SOURCE_HELD, 5, 0 },
    { "folded", NetwPredictStats::SOURCE_HELD, 6, 0 },
    { "drive_seq", NetwPredictStats::SOURCE_DRIVE,
      NetwPredictionEngine::STAT_DRIVE_SEQ, 0 },
    { "last_drive_label", NetwPredictStats::SOURCE_DRIVE,
      NetwPredictionEngine::STAT_LAST_DRIVE_LABEL, -1 },
    { "last_drive_kind", NetwPredictStats::SOURCE_DRIVE,
      NetwPredictionEngine::STAT_LAST_DRIVE_KIND, 0 },
    { "tape_epoch", NetwPredictStats::SOURCE_HELD, 7, -1 },
    { "tape_index", NetwPredictStats::SOURCE_HELD, 8, -1 },
    { "tape_queue_depth", NetwPredictStats::SOURCE_HELD, 9, 0 },
    { "resync", NetwPredictStats::SOURCE_HELD, 10, 0 },
    { "skipped", NetwPredictStats::SOURCE_HELD, 11, 0 },
    { "frames_dropped_invalid", NetwPredictStats::SOURCE_HELD, 12, 0 },
    { "command_frames_sent", NetwPredictStats::SOURCE_HELD, 13, 0 },
    { "command_frames_received", NetwPredictStats::SOURCE_HELD, 14, 0 },
    { "command_queue_depth", NetwPredictStats::SOURCE_HELD, 15, 0 },
    { "relayed_recorded", NetwPredictStats::SOURCE_HELD, 16, 0 },
    { "relayed_dropped_late", NetwPredictStats::SOURCE_HELD, 17, 0 },
    { "ack_confirmed", NetwPredictStats::SOURCE_HELD, 18, -1 },
    { "ack_frontier", NetwPredictStats::SOURCE_HELD, 19, -1 },
    { "journal_closed", NetwPredictStats::SOURCE_HELD, 20, -1 },
    { "comparisons_ran", NetwPredictStats::SOURCE_HELD, 21, 0 },
    { "comparisons_skipped", NetwPredictStats::SOURCE_HELD, 22, 0 },
    { "substituted", NetwPredictStats::SOURCE_HELD, 23, 0 },
    { "arrivals", NetwPredictStats::SOURCE_ARRIVALS, 0, 0 },
    { "replay_depth", NetwPredictStats::SOURCE_REPLAY_DEPTH, 0, 0 },
    { "consume_shape", NetwPredictStats::SOURCE_CONSUME_SHAPE, 0, 0 },
    { "fp_verified", NetwPredictStats::SOURCE_HELD, 24, 0 },
    { "fp_mismatches", NetwPredictStats::SOURCE_HELD, 25, 0 },
    { "first_divergent_transition", NetwPredictStats::SOURCE_HELD, 26, -1 },
    { "chain_breaks", NetwPredictStats::SOURCE_DRIVE,
      NetwPredictionEngine::STAT_CHAIN_BREAKS, 0 },
    { "client_fp_verified", NetwPredictStats::SOURCE_HELD, 27, 0 },
    { "client_mismatches", NetwPredictStats::SOURCE_HELD, 28, 0 },
    { "island_members", NetwPredictStats::SOURCE_ISLAND_MEMBERS, 0, 0 },
    { "simulated_members", NetwPredictStats::SOURCE_SIMULATED_MEMBERS, 0, 0 },
    { "joint_passes", NetwPredictStats::SOURCE_JOINT,
      NetwPredictionEngine::STAT_JOINT_PASSES, 0 },
    { "joint_depth", NetwPredictStats::SOURCE_JOINT_DEPTH, 0, 0 },
    { "joint_floor", NetwPredictStats::SOURCE_JOINT,
      NetwPredictionEngine::STAT_JOINT_FLOOR, -1 },
    { "joint_present", NetwPredictStats::SOURCE_JOINT,
      NetwPredictionEngine::STAT_JOINT_PRESENT, -1 },
    { "joint_members", NetwPredictStats::SOURCE_JOINT,
      NetwPredictionEngine::STAT_JOINT_MEMBERS, 0 },
    { "cells_relayed", NetwPredictStats::SOURCE_JOINT,
      NetwPredictionEngine::STAT_JOINT_CELLS_RELAYED, 0 },
    { "cells_substituted", NetwPredictStats::SOURCE_JOINT,
      NetwPredictionEngine::STAT_JOINT_CELLS_SUBSTITUTED, 0 },
    { "floor_moves_by_source", NetwPredictStats::SOURCE_FLOOR_MOVES, 0, 0 },
    { "heal_snaps", NetwPredictStats::SOURCE_JOINT,
      NetwPredictionEngine::STAT_JOINT_HEAL_SNAPS, 0 },
    { "linger_held", NetwPredictStats::SOURCE_HELD, 29, 0 },
};

constexpr int FACT_COUNT = int(sizeof(FACTS) / sizeof(FACTS[0]));
constexpr int HELD_COUNT = 30;

} // namespace

const NetwPredictStats::Fact *NetwPredictStats::facts() {
    return FACTS;
}

int NetwPredictStats::fact_count() {
    return FACT_COUNT;
}

NetwPredictStats::NetwPredictStats() {
    held.resize(HELD_COUNT);
    for (int at = 0; at < FACT_COUNT; ++at) {
        if (FACTS[at].source == SOURCE_HELD) {
            held[FACTS[at].index] = FACTS[at].fallback;
        }
    }
    arrivals.resize(ARRIVAL_BUCKETS);
    replay_depth.resize(REPLAY_DEPTH_BUCKETS);
}

void NetwPredictStats::bind_slot(
    const Ref<NetwPredictionEngine> &p_pool,
    const Ref<RefCounted> &p_entity
) {
    pool = p_pool;
    entity = p_entity;
}

int64_t NetwPredictStats::slot() const {
    if (pool.is_null() || entity.is_null()) {
        return -1;
    }
    return pool->slot_of(entity);
}

int64_t NetwPredictStats::column(
    Source source,
    int index,
    int64_t fallback
) const {
    const int64_t at = slot();
    if (at < 0) {
        return fallback;
    }
    PackedInt64Array columns;
    switch (source) {
        case SOURCE_DRIVE:
            columns = pool->drive_stats(at);
            break;
        case SOURCE_COMPARE:
            columns = pool->compare_stats(at);
            break;
        case SOURCE_JOINT:
        case SOURCE_FLOOR_MOVES:
            columns = pool->joint_stats(at);
            break;
        default:
            return fallback;
    }
    if (index >= columns.size()) {
        return fallback;
    }
    return columns[index];
}

Variant NetwPredictStats::read(const Fact &fact) const {
    switch (fact.source) {
        case SOURCE_HELD:
            return held[fact.index];
        case SOURCE_ARRIVALS:
            return arrivals;
        case SOURCE_REPLAY_DEPTH:
            return replay_depth;
        case SOURCE_CONSUME_SHAPE:
            return consume_shape;
        case SOURCE_JOINT_DEPTH:
            return joint_depth;
        case SOURCE_ISLAND_MEMBERS:
            return island_members;
        case SOURCE_SIMULATED_MEMBERS:
            return simulated_members;
        case SOURCE_FLOOR_MOVES: {
            Dictionary out;
            out["ack"] = column(
                SOURCE_FLOOR_MOVES,
                NetwPredictionEngine::STAT_JOINT_FLOOR_ACK_MOVES,
                0
            );
            out["state"] = column(
                SOURCE_FLOOR_MOVES,
                NetwPredictionEngine::STAT_JOINT_FLOOR_STATE_MOVES,
                0
            );
            out["relay"] = column(
                SOURCE_FLOOR_MOVES,
                NetwPredictionEngine::STAT_JOINT_FLOOR_RELAY_MOVES,
                0
            );
            out["epoch"] = column(
                SOURCE_FLOOR_MOVES,
                NetwPredictionEngine::STAT_JOINT_FLOOR_EPOCH_MOVES,
                0
            );
            return out;
        }
        default:
            return column(fact.source, fact.index, fact.fallback);
    }
}

int64_t NetwPredictStats::get_int_fact(int index) const {
    if (index < 0 || index >= FACT_COUNT) {
        return 0;
    }
    const Fact &fact = FACTS[index];
    if (fact.source == SOURCE_HELD) {
        return held[fact.index];
    }
    return column(fact.source, fact.index, fact.fallback);
}

void NetwPredictStats::set_int_fact(int index, int64_t value) {
    if (index < 0 || index >= FACT_COUNT) {
        return;
    }
    const Fact &fact = FACTS[index];
    if (fact.source == SOURCE_HELD) {
        held[fact.index] = value;
    }
}

Dictionary NetwPredictStats::get_dict_fact(int index) const {
    if (index < 0 || index >= FACT_COUNT) {
        return Dictionary();
    }
    return read(FACTS[index]);
}

void NetwPredictStats::set_dict_fact(int index, const Dictionary &value) {
    if (index < 0 || index >= FACT_COUNT) {
        return;
    }
    switch (FACTS[index].source) {
        case SOURCE_CONSUME_SHAPE:
            consume_shape = value;
            break;
        case SOURCE_JOINT_DEPTH:
            joint_depth = value;
            break;
        default:
            break;
    }
}

PackedInt32Array NetwPredictStats::get_buckets_fact(int index) const {
    if (index < 0 || index >= FACT_COUNT) {
        return PackedInt32Array();
    }
    return read(FACTS[index]);
}

void NetwPredictStats::set_buckets_fact(
    int index,
    const PackedInt32Array &value
) {
    if (index < 0 || index >= FACT_COUNT) {
        return;
    }
    switch (FACTS[index].source) {
        case SOURCE_ARRIVALS:
            arrivals = value;
            break;
        case SOURCE_REPLAY_DEPTH:
            replay_depth = value;
            break;
        default:
            break;
    }
}

PackedStringArray NetwPredictStats::get_names_fact(int index) const {
    if (index < 0 || index >= FACT_COUNT) {
        return PackedStringArray();
    }
    return read(FACTS[index]);
}

void NetwPredictStats::set_names_fact(
    int index,
    const PackedStringArray &value
) {
    if (index < 0 || index >= FACT_COUNT) {
        return;
    }
    switch (FACTS[index].source) {
        case SOURCE_ISLAND_MEMBERS:
            island_members = value;
            break;
        case SOURCE_SIMULATED_MEMBERS:
            simulated_members = value;
            break;
        default:
            break;
    }
}

Dictionary NetwPredictStats::to_dictionary() const {
    Dictionary out;
    for (int at = 0; at < FACT_COUNT; ++at) {
        out[StringName(FACTS[at].name)] = read(FACTS[at]);
    }
    return out;
}

void NetwPredictStats::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("bind_slot", "pool", "entity"),
        &NetwPredictStats::bind_slot
    );
    ClassDB::bind_method(
        D_METHOD("to_dictionary"),
        &NetwPredictStats::to_dictionary
    );
    ClassDB::bind_method(
        D_METHOD("set_int_fact", "index", "value"),
        &NetwPredictStats::set_int_fact
    );
    ClassDB::bind_method(
        D_METHOD("get_int_fact", "index"),
        &NetwPredictStats::get_int_fact
    );
    ClassDB::bind_method(
        D_METHOD("set_dict_fact", "index", "value"),
        &NetwPredictStats::set_dict_fact
    );
    ClassDB::bind_method(
        D_METHOD("get_dict_fact", "index"),
        &NetwPredictStats::get_dict_fact
    );
    ClassDB::bind_method(
        D_METHOD("set_buckets_fact", "index", "value"),
        &NetwPredictStats::set_buckets_fact
    );
    ClassDB::bind_method(
        D_METHOD("get_buckets_fact", "index"),
        &NetwPredictStats::get_buckets_fact
    );
    ClassDB::bind_method(
        D_METHOD("set_names_fact", "index", "value"),
        &NetwPredictStats::set_names_fact
    );
    ClassDB::bind_method(
        D_METHOD("get_names_fact", "index"),
        &NetwPredictStats::get_names_fact
    );
    for (int at = 0; at < FACT_COUNT; ++at) {
        Variant::Type type = Variant::INT;
        const char *setter = "set_int_fact";
        const char *getter = "get_int_fact";
        switch (FACTS[at].source) {
            case SOURCE_ARRIVALS:
            case SOURCE_REPLAY_DEPTH:
                type = Variant::PACKED_INT32_ARRAY;
                setter = "set_buckets_fact";
                getter = "get_buckets_fact";
                break;
            case SOURCE_CONSUME_SHAPE:
            case SOURCE_JOINT_DEPTH:
            case SOURCE_FLOOR_MOVES:
                type = Variant::DICTIONARY;
                setter = "set_dict_fact";
                getter = "get_dict_fact";
                break;
            case SOURCE_ISLAND_MEMBERS:
            case SOURCE_SIMULATED_MEMBERS:
                type = Variant::PACKED_STRING_ARRAY;
                setter = "set_names_fact";
                getter = "get_names_fact";
                break;
            default:
                break;
        }
        ClassDB::add_property(
            "NetwPredictStats",
            PropertyInfo(type, FACTS[at].name),
            setter,
            getter,
            at
        );
    }
    BIND_CONSTANT(ARRIVAL_BUCKETS);
    BIND_CONSTANT(REPLAY_DEPTH_BUCKETS);
}

} // namespace netw

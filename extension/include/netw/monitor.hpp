#pragma once

#include <cstdint>

#if defined(DEBUG_ENABLED)

#include "godot/local_vector.hpp"

namespace netw::monitor {

enum Monitor : uint8_t {
    PEERS,
    ROUND_TRIP,
    JITTER,
    LOSS,
    UPLOAD_BUDGET_USED,
    DISPLAY_DELAY,
    LATE_INPUTS,
    ENTITIES,
    AWAKE_ROWS,
    ROWS_WAITING,
    CORRECTIONS,
    REPLAYED_TICKS,
    MEMORY_REPLICATION,
    MEMORY_PREDICTION,
    MEMORY_DISPLAY,
    COUNT,
};

enum Kind : uint8_t {
    QUANTITY,
    MEMORY,
    TIME,
    PERCENTAGE,
};

struct Spec {
    const char *id;
    Kind kind;
};

extern const Spec SPECS[COUNT];

struct Upload {
    int64_t peer = 0;
    int64_t sent_bytes = 0;
    double budget_per_second = 0.0;
};

struct Sample {
    int64_t peers = 0;
    double round_trip = 0.0;
    double jitter = 0.0;
    double loss = 0.0;
    godot::LocalVector<Upload> uploads;
    double display_delay = 0.0;
    int64_t late_inputs = 0;
    int64_t entities = 0;
    int64_t awake_rows = 0;
    int64_t rows_waiting = 0;
    int64_t corrections = 0;
    int64_t replayed_ticks = 0;
    int64_t memory_replication = 0;
    int64_t memory_prediction = 0;
    int64_t memory_display = 0;
};

void read(
    const Sample &p_before,
    const Sample &p_now,
    double p_seconds,
    double r_values[COUNT]
);

void install();
void remove();

} // namespace netw::monitor

#endif

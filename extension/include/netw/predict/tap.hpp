#pragma once

#include <cstdint>

#include "godot/file_access.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwPredictionEngine;

namespace predict {

constexpr int TAP_AGGREGATE_PERIOD = 60;
constexpr int64_t TAP_FLUSH_INTERVAL_USEC = 1'000'000;

struct TapState {
    godot::Ref<godot::FileAccess> file;
    godot::Dictionary carried;
    godot::Dictionary pending;
    godot::String episode_stamp;
    int64_t epoch = -1;
    int64_t last_exported = -1;
    int64_t lines = 0;
};

godot::Dictionary tap_stats_delta(
    godot::Dictionary &r_carried,
    const godot::Dictionary &p_stats,
    int64_t p_lines
);

godot::String tap_episode_stamp(const godot::Dictionary &p_digest);

bool tap_row_settled(int p_flags);

godot::Dictionary tap_settlement(const godot::Dictionary &p_row);

class Tap {
    godot::String dir;
    godot::HashMap<int64_t, TapState> states;
    godot::HashMap<godot::String, int64_t> names;
    int64_t bytes_written = 0;
    int64_t lines_written = 0;
    int64_t drains = 0;
    int64_t drain_usec = 0;
    int64_t last_flush_usec = 0;

    TapState &state_for(int64_t p_slot, int64_t p_epoch);
    godot::Ref<godot::FileAccess> file_for(
        const godot::StringName &p_entity_id,
        int64_t p_slot
    );

public:
    static godot::String armed_dir();

    void open(const godot::String &p_dir);

    bool ready() const {
        return !dir.is_empty();
    }

    void drain(
        const godot::StringName &p_entity_id,
        NetwPredictionEngine &p_pool,
        int64_t p_slot,
        const godot::Dictionary &p_stats
    );
    void flush();
    godot::Dictionary cost() const;
    void close();
};

} // namespace predict

} // namespace netw

#pragma once

#include "netw_test.h"

#include <cstring>

#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw_test {

struct Lane {
    godot::StringName name;

    int lane_corrections = 0;
    int lane_consumed = 0;
    int lane_missing = 0;
    int lane_escalations = 0;
    int lane_joint_passes = 0;
    int lane_joint_members = 0;
    int lane_max_replay_depth = 0;
    int lane_journal_rows = 0;
    int lane_closed_rows = 0;
    int lane_chain_breaks = 0;
    int lane_held = 0;
    int lane_starved = 0;
    int lane_speculation_held = 0;
    int lane_resyncs = 0;
    int lane_skipped = 0;
    int lane_queue_depth = 0;
    int lane_authority_journal_rows = 0;
    int lane_buffer_declared = 0;
    int lane_drives = 0;
    int lane_frames = 0;
    int lane_authoring_clamped = 0;
    int lane_quantum_declared = 0;
    int lane_quantum_steps = 0;
    int lane_quantum_faults = 0;
    int lane_fresh_effects = 0;
    int lane_authority_fresh_effects = 0;
    int lane_fp_verified = 0;
    int lane_fp_mismatches = 0;
    int lane_first_divergence = -1;
    double lane_epsilon = 0.0;
    bool lane_sample_found = false;
    bool lane_peer_sample_found = false;
    bool lane_peer_asked = false;
    int lane_rewind_visits = 0;
    double lane_rewound_x = 0.0;
    double lane_restored_x = 0.0;
    double lane_sample_x = 0.0;
    double lane_position_x = 0.0;
    double lane_authority_position_x = 0.0;
    godot::Variant lane_position;
    godot::Vector<double> lane_divergence;

    int64_t digest() const {
        int64_t hash = 1469598103934665603LL;
        const int counters[] = {
            lane_corrections,
            lane_consumed,
            lane_missing,
            lane_escalations,
            lane_max_replay_depth,
            lane_journal_rows,
            lane_closed_rows,
            lane_chain_breaks,
            lane_held,
            lane_starved,
            lane_speculation_held,
            lane_resyncs,
            lane_skipped,
            lane_queue_depth,
            lane_authority_journal_rows,
            lane_drives,
            lane_authoring_clamped,
            lane_quantum_steps,
            lane_quantum_faults,
            lane_fresh_effects,
            lane_authority_fresh_effects,
            lane_fp_verified,
            lane_fp_mismatches,
            lane_first_divergence,
        };
        for (const int value : counters) {
            hash = fold(hash, int64_t(value));
        }
        hash = fold(hash, bits(lane_position_x));
        hash = fold(hash, bits(lane_authority_position_x));
        for (int index = 0; index < lane_divergence.size(); ++index) {
            hash = fold(hash, bits(lane_divergence[index]));
        }
        return hash;
    }

private:
    static int64_t fold(int64_t p_hash, int64_t p_value) {
        return (p_hash ^ p_value) * 1099511628211LL;
    }

    static int64_t bits(double p_value) {
        int64_t out = 0;
        std::memcpy(&out, &p_value, sizeof(out));
        return out;
    }

public:
    int corrections() const {
        return lane_corrections;
    }

    int consumed() const {
        return lane_consumed;
    }

    int missing() const {
        return lane_missing;
    }

    int escalations() const {
        return lane_escalations;
    }

    int joint_passes() const {
        return lane_joint_passes;
    }

    int joint_members() const {
        return lane_joint_members;
    }

    int max_replay_depth() const {
        return lane_max_replay_depth;
    }

    int journal_rows() const {
        return lane_journal_rows;
    }

    int closed_rows() const {
        return lane_closed_rows;
    }

    int chain_breaks() const {
        return lane_chain_breaks;
    }

    int held() const {
        return lane_held;
    }

    int speculation_held() const {
        return lane_speculation_held;
    }

    int starved() const {
        return lane_starved;
    }

    int resyncs() const {
        return lane_resyncs;
    }

    int skipped() const {
        return lane_skipped;
    }

    int queue_depth() const {
        return lane_queue_depth;
    }

    int authority_journal_rows() const {
        return lane_authority_journal_rows;
    }

    int buffer_declared() const {
        return lane_buffer_declared;
    }

    int drives() const {
        return lane_drives;
    }

    int frames() const {
        return lane_frames;
    }

    int authoring_clamped() const {
        return lane_authoring_clamped;
    }

    int quantum_declared() const {
        return lane_quantum_declared;
    }

    int quantum_steps() const {
        return lane_quantum_steps;
    }

    int quantum_faults() const {
        return lane_quantum_faults;
    }

    int fresh_effects() const {
        return lane_fresh_effects;
    }

    int authority_fresh_effects() const {
        return lane_authority_fresh_effects;
    }

    int fp_verified() const {
        return lane_fp_verified;
    }

    int fp_mismatches() const {
        return lane_fp_mismatches;
    }

    int first_divergence() const {
        return lane_first_divergence;
    }

    double epsilon() const {
        return lane_epsilon;
    }

    bool peer_asked() const {
        return lane_peer_asked;
    }

    bool peer_sample_found() const {
        return lane_peer_sample_found;
    }

    bool sample_found() const {
        return lane_sample_found;
    }

    double sample_x() const {
        return lane_sample_x;
    }

    int rewind_visits() const {
        return lane_rewind_visits;
    }

    double rewound_x() const {
        return lane_rewound_x;
    }

    double restored_x() const {
        return lane_restored_x;
    }

    double position_x() const {
        return lane_position_x;
    }

    double authority_position_x() const {
        return lane_authority_position_x;
    }

    double tail_divergence(int p_window) const {
        const int count = lane_divergence.size();
        const int from = p_window >= count ? 0 : count - p_window;
        double worst = 0.0;
        for (int index = from; index < count; ++index) {
            worst = worst > lane_divergence[index] ? worst
                                                   : lane_divergence[index];
        }
        return worst;
    }

    godot::Variant position() const {
        return lane_position;
    }
};

} // namespace netw_test

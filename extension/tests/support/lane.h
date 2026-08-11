#pragma once

/* What one predicted entity's run can be read for, after the driver ran it.
 *
 * A lane answers CONTRACT and never representation. A question with no reader
 * here is a port obligation on the engine rather than a licence to reach into
 * a driver's state.
 *
 * A lane is a value rather than a view, so reading one is answering a question
 * the run already settled.
 *
 * [codeblock]
 * const Lane p = run.lane("P");
 * NETW_CHECK_GE(p.corrections(), 1);
 * NETW_CHECK_LT(p.tail_divergence(5), p.epsilon());
 * [/codeblock]
 */

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
    int lane_rewind_visits = 0;
    double lane_rewound_x = 0.0;
    double lane_restored_x = 0.0;
    double lane_sample_x = 0.0;
    double lane_position_x = 0.0;
    double lane_authority_position_x = 0.0;
    godot::Variant lane_position;
    godot::Vector<double> lane_divergence;

    /* The whole of this lane's evidence, reduced to one number.
     *
     * Every counter and every position a law can read folds in, so two runs
     * agreeing on this agree on everything a law could have compared. Doubles
     * fold through their exact bit pattern rather than through a tolerance,
     * because the claim is reproduction and not convergence.
     */
    int64_t digest() const {
        int64_t hash = 1469598103934665603LL;
        const int counters[] = {
            lane_corrections,      lane_consumed,
            lane_missing,          lane_escalations,
            lane_max_replay_depth, lane_journal_rows,
            lane_closed_rows,      lane_chain_breaks,
            lane_held,             lane_starved,
            lane_speculation_held, lane_resyncs,
            lane_skipped,          lane_queue_depth,
            lane_authority_journal_rows,
            lane_drives,           lane_authoring_clamped,
            lane_quantum_steps,    lane_quantum_faults,
            lane_fresh_effects,    lane_authority_fresh_effects,
            lane_fp_verified,      lane_fp_mismatches,
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

    // The bit pattern rather than the value, so a NaN and a negative zero are
    // each themselves and no comparison is made on the way in.
    static int64_t bits(double p_value) {
        int64_t out = 0;
        std::memcpy(&out, &p_value, sizeof(out));
        return out;
    }

public:

    int corrections() const {
        return lane_corrections;
    }

    // Ticks driven from input the lane had not driven before, which is what a
    // predicting engine consumes rather than repeats.
    int consumed() const {
        return lane_consumed;
    }

    int missing() const {
        return lane_missing;
    }

    int escalations() const {
        return lane_escalations;
    }

    // Passes this lane's island owner ran. A group that reconciles jointly
    // replays its members together, so a declared JOINT island that never
    // passes is one the session refused rather than admitted.
    int joint_passes() const {
        return lane_joint_passes;
    }

    int joint_members() const {
        return lane_joint_members;
    }

    int max_replay_depth() const {
        return lane_max_replay_depth;
    }

    // What the engine retained about the run, as against what the driver
    // counted while producing it. A law that reads only the driver's tally is
    // a law the engine could fail silently.
    int journal_rows() const {
        return lane_journal_rows;
    }

    int closed_rows() const {
        return lane_closed_rows;
    }

    int chain_breaks() const {
        return lane_chain_breaks;
    }

    // Frames authority declined a transition it had, because the buffer it
    // keeps standing behind the live edge was not full.
    int held() const {
        return lane_held;
    }

    // Frames the owner refused to author because its speculative horizon was
    // full, which is a lane declining to run ahead rather than failing to.
    int speculation_held() const {
        return lane_speculation_held;
    }

    // Passes that found nothing at all to consume, as against a hold, which is
    // a pass declining a transition it had.
    int starved() const {
        return lane_starved;
    }

    // Times authority gave up on walking to the live edge and jumped to it.
    // A lane whose stream keeps up never does.
    int resyncs() const {
        return lane_resyncs;
    }

    // Transitions authority declared skipped as it jumped, which are the ones
    // it will never run and the owner must be told about.
    int skipped() const {
        return lane_skipped;
    }

    // Transitions standing in authority's tape when the run ended, which is
    // the queue a resync leaves behind rather than the one it cleared.
    int queue_depth() const {
        return lane_queue_depth;
    }

    // What authority retained, which for a consuming role is one row per
    // transition it actually ran.
    int authority_journal_rows() const {
        return lane_authority_journal_rows;
    }

    // The buffer the SCENARIO declared, never the one the engine installed. A
    // lane judged against its own setting agrees with itself by construction.
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

    // Transitions authority answered for, which is the denominator every
    // agreement question is asked out of. A lane that verified nothing agrees
    // with authority about nothing.
    int fp_verified() const {
        return lane_fp_verified;
    }

    int fp_mismatches() const {
        return lane_fp_mismatches;
    }

    // The oldest transition that disagreed, or -1 while the lane and authority
    // still close every transition the same way.
    int first_divergence() const {
        return lane_first_divergence;
    }

    // The tolerance the lane was judged at, so a law states its bound in the
    // scenario's units rather than in a literal.
    double epsilon() const {
        return lane_epsilon;
    }

    // Whether the lane's history answered at the tick the scenario named. An
    // entity with no timeline answers nothing, which is a fact rather than a
    // zero.
    bool sample_found() const {
        return lane_sample_found;
    }

    // Where the history says the lane was at that tick, against
    // `authority_position_x` for where it is now.
    double sample_x() const {
        return lane_sample_x;
    }

    // How many times a declared rewind ran its body. A rewind that never ran
    // it proves nothing about what it would have seen.
    int rewind_visits() const {
        return lane_rewind_visits;
    }

    // Where the body stood while the rewind held it, against `sample_x` for
    // where history said it was.
    double rewound_x() const {
        return lane_rewound_x;
    }

    // Where the body stood once the rewind returned, against `position_x` for
    // where it stood before.
    double restored_x() const {
        return lane_restored_x;
    }

    double position_x() const {
        return lane_position_x;
    }

    double authority_position_x() const {
        return lane_authority_position_x;
    }

    // A window wider than the run reads the whole run, and a run that judged
    // nothing has no tail. That is zero rather than an error, because
    // `ScenarioRun::decisions` is where a vacuous run is refused and a second
    // refusal here would only move the failure.
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

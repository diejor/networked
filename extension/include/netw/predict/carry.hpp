#pragma once

/* Carry-rule admission and retirement, indexed by the compiled field table.
 *
 * A game-authored callable stays at the binding boundary. The core receives
 * evidence about one invocation and owns the durable decision, so a bad rule
 * degrades to an exact acknowledged write and cannot keep retrying forever.
 */

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

namespace predict {

constexpr int CARRY_INFIDELITY_LIMIT = 8;

// A retirement is reported on the transition into it and never again, so a
// caller explaining one to the game needs no record of what it has said.
enum class CarryVerdict : int {
    CARRIED = 0,
    DECLINED = 1,
    UNFAITHFUL = 2,
    RETIRED_ALREADY = 3,
    RETIRED_SCHEDULE = 4,
    RETIRED_IMPURE = 5,
    RETIRED_INFIDELITY = 6,
};

struct CarryProbe {
    bool same_type = true;
    bool finite = true;
    bool within_envelope = true;
    bool pure = true;
    bool faithful = true;
};

/* What one invocation of a rule produced, and every probe it earned.
 *
 * `evidence` false is the case with nothing to judge: no transition the rule
 * could be replayed against, or a recorded state the book no longer holds. A
 * caller charges that as a refusal rather than handing the probes to a verdict
 * that would convict the rule of the absence.
 */
struct CarryAttempt {
    Variant value;
    CarryProbe probe;
    bool evidence = false;
    // The distance between what the rule reproduced and what the transition
    // actually reached, against the tolerance it was judged at. Both -1.0 when
    // no replay measured one, which is every path but an unfaithful one.
    double residual = -1.0;
    double tolerance = -1.0;
};

struct CarryFieldStats {
    int carried = 0;
    int declined = 0;
    int infidelity = 0;
    bool retired = false;
};

/* Which recorded states are something other than a drive result.
 *
 * A rule states what a DRIVE does, so a transition with either end marked is
 * one no rule may be judged across: the disagreement would convict the rule of
 * the engine's own correction. Bounded with the tape it indexes into, because a
 * mark for a transition the ring no longer holds can never be read again.
 */
class CarryDirty {
    LocalVector<int64_t> marks;
    int start = 0;

public:
    void clear();
    void mark(int64_t p_transition);
    bool holds(int64_t p_transition) const;
};

class CarryTrack {
    LocalVector<CarryFieldStats> fields;

public:
    void resize(int p_count);
    CarryVerdict judge(int p_field, int p_schedule, const CarryProbe &p_probe);

    // The refusal with no invocation to hand over. The schedule is the one
    // conviction reachable here, because a rule declared where nothing can
    // replay it is already wrong about itself.
    CarryVerdict decline(int p_field, int p_schedule);
    const CarryFieldStats *field(int p_field) const;
};

} // namespace predict

} // namespace netw

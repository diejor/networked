#pragma once

/* Carry-rule admission and retirement, indexed by the compiled field table.
 *
 * A game-authored callable stays at the binding boundary. The core receives
 * evidence about one invocation and owns the durable decision, so a bad rule
 * degrades to an exact acknowledged write and cannot keep retrying forever.
 */

#include "godot/local_vector.hpp"

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

struct CarryFieldStats {
    int carried = 0;
    int declined = 0;
    int infidelity = 0;
    bool retired = false;
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

#pragma once

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"

namespace netw {

namespace predict {

constexpr int CARRY_INFIDELITY_LIMIT = 8;

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

struct CarryAttempt {
    godot::Variant value;
    CarryProbe probe;
    bool evidence = false;
    double residual = -1.0;
    double tolerance = -1.0;
};

struct CarryFieldStats {
    int carried = 0;
    int declined = 0;
    int infidelity = 0;
    bool retired = false;
};

class CarryDirty {
    godot::LocalVector<int64_t> marks;
    int start = 0;

public:
    void clear();
    void mark(int64_t p_transition);
    bool holds(int64_t p_transition) const;
};

class CarryTrack {
    godot::LocalVector<CarryFieldStats> fields;

public:
    void resize(int p_count);
    CarryVerdict judge(int p_field, int p_schedule, const CarryProbe &p_probe);

    CarryVerdict decline(int p_field, int p_schedule);
    const CarryFieldStats *field(int p_field) const;
};

} // namespace predict

} // namespace netw

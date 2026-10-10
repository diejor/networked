#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"

namespace netw {

class GateVerdictBook {
public:
    static constexpr int ROWS = 6;

    enum Report {
        QUIET,
        WARN,
        DEFECT,
    };

private:
    int64_t totals[ROWS] = {0};
    godot::HashSet<int64_t> warned_routes;

public:
    static int row_of(int64_t verdict);

    static bool counts(int64_t verdict);

    static Report report_of(int64_t verdict);

    bool count(int64_t verdict);
    int64_t total(int64_t verdict) const;

    bool claim_warning(int64_t verdict, int64_t route);

    void clear();
};

} // namespace netw

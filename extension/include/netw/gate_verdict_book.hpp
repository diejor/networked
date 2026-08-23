#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"

namespace netw {

class GateVerdictBook {
public:
    static constexpr int ROWS = 6;

    // What a verdict is worth saying out loud, which is a different partition
    // from the one the book counts.
    //
    // [codeblock]
    // QUIET   DOES_NOT_EXIST  SKIP  UNAVAILABLE   a route this peer has not
    //                                             seen, one that is dead, one
    //                                             whose node is gone.  Every
    //                                             session in a live game hits
    //                                             these constantly
    // WARN    UNAUTHORIZED  INVALID_DATA  BUSY    a sender sent what it may
    //                                             not, so the operator is
    //                                             told once per route
    // DEFECT  everything else                     this session sank a value
    //                                             that is not a remote-input
    //                                             verdict at all
    // [/codeblock]
    enum Report {
        QUIET,
        WARN,
        DEFECT,
    };

private:
    int64_t totals[ROWS] = { 0 };
    godot::HashSet<int64_t> warned_routes;

public:
    // The row a verdict counts into, or -1 when it is outside the partition.
    static int row_of(int64_t verdict);

    // Whether the verdict is one this book counts at all. A caller that sinks
    // something outside the partition has a defect rather than a new row.
    static bool counts(int64_t verdict);

    // OK reports QUIET rather than DEFECT, because a gate that admitted a
    // frame answered nothing this book is about.
    static Report report_of(int64_t verdict);

    bool count(int64_t verdict);
    int64_t total(int64_t verdict) const;

    bool claim_warning(int64_t verdict, int64_t route);

    void clear();
};

} // namespace netw

#include "netw/gate_verdict_book.hpp"

using namespace godot;

namespace netw {

int GateVerdictBook::row_of(int64_t verdict) {
    switch (verdict) {
        case ERR_DOES_NOT_EXIST:
            return 0;
        case ERR_SKIP:
            return 1;
        case ERR_UNAVAILABLE:
            return 2;
        case ERR_UNAUTHORIZED:
            return 3;
        case ERR_INVALID_DATA:
            return 4;
        case ERR_BUSY:
            return 5;
        default:
            return -1;
    }
}

bool GateVerdictBook::counts(int64_t verdict) {
    return row_of(verdict) >= 0;
}

GateVerdictBook::Report GateVerdictBook::report_of(int64_t verdict) {
    switch (verdict) {
        case OK:
        case ERR_DOES_NOT_EXIST:
        case ERR_SKIP:
        case ERR_UNAVAILABLE:
            return QUIET;
        case ERR_UNAUTHORIZED:
        case ERR_INVALID_DATA:
        case ERR_BUSY:
            return WARN;
        default:
            return DEFECT;
    }
}

bool GateVerdictBook::count(int64_t verdict) {
    const int row = row_of(verdict);
    if (row < 0) {
        return false;
    }
    totals[row] += 1;
    return true;
}

int64_t GateVerdictBook::total(int64_t verdict) const {
    const int row = row_of(verdict);
    return row < 0 ? 0 : totals[row];
}

bool GateVerdictBook::claim_warning(int64_t verdict, int64_t route) {
    const int64_t pair = (verdict << 32) | (route & 0xFFFFFFFF);
    if (warned_routes.has(pair)) {
        return false;
    }
    warned_routes.insert(pair);
    return true;
}

void GateVerdictBook::clear() {
    for (int row = 0; row < ROWS; row++) {
        totals[row] = 0;
    }
    warned_routes.clear();
}

} // namespace netw

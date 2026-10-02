#include "netw/predict/tick_work.hpp"

#if defined(NETW_TESTS)

#include "netw/store/validate.hpp"

namespace netw::predict {

namespace {
int depth = 0;
} // namespace

TickWork &tick_work() {
    static TickWork held;
    return held;
}

TickScope::TickScope() {
    outermost = depth == 0;
    depth += 1;
    if (outermost) {
        allocated = store::allocations_seen();
        written = store::dictionary_writes_seen();
    }
}

TickScope::~TickScope() {
    depth -= 1;
    if (!outermost) {
        return;
    }
    TickWork &work = tick_work();
    work.scopes += 1;
    work.allocations += store::allocations_seen() - allocated;
    work.dictionary_writes += store::dictionary_writes_seen() - written;
}

} // namespace netw::predict

#endif

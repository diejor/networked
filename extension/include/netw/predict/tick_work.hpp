#pragma once

#include <cstdint>

namespace netw::predict {

#if defined(NETW_TESTS)

struct TickWork {
    int64_t scopes = 0;
    int64_t allocations = 0;
    int64_t dictionary_writes = 0;
};

TickWork &tick_work();

class TickScope {
    int64_t allocated = 0;
    int64_t written = 0;
    bool outermost = false;

public:
    TickScope();
    ~TickScope();
    TickScope(const TickScope &) = delete;
    TickScope &operator=(const TickScope &) = delete;
};

#define NETW_PREDICT_TICK_SCOPE() \
    const ::netw::predict::TickScope netw_predict_tick_scope

#else

#define NETW_PREDICT_TICK_SCOPE() ((void)0)

#endif

} // namespace netw::predict

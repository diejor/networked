#pragma once

#include <cstdint>
#include <string>

#include "godot/variant.hpp"

namespace netw::store {

struct Miss {
    int64_t route = 0;
    uint32_t slot = 0;
    int64_t tick = -1;
    godot::StringName column;
    const char *gate = "";
};

void configure();

int64_t validation_interval();

void report_miss(const Miss &p_miss);

#if defined(NETW_TESTS)

namespace validation_audit {

struct Ledger {
    int64_t checks = 0;
    int64_t faults = 0;
    int64_t planted = 0;
    std::string first;
};

Ledger &ledger();

} // namespace validation_audit

int64_t &allocations_seen();

class AllocationTally {
    int64_t &into;
    const int64_t from;

public:
    explicit AllocationTally(int64_t &r_into) :
            into(r_into), from(allocations_seen()) {
    }

    ~AllocationTally() {
        into += allocations_seen() - from;
    }
};

int64_t &dictionary_writes_seen();

int64_t &peer_reads_seen();

#endif

} // namespace netw::store

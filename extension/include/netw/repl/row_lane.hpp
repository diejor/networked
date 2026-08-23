#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/baseline_book.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

class RowLane {
    wire::WirePlan compiled;
    wire::BaselineBook baselines;
    godot::Ref<SchemaRecord> declaration;

public:
    static RowLane open(const godot::Ref<SchemaRecord> &p_schema);

    bool valid() const {
        return compiled.valid();
    }

    const wire::WirePlan &plan() const {
        return compiled;
    }

    bool gather(const godot::Array &p_values, wire::CodeRow &r_row) const;

    uint64_t mask_for(int p_peer, const wire::CodeRow &p_row);

    void stage(int p_peer, uint16_t p_seq, const wire::CodeRow &p_row);

    void acknowledge(int p_peer, uint16_t p_acked_seq);

    void retain(const godot::LocalVector<int> &p_recipients);

    void forget(int p_peer) {
        baselines.forget(p_peer);
    }

    bool knows(int p_peer) const {
        return baselines.has_baseline(p_peer);
    }

    uint32_t in_flight(int p_peer) const {
        return baselines.in_flight_count(p_peer);
    }
};

} // namespace netw::repl

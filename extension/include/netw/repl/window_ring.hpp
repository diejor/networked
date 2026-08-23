#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

struct WindowSample {
    int64_t tick = -1;
    wire::CodeRow row;
};

class WindowRing {
    godot::LocalVector<WindowSample> samples;
    godot::Ref<SchemaRecord> declaration;
    wire::WirePlan compiled;
    uint32_t depth = 1;
    int64_t confirmed = -1;

public:
    static WindowRing open(uint32_t p_depth);

    static WindowRing declare(
        const godot::Ref<SchemaRecord> &p_schema,
        uint32_t p_depth
    );

    bool valid() const {
        return compiled.valid();
    }

    const wire::WirePlan &plan() const {
        return compiled;
    }

    bool gather(const godot::Array &p_values, wire::CodeRow &r_row) const;

    uint32_t held() const {
        return samples.size();
    }

    int64_t floor_tick() const {
        return confirmed;
    }

    bool record(int64_t p_tick, const wire::CodeRow &p_row);

    void confirm(int64_t p_tick);

    godot::LocalVector<WindowSample> pending() const;
};

} // namespace netw::repl

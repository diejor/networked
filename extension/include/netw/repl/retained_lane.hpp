#pragma once

#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

using netw::table::SchemaRecord;

class RetainedLane {
    wire::WirePlan compiled;
    SchemaRecord declaration;

public:
    static RetainedLane open(const wire::WirePlan &p_plan);

    static RetainedLane declare(const SchemaRecord &p_schema);

    bool gather(const godot::Array &p_values, wire::CodeRow &r_row) const;

    bool valid() const {
        return compiled.valid();
    }

    const wire::WirePlan &plan() const {
        return compiled;
    }
};

} // namespace netw::repl

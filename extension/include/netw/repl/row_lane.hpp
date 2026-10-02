#pragma once

#include "netw/api/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

using netw::table::SchemaRecord;

class RowLane {
    wire::WirePlan compiled;

public:
    static RowLane open(const SchemaRecord &p_schema);

    bool valid() const {
        return compiled.valid();
    }

    const wire::WirePlan &plan() const {
        return compiled;
    }
};

} // namespace netw::repl

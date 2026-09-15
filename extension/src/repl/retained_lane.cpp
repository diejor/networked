#include "netw/repl/retained_lane.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/wire/value_row.hpp"

namespace netw::repl {

using namespace godot;

RetainedLane RetainedLane::open(const wire::WirePlan &p_plan) {
    RetainedLane lane;
    lane.compiled = p_plan;
    return lane;
}

RetainedLane RetainedLane::declare(const SchemaRecord &p_schema) {
    RetainedLane lane;
    lane.declaration = p_schema;
    lane.compiled = wire::WirePlan::compile(p_schema);
    return lane;
}

bool RetainedLane::gather(const Array &p_values, wire::CodeRow &r_row) const {
    NETW_ZONE_NC("Retained lane gather", colors::WIRE);
    return wire::gather_scalar_row(declaration, compiled, p_values, r_row);
}

} // namespace netw::repl

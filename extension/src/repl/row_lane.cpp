#include "netw/repl/row_lane.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/wire/value_row.hpp"

namespace netw::repl {

using namespace godot;

RowLane RowLane::open(const SchemaRecord &p_schema) {
    RowLane lane;
    lane.declaration = p_schema;
    lane.compiled = wire::WirePlan::compile(p_schema);
    return lane;
}

bool RowLane::gather(const Array &p_values, wire::CodeRow &r_row) const {
    NETW_ZONE_NC("Row lane gather", colors::WIRE);
    return wire::gather_scalar_row(declaration, compiled, p_values, r_row);
}

} // namespace netw::repl

#include "netw/repl/row_lane.hpp"

namespace netw::repl {

RowLane RowLane::open(const SchemaRecord &p_schema) {
    RowLane lane;
    lane.compiled = wire::WirePlan::compile(p_schema);
    return lane;
}

} // namespace netw::repl

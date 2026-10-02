#include "netw/repl/retained_lane.hpp"

namespace netw::repl {

RetainedLane RetainedLane::open(const wire::WirePlan &p_plan) {
    RetainedLane lane;
    lane.compiled = p_plan;
    return lane;
}

RetainedLane RetainedLane::declare(const SchemaRecord &p_schema) {
    RetainedLane lane;
    lane.compiled = wire::WirePlan::compile(p_schema);
    return lane;
}

} // namespace netw::repl

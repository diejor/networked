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

uint64_t RowLane::mask_for(int p_peer, const wire::CodeRow &p_row) {
    return baselines.mask_to_send(p_peer, compiled, p_row);
}

void RowLane::stage(int p_peer, uint16_t p_seq, const wire::CodeRow &p_row) {
    baselines.stage(p_peer, p_seq, p_row);
}

void RowLane::acknowledge(
    int p_peer,
    uint16_t p_acked_seq,
    uint32_t p_history
) {
    baselines.acknowledge(p_peer, p_acked_seq, p_history);
}

void RowLane::retain(const LocalVector<int> &p_recipients) {
    baselines.retain(p_recipients);
}

} // namespace netw::repl

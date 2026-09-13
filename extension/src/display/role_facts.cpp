#include "netw/display/role_facts.hpp"

#include "netw/display/decl.hpp"

namespace netw::display {

int resolve_role_facts(const RoleFacts &p_facts) {
    if (p_facts.simulates_locally
        && (p_facts.controlled_locally || p_facts.predicted_input)) {
        return netw::display::ROLE_PREDICTED;
    }
    if (p_facts.prediction_registered) {
        if (p_facts.simulates_locally) {
            return netw::display::ROLE_AUTHORITY;
        }
        if (!p_facts.authors_streams) {
            return netw::display::ROLE_REMOTE;
        }
    }
    if (p_facts.owner_is_authority && p_facts.controlled_locally) {
        return netw::display::ROLE_DISABLED;
    }
    if (p_facts.authors_streams) {
        return netw::display::ROLE_AUTHORITY;
    }
    if (p_facts.owner_is_authority) {
        return netw::display::ROLE_DISABLED;
    }
    return netw::display::ROLE_REMOTE;
}

} // namespace netw::display

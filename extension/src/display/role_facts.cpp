#include "netw/display/role_facts.hpp"

#include "netw/display/decl.hpp"

namespace netw::display {

int resolve_role_facts(const RoleFacts &p_facts) {
    if (p_facts.simulates_locally && p_facts.controlled_locally) {
        return netw::display::ROLE_PREDICTED;
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

int role_for_mode(
    sim::Mode p_mode,
    bool p_predicted,
    bool p_controlled_locally
) {
    switch (p_mode) {
        case sim::Mode::PREDICT:
        case sim::Mode::ACTIVE:
            return netw::display::ROLE_PREDICTED;
        case sim::Mode::PROXY:
            return p_predicted ? netw::display::ROLE_REMOTE
                               : netw::display::ROLE_AUTO;
        case sim::Mode::AUTHORITY:
            if (!p_predicted) {
                return netw::display::ROLE_AUTO;
            }
            return p_controlled_locally ? netw::display::ROLE_PREDICTED
                                        : netw::display::ROLE_AUTHORITY;
        case sim::Mode::NONE:
            break;
    }
    return netw::display::ROLE_AUTO;
}

} // namespace netw::display

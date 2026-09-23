#pragma once

#include "netw/sim/resolve.hpp"

namespace netw::display {

struct RoleFacts {
    bool simulates_locally = false;
    bool controlled_locally = false;
    bool authors_streams = false;
    bool owner_is_authority = false;
};

int resolve_role_facts(const RoleFacts &p_facts);

int role_for_mode(
    sim::Mode p_mode,
    bool p_predicted,
    bool p_controlled_locally
);

} // namespace netw::display

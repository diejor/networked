#pragma once

namespace netw::display {

struct RoleFacts {
    bool simulates_locally = false;
    bool controlled_locally = false;
    bool predicted_input = false;
    bool prediction_registered = false;
    bool authors_streams = false;
    bool owner_is_authority = false;
};

int resolve_role_facts(const RoleFacts &p_facts);

} // namespace netw::display

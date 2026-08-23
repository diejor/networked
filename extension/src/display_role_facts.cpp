#include "netw/display_role_facts.hpp"

#include "godot/class_db.hpp"
#include "netw/api/display_decl.hpp"

using namespace godot;

namespace netw {

int NetwDisplayRoleFacts::resolve() const {
    if (simulates_locally && (controlled_locally || predicted_input)) {
        return NetwDisplayDecl::ROLE_PREDICTED;
    }
    if (prediction_registered) {
        if (simulates_locally) {
            return NetwDisplayDecl::ROLE_AUTHORITY;
        }
        if (!authors_streams) {
            return NetwDisplayDecl::ROLE_REMOTE;
        }
    }
    if (owner_is_authority && controlled_locally) {
        return NetwDisplayDecl::ROLE_DISABLED;
    }
    if (authors_streams) {
        return NetwDisplayDecl::ROLE_AUTHORITY;
    }
    if (owner_is_authority) {
        return NetwDisplayDecl::ROLE_DISABLED;
    }
    return NetwDisplayDecl::ROLE_REMOTE;
}

void NetwDisplayRoleFacts::_bind_methods() {
#define NETW_ROLE_FACT(m_name) \
    ClassDB::bind_method( \
        D_METHOD("set_" #m_name, "value"), \
        &NetwDisplayRoleFacts::set_##m_name \
    ); \
    ClassDB::bind_method( \
        D_METHOD("get_" #m_name), \
        &NetwDisplayRoleFacts::get_##m_name \
    ); \
    ADD_PROPERTY( \
        PropertyInfo(Variant::BOOL, #m_name), \
        "set_" #m_name, \
        "get_" #m_name \
    )

    ClassDB::bind_method(D_METHOD("resolve"), &NetwDisplayRoleFacts::resolve);

    NETW_ROLE_FACT(simulates_locally);
    NETW_ROLE_FACT(controlled_locally);
    NETW_ROLE_FACT(predicted_input);
    NETW_ROLE_FACT(prediction_registered);
    NETW_ROLE_FACT(authors_streams);
    NETW_ROLE_FACT(owner_is_authority);

#undef NETW_ROLE_FACT
}

} // namespace netw

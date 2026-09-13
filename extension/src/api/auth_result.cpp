#include "netw/api/auth_result.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

Ref<AuthResult> AuthResult::accept(const Ref<NetwIdentity> &p_identity) {
    Ref<AuthResult> made;
    made.instantiate();
    made->accepted = true;
    made->identity = p_identity;
    return made;
}

Ref<AuthResult> AuthResult::reject(const String &p_reason) {
    Ref<AuthResult> made;
    made.instantiate();
    made->accepted = false;
    made->rejection_reason = p_reason;
    return made;
}

void AuthResult::_bind_methods() {
    ClassDB::bind_static_method(
        "AuthResult",
        D_METHOD("accept", "identity"),
        &AuthResult::accept
    );
    ClassDB::bind_static_method(
        "AuthResult",
        D_METHOD("reject", "reason"),
        &AuthResult::reject
    );

    ClassDB::bind_method(D_METHOD("get_accepted"), &AuthResult::get_accepted);
    ClassDB::bind_method(
        D_METHOD("set_accepted", "accepted"),
        &AuthResult::set_accepted
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "accepted"),
        "set_accepted",
        "get_accepted"
    );

    ClassDB::bind_method(D_METHOD("get_identity"), &AuthResult::get_identity);
    ClassDB::bind_method(
        D_METHOD("set_identity", "identity"),
        &AuthResult::set_identity
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "identity",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwIdentity"
        ),
        "set_identity",
        "get_identity"
    );

    ClassDB::bind_method(
        D_METHOD("get_rejection_reason"),
        &AuthResult::get_rejection_reason
    );
    ClassDB::bind_method(
        D_METHOD("set_rejection_reason", "reason"),
        &AuthResult::set_rejection_reason
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "rejection_reason"),
        "set_rejection_reason",
        "get_rejection_reason"
    );
}

} // namespace netw

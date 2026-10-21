#include "netw/api/join_request.hpp"

#include "godot/utility.hpp"
#include "godot/class_db.hpp"
#include "netw/log.hpp"
#include "netw/session/frames.hpp"

using namespace godot;

namespace netw {

void NetwJoinRequest::bind_to(
    int64_t p_peer_id,
    const StringName &p_username,
    const Array &p_args,
    const String &p_challenge,
    bool p_challenge_bound,
    const Variant &p_proof,
    const PackedStringArray &p_identities,
    const String &p_account_name
) {
    peer_id = p_peer_id;
    username = p_username;
    args = p_args.duplicate(true);
    challenge = p_challenge;
    challenge_bound = p_challenge_bound;
    proof = p_proof;
    identities = p_identities;
    account = identities.is_empty() ? String() : identities[0];
    account_name = account.is_empty() ? String() : p_account_name;
}

void NetwJoinRequest::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_peer_id"), &NetwJoinRequest::get_peer_id
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "peer_id"), "", "get_peer_id");
    ClassDB::bind_method(
        D_METHOD("get_username"), &NetwJoinRequest::get_username
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "username"), "", "get_username"
    );
    ClassDB::bind_method(D_METHOD("get_args"), &NetwJoinRequest::get_args);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "args"), "", "get_args");
    ClassDB::bind_method(
        D_METHOD("get_challenge"), &NetwJoinRequest::get_challenge
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "challenge"), "", "get_challenge"
    );
    ClassDB::bind_method(
        D_METHOD("get_challenge_bound"), &NetwJoinRequest::get_challenge_bound
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "challenge_bound"), "",
        "get_challenge_bound"
    );
    ClassDB::bind_method(D_METHOD("get_proof"), &NetwJoinRequest::get_proof);
    ADD_PROPERTY(
        PropertyInfo(Variant::NIL, "proof", PROPERTY_HINT_NONE, "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT),
        "", "get_proof"
    );
    ClassDB::bind_method(
        D_METHOD("get_identities"), &NetwJoinRequest::get_identities
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::PACKED_STRING_ARRAY, "identities"),
        "", "get_identities"
    );
    ClassDB::bind_method(
        D_METHOD("set_account", "account"), &NetwJoinRequest::set_account
    );
    ClassDB::bind_method(D_METHOD("get_account"), &NetwJoinRequest::get_account);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "account"), "set_account", "get_account"
    );
    ClassDB::bind_method(
        D_METHOD("set_account_name", "name"),
        &NetwJoinRequest::set_account_name
    );
    ClassDB::bind_method(
        D_METHOD("get_account_name"), &NetwJoinRequest::get_account_name
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "account_name"),
        "set_account_name", "get_account_name"
    );
}

PackedByteArray JoinRequest::serialize() const {
    JoinFrame frame;
    frame.username = username;
    frame.args = arg_bytes;
    frame.proof = proof_bytes;
    frame.schema_hash = schema_hash;
    frame.peer_id = peer_id;
    frame.app_tag = app_tag;
    frame.wire_identity = wire_identity;
    frame.schema_identity = schema_identity;
    return session::frame_write(frame);
}

bool JoinRequest::deserialize(const PackedByteArray &p_bytes) {
    JoinFrame frame;
    if (!session::frame_read(p_bytes, frame)) {
        NETW_TRACE(
            sys::SESSION,
            "A join request that did not decode fully was refused."
        );
        return false;
    }
    username = frame.username;
    peer_id = frame.peer_id;
    arg_bytes = frame.args;
    proof_bytes = frame.proof;
    schema_hash = frame.schema_hash;
    app_tag = frame.app_tag;
    wire_identity = frame.wire_identity;
    schema_identity = frame.schema_identity;
    return true;
}

} // namespace netw

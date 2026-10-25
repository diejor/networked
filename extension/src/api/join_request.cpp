#include "netw/api/join_request.hpp"

#include "godot/utility.hpp"
#include "godot/class_db.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"
#include "netw/session/frames.hpp"

using namespace godot;

namespace netw {

void NetwJoinRequest::bind_to(
    int64_t p_peer_id,
    const StringName &p_username,
    const Array &p_args,
    const String &p_challenge,
    bool p_challenge_bound
) {
    peer_id = p_peer_id;
    username = p_username;
    args = p_args.duplicate(true);
    challenge = p_challenge;
    challenge_bound = p_challenge_bound;
    proven.clear();
    account = String();
    account_name = String();
}

void NetwJoinRequest::bind_session(NetwMultiplayer *p_core) {
    core_id = gd::instance_id(p_core);
}

Ref<NetwSession> NetwJoinRequest::get_session() const {
    NetwMultiplayer *core
        = Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
    return core != nullptr ? core->get_session() : Ref<NetwSession>();
}

const NetwJoinRequest::Proven *NetwJoinRequest::find(
    const String &p_account
) const {
    for (const Proven &row : proven) {
        if (row.account == p_account) {
            return &row;
        }
    }
    return nullptr;
}

void NetwJoinRequest::merge(
    const String &p_account,
    const String &p_name,
    bool p_bound
) {
    NETW_ERR_COND(
        p_account.is_empty(),
        sys::SESSION,
        "NetwJoinRequest.add_account got an empty account. Pass an id with "
        "a prefix, such as \"club:ana\"."
    );
    for (Proven &row : proven) {
        if (row.account != p_account) {
            continue;
        }
        if (row.name.is_empty()) {
            row.name = p_name;
        }
        row.bound = row.bound || p_bound;
        return;
    }
    proven.push_back(Proven{p_account, p_name, p_bound});
}

void NetwJoinRequest::add_account(
    const String &p_account,
    const String &p_name,
    bool p_answers_challenge
) {
    merge(p_account, p_name, p_answers_challenge && challenge_bound);
}

void NetwJoinRequest::add_vouched_account(
    const String &p_account,
    const String &p_name
) {
    merge(p_account, p_name, true);
}

void NetwJoinRequest::start_from_first_account() {
    account = proven.is_empty() ? String() : proven[0].account;
    account_name = proven.is_empty() ? String() : proven[0].name;
}

String NetwJoinRequest::name_of(const String &p_account) const {
    const Proven *row = find(p_account);
    return row != nullptr ? row->name : String();
}

bool NetwJoinRequest::is_bound(const String &p_account) const {
    const Proven *row = find(p_account);
    return row != nullptr && row->bound;
}

PackedStringArray NetwJoinRequest::get_accounts() const {
    PackedStringArray out;
    for (const Proven &row : proven) {
        out.push_back(row.account);
    }
    return out;
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
    ClassDB::bind_method(
        D_METHOD("get_session"), &NetwJoinRequest::get_session
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "session",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSession",
            PROPERTY_USAGE_NONE
        ),
        "", "get_session"
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
        D_METHOD("get_accounts"), &NetwJoinRequest::get_accounts
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::PACKED_STRING_ARRAY, "accounts"),
        "", "get_accounts"
    );
    ClassDB::bind_method(
        D_METHOD("add_account", "account", "name", "answers_challenge"),
        &NetwJoinRequest::add_account,
        DEFVAL(true)
    );
    ClassDB::bind_method(
        D_METHOD("name_of", "account"), &NetwJoinRequest::name_of
    );
    ClassDB::bind_method(
        D_METHOD("is_bound", "account"), &NetwJoinRequest::is_bound
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

#include "netw/join_payload.hpp"

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

const StringName &key_username() {
    static const StringName name("username");
    return name;
}

const StringName &key_arg_bytes() {
    static const StringName name("arg_bytes");
    return name;
}

const StringName &key_schema_hash() {
    static const StringName name("schema_hash");
    return name;
}

const StringName &key_peer_id() {
    static const StringName name("peer_id");
    return name;
}

const StringName &key_is_debug() {
    static const StringName name("is_debug");
    return name;
}

} // namespace

StringName JoinPayload::get_username() const {
    return username;
}

void JoinPayload::set_username(const StringName &p_value) {
    username = p_value;
}

Array JoinPayload::get_arg_values() const {
    return arg_values;
}

void JoinPayload::set_arg_values(const Array &p_value) {
    arg_values = p_value;
}

PackedByteArray JoinPayload::get_arg_bytes() const {
    return arg_bytes;
}

void JoinPayload::set_arg_bytes(const PackedByteArray &p_value) {
    arg_bytes = p_value;
}

int64_t JoinPayload::get_schema_hash() const {
    return schema_hash;
}

void JoinPayload::set_schema_hash(int64_t p_value) {
    schema_hash = p_value;
}

int64_t JoinPayload::get_peer_id() const {
    return peer_id;
}

void JoinPayload::set_peer_id(int64_t p_value) {
    peer_id = p_value;
}

bool JoinPayload::get_is_debug() const {
    return is_debug;
}

void JoinPayload::set_is_debug(bool p_value) {
    is_debug = p_value;
}

Ref<ResolvedJoin> JoinPayload::resolve() const {
    if (String(username).is_empty()) {
        return Ref<ResolvedJoin>();
    }
    Ref<ResolvedJoin> out;
    out.instantiate();
    out->set_peer_id(peer_id);
    out->set_username(username);
    out->set_is_debug(is_debug);
    out->set_arg_values(arg_values.duplicate(true));
    return out;
}

PackedByteArray JoinPayload::serialize() const {
    Dictionary out;
    // StringName keys, matching the frame the GDScript form wrote, and
    // arg_values is absent because the encoded arg_bytes is what travels.
    out[key_username()] = username;
    out[key_arg_bytes()] = arg_bytes;
    out[key_schema_hash()] = schema_hash;
    out[key_peer_id()] = peer_id;
    out[key_is_debug()] = is_debug;
    return gd::var_to_bytes(out);
}

bool JoinPayload::deserialize(const PackedByteArray &p_bytes) {
    const Variant decoded = gd::bytes_to_var(p_bytes);
    if (decoded.get_type() != Variant::DICTIONARY) {
        NETW_TRACE(sys::SESSION, "a join request that is not a record is refused");
        return false;
    }
    const Dictionary data = decoded;
    if (!data.has(key_username()) || !data.has(key_peer_id())) {
        NETW_TRACE(sys::SESSION, "a join request missing its named fields is refused");
        return false;
    }
    if (Variant(data[key_peer_id()]).get_type() != Variant::INT) {
        NETW_TRACE(sys::SESSION, "a join request whose peer is not a number is refused");
        return false;
    }
    username = StringName(String(data[key_username()]));
    peer_id = data[key_peer_id()];
    const Variant bytes = data.get(key_arg_bytes(), PackedByteArray());
    arg_bytes = bytes.get_type() == Variant::PACKED_BYTE_ARRAY
        ? PackedByteArray(bytes)
        : PackedByteArray();
    const Variant hash = data.get(key_schema_hash(), 0);
    schema_hash = hash.get_type() == Variant::INT ? int64_t(hash) : 0;
    const Variant debug = data.get(key_is_debug(), false);
    is_debug = debug.get_type() == Variant::BOOL ? bool(debug) : false;
    return true;
}

void JoinPayload::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_username"), &JoinPayload::get_username);
    ClassDB::bind_method(
        D_METHOD("set_username", "value"),
        &JoinPayload::set_username
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "username"),
        "set_username",
        "get_username"
    );

    ClassDB::bind_method(
        D_METHOD("get_arg_values"),
        &JoinPayload::get_arg_values
    );
    ClassDB::bind_method(
        D_METHOD("set_arg_values", "value"),
        &JoinPayload::set_arg_values
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "arg_values"),
        "set_arg_values",
        "get_arg_values"
    );

    ClassDB::bind_method(
        D_METHOD("get_arg_bytes"),
        &JoinPayload::get_arg_bytes
    );
    ClassDB::bind_method(
        D_METHOD("set_arg_bytes", "value"),
        &JoinPayload::set_arg_bytes
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::PACKED_BYTE_ARRAY, "arg_bytes"),
        "set_arg_bytes",
        "get_arg_bytes"
    );

    ClassDB::bind_method(
        D_METHOD("get_schema_hash"),
        &JoinPayload::get_schema_hash
    );
    ClassDB::bind_method(
        D_METHOD("set_schema_hash", "value"),
        &JoinPayload::set_schema_hash
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "schema_hash"),
        "set_schema_hash",
        "get_schema_hash"
    );

    ClassDB::bind_method(D_METHOD("get_peer_id"), &JoinPayload::get_peer_id);
    ClassDB::bind_method(
        D_METHOD("set_peer_id", "value"),
        &JoinPayload::set_peer_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "peer_id"),
        "set_peer_id",
        "get_peer_id"
    );

    ClassDB::bind_method(D_METHOD("get_is_debug"), &JoinPayload::get_is_debug);
    ClassDB::bind_method(
        D_METHOD("set_is_debug", "value"),
        &JoinPayload::set_is_debug
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_debug"),
        "set_is_debug",
        "get_is_debug"
    );

    ClassDB::bind_method(D_METHOD("resolve"), &JoinPayload::resolve);
    ClassDB::bind_method(D_METHOD("serialize"), &JoinPayload::serialize);
    ClassDB::bind_method(
        D_METHOD("deserialize", "bytes"),
        &JoinPayload::deserialize
    );
}

} // namespace netw

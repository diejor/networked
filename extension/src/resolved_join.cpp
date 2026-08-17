#include "netw/resolved_join.hpp"

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

const StringName &key_peer_id() {
    static const StringName name("peer_id");
    return name;
}

const StringName &key_username() {
    static const StringName name("username");
    return name;
}

const StringName &key_arg_values() {
    static const StringName name("arg_values");
    return name;
}

const StringName &key_is_debug() {
    static const StringName name("is_debug");
    return name;
}

} // namespace

int64_t ResolvedJoin::get_peer_id() const {
    return peer_id;
}

void ResolvedJoin::set_peer_id(int64_t p_value) {
    peer_id = p_value;
}

StringName ResolvedJoin::get_username() const {
    return username;
}

void ResolvedJoin::set_username(const StringName &p_value) {
    username = p_value;
}

Array ResolvedJoin::get_arg_values() const {
    return arg_values;
}

void ResolvedJoin::set_arg_values(const Array &p_value) {
    arg_values = p_value;
}

bool ResolvedJoin::get_is_debug() const {
    return is_debug;
}

void ResolvedJoin::set_is_debug(bool p_value) {
    is_debug = p_value;
}

PackedByteArray ResolvedJoin::serialize() const {
    Dictionary out;
    // StringName keys, because that is what the GDScript form wrote and the
    // frame is read by peers that may still be running it.
    out[key_peer_id()] = peer_id;
    out[key_username()] = username;
    out[key_arg_values()] = arg_values;
    out[key_is_debug()] = is_debug;
    return gd::var_to_bytes(out);
}

Ref<ResolvedJoin> ResolvedJoin::deserialize(const PackedByteArray &p_bytes) {
    const Variant decoded = gd::bytes_to_var(p_bytes);
    if (decoded.get_type() != Variant::DICTIONARY) {
        NETW_TRACE(sys::SESSION, "a join payload that is not a record is refused");
        return Ref<ResolvedJoin>();
    }
    const Dictionary data = decoded;
    if (!data.has(key_peer_id()) || !data.has(key_username())) {
        NETW_TRACE(sys::SESSION, "a join payload naming no peer is refused");
        return Ref<ResolvedJoin>();
    }
    if (Variant(data[key_peer_id()]).get_type() != Variant::INT) {
        NETW_TRACE(sys::SESSION, "a join payload whose peer is not a number is refused");
        return Ref<ResolvedJoin>();
    }
    Ref<ResolvedJoin> out;
    out.instantiate();
    out->peer_id = data[key_peer_id()];
    out->username = StringName(String(data[key_username()]));
    const Variant args = data.get(key_arg_values(), Array());
    out->arg_values = args.get_type() == Variant::ARRAY ? Array(args) : Array();
    const Variant debug = data.get(key_is_debug(), false);
    out->is_debug = debug.get_type() == Variant::BOOL ? bool(debug) : false;
    return out;
}

void ResolvedJoin::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_peer_id"),
        &ResolvedJoin::get_peer_id
    );
    ClassDB::bind_method(
        D_METHOD("set_peer_id", "value"),
        &ResolvedJoin::set_peer_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "peer_id"),
        "set_peer_id",
        "get_peer_id"
    );

    ClassDB::bind_method(
        D_METHOD("get_username"),
        &ResolvedJoin::get_username
    );
    ClassDB::bind_method(
        D_METHOD("set_username", "value"),
        &ResolvedJoin::set_username
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "username"),
        "set_username",
        "get_username"
    );

    ClassDB::bind_method(
        D_METHOD("get_arg_values"),
        &ResolvedJoin::get_arg_values
    );
    ClassDB::bind_method(
        D_METHOD("set_arg_values", "value"),
        &ResolvedJoin::set_arg_values
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "arg_values"),
        "set_arg_values",
        "get_arg_values"
    );

    ClassDB::bind_method(
        D_METHOD("get_is_debug"),
        &ResolvedJoin::get_is_debug
    );
    ClassDB::bind_method(
        D_METHOD("set_is_debug", "value"),
        &ResolvedJoin::set_is_debug
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_debug"),
        "set_is_debug",
        "get_is_debug"
    );

    ClassDB::bind_method(D_METHOD("serialize"), &ResolvedJoin::serialize);
    ClassDB::bind_static_method(
        "ResolvedJoin",
        D_METHOD("deserialize", "bytes"),
        &ResolvedJoin::deserialize
    );
}

} // namespace netw

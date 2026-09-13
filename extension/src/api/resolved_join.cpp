#include "netw/api/resolved_join.hpp"

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/log.hpp"
#include "netw/session/frames.hpp"

using namespace godot;

namespace netw {

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

AcceptFrame ResolvedJoin::accept_frame() const {
    AcceptFrame frame;
    frame.peer_id = peer_id;
    frame.username = username;
    frame.values = gd::var_to_bytes(arg_values);
    return frame;
}

Ref<ResolvedJoin> ResolvedJoin::of_frame(const AcceptFrame &p_frame) {
    Ref<ResolvedJoin> out;
    out.instantiate();
    out->peer_id = p_frame.peer_id;
    out->username = p_frame.username;
    const Variant values = gd::bytes_to_var(p_frame.values);
    out->arg_values
        = values.get_type() == Variant::ARRAY ? Array(values) : Array();
    return out;
}

PackedByteArray ResolvedJoin::serialize() const {
    return session::frame_write(accept_frame());
}

Ref<ResolvedJoin> ResolvedJoin::deserialize(const PackedByteArray &p_bytes) {
    AcceptFrame frame;
    if (!session::frame_read(p_bytes, frame)) {
        NETW_TRACE(
            sys::SESSION,
            "a join payload that did not decode whole is refused"
        );
        return Ref<ResolvedJoin>();
    }
    return of_frame(frame);
}

void ResolvedJoin::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_peer_id"), &ResolvedJoin::get_peer_id);
    ClassDB::bind_method(
        D_METHOD("set_peer_id", "value"),
        &ResolvedJoin::set_peer_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "peer_id"),
        "set_peer_id",
        "get_peer_id"
    );

    ClassDB::bind_method(D_METHOD("get_username"), &ResolvedJoin::get_username);
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

    ClassDB::bind_method(D_METHOD("serialize"), &ResolvedJoin::serialize);
    ClassDB::bind_static_method(
        "ResolvedJoin",
        D_METHOD("deserialize", "bytes"),
        &ResolvedJoin::deserialize
    );
}

} // namespace netw

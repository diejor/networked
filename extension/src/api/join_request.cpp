#include "netw/api/join_request.hpp"

#include "godot/utility.hpp"
#include "netw/log.hpp"
#include "netw/session/frames.hpp"

using namespace godot;

namespace netw {

Ref<ResolvedJoin> JoinRequest::resolve() const {
    if (String(username).is_empty()) {
        return Ref<ResolvedJoin>();
    }
    Ref<ResolvedJoin> out;
    out.instantiate();
    out->set_peer_id(peer_id);
    out->set_username(username);
    out->set_arg_values(arg_values.duplicate(true));
    return out;
}

PackedByteArray JoinRequest::serialize() const {
    JoinFrame frame;
    frame.username = username;
    frame.args = arg_bytes;
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
            "a join request that did not decode whole is refused"
        );
        return false;
    }
    username = frame.username;
    peer_id = frame.peer_id;
    arg_bytes = frame.args;
    schema_hash = frame.schema_hash;
    app_tag = frame.app_tag;
    wire_identity = frame.wire_identity;
    schema_identity = frame.schema_identity;
    return true;
}

} // namespace netw

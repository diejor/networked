#include "netw/api/auth_protocol.hpp"

#include "godot/class_db.hpp"
#include "netw/auth_protocol.hpp"

using namespace godot;

namespace netw {

namespace {

const char *refusal_name(auth::Refusal p_refusal) {
    switch (p_refusal) {
        case auth::Refusal::FRAMING:
            return "framing";
        case auth::Refusal::VERSION:
            return "version";
        case auth::Refusal::APP:
            return "app";
        default:
            return "";
    }
}

} // namespace

int NetwAuthProtocol::protocol_version() {
    return int(auth::PROTOCOL_VERSION);
}

PackedByteArray NetwAuthProtocol::magic_hello() {
    return auth::magic_hello();
}

PackedByteArray NetwAuthProtocol::magic_probe() {
    return auth::magic_probe();
}

NetwAuthProtocol::Kind NetwAuthProtocol::classify(
    const PackedByteArray &p_data
) {
    return Kind(int(auth::classify(p_data)));
}

PackedByteArray NetwAuthProtocol::encode_client_hello(
    const PackedByteArray &p_provider_payload,
    int64_t p_app_tag,
    int p_flags
) {
    return auth::encode_client_hello(
        p_provider_payload,
        uint64_t(p_app_tag),
        p_flags
    );
}

Dictionary NetwAuthProtocol::decode_client_hello(
    const PackedByteArray &p_data,
    int64_t p_local_app_tag
) {
    const auth::Hello decoded
        = auth::decode_client_hello(p_data, uint64_t(p_local_app_tag));
    Dictionary row;
    row["ok"] = decoded.ok();
    row["reason"] = String(refusal_name(decoded.refusal));
    row["version"] = decoded.version;
    row["app_tag"] = int64_t(decoded.app_tag);
    row["flags"] = decoded.flags;
    row["provider_payload"] = decoded.provider_payload;
    return row;
}

PackedByteArray NetwAuthProtocol::encode_probe_request(int p_flags) {
    return auth::encode_probe_request(p_flags);
}

Dictionary NetwAuthProtocol::decode_probe_request(
    const PackedByteArray &p_data
) {
    const auth::ProbeRequest decoded = auth::decode_probe_request(p_data);
    Dictionary row;
    row["ok"] = decoded.ok;
    row["version"] = decoded.version;
    row["flags"] = decoded.flags;
    return row;
}

PackedByteArray NetwAuthProtocol::encode_probe_reply(
    int p_status,
    const PackedByteArray &p_payload
) {
    return auth::encode_probe_reply(p_status, p_payload);
}

Dictionary NetwAuthProtocol::decode_probe_reply(const PackedByteArray &p_data) {
    const auth::ProbeReply decoded = auth::decode_probe_reply(p_data);
    Dictionary row;
    row["ok"] = decoded.ok;
    row["version"] = decoded.version;
    row["status"] = decoded.status;
    row["payload"] = decoded.payload;
    return row;
}

void NetwAuthProtocol::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("protocol_version"),
        &NetwAuthProtocol::protocol_version
    );
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("magic_hello"),
        &NetwAuthProtocol::magic_hello
    );
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("magic_probe"),
        &NetwAuthProtocol::magic_probe
    );
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("classify", "data"),
        &NetwAuthProtocol::classify
    );
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("encode_client_hello", "provider_payload", "app_tag", "flags"),
        &NetwAuthProtocol::encode_client_hello,
        DEFVAL(0),
        DEFVAL(0)
    );
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("decode_client_hello", "data", "local_app_tag"),
        &NetwAuthProtocol::decode_client_hello,
        DEFVAL(0)
    );
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("encode_probe_request", "flags"),
        &NetwAuthProtocol::encode_probe_request,
        DEFVAL(0)
    );
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("decode_probe_request", "data"),
        &NetwAuthProtocol::decode_probe_request
    );
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("encode_probe_reply", "status", "payload"),
        &NetwAuthProtocol::encode_probe_reply,
        DEFVAL(PackedByteArray())
    );
    ClassDB::bind_static_method(
        "NetwAuthProtocol",
        D_METHOD("decode_probe_reply", "data"),
        &NetwAuthProtocol::decode_probe_reply
    );

    BIND_ENUM_CONSTANT(KIND_UNKNOWN);
    BIND_ENUM_CONSTANT(KIND_HELLO);
    BIND_ENUM_CONSTANT(KIND_PROBE);
    BIND_ENUM_CONSTANT(PROBE_OK);
    BIND_ENUM_CONSTANT(PROBE_BUSY);
    BIND_ENUM_CONSTANT(PROBE_UNSUPPORTED);
    BIND_ENUM_CONSTANT(PROBE_ERROR);
}

} // namespace netw

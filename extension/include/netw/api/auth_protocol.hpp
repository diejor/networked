#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwAuthProtocol : public godot::RefCounted {
    GDCLASS(NetwAuthProtocol, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    enum Kind {
        KIND_UNKNOWN = 0,
        KIND_HELLO = 1,
        KIND_PROBE = 2,
    };

    enum ProbeStatus {
        PROBE_OK = 0,
        PROBE_BUSY = 1,
        PROBE_UNSUPPORTED = 2,
        PROBE_ERROR = 3,
    };

    static int protocol_version();
    static godot::PackedByteArray magic_hello();
    static godot::PackedByteArray magic_probe();

    static Kind classify(const godot::PackedByteArray &p_data);

    static godot::PackedByteArray encode_client_hello(
        const godot::PackedByteArray &p_provider_payload,
        int64_t p_app_tag,
        int p_flags
    );
    static godot::Dictionary decode_client_hello(
        const godot::PackedByteArray &p_data,
        int64_t p_local_app_tag
    );

    static godot::PackedByteArray encode_probe_request(int p_flags);
    static godot::Dictionary decode_probe_request(
        const godot::PackedByteArray &p_data
    );

    static godot::PackedByteArray encode_probe_reply(
        int p_status,
        const godot::PackedByteArray &p_payload
    );
    static godot::Dictionary decode_probe_reply(
        const godot::PackedByteArray &p_data
    );
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwAuthProtocol::Kind);
VARIANT_ENUM_CAST(netw::NetwAuthProtocol::ProbeStatus);

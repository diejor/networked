#pragma once

#include <cstdint>

#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw::gd {

enum HttpStatus {
    HTTP_DISCONNECTED = 0,
    HTTP_RESOLVING = 1,
    HTTP_CANT_RESOLVE = 2,
    HTTP_CONNECTING = 3,
    HTTP_CANT_CONNECT = 4,
    HTTP_CONNECTED = 5,
    HTTP_REQUESTING = 6,
    HTTP_BODY = 7,
    HTTP_CONNECTION_ERROR = 8,
    HTTP_TLS_HANDSHAKE_ERROR = 9,
};

enum HttpMethod {
    HTTP_METHOD_GET = 0,
};

inline bool http_status_is_fault(int p_status) {
    return p_status == HTTP_CANT_RESOLVE || p_status == HTTP_CANT_CONNECT
        || p_status == HTTP_CONNECTION_ERROR
        || p_status == HTTP_TLS_HANDSHAKE_ERROR;
}

inline godot::Ref<godot::RefCounted> new_http_client() {
    const godot::Ref<godot::RefCounted> made
        = instantiate_class(godot::StringName("HTTPClient"));
    return made;
}

inline godot::Error http_connect(
    const godot::Ref<godot::RefCounted> &p_client,
    const godot::String &p_host,
    int64_t p_port
) {
    if (p_client.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(
        int(
            p_client->call(godot::StringName("connect_to_host"), p_host, p_port)
        )
    );
}

inline void http_poll(const godot::Ref<godot::RefCounted> &p_client) {
    if (p_client.is_valid()) {
        p_client->call(godot::StringName("poll"));
    }
}

inline int http_status(const godot::Ref<godot::RefCounted> &p_client) {
    if (p_client.is_null()) {
        return HTTP_DISCONNECTED;
    }
    return int(p_client->call(godot::StringName("get_status")));
}

inline godot::Error http_request(
    const godot::Ref<godot::RefCounted> &p_client,
    int p_method,
    const godot::String &p_path,
    const godot::PackedStringArray &p_headers
) {
    if (p_client.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(
        int(p_client->call(
            godot::StringName("request"),
            p_method,
            p_path,
            p_headers,
            godot::String()
        ))
    );
}

inline bool http_has_response(const godot::Ref<godot::RefCounted> &p_client) {
    if (p_client.is_null()) {
        return false;
    }
    return bool(p_client->call(godot::StringName("has_response")));
}

inline int http_response_code(const godot::Ref<godot::RefCounted> &p_client) {
    if (p_client.is_null()) {
        return 0;
    }
    return int(p_client->call(godot::StringName("get_response_code")));
}

inline godot::PackedByteArray http_read_chunk(
    const godot::Ref<godot::RefCounted> &p_client
) {
    if (p_client.is_null()) {
        return godot::PackedByteArray();
    }
    return p_client->call(godot::StringName("read_response_body_chunk"));
}

inline void http_close(const godot::Ref<godot::RefCounted> &p_client) {
    if (p_client.is_valid()) {
        p_client->call(godot::StringName("close"));
    }
}

} // namespace netw::gd

#include "netw/api/probe_result.hpp"

#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "godot/utility.hpp"

using namespace godot;

namespace netw {

void NetwProbeResult::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwProbeResult",
        D_METHOD("ok", "info", "latency_ms"),
        &NetwProbeResult::ok,
        DEFVAL(0)
    );
    ClassDB::bind_static_method(
        "NetwProbeResult",
        D_METHOD("unreachable", "message"),
        &NetwProbeResult::unreachable,
        DEFVAL(String())
    );
    ClassDB::bind_static_method(
        "NetwProbeResult",
        D_METHOD("timeout", "message"),
        &NetwProbeResult::timeout,
        DEFVAL(String())
    );
    ClassDB::bind_static_method(
        "NetwProbeResult",
        D_METHOD("unsupported"),
        &NetwProbeResult::unsupported
    );
    ClassDB::bind_static_method(
        "NetwProbeResult",
        D_METHOD("busy", "message"),
        &NetwProbeResult::busy,
        DEFVAL(String())
    );
    ClassDB::bind_static_method(
        "NetwProbeResult",
        D_METHOD("error", "message"),
        &NetwProbeResult::error,
        DEFVAL(String())
    );
    ClassDB::bind_static_method(
        "NetwProbeResult",
        D_METHOD("incompatible", "info", "message"),
        &NetwProbeResult::incompatible,
        DEFVAL(Ref<NetwServerInfo>()),
        DEFVAL(String())
    );

    ClassDB::bind_method(D_METHOD("is_ok"), &NetwProbeResult::is_ok);

    ClassDB::bind_method(
        D_METHOD("set_status", "status"),
        &NetwProbeResult::set_status
    );
    ClassDB::bind_method(D_METHOD("get_status"), &NetwProbeResult::get_status);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "status",
            PROPERTY_HINT_ENUM,
            "Ok,Unreachable,Timeout,Unsupported,Busy,Error,Incompatible"
        ),
        "set_status",
        "get_status"
    );

    ClassDB::bind_method(
        D_METHOD("set_info", "info"),
        &NetwProbeResult::set_info
    );
    ClassDB::bind_method(D_METHOD("get_info"), &NetwProbeResult::get_info);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "info",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwServerInfo"
        ),
        "set_info",
        "get_info"
    );

    ClassDB::bind_method(
        D_METHOD("set_latency_ms", "latency_ms"),
        &NetwProbeResult::set_latency_ms
    );
    ClassDB::bind_method(
        D_METHOD("get_latency_ms"),
        &NetwProbeResult::get_latency_ms
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "latency_ms"),
        "set_latency_ms",
        "get_latency_ms"
    );

    ClassDB::bind_method(
        D_METHOD("set_message", "message"),
        &NetwProbeResult::set_message
    );
    ClassDB::bind_method(
        D_METHOD("get_message"),
        &NetwProbeResult::get_message
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "message"),
        "set_message",
        "get_message"
    );

    BIND_ENUM_CONSTANT(STATUS_OK);
    BIND_ENUM_CONSTANT(STATUS_UNREACHABLE);
    BIND_ENUM_CONSTANT(STATUS_TIMEOUT);
    BIND_ENUM_CONSTANT(STATUS_UNSUPPORTED);
    BIND_ENUM_CONSTANT(STATUS_BUSY);
    BIND_ENUM_CONSTANT(STATUS_ERROR);
    BIND_ENUM_CONSTANT(STATUS_INCOMPATIBLE);
}

Ref<NetwProbeResult> NetwProbeResult::made(
    Status p_status,
    const String &p_message
) {
    Ref<NetwProbeResult> result(memnew(NetwProbeResult));
    result->set_status(p_status);
    result->set_message(p_message);
    return result;
}

Ref<NetwProbeResult> NetwProbeResult::ok(
    const Ref<NetwServerInfo> &p_info,
    int64_t p_latency_ms
) {
    Ref<NetwProbeResult> result = made(STATUS_OK, String());
    result->set_info(p_info);
    result->set_latency_ms(p_latency_ms);
    return result;
}

Ref<NetwProbeResult> NetwProbeResult::unreachable(const String &p_message) {
    return made(STATUS_UNREACHABLE, p_message);
}

Ref<NetwProbeResult> NetwProbeResult::timeout(const String &p_message) {
    return made(STATUS_TIMEOUT, p_message);
}

Ref<NetwProbeResult> NetwProbeResult::unsupported() {
    return made(STATUS_UNSUPPORTED, String());
}

Ref<NetwProbeResult> NetwProbeResult::busy(const String &p_message) {
    return made(STATUS_BUSY, p_message);
}

Ref<NetwProbeResult> NetwProbeResult::error(const String &p_message) {
    return made(STATUS_ERROR, p_message);
}

Ref<NetwProbeResult> NetwProbeResult::incompatible(
    const Ref<NetwServerInfo> &p_info,
    const String &p_message
) {
    Ref<NetwProbeResult> result = made(STATUS_INCOMPATIBLE, p_message);
    result->set_info(p_info);
    return result;
}

String NetwProbeResult::_to_string() const {
    switch (status) {
        case STATUS_OK:
            return "NetwProbeResult(ok, "
                + String::num_int64(info.is_valid() ? info->get_players() : 0)
                + " players, " + String::num_int64(latency_ms) + "ms)";
        case STATUS_UNREACHABLE:
            return "NetwProbeResult(unreachable: " + message + ")";
        case STATUS_TIMEOUT:
            return "NetwProbeResult(timeout)";
        case STATUS_UNSUPPORTED:
            return "NetwProbeResult(unsupported)";
        case STATUS_BUSY:
            return "NetwProbeResult(busy: " + message + ")";
        case STATUS_ERROR:
            return "NetwProbeResult(error: " + message + ")";
        case STATUS_INCOMPATIBLE:
            return "NetwProbeResult(incompatible)";
    }
    return "NetwProbeResult(?)";
}

} // namespace netw

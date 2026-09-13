#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/server_info.hpp"

namespace netw {

class NetwProbeResult : public godot::RefCounted {
    GDCLASS(NetwProbeResult, godot::RefCounted)

public:
    enum Status {
        STATUS_OK,
        STATUS_UNREACHABLE,
        STATUS_TIMEOUT,
        STATUS_UNSUPPORTED,
        STATUS_BUSY,
        STATUS_ERROR,
        STATUS_INCOMPATIBLE,
    };

private:
    Status status = STATUS_UNSUPPORTED;
    godot::Ref<NetwServerInfo> info;
    int64_t latency_ms = 0;
    godot::String message;

    static godot::Ref<NetwProbeResult> made(
        Status p_status,
        const godot::String &p_message
    );

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwProbeResult> ok(
        const godot::Ref<NetwServerInfo> &p_info,
        int64_t p_latency_ms
    );
    static godot::Ref<NetwProbeResult> unreachable(
        const godot::String &p_message
    );
    static godot::Ref<NetwProbeResult> timeout(const godot::String &p_message);
    static godot::Ref<NetwProbeResult> unsupported();
    static godot::Ref<NetwProbeResult> busy(const godot::String &p_message);
    static godot::Ref<NetwProbeResult> error(const godot::String &p_message);
    static godot::Ref<NetwProbeResult> incompatible(
        const godot::Ref<NetwServerInfo> &p_info,
        const godot::String &p_message
    );

    bool is_ok() const {
        return status == STATUS_OK;
    }

    void set_status(Status p_status) {
        status = p_status;
    }
    Status get_status() const {
        return status;
    }

    void set_info(const godot::Ref<NetwServerInfo> &p_info) {
        info = p_info;
    }
    godot::Ref<NetwServerInfo> get_info() const {
        return info;
    }

    void set_latency_ms(int64_t p_latency_ms) {
        latency_ms = p_latency_ms;
    }
    int64_t get_latency_ms() const {
        return latency_ms;
    }

    void set_message(const godot::String &p_message) {
        message = p_message;
    }
    godot::String get_message() const {
        return message;
    }

    godot::String _to_string() const;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwProbeResult::Status);

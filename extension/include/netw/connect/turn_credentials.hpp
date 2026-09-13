#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw::connect {

class TurnCredentials {
public:
    enum Phase {
        PHASE_IDLE,
        PHASE_CONNECTING,
        PHASE_REQUESTING,
        PHASE_READING,
        PHASE_READY,
        PHASE_FAILED,
    };

    static const char *URL_SETTING;
    static const char *HEADERS_SETTING;

    static godot::String configured_url();
    static godot::PackedStringArray configured_headers();

    static godot::String resolve_url(
        const godot::String &p_configured,
        const godot::String &p_origin
    );
    static void split_url(
        const godot::String &p_url,
        godot::String &r_host,
        godot::String &r_path
    );
    static godot::Array servers_from_body(
        int p_code,
        const godot::PackedByteArray &p_body
    );

    static const godot::Array &cached();
    static bool attempted();
    static void adopt(const godot::Array &p_servers);
    static void forget();

    ~TurnCredentials();

    godot::Error begin(
        const godot::String &p_url,
        const godot::PackedStringArray &p_headers,
        int64_t p_now_msec
    );
    void poll(int64_t p_now_msec);
    Phase phase() const {
        return stage;
    }
    const godot::String &failure() const {
        return reason;
    }

    double budget_msec = 5000.0;

private:
    godot::Ref<godot::RefCounted> client;
    godot::String path;
    godot::PackedStringArray headers;
    godot::PackedByteArray body;
    godot::String reason;
    Phase stage = PHASE_IDLE;
    int64_t started_msec = 0;

    void settle(const godot::Array &p_servers);
    void refuse(const godot::String &p_reason);
    void read_body();
};

} // namespace netw::connect

#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/log.hpp"

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

    struct Answer {
        godot::Array servers;
        log::Level level = log::Level::ERROR;
        godot::String message;
    };

    static const char *URL_SETTING;
    static godot::String configured_url();
    static void split_url(
        const godot::String &p_url,
        godot::String &r_host,
        godot::String &r_path
    );
    static Answer read_answer(int p_code, const godot::PackedByteArray &p_body);

    ~TurnCredentials();

    godot::Error begin(const godot::String &p_url, int64_t p_now_msec);
    void poll(int64_t p_now_msec);
    Phase phase() const {
        return stage;
    }
    const godot::String &failure() const {
        return reason;
    }
    const godot::Array &servers() const {
        return fetched_servers;
    }

    double budget_msec = 5000.0;

private:
    godot::Ref<godot::RefCounted> client;
    godot::String host;
    godot::String path;
    godot::PackedByteArray body;
    godot::String reason;
    godot::Array fetched_servers;
    Phase stage = PHASE_IDLE;
    int64_t started_msec = 0;

    void settle(const godot::Array &p_servers);
    void refuse(log::Level p_level, const godot::String &p_reason);
    void unreachable();
    void read_body();
};

} // namespace netw::connect

#pragma once

#include "netw_test.h"

#include <memory>

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace netw_test {

inline int notification_named(const godot::StringName &p_name) {
    static const char *const NAMES[] = {
        "endpoint_added",
        "endpoint_removed",
        "endpoint_updated",
        "clock_before_tick",
        "clock_on_tick",
        "clock_after_tick",
        "predict_owner_divergence",
        "lagcomp_action_gate_fallback",
        "scene_spawned",
        "scene_activated",
        "scene_despawned",
        "scene_entity_moved",
        "scene_startup_spawned",
        "clock_before_tick_loop",
        "clock_after_tick_loop",
        "clock_synchronized",
        "clock_configured",
        "clock_tickrate_mismatch",
        "clock_pong_received",
        "clock_stability_changed",
        "session_state_changed",
        "session_join_submitted",
        "session_entered",
        "session_join_failed",
        "session_ended",
        "session_reclaimed",
        "peer_authenticating",
        "peer_authentication_failed",
        "session_poll_started",
        "table_received",
        "database_failed",
        "persist_loaded",
        "persist_saved",
        "peer_packet",
        "peer_kicked",
        "session_server_disconnecting",
        "peer_kick_requested",
        "session_disconnect_requested",
        "session_tree_paused",
        "session_tree_unpaused",
        "service_registered",
        "service_unregistered",
        "entity_live",
        "entity_lingering",
        "entity_dead",
        "entity_hidden",
        "player_joined",
        "player_local_joined",
        "player_left",
        "scene_live",
        "connection_progressed",
        "connection_failed",
        "connection_join_address_changed",
        "connection_invited",
    };
    static_assert(
        sizeof(NAMES) / sizeof(NAMES[0])
        == netw::NetwMultiplayer::NOTIFICATION_COUNT
    );
    for (int at = 0; at < netw::NetwMultiplayer::NOTIFICATION_COUNT; ++at) {
        if (p_name == godot::StringName(NAMES[at])) {
            return at;
        }
    }
    return -1;
}

struct RecorderLog {
    struct Emission {
        godot::StringName signal;
        godot::Array args;
    };

    godot::Vector<Emission> emissions;
};

class RecorderSink final : public godot::CallableCustom {
    std::shared_ptr<RecorderLog> log;
    godot::StringName signal;
    godot::ObjectID source;

    static bool same(
        const godot::CallableCustom *a,
        const godot::CallableCustom *b
    ) {
        return a == b;
    }

    static bool before(
        const godot::CallableCustom *a,
        const godot::CallableCustom *b
    ) {
        return a < b;
    }

public:
    RecorderSink(
        const std::shared_ptr<RecorderLog> &p_log,
        const godot::StringName &p_signal,
        const godot::Object *p_source
    )
        : log(p_log), signal(p_signal),
          source(netw::gd::instance_id(p_source)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    godot::String get_as_text() const override {
        return godot::String("NetwRecorder::") + godot::String(signal);
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &RecorderSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &RecorderSink::before;
    }

    godot::ObjectID get_object() const override {
        return source;
    }

    void call(
        const godot::Variant **p_arguments,
        int p_argcount,
        godot::Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        godot::Array args;
        for (int index = 0; index < p_argcount; ++index) {
            args.push_back(*p_arguments[index]);
        }
        log->emissions.push_back({signal, args});
        r_return_value = godot::Variant();
        netw::gd::call_ok(r_call_error);
    }
};

class Recorder {
    godot::Object *source = nullptr;
    std::shared_ptr<RecorderLog> log;
    godot::Vector<godot::StringName> signals;
    godot::Vector<godot::Callable> callables;
    godot::Vector<int> observed;
    godot::Vector<godot::Callable> observers;

public:
    Recorder(
        godot::Object *p_object,
        const godot::Vector<godot::StringName> &p_signals
    )
        : source(p_object), log(std::make_shared<RecorderLog>()) {
        REQUIRE(p_object != nullptr);
        netw::NetwMultiplayer *session
            = godot::Object::cast_to<netw::NetwMultiplayer>(p_object);
        for (const godot::StringName &name : p_signals) {
            const int event
                = session != nullptr ? notification_named(name) : -1;
            if (event >= 0) {
                const godot::Callable callable(
                    memnew(RecorderSink(log, name, p_object))
                );
                session->observe(
                    netw::NetwMultiplayer::Notification(event),
                    callable
                );
                observed.push_back(event);
                observers.push_back(callable);
                continue;
            }
            REQUIRE_MESSAGE(
                p_object->has_signal(name),
                "no such signal on the recorded object"
            );
            if (!p_object->has_signal(name)) {
                continue;
            }
            const godot::Callable callable(
                memnew(RecorderSink(log, name, p_object))
            );
            p_object->connect(name, callable);
            signals.push_back(name);
            callables.push_back(callable);
        }
    }

    ~Recorder() {
        if (source == nullptr) {
            return;
        }
        for (int index = 0; index < signals.size(); ++index) {
            if (source->is_connected(signals[index], callables[index])) {
                source->disconnect(signals[index], callables[index]);
            }
        }
        netw::NetwMultiplayer *session
            = godot::Object::cast_to<netw::NetwMultiplayer>(source);
        if (session == nullptr) {
            return;
        }
        for (int index = 0; index < observed.size(); ++index) {
            session->unobserve(
                netw::NetwMultiplayer::Notification(observed[index]),
                observers[index]
            );
        }
    }

    Recorder(const Recorder &) = delete;
    Recorder &operator=(const Recorder &) = delete;

    int count(const godot::StringName &p_signal) const {
        int total = 0;
        for (const RecorderLog::Emission &emission : log->emissions) {
            total += emission.signal == p_signal ? 1 : 0;
        }
        return total;
    }

    godot::Array args(const godot::StringName &p_signal, int p_nth = 0) const {
        int seen = 0;
        for (const RecorderLog::Emission &emission : log->emissions) {
            if (emission.signal != p_signal) {
                continue;
            }
            if (seen == p_nth) {
                return emission.args;
            }
            ++seen;
        }
        return godot::Array();
    }

    godot::Vector<godot::StringName> order() const {
        godot::Vector<godot::StringName> names;
        for (const RecorderLog::Emission &emission : log->emissions) {
            names.push_back(emission.signal);
        }
        return names;
    }

    void clear() {
        log->emissions.clear();
    }
};

} // namespace netw_test

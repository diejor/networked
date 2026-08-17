#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwDespawnOpts : public godot::RefCounted {
    GDCLASS(NetwDespawnOpts, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    godot::StringName reason;
    bool flush_save = true;
    bool defer_free = true;
    bool linger = false;
    double linger_seconds = 1.0;

    static godot::Ref<NetwDespawnOpts> create(const godot::StringName &p_reason);

    godot::StringName get_reason() const { return reason; }
    void set_reason(const godot::StringName &p_reason) { reason = p_reason; }
    bool get_flush_save() const { return flush_save; }
    void set_flush_save(bool p_flush_save) { flush_save = p_flush_save; }
    bool get_defer_free() const { return defer_free; }
    void set_defer_free(bool p_defer_free) { defer_free = p_defer_free; }
    bool get_linger() const { return linger; }
    void set_linger(bool p_linger) { linger = p_linger; }
    double get_linger_seconds() const { return linger_seconds; }
    void set_linger_seconds(double p_seconds) { linger_seconds = p_seconds; }
};

class NetwReparentOpts : public godot::RefCounted {
    GDCLASS(NetwReparentOpts, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    bool preserve_history = false;
    godot::StringName reason;
    godot::Variant target_global_position;

    bool get_preserve_history() const { return preserve_history; }
    void set_preserve_history(bool p_preserve) { preserve_history = p_preserve; }
    godot::StringName get_reason() const { return reason; }
    void set_reason(const godot::StringName &p_reason) { reason = p_reason; }
    godot::Variant get_target_global_position() const {
        return target_global_position;
    }
    void set_target_global_position(const godot::Variant &p_target) {
        target_global_position = p_target;
    }
};

class NetwControlRequest : public godot::RefCounted {
    GDCLASS(NetwControlRequest, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    int64_t requester = 0;
    bool denied = false;

    void deny() { denied = true; }

    int64_t get_requester() const { return requester; }
    void set_requester(int64_t p_requester) { requester = p_requester; }
    bool get_denied() const { return denied; }
    void set_denied(bool p_denied) { denied = denied || p_denied; }
};

} // namespace netw

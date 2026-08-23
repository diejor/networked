#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwSceneMark : public godot::RefCounted {
    GDCLASS(NetwSceneMark, godot::RefCounted)

private:
    bool marked = false;
    bool gated = false;
    bool session_wide = false;
    bool captured = false;
    godot::StringName pending_method;
    double deadline = 0.0;

protected:
    static void _bind_methods();

public:
    void set_marked(bool p_marked) { marked = p_marked; }
    bool get_marked() const { return marked; }

    void set_gated(bool p_gated) { gated = p_gated; }
    bool get_gated() const { return gated; }

    void set_session_wide(bool p_session_wide) {
        session_wide = p_session_wide;
    }
    bool get_session_wide() const { return session_wide; }

    void set_captured(bool p_captured) { captured = p_captured; }
    bool get_captured() const { return captured; }

    void set_pending_method(const godot::StringName &p_pending_method) {
        pending_method = p_pending_method;
    }
    godot::StringName get_pending_method() const { return pending_method; }

    void set_deadline(double p_deadline) { deadline = p_deadline; }
    double get_deadline() const { return deadline; }

    bool is_deny_default() const { return gated || session_wide; }

    double deadline_or(double p_fallback) const {
        return deadline > 0.0 ? deadline : p_fallback;
    }
};

} // namespace netw

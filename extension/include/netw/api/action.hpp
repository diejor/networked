#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwMultiplayer;

class NetwActionContext : public godot::RefCounted {
    GDCLASS(NetwActionContext, godot::RefCounted)

    godot::ObjectID session_id;
    int64_t requester = 0;
    int64_t view_tick = 0;
    int64_t requested_tick = 0;
    int64_t execution_tick = 0;
    godot::StringName key;
    bool denied = false;

    NetwMultiplayer *session() const;

protected:
    static void _bind_methods();

public:
    void open(
        NetwMultiplayer *p_session,
        int64_t p_requester,
        int64_t p_view_tick,
        int64_t p_requested_tick,
        int64_t p_execution_tick,
        const godot::StringName &p_key
    );

    int64_t get_requester() const {
        return requester;
    }
    int64_t get_view_tick() const {
        return view_tick;
    }
    int64_t get_requested_tick() const {
        return requested_tick;
    }
    int64_t get_execution_tick() const {
        return execution_tick;
    }

    void bind_result(godot::Node *p_node);
    void deny();
};

class NetwAction : public godot::RefCounted {
    GDCLASS(NetwAction, godot::RefCounted)

    godot::ObjectID session_id;
    godot::Callable authority;
    godot::ObjectID entity_id;
    godot::NodePath target_path;
    godot::StringName method;
    int64_t slot = 0;

    godot::Callable predict;
    godot::Callable revert;
    godot::Callable confirm;
    int64_t timeout_ticks = 0;
    int timing_mode = 2;

    NetwMultiplayer *session() const;
    static godot::NodePath tree_relative_path(godot::Node *p_target);
    void run_revert(int64_t p_ghost);
    void run_confirm(int64_t p_ghost);
    void run_denied();

protected:
    static void _bind_methods();

public:
    enum TimingMode {
        TIMING_TICK_ALIGNED = 0,
        TIMING_TICK_ALIGNED_STATE_READY = 1,
        TIMING_IMMEDIATE = 2,
    };

    void open(
        NetwMultiplayer *p_session,
        const godot::Callable &p_authority,
        int64_t p_slot
    );

    void set_predict(const godot::Callable &p_predict) {
        predict = p_predict;
    }
    godot::Callable get_predict() const {
        return predict;
    }
    void set_revert(const godot::Callable &p_revert) {
        revert = p_revert;
    }
    godot::Callable get_revert() const {
        return revert;
    }
    void set_confirm(const godot::Callable &p_confirm) {
        confirm = p_confirm;
    }
    godot::Callable get_confirm() const {
        return confirm;
    }
    void set_timeout_ticks(int64_t p_ticks) {
        timeout_ticks = p_ticks;
    }
    int64_t get_timeout_ticks() const {
        return timeout_ticks;
    }
    void set_timing_mode(TimingMode p_mode) {
        timing_mode = int(p_mode);
    }
    TimingMode get_timing_mode() const {
        return TimingMode(timing_mode);
    }

    int64_t action_slot() const {
        return slot;
    }

    void request(int64_t p_view_tick, const godot::Variant &p_data);
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwAction::TimingMode);

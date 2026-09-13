#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/ring_buffer.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/playhead.hpp"
#include "netw/display/tracks.hpp"
#include "netw/object_port.hpp"

namespace netw::display {

class Runtime {
private:
    ObjectPort entity_port;
    ObjectPort owner_port;

    int64_t route = 0;
    Decl config;
    Playhead playhead;
    godot::LocalVector<Channel *> states;
    Tracks tracks;
    int64_t pump_mode = netw::display::PUMP_UNRESOLVED;
    int64_t role = 0;
    int64_t pumped = 0;
    bool rebuild_queued = false;
    godot::Ref<NetwPropertySetBinding> authoring_binding;
    int64_t trace_frame = 0;
    godot::Dictionary saved_freeze;
    godot::Array entity_hooks;
    godot::Array chase_hooks;
    bool disabled = false;
    int64_t disable_until_tick = -1;
    bool warned_self_feedback = false;
    double display_offset_limit = 0.0;

public:
    Runtime();
    ~Runtime();

    void bind(NetwEntity *p_entity, godot::Node *p_owner);
    godot::Ref<NetwEntity> entity();
    godot::RID entity_rid();
    godot::Node *owner();

    const godot::LocalVector<Channel *> &channels() const {
        return states;
    }
    Channel *add_channel();
    void clear_channels();

    Channel *channel_named(const godot::StringName &p_name) const;
    void snap_named(
        const godot::StringName &p_name,
        const godot::Variant &p_value
    );
    void reset(int p_display_offset, int p_recommended_display_offset);
    int64_t authoring_tick() const;
    godot::Ref<NetwRingBuffer> buffer_of(
        const godot::StringName &p_track
    ) const;
    godot::Variant track_stat(
        const godot::StringName &p_track,
        const godot::StringName &p_stat
    ) const;

    void set_route(int64_t p_route) {
        route = p_route;
    }
    int64_t get_route() const {
        return route;
    }

    void set_config(const Decl &p_config) {
        config = p_config;
    }
    const Decl &get_config() const {
        return config;
    }

    Playhead &display_playhead() {
        return playhead;
    }
    const Playhead &display_playhead() const {
        return playhead;
    }

    Tracks &display_tracks() {
        return tracks;
    }
    const Tracks &display_tracks() const {
        return tracks;
    }

    void set_pump_mode(int64_t p_mode) {
        pump_mode = p_mode;
    }
    int64_t get_pump_mode() const {
        return pump_mode;
    }

    void set_role(int64_t p_role) {
        role = p_role;
    }
    int64_t get_role() const {
        return role;
    }

    void set_pumped(int64_t p_pumped) {
        pumped = p_pumped;
    }
    int64_t get_pumped() const {
        return pumped;
    }

    void set_rebuild_queued(bool p_queued) {
        rebuild_queued = p_queued;
    }
    bool get_rebuild_queued() const {
        return rebuild_queued;
    }

    void set_authoring_binding(
        const godot::Ref<NetwPropertySetBinding> &p_binding
    ) {
        authoring_binding = p_binding;
    }
    godot::Ref<NetwPropertySetBinding> get_authoring_binding() const {
        return authoring_binding;
    }

    void set_trace_frame(int64_t p_frame) {
        trace_frame = p_frame;
    }
    int64_t get_trace_frame() const {
        return trace_frame;
    }

    void set_saved_freeze(const godot::Dictionary &p_saved) {
        saved_freeze = p_saved;
    }
    godot::Dictionary get_saved_freeze() const {
        return saved_freeze;
    }

    void set_entity_hooks(const godot::Array &p_hooks) {
        entity_hooks = p_hooks;
    }
    godot::Array get_entity_hooks() const {
        return entity_hooks;
    }

    void set_chase_hooks(const godot::Array &p_hooks) {
        chase_hooks = p_hooks;
    }
    godot::Array get_chase_hooks() const {
        return chase_hooks;
    }

    void set_disabled(bool p_disabled) {
        disabled = p_disabled;
    }
    bool get_disabled() const {
        return disabled;
    }

    void set_disable_until_tick(int64_t p_tick) {
        disable_until_tick = p_tick;
    }
    int64_t get_disable_until_tick() const {
        return disable_until_tick;
    }

    void set_warned_self_feedback(bool p_warned) {
        warned_self_feedback = p_warned;
    }
    bool get_warned_self_feedback() const {
        return warned_self_feedback;
    }

    void set_display_offset_limit(double p_limit) {
        display_offset_limit = p_limit;
    }
    double get_display_offset_limit() const {
        return display_offset_limit;
    }
};

} // namespace netw::display

#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/display_channel.hpp"
#include "netw/display_decl.hpp"
#include "netw/display_playhead.hpp"
#include "netw/display_tracks.hpp"
#include "netw/entity.hpp"
#include "netw/object_port.hpp"
#include "netw/ring_buffer.hpp"

namespace netw {

using namespace godot;

class NetwDisplayRuntime : public RefCounted {
    GDCLASS(NetwDisplayRuntime, RefCounted)

private:
    ObjectPort entity_port;
    ObjectPort owner_port;

    int64_t route = 0;
    Ref<NetwDisplayDecl> config;
    Ref<NetwDisplayPlayhead> playhead;
    TypedArray<NetwDisplayChannel> states;
    Ref<NetwDisplayTracks> tracks;
    int64_t pump_mode = NetwDisplayDecl::PUMP_UNRESOLVED;
    int64_t role = 0;
    int64_t pumped = 0;
    bool rebuild_queued = false;
    Ref<RefCounted> authoring_binding;
    int64_t trace_frame = 0;
    Dictionary saved_freeze;
    Array entity_hooks;
    Array chase_hooks;
    bool disabled = false;
    int64_t disable_until_tick = -1;
    bool warned_self_feedback = false;
    double display_offset_limit = 0.0;

protected:
    static void _bind_methods();

public:
    NetwDisplayRuntime();

    void bind(NetwEntity *p_entity, Node *p_owner);
    Ref<NetwEntity> entity();
    Node *owner();

    Ref<NetwDisplayChannel> channel_named(const StringName &p_name) const;
    int64_t authoring_tick() const;
    Ref<NetwRingBuffer> buffer_of(const StringName &p_track) const;
    Variant track_stat(
        const StringName &p_track,
        const StringName &p_stat
    ) const;

    void set_route(int64_t p_route) { route = p_route; }
    int64_t get_route() const { return route; }

    void set_config(const Ref<NetwDisplayDecl> &p_config) { config = p_config; }
    Ref<NetwDisplayDecl> get_config() const { return config; }

    void set_playhead(const Ref<NetwDisplayPlayhead> &p_playhead) {
        playhead = p_playhead;
    }
    Ref<NetwDisplayPlayhead> get_playhead() const { return playhead; }

    void set_states(const TypedArray<NetwDisplayChannel> &p_states) {
        states = p_states;
    }
    TypedArray<NetwDisplayChannel> get_states() const { return states; }

    void set_tracks(const Ref<NetwDisplayTracks> &p_tracks) {
        tracks = p_tracks;
    }
    Ref<NetwDisplayTracks> get_tracks() const { return tracks; }

    void set_pump_mode(int64_t p_mode) { pump_mode = p_mode; }
    int64_t get_pump_mode() const { return pump_mode; }

    void set_role(int64_t p_role) { role = p_role; }
    int64_t get_role() const { return role; }

    void set_pumped(int64_t p_pumped) { pumped = p_pumped; }
    int64_t get_pumped() const { return pumped; }

    void set_rebuild_queued(bool p_queued) { rebuild_queued = p_queued; }
    bool get_rebuild_queued() const { return rebuild_queued; }

    void set_authoring_binding(const Ref<RefCounted> &p_binding) {
        authoring_binding = p_binding;
    }
    Ref<RefCounted> get_authoring_binding() const { return authoring_binding; }

    void set_trace_frame(int64_t p_frame) { trace_frame = p_frame; }
    int64_t get_trace_frame() const { return trace_frame; }

    void set_saved_freeze(const Dictionary &p_saved) { saved_freeze = p_saved; }
    Dictionary get_saved_freeze() const { return saved_freeze; }

    void set_entity_hooks(const Array &p_hooks) { entity_hooks = p_hooks; }
    Array get_entity_hooks() const { return entity_hooks; }

    void set_chase_hooks(const Array &p_hooks) { chase_hooks = p_hooks; }
    Array get_chase_hooks() const { return chase_hooks; }

    void set_disabled(bool p_disabled) { disabled = p_disabled; }
    bool get_disabled() const { return disabled; }

    void set_disable_until_tick(int64_t p_tick) { disable_until_tick = p_tick; }
    int64_t get_disable_until_tick() const { return disable_until_tick; }

    void set_warned_self_feedback(bool p_warned) {
        warned_self_feedback = p_warned;
    }
    bool get_warned_self_feedback() const { return warned_self_feedback; }

    void set_display_offset_limit(double p_limit) {
        display_offset_limit = p_limit;
    }
    double get_display_offset_limit() const { return display_offset_limit; }
};

} // namespace netw

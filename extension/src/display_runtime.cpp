#include "netw/display_runtime.hpp"

#include <limits>

#include "godot/class_db.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

NetwDisplayRuntime::NetwDisplayRuntime() {
    tracks.instantiate();
    display_offset_limit = std::numeric_limits<double>::infinity();
}

void NetwDisplayRuntime::bind(NetwEntity *p_entity, Node *p_owner) {
    entity_port.bind(p_entity);
    owner_port.bind(p_owner);
}

Ref<NetwEntity> NetwDisplayRuntime::entity() {
    return Ref<NetwEntity>(
        Object::cast_to<NetwEntity>(entity_port.resolve(sys::INTERPOLATION))
    );
}

Node *NetwDisplayRuntime::owner() {
    return Object::cast_to<Node>(owner_port.resolve(sys::INTERPOLATION));
}

Ref<NetwDisplayChannel> NetwDisplayRuntime::channel_named(
    const StringName &p_name
) const {
    if (tracks.is_null()) {
        return Ref<NetwDisplayChannel>();
    }
    const int at = tracks->by_name(p_name);
    if (at < 0 || at >= states.size()) {
        return Ref<NetwDisplayChannel>();
    }
    return states[at];
}

int64_t NetwDisplayRuntime::authoring_tick() const {
    if (authoring_binding.is_null() || playhead.is_null()
        || playhead->get_display_tick() < 0) {
        return -1;
    }
    for (int at = 0; at < states.size(); ++at) {
        const Ref<NetwDisplayChannel> channel = states[at];
        if (channel.is_null() || !channel->get_authoring_ticks()) {
            continue;
        }
        const Ref<NetwDisplayHistory> history = channel->get_history();
        if (history.is_null()) {
            continue;
        }
        const int64_t previous
            = history->bracketing_ticks(playhead->get_display_tick()).x;
        if (previous >= 0) {
            return previous;
        }
    }
    return -1;
}

Ref<NetwRingBuffer> NetwDisplayRuntime::buffer_of(
    const StringName &p_track
) const {
    if (pump_mode == NetwDisplayDecl::PUMP_CHASE) {
        return Ref<NetwRingBuffer>();
    }
    const Ref<NetwDisplayChannel> channel = channel_named(p_track);
    if (channel.is_null() || channel->get_history().is_null()) {
        return Ref<NetwRingBuffer>();
    }
    return channel->get_history()->get_buffer();
}

Variant NetwDisplayRuntime::track_stat(
    const StringName &p_track,
    const StringName &p_stat
) const {
    const Ref<NetwDisplayChannel> channel
        = p_track.is_empty() ? Ref<NetwDisplayChannel>() : channel_named(p_track);
    if (p_stat == StringName("sleeping")) {
        return channel.is_valid() && channel->get_history().is_valid()
            && channel->get_history()->is_sleeping();
    }
    if (p_stat == StringName("buffer")) {
        return buffer_of(p_track);
    }
    if (p_stat == StringName("buffer_size")) {
        const Ref<NetwRingBuffer> buffer = buffer_of(p_track);
        return buffer.is_valid() ? buffer->size() : 0;
    }
    if (p_stat == StringName("channels")) {
        return states.size();
    }
    if (p_stat == StringName("ambiguous")) {
        return tracks.is_valid() ? tracks->ambiguous_count() : 0;
    }
    if (p_stat == StringName("pumped_frames")) {
        return pumped;
    }
    if (p_stat == StringName("starvation_ticks")) {
        return playhead.is_valid() ? playhead->get_starvation_ticks() : 0;
    }
    if (p_stat == StringName("display_lag")) {
        return playhead.is_valid() ? playhead->get_display_lag() : 0.0;
    }
    return Variant();
}

void NetwDisplayRuntime::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("bind", "entity", "owner"),
        &NetwDisplayRuntime::bind
    );
    ClassDB::bind_method(D_METHOD("entity"), &NetwDisplayRuntime::entity);
    ClassDB::bind_method(D_METHOD("owner"), &NetwDisplayRuntime::owner);
    ClassDB::bind_method(
        D_METHOD("channel_named", "name"),
        &NetwDisplayRuntime::channel_named
    );
    ClassDB::bind_method(
        D_METHOD("authoring_tick"),
        &NetwDisplayRuntime::authoring_tick
    );
    ClassDB::bind_method(
        D_METHOD("buffer_of", "track"),
        &NetwDisplayRuntime::buffer_of
    );
    ClassDB::bind_method(
        D_METHOD("track_stat", "track", "stat"),
        &NetwDisplayRuntime::track_stat
    );

#define NETW_RUNTIME_PROPERTY(m_type, m_name) \
    ClassDB::bind_method( \
        D_METHOD("set_" #m_name, #m_name), \
        &NetwDisplayRuntime::set_##m_name \
    ); \
    ClassDB::bind_method( \
        D_METHOD("get_" #m_name), \
        &NetwDisplayRuntime::get_##m_name \
    ); \
    ADD_PROPERTY( \
        PropertyInfo(m_type, #m_name), \
        "set_" #m_name, \
        "get_" #m_name \
    )

    NETW_RUNTIME_PROPERTY(Variant::INT, route);
    NETW_RUNTIME_PROPERTY(Variant::OBJECT, config);
    NETW_RUNTIME_PROPERTY(Variant::OBJECT, playhead);
    NETW_RUNTIME_PROPERTY(Variant::OBJECT, tracks);
    NETW_RUNTIME_PROPERTY(Variant::INT, pump_mode);
    NETW_RUNTIME_PROPERTY(Variant::INT, role);
    NETW_RUNTIME_PROPERTY(Variant::INT, pumped);
    NETW_RUNTIME_PROPERTY(Variant::BOOL, rebuild_queued);
    NETW_RUNTIME_PROPERTY(Variant::OBJECT, authoring_binding);
    NETW_RUNTIME_PROPERTY(Variant::INT, trace_frame);
    NETW_RUNTIME_PROPERTY(Variant::DICTIONARY, saved_freeze);
    NETW_RUNTIME_PROPERTY(Variant::ARRAY, entity_hooks);
    NETW_RUNTIME_PROPERTY(Variant::ARRAY, chase_hooks);
    NETW_RUNTIME_PROPERTY(Variant::BOOL, disabled);
    NETW_RUNTIME_PROPERTY(Variant::INT, disable_until_tick);
    NETW_RUNTIME_PROPERTY(Variant::BOOL, warned_self_feedback);
    NETW_RUNTIME_PROPERTY(Variant::FLOAT, display_offset_limit);

#undef NETW_RUNTIME_PROPERTY

    ClassDB::bind_method(
        D_METHOD("set_states", "states"),
        &NetwDisplayRuntime::set_states
    );
    ClassDB::bind_method(
        D_METHOD("get_states"),
        &NetwDisplayRuntime::get_states
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "states",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwDisplayChannel"
        ),
        "set_states",
        "get_states"
    );
}

} // namespace netw
